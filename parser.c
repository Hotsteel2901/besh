/* ================================================================
 *  parser.c — recursive-descent parser for besh
 *
 *  Grammar (simplified bash grammar):
 *    complete_command → list
 *    list             → and_or ((';' | '&' | '\n') and_or)*
 *    and_or           → pipeline (('&&' | '||') pipeline)*
 *    pipeline         → '!'? command ('|' command)*
 *    command          → simple_command
 *                      | '(' list ')'
 *                      | function_def
 *                      | if_clause | for_clause | while_clause
 *    simple_command   → word* redirection*
 *    redirection      → [fd]('>'|'<'|'>>'|'2>'|'&>'|'<<'|'<<-') word
 * ================================================================ */

#include "shell.h"

/* ---- forward declarations ------------------------------------ */
static ASTNode *parse_list(Lexer *l);
static ASTNode *parse_and_or(Lexer *l);
static ASTNode *parse_pipeline(Lexer *l);
static ASTNode *parse_command(Lexer *l);
static ASTNode *parse_simple_command(Lexer *l);
static Redir    *parse_redirection(Lexer *l);
static ASTNode *parse_if(Lexer *l);
static ASTNode *parse_for(Lexer *l);
static ASTNode *parse_while(Lexer *l, int is_until);
static ASTNode *parse_case(Lexer *l);
static ASTNode *parse_funcdef(Lexer *l, char *name);

/* ---- pending here-document queue ---------------------------- */
/* Here-document bodies follow the *whole* command line, not the command
 * they are attached to.  A pipeline makes the difference visible:
 *
 *     cat <<EOF | cat
 *     body
 *     EOF
 *
 * The body comes after `| cat`, so reading it as soon as `cat <<EOF` is
 * parsed would consume `| cat` as document text — which is exactly what
 * happened: the rest of the pipeline ended up printed literally.  Bodies
 * are therefore recorded here during parsing and drained in declaration
 * order by parse_complete(), once the entire line has been consumed.
 *
 * The Redir objects belong to the AST and outlive this list, so only the
 * pointers are stored. */
typedef struct PendingHeredoc {
    Redir *redir;
    struct PendingHeredoc *next;
} PendingHeredoc;

static PendingHeredoc *pending_head = NULL;
static PendingHeredoc *pending_tail = NULL;

static void pending_push(Redir *r) {
    PendingHeredoc *n = sh_malloc(sizeof(*n));
    n->redir = r;
    n->next = NULL;
    if (pending_tail) pending_tail->next = n;
    else              pending_head = n;
    pending_tail = n;
}

/* Read every queued body, in the order the `<<` operators appeared.
 *
 * Called from parse_complete() once the whole line has been parsed.  Each
 * entry remembers the exact input offset just past the newline that ended
 * its declaration line, captured while the lexer was still on that line
 * (see body_pos on Redir).  Seeking there explicitly makes the drain
 * independent of how far parse_list's lookahead ran ahead — which is what
 * makes it correct for a pipeline, where `| cat` follows the `<<EOF` and
 * must not be mistaken for document text.
 *
 * Only the *first* body needs that seek.  lexer_heredoc_ex leaves the lexer
 * just past each body's terminator, so consecutive bodies continue from
 * there; seeking absolutely for every entry would be wrong when several
 * `<<` share a line — `cat <<A <<B` — because they would all point at the
 * same offset and the first body would be re-read looking for `B`. */
static void pending_drain(Lexer *l) {
    int first = 1;
    for (PendingHeredoc *n = pending_head; n; ) {
        PendingHeredoc *next = n->next;
        Redir *r = n->redir;
        r->delim_pending = 0;
        if (first) {
            if (r->body_pos >= 0 && r->body_pos <= l->len)
                l->pos = r->body_pos;
            first = 0;
        }
        r->heredoc = lexer_heredoc_ex(l, r->filename,
                                      r->type == REDIR_HEREDOC_DASH, 1);
        /* lexer_heredoc_ex stops *on* the newline that follows the
         * terminator; step over it so the next body starts on its own
         * first line instead of a spurious blank one */
        while (l->pos < l->len && l->input[l->pos] != '\n') l->pos++;
        if (l->pos < l->len) l->pos++;
        free(n);
        n = next;
    }
    pending_head = pending_tail = NULL;
}

/* ---- AST node allocation ------------------------------------- */
static ASTNode *ast_new(NodeType type) {
    ASTNode *n = sh_malloc(sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->type = type;
    return n;
}

void ast_free(ASTNode *node) {
    if (!node) return;

    /* Redirections can hang off *any* node: a simple command owns the ones
     * written after it, and parse_compound_redirs() attaches the trailing
     * ones (`if ...; fi >f`, `( ... ) 2>&1`, `while ...; done <x`, ...) to
     * the compound node itself.  Freeing them inside the NODE_COMMAND case
     * alone, as this used to, leaked every redirection belonging to a
     * compound construct — the struct, its filename and its heredoc body. */
    {
        Redir *r = node->redirs;
        while (r) {
            Redir *next = r->next;
            free(r->filename);
            free(r->heredoc);
            free(r);
            r = next;
        }
        node->redirs = NULL;
    }

    switch (node->type) {
    case NODE_COMMAND:
    case NODE_REDIRECT:
        if (node->argv) {
            for (int i = 0; i < node->argc; i++) free(node->argv[i]);
            free(node->argv);
        }
        free(node->argv_quoted);
        node->argv_quoted = NULL;
        break;
    case NODE_PIPELINE:
    case NODE_LIST:
    case NODE_AND:
    case NODE_OR:
        ast_free(node->left);
        ast_free(node->right);
        break;
    case NODE_BG:
        ast_free(node->left);
        break;
    case NODE_SUBSHELL:
    case NODE_BRACEGROUP:
        ast_free(node->left);
        break;
    case NODE_FUNCDEF:
        free(node->func_name);
        ast_free(node->func_body);
        break;
    case NODE_FOR:
        /* NODE_FOR keeps the loop variable name in func_name (see
         * parse_for) — without this free the name leaked on every
         * executed `for` that was not part of a function definition. */
        free(node->func_name);
        ast_free(node->cond);
        ast_free(node->body);
        break;
    case NODE_WHILE:
        ast_free(node->cond);
        ast_free(node->body);
        break;
    case NODE_IF:
        ast_free(node->cond);
        ast_free(node->body);
        ast_free(node->else_body);
        break;
    case NODE_CASE:
        free(node->case_word);
        if (node->case_patterns)
            for (int i = 0; i < node->case_count; i++)
                free(node->case_patterns[i]);
        free(node->case_patterns);
        free(node->case_pat_literal);
        if (node->case_bodies)
            for (int i = 0; i < node->case_count; i++)
                ast_free(node->case_bodies[i]);
        free(node->case_bodies);
        break;
    default:
        break;
    }
    free(node);
}

/* pretty-print AST (for debugging) */
void ast_print(ASTNode *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) printf("  ");
    switch (node->type) {
    case NODE_COMMAND:
        printf("COMMAND: ");
        for (int i = 0; i < node->argc; i++)
            printf("[%s] ", node->argv[i]);
        printf("\n");
        break;
    case NODE_PIPELINE: printf("PIPELINE\n"); break;
    case NODE_LIST:     printf("LIST\n"); break;
    case NODE_AND:      printf("AND\n"); break;
    case NODE_OR:       printf("OR\n"); break;
    case NODE_BG:       printf("BG\n"); break;
    case NODE_SUBSHELL: printf("SUBSHELL\n"); break;
    case NODE_BRACEGROUP: printf("BRACEGROUP\n"); break;
    case NODE_IF:       printf("IF\n"); break;
    case NODE_FOR:      printf("FOR\n"); break;
    case NODE_WHILE:    printf("WHILE\n"); break;
    case NODE_FUNCDEF:  printf("FUNCDEF: %s\n", node->func_name); break;
    default:            printf("UNKNOWN\n"); break;
    }
    if (node->left)  ast_print(node->left, indent + 1);
    if (node->right) ast_print(node->right, indent + 1);
}

/* ---- check if token is a reserved word ----------------------- */
static int is_reserved_word(const char *s) {
    static const char *words[] = {
        "if", "then", "else", "elif", "fi", "case", "esac",
        "for", "while", "until", "do", "done", "in",
        "function", "select", "time", "coproc", NULL
    };
    for (int i = 0; words[i]; i++)
        if (strcmp(s, words[i]) == 0) return 1;
    return 0;
}

/* ---- parse a redirection ------------------------------------- */
static Redir *parse_redirection(Lexer *l) {
    Redir *r = sh_malloc(sizeof(Redir));
    memset(r, 0, sizeof(*r));
    r->src_fd = -1;

    /* The fd prefix (`2>`, `3>&1`, `2>>`, ...) has already been consumed by
     * the lexer, which records the number in token_fd.  There is no
     * "bare number in front of a redirection" case left to handle here:
     * `echo 3 >f` lexes `3` as an ordinary word argument, which is exactly
     * what bash does too. */
    int fd_num = (l->token_fd >= 0) ? l->token_fd : -1;

    int tok = l->token_type;

    switch (tok) {
    case TOK_LREDIR:
        r->type = REDIR_IN;
        r->src_fd = (fd_num >= 0) ? fd_num : STDIN_FILENO;
        break;
    case TOK_RREDIR:
        r->type = REDIR_OUT;
        r->src_fd = (fd_num >= 0) ? fd_num : STDOUT_FILENO;
        break;
    case TOK_APPEND:
        r->type = REDIR_APPEND;
        r->src_fd = (fd_num >= 0) ? fd_num : STDOUT_FILENO;
        break;
    case TOK_RREDIR2:
        r->type = REDIR_CLOBBER;
        r->src_fd = (fd_num >= 0) ? fd_num : STDOUT_FILENO;
        break;
    case TOK_ERRREDIR:
        r->type = REDIR_ERR;
        r->src_fd = STDERR_FILENO;
        break;
    case TOK_ERRAPPEND:
        r->type = REDIR_ERRAPPEND;
        r->src_fd = STDERR_FILENO;
        break;
    case TOK_BOTHREDIR: {
        /* Two spellings share the `>&` token:
         *   `> file`   — send stdout AND stderr to `file`
         *   `> N`      — duplicate fd N onto stdout (`-` closes it)
         * `2>&1` arrives as TOK_WORD "2" followed by this token, so
         * fd_num holds the fd being redirected.  The two forms are told
         * apart by peeking at the word that follows: a bare run of digits
         * means "duplicate", anything else is a filename. */
        int fd_src = (fd_num >= 0) ? fd_num : STDOUT_FILENO;

        /* `&>` (the & came first) always means "both to file"; only the
         * `> &` spelling can be a duplicate.  The lexer reports the fd
         * via token_fd for `N>&`, and the text starts with '>' for a
         * bare `>&`. */
        if (l->token_text[0] == '>' || fd_num >= 0) {   /* dup possible */
            int save_pos = l->pos;
            int save_type = l->token_type;
            int save_quoted = l->token_quoted;
            int save_fd = l->token_fd;
            int save_escaped = l->token_escaped;
            char *save_text = sh_strdup(l->token_text);
            lexer_next(l);

            if (l->token_type == TOK_WORD) {
                const char *t = l->token_text;
                const char *d = (*t == '-') ? t + 1 : t;
                if (*d && strspn(d, "0123456789") == strlen(d)) {
                    int close_it = (*t == '-');
                    r->type  = close_it ? REDIR_CLOSE : REDIR_DUPOUT;
                    r->src_fd = fd_src;
                    r->fd    = close_it ? -1 : atoi(d);
                    free(r->filename);
                    r->filename = NULL;
                    lexer_next(l);
                    /* the token text was copied only so the rewind below
                     * could restore it; this path keeps the peeked token,
                     * so the copy has to go or it leaks with every `>&N` */
                    free(save_text);
                    return r;
                }
            }
            /* not a dup — rewind so the word is read as a filename */
            free(l->token_text);
            l->token_text = save_text;
            l->pos = save_pos;
            l->token_type = save_type;
            l->token_quoted = save_quoted;
            /* token_fd and token_escaped describe the token just like
             * token_text does, so leaving them at the *peeked* token's
             * values made the caller see a redirection fd or a quoting
             * flag that belonged to a different word.  Restore the whole
             * token, not part of it. */
            l->token_fd = save_fd;
            l->token_escaped = save_escaped;
        }

        r->type = REDIR_BOTH;
        r->src_fd = -1;  /* both stdout and stderr */
        break;
    }
    case TOK_ERRDUP: {
        /* `2>&N` — duplicate fd N onto stderr.  Handled like the `>&N`
         * case: peek at the following word. */
        int save_pos = l->pos;
        int save_type = l->token_type;
        int save_quoted = l->token_quoted;
        int save_fd = l->token_fd;
        int save_escaped = l->token_escaped;
        char *save_text = sh_strdup(l->token_text);
        lexer_next(l);

        if (l->token_type == TOK_WORD) {
            const char *t = l->token_text;
            const char *d = (*t == '-') ? t + 1 : t;
            if (*d && strspn(d, "0123456789") == strlen(d)) {
                int close_it = (*t == '-');
                r->type   = close_it ? REDIR_CLOSE : REDIR_DUPOUT;
                r->src_fd = STDERR_FILENO;
                r->fd     = close_it ? -1 : atoi(d);
                r->filename = NULL;
                lexer_next(l);
                free(save_text);          /* unused on the dup path */
                return r;
            }
        }
        /* not a dup — treat `2>&` like `2>` with a filename */
        free(l->token_text);
        l->token_text = save_text;
        l->pos = save_pos;
        l->token_type = save_type;
        l->token_quoted = save_quoted;
        l->token_fd = save_fd;
        l->token_escaped = save_escaped;
        r->type = REDIR_ERR;
        r->src_fd = STDERR_FILENO;
        break;
    }
    case TOK_DLESS:
        r->type = REDIR_HEREDOC;
        r->src_fd = (fd_num >= 0) ? fd_num : STDIN_FILENO;
        break;
    case TOK_DLESSDASH:
        r->type = REDIR_HEREDOC_DASH;
        r->src_fd = (fd_num >= 0) ? fd_num : STDIN_FILENO;
        break;
    default:
        free(r);
        return NULL;
    }

    lexer_next(l);  /* skip operator */

    /* get the filename or heredoc delimiter */
    if (tok == TOK_DLESS || tok == TOK_DLESSDASH) {
        /* next token is the delimiter */
        if (l->token_type != TOK_WORD) {
            fprintf(stderr, "besh: parse error: expected here-document delimiter\n");
            r->filename = sh_strdup("EOF");
        } else {
            /* Quote removal: `<<'EOF'` and `<<\EOF` both denote the word
             * EOF, and both suppress expansion inside the body. */
            r->filename = strip_quotes(l->token_text);
            r->quoted = l->token_quoted || l->token_escaped;
            /* Only record the declaration here.  Reading the body now would
             * be wrong for `cat <<A <<B`: bodies are taken from the lines
             * following the *entire* command line, in declaration order, so
             * the reader must first see every `<<` on the line.  The caller
             * (parse_command) collects the list and calls
             * lexer_heredoc() for each in turn once the line is consumed. */
            r->heredoc = NULL;
            r->delim_pending = 1;
            /* Remember where this body will start: just past the newline
             * that ends the current (declaration) line.  The whole line is
             * parsed before any body is read, so we cannot assume the
             * lexer will still be near here later — a pipeline such as
             * `cat <<EOF | cat` has more tokens to consume first. */
            r->body_pos = -1;
            for (int i = l->pos; i < l->len; i++) {
                if (l->input[i] == '\n') { r->body_pos = i + 1; break; }
            }
            if (r->body_pos < 0) r->body_pos = l->len;   /* no newline: EOF */
            /* the body is read by parse_complete() after the whole line,
             * not here — a pipeline continues past this point */
            pending_push(r);
            lexer_next(l);
        }
    } else {
        /* normal redirection — next token is filename */
        if (l->token_type != TOK_WORD) {
            fprintf(stderr, "besh: parse error: expected filename after redirection\n");
            r->filename = sh_strdup("/dev/null");
        } else if (l->token_text[0] == '\002' || l->token_text[0] == '\003') {
            /* process substitution used as the redirection target:
             * `cmd > >(sink)` / `cmd < <(source)`.  Keep the marker so
             * the executor can fork the inner list at run time instead
             * of trying to open the literal text as a pathname. */
            r->filename = sh_strdup(l->token_text);
        } else {
            r->filename = unescape_token(l->token_text);
        }
        lexer_next(l);
    }

    return r;
}

/* ---- simple command: word* redirection* ---------------------- */
static ASTNode *parse_simple_command(Lexer *l) {
    ASTNode *node = ast_new(NODE_COMMAND);
    node->argv_cap = 64;
    node->argv = sh_malloc(node->argv_cap * sizeof(char *));
    node->argv_quoted = sh_malloc(node->argv_cap * sizeof(int));
    node->argc = 0;
    node->redirs = NULL;

    Redir *last_redir = NULL;

    while (1) {
        /* check for redirections */
        if (l->token_type == TOK_LREDIR || l->token_type == TOK_RREDIR ||
            l->token_type == TOK_APPEND || l->token_type == TOK_RREDIR2 ||
            l->token_type == TOK_ERRREDIR || l->token_type == TOK_ERRAPPEND ||
            l->token_type == TOK_ERRDUP ||
            l->token_type == TOK_BOTHREDIR ||
            l->token_type == TOK_DLESS || l->token_type == TOK_DLESSDASH) {

            Redir *r = parse_redirection(l);
            if (r) {
                if (!node->redirs) node->redirs = r;
                else last_redir->next = r;
                last_redir = r;
            }
            continue;
        }

        /* check for fd number followed by redirection (e.g., "2>") */
        if (l->token_type == TOK_WORD) {
            /* peek ahead — if next token looks like a redirection,
             * consume the fd number and handle it */
            /* Actually handled inside parse_redirection now */

            /* is it a reserved word? (only in command position) */
            if (node->argc == 0 && is_reserved_word(l->token_text)) break;

            /* regular word argument */
            if (node->argc >= node->argv_cap - 1) {
                node->argv_cap *= 2;
                node->argv = sh_realloc(node->argv,
                                        node->argv_cap * sizeof(char *));
                node->argv_quoted = sh_realloc(node->argv_quoted,
                                        node->argv_cap * sizeof(int));
            }
            node->argv_quoted[node->argc] = l->token_quoted;
            node->argv[node->argc++] = sh_strdup(l->token_text);
            node->argv[node->argc] = NULL;
            lexer_next(l);
            continue;
        }

        /* anything else ends the command */
        break;
    }

    /* Here-document bodies are *not* read here: they follow the entire
     * command line, and a pipeline or a list continues past this command.
     * parse_complete() drains the queue once the line is fully consumed. */

    /* check if first word is a builtin alias — expand it */
    if (node->argc > 0) {
        Shell *sh = shell_get();
        for (int i = 0; i < sh->naliases; i++) {
            if (strcmp(node->argv[0], sh->aliases[i].name) == 0) {
                /* lex the alias value so that it can expand into several
                 * words (e.g. alias ll="ls -l") while still honouring any
                 * quoting present in the value */
                Lexer *al = lexer_new(sh->aliases[i].value);
                char *awords[64];
                int na = 0;
                while (na < 63 && lexer_next(al) == TOK_WORD)
                    awords[na++] = sh_strdup(al->token_text);
                lexer_free(al);
                if (na == 0) break;

                int new_argc = na + (node->argc - 1);
                char **na_argv = sh_malloc((new_argc + 1) * sizeof(char *));
                int *na_quoted = sh_malloc((new_argc + 1) * sizeof(int));
                for (int k = 0; k < na; k++) {
                    na_argv[k] = awords[k];
                    na_quoted[k] = 0;
                }
                for (int k = 1; k < node->argc; k++) {
                    na_argv[na + k - 1] = sh_strdup(node->argv[k]);
                    na_quoted[na + k - 1] = node->argv_quoted[k];
                }
                na_argv[new_argc] = NULL;
                na_quoted[new_argc] = 0;

                for (int k = 0; k < node->argc; k++) free(node->argv[k]);
                free(node->argv);
                free(node->argv_quoted);
                node->argv = na_argv;
                node->argv_quoted = na_quoted;
                node->argc = new_argc;
                node->argv_cap = new_argc + 1;
                break;
            }
        }
    }

    return node;
}

/* Redirections may follow a compound command as well as a simple one:
 *
 *     while read -r l; do ...; done < "$file"
 *     for i in a b; do ...; done > out.log
 *     { echo hi; } 2>&1
 *
 * The compound parsers (parse_if/for/while/case/funcdef/subshell) leave the
 * lexer sitting on whatever follows their closing keyword, so a redirection
 * that belongs to the whole construct is still unread at that point.  This
 * helper collects it and hangs it off the node, where execute_node_internal
 * applies it around the body.  It is a no-op for nodes that already own the
 * redirection (simple commands parse their own). */
static void parse_compound_redirs(Lexer *l, ASTNode *node) {
    if (!node) return;
    Redir *last = NULL;
    for (Redir *r = node->redirs; r; r = r->next) last = r;

    while (l->token_type == TOK_LREDIR || l->token_type == TOK_RREDIR ||
           l->token_type == TOK_APPEND || l->token_type == TOK_RREDIR2 ||
           l->token_type == TOK_ERRREDIR || l->token_type == TOK_ERRAPPEND ||
            l->token_type == TOK_ERRDUP ||
           l->token_type == TOK_BOTHREDIR ||
           l->token_type == TOK_DLESS || l->token_type == TOK_DLESSDASH) {

        Redir *r = parse_redirection(l);
        if (!r) break;
        if (!node->redirs) node->redirs = r;
        else last->next = r;
        last = r;
    }

    /* bodies are read by parse_complete(), after the whole line */
}

/* ---- parse a command: simple_command | subshell | funcdef | if | for | while -- */
static ASTNode *parse_command(Lexer *l) {
    ASTNode *node = NULL;

    if (l->token_type == TOK_LPAREN) {
        /* subshell: ( list ) */
        lexer_next(l);
        node = ast_new(NODE_SUBSHELL);
        node->left = parse_list(l);
        if (l->token_type == TOK_RPAREN)
            lexer_next(l);
        else
            fprintf(stderr, "besh: expected )\n");
        parse_compound_redirs(l, node);
        return node;
    }

    if (l->token_type == TOK_LBRACE) {
        /* brace group: { list; } — runs in the current shell, unlike a
         * subshell.  The closing brace must be preceded by a `;` or
         * newline; parse_list stops at the TOK_RBRACE token. */
        lexer_next(l);
        node = ast_new(NODE_BRACEGROUP);
        node->left = parse_list(l);
        if (l->token_type == TOK_RBRACE)
            lexer_next(l);
        else
            fprintf(stderr, "besh: expected }\n");
        parse_compound_redirs(l, node);
        return node;
    }

    if (l->token_type == TOK_WORD) {
        char *word = sh_strdup(l->token_text);

        /* check for reserved words */
        if (strcmp(word, "if") == 0) {
            free(word);
            node = parse_if(l);
            parse_compound_redirs(l, node);
            return node;
        }
        if (strcmp(word, "for") == 0) {
            free(word);
            node = parse_for(l);
            parse_compound_redirs(l, node);
            return node;
        }
        if (strcmp(word, "while") == 0 || strcmp(word, "until") == 0) {
            int is_until = (strcmp(word, "until") == 0);
            free(word);
            node = parse_while(l, is_until);
            parse_compound_redirs(l, node);
            return node;
        }
        if (strcmp(word, "case") == 0) {
            free(word);
            node = parse_case(l);
            parse_compound_redirs(l, node);
            return node;
        }
        if (strcmp(word, "function") == 0) {
            free(word);
            lexer_next(l);
            /* function name */
            if (l->token_type == TOK_WORD) {
                char *fname = sh_strdup(l->token_text);
                lexer_next(l);
                return parse_funcdef(l, fname);
            }
            fprintf(stderr, "besh: expected function name\n");
            return NULL;
        }

        /* peek ahead to check for function definition: name() */
        if (l->pos < l->len) {
            int save_pos = l->pos;
            int save_type = l->token_type;
            int save_quoted = l->token_quoted;
            int save_fd = l->token_fd;
            int save_escaped = l->token_escaped;
            char *save_text = sh_strdup(l->token_text);   /* copy before lexer_next frees it */

            int next = lexer_next(l);
            if (next == TOK_LPAREN) {
                /* maybe function def — need ) after */
                int la = lexer_next(l);
                if (la == TOK_RPAREN) {
                    /* it's a function definition: name() { body } */
                    free(save_text);
                    char *fname = word;
                    lexer_next(l);         /* advance past ')' to next token */
                    return parse_funcdef(l, fname);
                }
                /* not a funcdef — rewind */
                free(l->token_text);
                l->token_text = save_text;
                l->pos = save_pos;
                l->token_type = save_type;
                l->token_quoted = save_quoted;
                l->token_fd = save_fd;
                l->token_escaped = save_escaped;
            } else {
                /* rewind */
                free(l->token_text);
                l->token_text = save_text;
                l->pos = save_pos;
                l->token_type = save_type;
                l->token_quoted = save_quoted;
                l->token_fd = save_fd;
                l->token_escaped = save_escaped;
            }
        }

        /* plain word — may start a simple command */
        free(word);  /* parse_simple_command will re-lex it */
    }

    /* ! negation */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "!") == 0 &&
        (l->token_quoted == 0)) {
        lexer_next(l);
        node = ast_new(NODE_NOT);
        node->left = parse_command(l);
        return node;
    }

    return parse_simple_command(l);
}

/* ---- pipeline: '!'? command ('|' command)* ------------------ */
static ASTNode *parse_pipeline(Lexer *l) {
    /* negation */
    int negate = 0;
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "!") == 0 &&
        !l->token_quoted) {
        negate = 1;
        lexer_next(l);
    }

    ASTNode *left = parse_command(l);

    while (l->token_type == TOK_PIPE) {
        lexer_next(l);
        ASTNode *right = parse_command(l);
        ASTNode *pipe = ast_new(NODE_PIPELINE);
        pipe->left = left;
        pipe->right = right;
        left = pipe;
    }

    if (negate) {
        ASTNode *not_node = ast_new(NODE_NOT);
        not_node->left = left;
        left = not_node;
    }

    return left;
}

/* ---- and_or: pipeline (('&&' | '||') pipeline)* ------------- */
static ASTNode *parse_and_or(Lexer *l) {
    ASTNode *left = parse_pipeline(l);

    while (l->token_type == TOK_AND || l->token_type == TOK_OR) {
        int type = l->token_type;
        lexer_next(l);
        ASTNode *right = parse_pipeline(l);
        ASTNode *bin = ast_new(type == TOK_AND ? NODE_AND : NODE_OR);
        bin->left = left;
        bin->right = right;
        left = bin;
    }

    return left;
}

/* ---- check if token ends a clause (do/done/then/fi/etc.) ------ */
static int is_clause_terminator(Lexer *l) {
    if (l->token_type != TOK_WORD) return 0;
    const char *s = l->token_text;
    return (strcmp(s, "do") == 0 || strcmp(s, "done") == 0 ||
            strcmp(s, "then") == 0 || strcmp(s, "fi") == 0 ||
            strcmp(s, "else") == 0 || strcmp(s, "elif") == 0 ||
            strcmp(s, "esac") == 0 || strcmp(s, "in") == 0 ||
            strcmp(s, "}") == 0);
}

/* ---- list: and_or ((';' | '&' | '\n') and_or)* --------------- */
static ASTNode *parse_list(Lexer *l) {
    ASTNode *left = parse_and_or(l);

    while (l->token_type == TOK_SEMI || l->token_type == TOK_BG ||
           l->token_type == TOK_NEWLINE) {
        int tok = l->token_type;

        /* A here-document body follows the *complete command line*, so it
         * must be consumed before the next statement is parsed — otherwise
         * the body lines are read as commands.  The `\n` token is the line
         * boundary; `;` and `&` keep us on the same line, so only the
         * newline branch drains.  Do this *before* lexer_next() so the
         * lexer has not yet tokenised the first body line. */
        if (tok == TOK_NEWLINE && pending_head)
            pending_drain(l);

        lexer_next(l);

        if (tok == TOK_BG) {
            /* background the left child — even at end of input */
            ASTNode *bg = ast_new(NODE_BG);
            bg->left = left;
            left = bg;
            /* if there's a command after &, start a new list */
            if (l->token_type != TOK_EOF && l->token_type != TOK_RPAREN &&
                l->token_type != TOK_RBRACE &&
                l->token_type != TOK_NEWLINE && l->token_type != TOK_SEMI &&
                l->token_type != TOK_DSEMI && !is_clause_terminator(l)) {
                ASTNode *right = parse_and_or(l);
                ASTNode *list = ast_new(NODE_LIST);
                list->left = left;
                list->right = right;
                left = list;
            }
            continue;
        }

        /* trailing separator — ignore */
        if (l->token_type == TOK_EOF || l->token_type == TOK_RPAREN ||
            l->token_type == TOK_RBRACE ||
            l->token_type == TOK_DSEMI || is_clause_terminator(l))
            break;

        /* ; or newline — sequential execution */
        if (l->token_type != TOK_EOF && l->token_type != TOK_RPAREN &&
            l->token_type != TOK_RBRACE &&
            l->token_type != TOK_DSEMI && !is_clause_terminator(l)) {
            ASTNode *right = parse_and_or(l);
            ASTNode *list = ast_new(NODE_LIST);
            list->left = left;
            list->right = right;
            left = list;
        }
    }

    return left;
}

/* ---- parse if: if list; then list; [elif list; then list;] [else list;] fi -- */
static ASTNode *parse_if(Lexer *l) {
    /* 'if' already consumed */
    lexer_next(l);  /* skip 'if' */

    ASTNode *node = ast_new(NODE_IF);
    node->cond = parse_list(l);

    /* expect 'then' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "then") == 0)
        lexer_next(l);
    else
        fprintf(stderr, "besh: expected 'then'\n");

    node->body = parse_list(l);

    /* check for elif / else / fi */
    if (l->token_type == TOK_WORD) {
        if (strcmp(l->token_text, "elif") == 0) {
            /* parse elif as nested if */
            node->else_body = parse_if(l);
        } else if (strcmp(l->token_text, "else") == 0) {
            lexer_next(l);
            node->else_body = parse_list(l);
        }
    }

    /* expect 'fi' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "fi") == 0)
        lexer_next(l);

    return node;
}

/* ---- parse for: for name [in words...]; do list; done -------- */
static ASTNode *parse_for(Lexer *l) {
    /* 'for' already consumed */
    lexer_next(l);

    if (l->token_type != TOK_WORD) {
        fprintf(stderr, "besh: expected variable name after 'for'\n");
        return NULL;
    }

    ASTNode *node = ast_new(NODE_FOR);
    node->func_name = sh_strdup(l->token_text);  /* reuse field for var name */
    lexer_next(l);

    /* check for 'in' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "in") == 0) {
        lexer_next(l);
        /* collect words until ; or newline or 'do' */
        ASTNode *words_node = ast_new(NODE_COMMAND);
        words_node->argv_cap = 64;
        words_node->argv = sh_malloc(words_node->argv_cap * sizeof(char *));
        words_node->argv_quoted = sh_malloc(words_node->argv_cap * sizeof(int));
        words_node->argc = 0;

        /* `;` terminates the word list, but it arrives as TOK_SEMI (not a
         * word), so the loop condition below already stops on it — testing
         * for it in token_text was dead code. */
        while (l->token_type == TOK_WORD &&
               strcmp(l->token_text, "do") != 0) {
            if (words_node->argc >= words_node->argv_cap - 1) {
                words_node->argv_cap *= 2;
                words_node->argv = sh_realloc(words_node->argv,
                    words_node->argv_cap * sizeof(char *));
                words_node->argv_quoted = sh_realloc(words_node->argv_quoted,
                    words_node->argv_cap * sizeof(int));
            }
            words_node->argv_quoted[words_node->argc] = l->token_quoted;
            words_node->argv[words_node->argc++] = sh_strdup(l->token_text);
            lexer_next(l);
        }
        words_node->argv[words_node->argc] = NULL;
        node->cond = words_node;
    } else {
        /* no 'in' — iterate over positional params (empty for now) */
        node->cond = NULL;
    }

    /* skip ; or newline before 'do' */
    if (l->token_type == TOK_SEMI || l->token_type == TOK_NEWLINE)
        lexer_next(l);

    /* expect 'do' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "do") == 0)
        lexer_next(l);

    node->body = parse_list(l);

    /* expect 'done' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "done") == 0)
        lexer_next(l);

    return node;
}

/* ---- parse while/until: while list; do list; done ------------- */
static ASTNode *parse_while(Lexer *l, int is_until) {
    /* 'while' or 'until' keyword is the current token */
    lexer_next(l);  /* skip keyword */

    ASTNode *node = ast_new(NODE_WHILE);
    node->argc = is_until ? 1 : 0;  /* flag for executor */

    node->cond = parse_list(l);

    /* skip ; or newline */
    if (l->token_type == TOK_SEMI || l->token_type == TOK_NEWLINE)
        lexer_next(l);

    /* expect 'do' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "do") == 0)
        lexer_next(l);

    node->body = parse_list(l);

    /* expect 'done' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "done") == 0)
        lexer_next(l);

    return node;
}

/* ---- parse case: case word in pat) body;; ... esac ----------- */
static ASTNode *parse_case(Lexer *l) {
    /* 'case' already consumed */
    lexer_next(l);

    if (l->token_type != TOK_WORD) {
        fprintf(stderr, "besh: expected word after 'case'\n");
        return NULL;
    }

    ASTNode *node = ast_new(NODE_CASE);
    node->case_word = sh_strdup(l->token_text);
    lexer_next(l);

    /* expect 'in' */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "in") == 0)
        lexer_next(l);

    if (l->token_type == TOK_NEWLINE) lexer_next(l);

    /* collect patterns and bodies */
    int cap = 8;
    node->case_patterns = sh_malloc(cap * sizeof(char *));
    node->case_pat_literal = sh_malloc(cap * sizeof(int));
    node->case_bodies = sh_malloc(cap * sizeof(ASTNode *));
    node->case_count = 0;

    while (l->token_type != TOK_EOF) {
        if (l->token_type == TOK_WORD && strcmp(l->token_text, "esac") == 0) {
            lexer_next(l);
            break;
        }

        /* collect patterns until )
         *
         * Both limits here used to truncate silently: a branch with more
         * than 64 alternatives dropped the extras (and, worse, left the
         * 65th token to be mistaken for the next branch's pattern), and a
         * joined pattern longer than 4 KiB was cut mid-way, so the branch
         * simply never matched.  Grow instead of dropping. */
        int pcap = 8, npat = 0;
        char **patterns = sh_malloc(pcap * sizeof(char *));
        int *pat_lit = sh_malloc(pcap * sizeof(int));
        size_t pat_len = 0;
        while (l->token_type == TOK_WORD) {
            if (npat == pcap) {
                pcap *= 2;
                patterns = sh_realloc(patterns, pcap * sizeof(char *));
                pat_lit  = sh_realloc(pat_lit, pcap * sizeof(int));
            }
            patterns[npat] = sh_strdup(l->token_text);
            /* A pattern written entirely as one quoted word compares
             * literally (`case x in "a*")`), while an unquoted one is a
             * glob.  The lexer only records "quotes were involved", so
             * this is the closest exact test available: the word consisted
             * of a single quoted span and nothing else. */
            pat_lit[npat] = l->token_quoted ? 1 : 0;
            pat_len += strlen(patterns[npat]) + 1;   /* +1 for the `|` */
            npat++;
            lexer_next(l);
            if (l->token_type == TOK_PIPE) lexer_next(l);  /* | between patterns */
        }

        if (l->token_type == TOK_RPAREN) lexer_next(l);

        /* join patterns with | */
        char *pat_buf = sh_malloc(pat_len + 1);
        size_t at = 0;
        int all_literal = (npat > 0);
        for (int i = 0; i < npat; i++) {
            if (i > 0) pat_buf[at++] = '|';
            size_t nl = strlen(patterns[i]);
            memcpy(pat_buf + at, patterns[i], nl);
            at += nl;
            if (!pat_lit[i]) all_literal = 0;
            free(patterns[i]);
        }
        pat_buf[at] = '\0';
        free(patterns);
        free(pat_lit);
        int pat_is_literal = all_literal;

        if (node->case_count >= cap) {
            cap *= 2;
            node->case_patterns = sh_realloc(node->case_patterns, cap * sizeof(char *));
            node->case_pat_literal = sh_realloc(node->case_pat_literal, cap * sizeof(int));
            node->case_bodies = sh_realloc(node->case_bodies, cap * sizeof(ASTNode *));
        }

        /* parse body until ;; */
        ASTNode *body = ast_new(NODE_LIST);
        body->left = parse_list(l);
        body->right = NULL;

        /* expect ;; */
        if (l->token_type == TOK_DSEMI) {
            lexer_next(l);
        }
        /* skip newlines between case branches */
        while (l->token_type == TOK_NEWLINE) lexer_next(l);

        node->case_patterns[node->case_count] = pat_buf;
        node->case_pat_literal[node->case_count] = pat_is_literal;
        node->case_bodies[node->case_count] = body;
        node->case_count++;
    }

    return node;
}

/* ---- parse function definition: name() { list; } ------------- */
static ASTNode *parse_funcdef(Lexer *l, char *name) {
    /* After "function name" or "name()", we expect { body; } */
    /* ( ) already consumed */

    ASTNode *node = ast_new(NODE_FUNCDEF);
    node->func_name = name;

    /* expect { — either its own token (TOK_LBRACE) or, for compatibility
     * with the older lexer, a plain WORD "{" */
    if (l->token_type == TOK_LBRACE ||
        (l->token_type == TOK_WORD && strcmp(l->token_text, "{") == 0))
        lexer_next(l);
    else {
        fprintf(stderr, "besh: expected '{' in function definition\n");
        node->func_body = NULL;
        return node;
    }

    node->func_body = parse_list(l);

    /* expect } */
    if (l->token_type == TOK_RBRACE ||
        (l->token_type == TOK_WORD && strcmp(l->token_text, "}") == 0))
        lexer_next(l);
    /* also accept TOK_RPAREN for compatibility */
    else if (l->token_type == TOK_RPAREN) {
        /* this shouldn't happen for { }, but handle gracefully */
    }

    /* The definition is *not* registered here.  Parsing happens for the
     * whole command line before a single statement runs, so registering at
     * parse time made the definition order wrong whenever the two were
     * interleaved on one line:
     *
     *     h() { echo old; }; unset -f h; h() { echo new; }; h
     *
     * The parse-time registration inserted `h`, `unset -f h` then removed
     * it again when it executed, and the call found nothing (bash prints
     * `new`).  Registration is deferred to execution time instead, where
     * the statements run in their real order — see func_register() and
     * NODE_FUNCDEF in executor.c. */
    /* kept so NODE_FUNCDEF can install it at execution time */
    return node;
}

/* Install (or replace) a shell function.  Called when a NODE_FUNCDEF is
 * *executed*, so that definition, `unset -f` and invocation observe the
 * order the user actually wrote.  Redefinition overwrites in place:
 * exec_func_lookup() returns the first match, so appending a duplicate
 * would pin the stale body forever.
 *
 * Takes ownership of `body` on success. */
void func_register(const char *name, ASTNode *body) {
    Shell *sh = shell_get();
    int slot = -1;
    for (int i = 0; i < sh->nfuncs; i++) {
        if (strcmp(sh->funcs[i].name, name) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        if (sh->nfuncs >= sh->funcs_cap) {
            sh->funcs_cap = sh->funcs_cap ? sh->funcs_cap * 2 : 16;
            sh->funcs = sh_realloc(sh->funcs, sh->funcs_cap * sizeof(Function));
        }
        slot = sh->nfuncs++;
        sh->funcs[slot].name = sh_strdup(name);
        sh->funcs[slot].body = NULL;
    } else {
        ast_free(sh->funcs[slot].body);   /* drop the superseded body */
    }
    sh->funcs[slot].body = body;
}

/* ---- entry point — parse a complete command line ------------- */
ASTNode *parse_complete(Lexer *l) {
    lexer_next(l);  /* prime first token */
    if (l->token_type == TOK_EOF) return NULL;

    /* a previous parse may have been abandoned mid-line (syntax error,
     * early return); drop anything it queued so the bodies of this line
     * are not read against a stale list */
    while (pending_head) {
        PendingHeredoc *n = pending_head;
        pending_head = n->next;
        free(n);
    }
    pending_tail = NULL;

    ASTNode *ast = parse_list(l);

    /* Anything still queued belongs to a line that had no trailing newline
     * (e.g. `cat <<EOF` as the last text in -c).  Reading it now is the
     * only chance, and matches bash's end-of-input behaviour. */
    if (pending_head) pending_drain(l);

    return ast;
}
