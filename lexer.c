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

/* read a single-quoted string: '...' */
static char *read_single_quoted(Lexer *l) {
    int start = l->pos;  /* skip opening ' */
    l->pos++;
    while (l->pos < l->len && l->input[l->pos] != '\'') {
        if (l->input[l->pos] == '\n') l->lineno++;
        l->pos++;
    }
    int end = l->pos;
    if (l->pos < l->len) l->pos++;  /* skip closing ' */
    else {
        fprintf(stderr, "besh: unterminated single-quoted string\n");
    }
    return sh_strndup(l->input + start + 1, end - start - 1);
}

/* read a double-quoted string: " ... "  — handles \ escapes and $ expansion */
static char *read_double_quoted(Lexer *l) {
    l->pos++;  /* skip opening " */
    char *buf = sh_malloc(4096);
    int blen = 0, bcap = 4096;

    while (l->pos < l->len) {
        char c = l->input[l->pos];
        if (c == '"') {
            l->pos++;
            buf[blen] = '\0';
            return buf;
        }
        if (c == '\\') {
            l->pos++;
            if (l->pos < l->len) {
                char n = l->input[l->pos];
                switch (n) {
                case '"': case '\\': case '$': case '`':
                    buf[blen++] = n; l->pos++; break;
                case '\n':
                    l->pos++; l->lineno++; break;  /* line continuation */
                default:
                    buf[blen++] = '\\'; break;
                }
            }
            continue;
        }
        if (c == '\n') l->lineno++;
        /* ensure capacity */
        if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
        buf[blen++] = l->input[l->pos++];
    }
    /* unterminated */
    buf[blen] = '\0';
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
            int start = l->pos;
            l->pos += 2;                       /* skip '$(' */
            int depth = 1;
            /* detect arithmetic form $(( ... )) */
            if (l->pos < l->len && l->input[l->pos] == '(') {
                l->pos++;
                depth = 2;
            }
            while (l->pos < l->len && depth > 0) {
                if (l->input[l->pos] == '(') depth++;
                else if (l->input[l->pos] == ')') depth--;
                if (depth > 0) l->pos++;
            }
            if (l->pos < l->len) l->pos++;    /* skip final ')' */
            int sublen = l->pos - start;
            while (blen + sublen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, l->input + start, sublen);
            blen += sublen;
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

        /* handle backslash escape */
        if (c == '\\') {
            l->pos++;
            if (l->pos < l->len) {
                char n = l->input[l->pos];
                if (n == '\n') {
                    l->pos++; l->lineno++; continue;  /* line continuation */
                }
                /* special chars lose their meaning after \ */
                if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                buf[blen++] = n;
                l->pos++;
            }
            continue;
        }

        /* handle quotes inside words (no space before/after) */
        if (c == '\'') {
            char *inner = read_single_quoted(l);
            int ilen = strlen(inner);
            while (blen + ilen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, inner, ilen);
            blen += ilen;
            free(inner);
            continue;
        }
        if (c == '"') {
            char *inner = read_double_quoted(l);
            int ilen = strlen(inner);
            while (blen + ilen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, inner, ilen);
            blen += ilen;
            free(inner);
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

    /* single-quoted string (as a standalone token) */
    if (c == '\'') {
        l->token_text = read_single_quoted(l);
        l->token_type = TOK_WORD;
        l->token_quoted = 1;
        return TOK_WORD;
    }

    /* double-quoted string (as a standalone token) */
    if (c == '"') {
        l->token_text = read_double_quoted(l);
        l->token_type = TOK_WORD;
        l->token_quoted = 1;
        return TOK_WORD;
    }

    /* word token — read_word handles backticks and $(...) internally */
    l->token_text = read_word(l);
    l->token_type = TOK_WORD;
    return TOK_WORD;
}
