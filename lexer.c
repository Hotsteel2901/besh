/* ================================================================
 *  lexer.c — tokenizer for besh
 *
 *  Breaks input into tokens: words, operators (|, &&, ||, ;, &,
 *  <, >, >>, <<, (, )), handles single/double quotes,
 *  backslash escapes, and here-documents.
 * ================================================================ */

#include "shell.h"

Lexer *lexer_new(const char *input) {
    Lexer *l = sh_malloc(sizeof(Lexer));
    l->input = input;
    l->pos = 0;
    l->len = strlen(input);
    l->lineno = 1;
    l->token_type = 0;
    l->token_text = NULL;
    l->token_quoted = 0;
    return l;
}

void lexer_free(Lexer *l) {
    if (!l) return;
    free(l->token_text);
    free(l);
}

/* ---- helpers ------------------------------------------------- */

static void lexer_skip_whitespace(Lexer *l) {
    while (l->pos < l->len) {
        int c = (unsigned char)l->input[l->pos];
        if (c != ' ' && c != '\t') break;
        l->pos++;
    }
}

static void lexer_skip_comment(Lexer *l) {
    while (l->pos < l->len && l->input[l->pos] != '\n')
        l->pos++;
}

/* Characters that must be protected with a backslash in the token text so
 * that a later stage (expand_string / brace / glob) treats them literally. */
static int lex_protect(int c) {
    return c == '$' || c == '`' || c == '"' || c == '\'' || c == '\\' ||
           c == '~' || c == '*' || c == '?' || c == '[' || c == ']' ||
           c == '{' || c == '}';
}

/* variable-name characters following '$' */
static int lex_name_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int lex_name_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void lex_grow(char **buf, int *cap, int need) {
    if (need > *cap) {
        while (*cap < need) *cap *= 2;
        *buf = sh_realloc(*buf, *cap);
    }
}

/* append one char verbatim */
static void lex_append_raw(char **buf, int *len, int *cap, int c) {
    lex_grow(buf, cap, *len + 2);
    (*buf)[(*len)++] = (char)c;
}

/* append one char, escaping it if it is special to a later stage */
static void lex_append(char **buf, int *len, int *cap, int c) {
    int esc = lex_protect(c);
    lex_grow(buf, cap, *len + (esc ? 3 : 2));
    if (esc) (*buf)[(*len)++] = '\\';
    (*buf)[(*len)++] = (char)c;
}

/* Emit `$name` as `${name}` and advance past the name.  This keeps a
 * variable name from absorbing characters that live in a following,
 * separate quoted segment — bash ends the name at the closing quote
 * (e.g. 'a'"$a"'b' must yield axb, not a + $ab). */
static void lex_emit_dollar_name(Lexer *l, char **buf, int *blen, int *bcap) {
    int j = l->pos + 1;
    while (j < l->len && lex_name_char((unsigned char)l->input[j])) j++;
    lex_append_raw(buf, blen, bcap, '$');
    lex_append_raw(buf, blen, bcap, '{');
    for (int k = l->pos + 1; k < j; k++)
        lex_append_raw(buf, blen, bcap, (unsigned char)l->input[k]);
    lex_append_raw(buf, blen, bcap, '}');
    l->pos = j;
}

/* Copy a whole `$(...)` / `$((...))` substitution verbatim and advance past
 * it.  The text is re-parsed by command substitution later, so it must not
 * be escape-mangled here (quotes and backslashes inside are meaningful). */
static void lex_copy_dollar_paren(Lexer *l, char **buf, int *blen, int *bcap) {
    int start = l->pos;
    int i = l->pos + 2;              /* skip '$(' */
    int depth = 1;
    if (i < l->len && l->input[i] == '(') { i++; depth = 2; }  /* $(( */
    while (i < l->len && depth > 0) {
        char d = l->input[i];
        if (d == '\\' && i + 1 < l->len) { i += 2; continue; }
        if (d == '\'') {
            i++;
            while (i < l->len && l->input[i] != '\'') i++;
            if (i < l->len) i++;
            continue;
        }
        if (d == '"') {
            i++;
            while (i < l->len && l->input[i] != '"') {
                if (l->input[i] == '\\' && i + 1 < l->len) i++;
                i++;
            }
            if (i < l->len) i++;
            continue;
        }
        if (d == '(') depth++;
        else if (d == ')') { depth--; if (depth == 0) { i++; break; } }
        i++;
    }
    for (int k = start; k < i; k++)
        lex_append_raw(buf, blen, bcap, (unsigned char)l->input[k]);
    l->pos = i;
}

/* read a single-quoted string: '...'
 * Every character is literal, so each special character is backslash-
 * protected in the returned text (quote semantics survive until expansion). */
static char *read_single_quoted(Lexer *l) {
    l->pos++;  /* skip opening ' */
    int blen = 0, bcap = 64;
    char *buf = sh_malloc(bcap);

    while (l->pos < l->len && l->input[l->pos] != '\'') {
        if (l->input[l->pos] == '\n') l->lineno++;
        lex_append(&buf, &blen, &bcap, (unsigned char)l->input[l->pos]);
        l->pos++;
    }
    if (l->pos < l->len) l->pos++;  /* skip closing ' */
    else fprintf(stderr, "besh: unterminated single-quoted string\n");
    buf[blen] = '\0';
    return buf;
}

/* read a double-quoted string: " ... "
 * `$` and backtick stay unescaped (expansion still happens), while the
 * other special characters are backslash-protected so that they are not
 * expanded, globbed or otherwise reinterpreted later. */
static char *read_double_quoted(Lexer *l) {
    l->pos++;  /* skip opening " */
    int blen = 0, bcap = 64;
    int closed = 0;
    int var_depth = 0;   /* nesting of $ { ... } */
    int prev_dollar = 0;
    char *buf = sh_malloc(bcap);

    while (l->pos < l->len) {
        char c = l->input[l->pos];
        if (c == '"') { l->pos++; closed = 1; break; }
        if (c == '\\') {
            l->pos++;
            prev_dollar = 0;
            if (l->pos < l->len) {
                char n = l->input[l->pos];
                if (n == '$' || n == '`' || n == '"' || n == '\\') {
                    lex_append(&buf, &blen, &bcap, (unsigned char)n);
                    l->pos++;
                } else if (n == '\n') {
                    l->pos++; l->lineno++;  /* line continuation */
                } else {
                    /* backslash is literal here */
                    lex_append(&buf, &blen, &bcap, '\\');
                    lex_append(&buf, &blen, &bcap, (unsigned char)n);
                    l->pos++;
                }
            } else {
                lex_append(&buf, &blen, &bcap, '\\');
            }
            continue;
        }
        if (c == '\n') l->lineno++;

        /* $(...) / $((...)) must be copied verbatim (re-parsed later) */
        if (c == '$' && l->pos + 1 < l->len && l->input[l->pos + 1] == '(') {
            lex_copy_dollar_paren(l, &buf, &blen, &bcap);
            prev_dollar = 0;
            continue;
        }
        /* keep `$name` self-delimiting */
        if (c == '$' && l->pos + 1 < l->len &&
            lex_name_start((unsigned char)l->input[l->pos + 1])) {
            lex_emit_dollar_name(l, &buf, &blen, &bcap);
            prev_dollar = 0;
            continue;
        }
        if (c == '$') {                     /* ${...}, $?, $(, ... */
            lex_append_raw(&buf, &blen, &bcap, '$');
            prev_dollar = 1;
            l->pos++;
            continue;
        }
        if (c == '`') {
            lex_append_raw(&buf, &blen, &bcap, '`');
            prev_dollar = 0;
            l->pos++;
            continue;
        }
        /* $* is a special parameter, not a glob */
        if (prev_dollar && c == '*') {
            lex_append_raw(&buf, &blen, &bcap, '*');
            prev_dollar = 0;
            l->pos++;
            continue;
        }
        if (c == '{') {
            if (prev_dollar) { lex_append_raw(&buf, &blen, &bcap, '{'); var_depth++; }
            else             { lex_append(&buf, &blen, &bcap, '{'); }
            prev_dollar = 0;
            l->pos++;
            continue;
        }
        if (c == '}') {
            if (var_depth > 0) { lex_append_raw(&buf, &blen, &bcap, '}'); var_depth--; }
            else               { lex_append(&buf, &blen, &bcap, '}'); }
            prev_dollar = 0;
            l->pos++;
            continue;
        }
        lex_append(&buf, &blen, &bcap, (unsigned char)c);
        prev_dollar = 0;
        l->pos++;
    }
    buf[blen] = '\0';
    if (!closed)
        fprintf(stderr, "besh: unterminated double-quoted string\n");
    return buf;
}

/* read a word token: contiguous sequence of non-special, non-whitespace chars.
 * stops at whitespace or special characters, unless quoted/escaped. */
static char *read_word(Lexer *l) {
    char *buf = sh_malloc(1024);
    int blen = 0, bcap = 1024;

    while (l->pos < l->len) {
        char c = l->input[l->pos];

        /* whitespace ends word */
        if (c == ' ' || c == '\t' || c == '\n') break;

        /* handle $(...) and $((...)) before the special-char check,
         * since '(' would otherwise split the word */
        if (c == '$' && l->pos + 1 < l->len && l->input[l->pos + 1] == '(') {
            lex_copy_dollar_paren(l, &buf, &blen, &bcap);
            continue;
        }

        /* handle backtick command substitution inside a word */
        if (c == '`') {
            int start = l->pos;
            l->pos++;                          /* skip opening backtick */
            while (l->pos < l->len && l->input[l->pos] != '`') {
                if (l->input[l->pos] == '\\' && l->pos + 1 < l->len)
                    l->pos++;
                if (l->pos < l->len) l->pos++;
            }
            if (l->pos < l->len) l->pos++;    /* skip closing backtick */
            int sublen = l->pos - start;
            while (blen + sublen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, l->input + start, sublen);
            blen += sublen;
            continue;
        }

        /* special characters end word (but are returned separately) */
        if (sh_is_special_char(c) && !(c == '\n' || c == '\0')) break;

        /* handle backslash escape: the escaped char is literal, so protect
         * it in the token text if a later stage would treat it specially */
        if (c == '\\') {
            l->pos++;
            if (l->pos < l->len) {
                char n = l->input[l->pos];
                if (n == '\n') {
                    l->pos++; l->lineno++; continue;  /* line continuation */
                }
                if (lex_protect((unsigned char)n))
                    lex_append(&buf, &blen, &bcap, (unsigned char)n);
                else
                    lex_append_raw(&buf, &blen, &bcap, (unsigned char)n);
                l->pos++;
            }
            continue;
        }

        /* handle quotes inside words (no space before/after) */
        if (c == '\'') {
            char *inner = read_single_quoted(l);
            int ilen = strlen(inner);
            lex_grow(&buf, &bcap, blen + ilen + 1);
            memcpy(buf + blen, inner, ilen);
            blen += ilen;
            free(inner);
            l->token_quoted = 1;
            continue;
        }
        if (c == '"') {
            char *inner = read_double_quoted(l);
            int ilen = strlen(inner);
            lex_grow(&buf, &bcap, blen + ilen + 1);
            memcpy(buf + blen, inner, ilen);
            blen += ilen;
            free(inner);
            l->token_quoted = 1;
            continue;
        }

        /* keep `$name` self-delimiting (see lex_emit_dollar_name) */
        if (c == '$' && l->pos + 1 < l->len &&
            lex_name_start((unsigned char)l->input[l->pos + 1])) {
            lex_emit_dollar_name(l, &buf, &blen, &bcap);
            continue;
        }

        if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
        buf[blen++] = l->input[l->pos++];
    }
    buf[blen] = '\0';
    return buf;
}

/* read here-document content */
char *lexer_heredoc(Lexer *l, const char *delim, int strip_tabs) {
    char *buf = sh_malloc(HEREDOC_BUF);
    int blen = 0, bcap = HEREDOC_BUF;
    int dlen = strlen(delim);

    /* advance past << or <<- and the delimiter */
    while (l->pos < l->len && l->input[l->pos] != '\n')
        l->pos++;
    if (l->pos < l->len) l->pos++;  /* skip newline */

    while (l->pos < l->len) {
        /* start of a line */

        if (strip_tabs) {
            while (l->pos < l->len && l->input[l->pos] == '\t')
                l->pos++;
        }

        /* check if this line matches the delimiter */
        int match = 1;
        int peek = l->pos;
        for (int i = 0; i < dlen; i++) {
            if (peek + i >= l->len || l->input[peek + i] != delim[i]) {
                match = 0;
                break;
            }
        }
        /* delimiter must be followed by newline or EOF */
        if (match) {
            int after = peek + dlen;
            if (after >= l->len || l->input[after] == '\n') {
                l->pos = after;
                buf[blen] = '\0';
                return buf;
            }
        }

        /* copy this line */
        while (l->pos < l->len && l->input[l->pos] != '\n') {
            if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            buf[blen++] = l->input[l->pos++];
        }
        if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
        buf[blen++] = '\n';
        if (l->pos < l->len) l->pos++;  /* skip newline */
    }

    /* EOF reached without delimiter */
    buf[blen] = '\0';
    fprintf(stderr, "besh: warning: here-document at line %d delimited by "
            "end-of-file (wanted `%s')\n", l->lineno, delim);
    return buf;
}

/* ================================================================
 *  MAIN LEXER — lexer_next()
 *
 *  Returns the next token type.  The token text is stored in
 *  l->token_text (caller should NOT free it — it is owned by the
 *  lexer and overwritten on next call).
 * ================================================================ */
int lexer_next(Lexer *l) {
    free(l->token_text);
    l->token_text = NULL;
    l->token_quoted = 0;

    lexer_skip_whitespace(l);

    /* skip comments (lines starting with #, but not $# or # inside words) */
    if (l->pos < l->len && l->input[l->pos] == '#') {
        lexer_skip_comment(l);
        lexer_skip_whitespace(l);
    }

    if (l->pos >= l->len) {
        l->token_type = TOK_EOF;
        l->token_text = sh_strdup("");
        return TOK_EOF;
    }

    char c = l->input[l->pos];

    /* newline — return as TOK_NEWLINE, but skip consecutive ones */
    if (c == '\n') {
        l->pos++;
        l->lineno++;
        /* skip extra newlines */
        while (l->pos < l->len && l->input[l->pos] == '\n') {
            l->pos++;
            l->lineno++;
        }
        l->token_type = TOK_NEWLINE;
        l->token_text = sh_strdup("\n");
        return TOK_NEWLINE;
    }

    /* semicolon or ;; (case separator) */
    if (c == ';') {
        l->pos++;
        if (l->pos < l->len && l->input[l->pos] == ';') {
            l->pos++;
            l->token_type = TOK_DSEMI;
            l->token_text = sh_strdup(";;");
            return TOK_DSEMI;
        }
        l->token_type = TOK_SEMI;
        l->token_text = sh_strdup(";");
        return TOK_SEMI;
    }

    /* ampersand: &, &&, &>, >& */
    if (c == '&') {
        l->pos++;
        if (l->pos < l->len && l->input[l->pos] == '&') {
            l->pos++;
            l->token_type = TOK_AND;
            l->token_text = sh_strdup("&&");
            return TOK_AND;
        }
        if (l->pos < l->len && l->input[l->pos] == '>') {
            l->pos++;
            l->token_type = TOK_BOTHREDIR;
            l->token_text = sh_strdup("&>");
            return TOK_BOTHREDIR;
        }
        l->token_type = TOK_BG;
        l->token_text = sh_strdup("&");
        return TOK_BG;
    }

    /* pipe: |, || */
    if (c == '|') {
        l->pos++;
        if (l->pos < l->len && l->input[l->pos] == '|') {
            l->pos++;
            l->token_type = TOK_OR;
            l->token_text = sh_strdup("||");
            return TOK_OR;
        }
        l->token_type = TOK_PIPE;
        l->token_text = sh_strdup("|");
        return TOK_PIPE;
    }

    /* less-than: <, <<, <<-, <& */
    if (c == '<') {
        l->pos++;
        if (l->pos < l->len && l->input[l->pos] == '<') {
            l->pos++;
            if (l->pos < l->len && l->input[l->pos] == '-') {
                l->pos++;
                l->token_type = TOK_DLESSDASH;
                l->token_text = sh_strdup("<<-");
                return TOK_DLESSDASH;
            }
            l->token_type = TOK_DLESS;
            l->token_text = sh_strdup("<<");
            return TOK_DLESS;
        }
        if (l->pos < l->len && l->input[l->pos] == '&') {
            l->pos++;
            l->token_type = TOK_LREDIR;
            l->token_text = sh_strdup("<&");
            return TOK_LREDIR;
        }
        l->token_type = TOK_LREDIR;
        l->token_text = sh_strdup("<");
        return TOK_LREDIR;
    }

    /* greater-than: >, >>, >|, >&, 2>, 2>> */
    if (c == '>' || (c == '2' && l->pos + 1 < l->len &&
                     (l->input[l->pos + 1] == '>' || l->input[l->pos + 1] == '|'))) {
        if (c == '2') {
            l->pos++;  /* skip '2' */
            if (l->pos < l->len && l->input[l->pos] == '>') {
                l->pos++;
                if (l->pos < l->len && l->input[l->pos] == '>') {
                    l->pos++;
                    l->token_type = TOK_ERRAPPEND;
                    l->token_text = sh_strdup("2>>");
                    return TOK_ERRAPPEND;
                }
                l->token_type = TOK_ERRREDIR;
                l->token_text = sh_strdup("2>");
                return TOK_ERRREDIR;
            }
        }
        if (c == '>') {
            l->pos++;
            if (l->pos < l->len) {
                if (l->input[l->pos] == '>') {
                    l->pos++;
                    l->token_type = TOK_APPEND;
                    l->token_text = sh_strdup(">>");
                    return TOK_APPEND;
                }
                if (l->input[l->pos] == '|') {
                    l->pos++;
                    l->token_type = TOK_RREDIR2;
                    l->token_text = sh_strdup(">|");
                    return TOK_RREDIR2;
                }
                if (l->input[l->pos] == '&') {
                    l->pos++;
                    l->token_type = TOK_BOTHREDIR;
                    l->token_text = sh_strdup(">&");
                    return TOK_BOTHREDIR;
                }
            }
            l->token_type = TOK_RREDIR;
            l->token_text = sh_strdup(">");
            return TOK_RREDIR;
        }
    }

    /* parentheses */
    if (c == '(') {
        l->pos++;
        l->token_type = TOK_LPAREN;
        l->token_text = sh_strdup("(");
        return TOK_LPAREN;
    }
    if (c == ')') {
        l->pos++;
        l->token_type = TOK_RPAREN;
        l->token_text = sh_strdup(")");
        return TOK_RPAREN;
    }

    /* word token — read_word handles quotes, backticks and $(...)
     * internally, and merges adjacent quoted segments ("a"'b' → ab) */
    l->token_text = read_word(l);
    l->token_type = TOK_WORD;
    return TOK_WORD;
}
