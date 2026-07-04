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

/* ---- AST node allocation ------------------------------------- */
static ASTNode *ast_new(NodeType type) {
    ASTNode *n = sh_malloc(sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->type = type;
    return n;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
    case NODE_COMMAND:
    case NODE_REDIRECT:
        if (node->argv) {
            for (int i = 0; i < node->argc; i++) free(node->argv[i]);
            free(node->argv);
        }
        {
            Redir *r = node->redirs;
            while (r) {
                Redir *next = r->next;
                free(r->filename);
                free(r->heredoc);
                free(r);
                r = next;
            }
        }
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
        ast_free(node->left);
        break;
    case NODE_FUNCDEF:
        free(node->func_name);
        ast_free(node->func_body);
        break;
    case NODE_FOR:
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

    /* check if there is an fd number before the operator */
    int fd_num = -1;
    if (l->token_type == TOK_WORD) {
        char *end;
        long val = strtol(l->token_text, &end, 10);
        if (*end == '\0' && end != l->token_text) {
            fd_num = (int)val;
            lexer_next(l);  /* consume the fd number */
        }
    }

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
    case TOK_BOTHREDIR:
        r->type = REDIR_BOTH;
        r->src_fd = -1;  /* both stdout and stderr */
        break;
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
            r->filename = sh_strdup(l->token_text);
            r->quoted = l->token_quoted;
            /* read heredoc content */
            r->heredoc = lexer_heredoc(l, r->filename,
                                       tok == TOK_DLESSDASH);
            /* refresh token — lexer_heredoc advanced pos past the body */
            lexer_next(l);
        }
    } else {
        /* normal redirection — next token is filename */
        if (l->token_type != TOK_WORD) {
            fprintf(stderr, "besh: parse error: expected filename after redirection\n");
            r->filename = sh_strdup("/dev/null");
        } else {
            r->filename = sh_strdup(l->token_text);
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
    node->argc = 0;
    node->redirs = NULL;

    Redir *last_redir = NULL;

    while (1) {
        /* check for redirections */
        if (l->token_type == TOK_LREDIR || l->token_type == TOK_RREDIR ||
            l->token_type == TOK_APPEND || l->token_type == TOK_RREDIR2 ||
            l->token_type == TOK_ERRREDIR || l->token_type == TOK_ERRAPPEND ||
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
            }
            node->argv[node->argc++] = sh_strdup(l->token_text);
            node->argv[node->argc] = NULL;
            lexer_next(l);
            continue;
        }

        /* anything else ends the command */
        break;
    }

    /* check if first word is a builtin alias — expand it */
    if (node->argc > 0) {
        Shell *sh = shell_get();
        for (int i = 0; i < sh->naliases; i++) {
            if (strcmp(node->argv[0], sh->aliases[i].name) == 0) {
                /* replace first word with alias expansion */
                char *exp = sh_strdup(sh->aliases[i].value);
                /* parse the alias value and insert its words */
                /* For simplicity, just replace argv[0] with expanded value */
                /* A full implementation would lex the alias value */
                free(node->argv[0]);
                node->argv[0] = exp;
                break;
            }
        }
    }

    return node;
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
        return node;
    }

    if (l->token_type == TOK_WORD) {
        char *word = sh_strdup(l->token_text);

        /* check for reserved words */
        if (strcmp(word, "if") == 0) {
            free(word);
            return parse_if(l);
        }
        if (strcmp(word, "for") == 0) {
            free(word);
            return parse_for(l);
        }
        if (strcmp(word, "while") == 0 || strcmp(word, "until") == 0) {
            int is_until = (strcmp(word, "until") == 0);
            free(word);
            return parse_while(l, is_until);
        }
        if (strcmp(word, "case") == 0) {
            free(word);
            return parse_case(l);
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
            } else {
                /* rewind */
                free(l->token_text);
                l->token_text = save_text;
                l->pos = save_pos;
                l->token_type = save_type;
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
        lexer_next(l);

        /* trailing separator — ignore */
        if (l->token_type == TOK_EOF || l->token_type == TOK_RPAREN ||
            l->token_type == TOK_DSEMI || is_clause_terminator(l))
            break;

        if (tok == TOK_BG) {
            /* background the left child */
            ASTNode *bg = ast_new(NODE_BG);
            bg->left = left;
            left = bg;
            /* if there's a command after &, start a new list */
            if (l->token_type != TOK_EOF && l->token_type != TOK_RPAREN &&
                l->token_type != TOK_NEWLINE && l->token_type != TOK_SEMI &&
                l->token_type != TOK_DSEMI && !is_clause_terminator(l)) {
                ASTNode *right = parse_and_or(l);
                ASTNode *list = ast_new(NODE_LIST);
                list->left = left;
                list->right = right;
                left = list;
            }
        } else {
            /* ; or newline — sequential execution */
            if (l->token_type != TOK_EOF && l->token_type != TOK_RPAREN &&
                l->token_type != TOK_DSEMI && !is_clause_terminator(l)) {
                ASTNode *right = parse_and_or(l);
                ASTNode *list = ast_new(NODE_LIST);
                list->left = left;
                list->right = right;
                left = list;
            }
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
        words_node->argc = 0;

        while (l->token_type == TOK_WORD &&
               strcmp(l->token_text, "do") != 0 &&
               strcmp(l->token_text, ";") != 0) {
            if (words_node->argc >= words_node->argv_cap - 1) {
                words_node->argv_cap *= 2;
                words_node->argv = sh_realloc(words_node->argv,
                    words_node->argv_cap * sizeof(char *));
            }
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
    node->case_bodies = sh_malloc(cap * sizeof(ASTNode *));
    node->case_count = 0;

    while (l->token_type != TOK_EOF) {
        if (l->token_type == TOK_WORD && strcmp(l->token_text, "esac") == 0) {
            lexer_next(l);
            break;
        }

        /* collect patterns until ) */
        char *patterns[64];
        int npat = 0;
        while (l->token_type == TOK_WORD && npat < 64) {
            patterns[npat++] = sh_strdup(l->token_text);
            lexer_next(l);
            if (l->token_type == TOK_PIPE) lexer_next(l);  /* | between patterns */
        }

        if (l->token_type == TOK_RPAREN) lexer_next(l);

        /* join patterns with | */
        char pat_buf[4096] = "";
        for (int i = 0; i < npat; i++) {
            if (i > 0) strcat(pat_buf, "|");
            strncat(pat_buf, patterns[i], sizeof(pat_buf) - strlen(pat_buf) - 1);
            free(patterns[i]);
        }

        if (node->case_count >= cap) {
            cap *= 2;
            node->case_patterns = sh_realloc(node->case_patterns, cap * sizeof(char *));
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

        node->case_patterns[node->case_count] = sh_strdup(pat_buf);
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

    /* expect { */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "{") == 0)
        lexer_next(l);
    else {
        fprintf(stderr, "besh: expected '{' in function definition\n");
        node->func_body = NULL;
        return node;
    }

    node->func_body = parse_list(l);

    /* expect } */
    if (l->token_type == TOK_WORD && strcmp(l->token_text, "}") == 0)
        lexer_next(l);
    /* also accept TOK_RPAREN for compatibility */
    else if (l->token_type == TOK_RPAREN) {
        /* this shouldn't happen for { }, but handle gracefully */
    }

    /* register the function */
    Shell *sh = shell_get();
    if (sh->nfuncs >= sh->funcs_cap) {
        sh->funcs_cap *= 2;
        sh->funcs = sh_realloc(sh->funcs, sh->funcs_cap * sizeof(Function));
    }
    sh->funcs[sh->nfuncs].name = sh_strdup(name);
    sh->funcs[sh->nfuncs].body = node->func_body;
    sh->nfuncs++;

    /* transfer ownership of body to the funcs table so ast_free
     * on this node does not free it (prevents double-free) */
    node->func_body = NULL;

    return node;
}

/* ---- entry point — parse a complete command line ------------- */
ASTNode *parse_complete(Lexer *l) {
    lexer_next(l);  /* prime first token */
    if (l->token_type == TOK_EOF) return NULL;
    return parse_list(l);
}
