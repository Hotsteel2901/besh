/* ================================================================
 *  expand.c — word expansion for besh
 *
 *  Handles:
 *   - variable expansion  ($VAR, ${VAR}, $?, $$, $!, $0..$9)
 *   - tilde expansion     (~, ~user)
 *   - wildcard globbing   (*, ?, [chars])
 *   - command substitution ($(...) and backtick)
 * ================================================================ */

#include "shell.h"
#include <ctype.h>
#include <locale.h>
#include <langinfo.h>

/* ================================================================
 *  Pattern / string helpers used by parameter expansion
 * ================================================================ */

/* glob-style match with no path semantics (equivalent to fnmatch(s,p,0)).
 * Used by ${var#pat}, ${var/pat/rep} and (elsewhere) [[ a == pat ]]. */
int sh_pattern_match(const char *str, const char *pattern) {
    if (!str || !pattern) return 0;
    return fnmatch(pattern, str, 0) == 0;
}

/* ---- UTF-8 aware character helpers ---------------------------- */
/* NOTE: the shell does not call setlocale(), so nl_langinfo() would
 * always report the "C" codeset.  Inspect the locale environment
 * variables instead (matches bash running under LC_ALL=C.UTF-8). */
static int is_utf8_locale(const char *s) {
    return s && (strstr(s, "UTF-8") || strstr(s, "utf8") || strstr(s, "UTF8"));
}

static int utf8_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *cs = sh_getenv("LC_ALL");
        if (!cs || !*cs) cs = sh_getenv("LC_CTYPE");
        if (!cs || !*cs) cs = sh_getenv("LANG");
        cached = is_utf8_locale(cs) ? 1 : 0;
        if (!cached) {   /* fall back to the process locale, if set */
            const char *nl = nl_langinfo(CODESET);
            cached = is_utf8_locale(nl) ? 1 : 0;
        }
    }
    return cached;
}

static int utf8_seqlen(unsigned char c) {
    if (!utf8_enabled()) return 1;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* number of (multi-byte) characters in a UTF-8 string */
static int utf8_count(const char *s) {
    int n = 0;
    while (s && *s) { s += utf8_seqlen((unsigned char)*s); n++; }
    return n;
}

/* byte offset of the n-th character (clamped to the string end) */
static size_t utf8_byteoff(const char *s, int nchars) {
    size_t off = 0;
    while (nchars > 0 && s[off]) {
        off += utf8_seqlen((unsigned char)s[off]);
        nchars--;
    }
    return off;
}

/* match pattern against the substring str[l..r) */
static int match_range(const char *str, size_t l, size_t r, const char *pat) {
    if (r < l) return 0;
    size_t n = r - l;
    char stackbuf[256];
    char *sub = (n < sizeof(stackbuf)) ? stackbuf : sh_malloc(n + 1);
    memcpy(sub, str + l, n);
    sub[n] = '\0';
    int rc = (fnmatch(pat, sub, 0) == 0);
    if (sub != stackbuf) free(sub);
    return rc;
}

/* ---- variable-name character classes ------------------------- */
static int name_start_ch(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int name_ch(int c) {
    return name_start_ch(c) || (c >= '0' && c <= '9');
}
static int is_valid_name(const char *s) {
    if (!s || !name_start_ch((unsigned char)*s)) return 0;
    for (s++; *s; s++)
        if (!name_ch((unsigned char)*s)) return 0;
    return 1;
}

/* ---- join positional parameters with IFS first char ---------- */
static char *join_positional(void) {
    Shell *sh = shell_get();
    char *ifs = sh_getenv("IFS");
    char sep = (ifs && *ifs) ? *ifs : ' ';
    size_t total = 1;
    for (int i = 0; i < sh->npositional; i++)
        total += strlen(sh->positional[i]) + 1;
    char *r = sh_malloc(total);
    size_t pos = 0;
    for (int i = 0; i < sh->npositional; i++) {
        if (i > 0) r[pos++] = sep;
        size_t l = strlen(sh->positional[i]);
        memcpy(r + pos, sh->positional[i], l);
        pos += l;
    }
    r[pos] = '\0';
    return r;
}

/* value of a parameter by name; *is_set is 0 when the parameter is unset */
static char *param_get(const char *name, int *is_set) {
    Shell *sh = shell_get();
    if (is_set) *is_set = 1;
    if (!name || !*name) { if (is_set) *is_set = 0; return sh_strdup(""); }

    if (!name[1]) {   /* one-character special parameter */
        char buf[32];
        switch (name[0]) {
        case '*': case '@': return join_positional();
        case '?': snprintf(buf, sizeof(buf), "%d", sh->exit_status); return sh_strdup(buf);
        case '$': snprintf(buf, sizeof(buf), "%d", getpid());        return sh_strdup(buf);
        case '#': snprintf(buf, sizeof(buf), "%d", sh->npositional);  return sh_strdup(buf);
        case '!': {
            pid_t lp = 0;
            for (Job *j = sh->jobs; j; j = j->next)
                if (j->npids > 0) lp = j->pids[j->npids - 1];
            snprintf(buf, sizeof(buf), "%d", lp);
            return sh_strdup(buf);
        }
        default: break;
        }
    }

    int alldig = 1;
    for (const char *q = name; *q; q++)
        if (!(*q >= '0' && *q <= '9')) { alldig = 0; break; }
    if (alldig) {
        int idx = atoi(name);
        if (idx == 0) {   /* ${0} — shell name */
            char *shell = sh_getenv("0");
            return sh_strdup(shell ? shell : "besh");
        }
        if (idx <= sh->npositional && sh->positional[idx - 1])
            return sh_strdup(sh->positional[idx - 1]);
        if (is_set) *is_set = 0;
        return sh_strdup("");
    }

    char *v = sh_getenv(name);
    if (!v) { if (is_set) *is_set = 0; return sh_strdup(""); }
    return sh_strdup(v);
}

/* names of all variables beginning with prefix (space separated) */
static char *list_var_names(const char *prefix, int plen) {
    Shell *sh = shell_get();
    size_t cap = 64, len = 0;
    char *out = sh_malloc(cap);
    out[0] = '\0';
    int first = 1;
    for (int i = 0; i < sh->nvars; i++) {
        if (plen > 0 && strncmp(sh->vars[i].name, prefix, plen) != 0) continue;
        size_t nl = strlen(sh->vars[i].name);
        while (len + nl + 2 > cap) { cap *= 2; out = sh_realloc(out, cap); }
        if (!first) out[len++] = ' ';
        memcpy(out + len, sh->vars[i].name, nl);
        len += nl;
        out[len] = '\0';
        first = 0;
    }
    return out;
}

/* build the replacement text of ${v/pat/rep}: '&' → matched text,
 * '\&' → a literal '&' (other backslashes are kept for the final
 * quote-protection removal stage). */
static char *build_repl(const char *rep, const char *m, size_t mlen) {
    size_t cap = strlen(rep) + mlen + 16, len = 0;
    char *out = sh_malloc(cap);
    for (const char *p = rep; *p; ) {
        if (*p == '\\' && p[1] == '&') {
            while (len + 2 > cap) { cap *= 2; out = sh_realloc(out, cap); }
            out[len++] = '&';
            p += 2;
            continue;
        }
        const char *ins = p;
        size_t il = 1;
        if (*p == '&') { ins = m; il = mlen; }
        while (len + il + 1 > cap) { cap *= 2; out = sh_realloc(out, cap); }
        memcpy(out + len, ins, il);
        len += il;
        p++;
    }
    out[len] = '\0';
    return out;
}

/* perform ${v/pat/rep} style replacement.
 * anchor 0 = anywhere, 1 = prefix (${v/#pat/rep}), 2 = suffix (${v/%pat/rep}).
 * unanchored matches are leftmost, longest-at-that-position (like bash). */
static char *do_replace(const char *val, const char *pat, const char *rep,
                        int global, int anchor) {
    size_t len = strlen(val), cap = len + 64, olen = 0;
    char *out = sh_malloc(cap);
#define APP(s, l) do { size_t _l = (l); \
        while (olen + _l + 1 > cap) { cap *= 2; out = sh_realloc(out, cap); } \
        memcpy(out + olen, (s), _l); olen += _l; } while (0)

    if (anchor == 1 || anchor == 2) {
        size_t ms = 0, me = len;
        int found = 0;
        if (anchor == 1) {
            for (size_t j = len; ; j--) {
                if (match_range(val, 0, j, pat)) { ms = 0; me = j; found = 1; break; }
                if (j == 0) break;
            }
        } else {
            for (size_t s = 0; s <= len; s++)
                if (match_range(val, s, len, pat)) { ms = s; me = len; found = 1; break; }
        }
        if (found) {
            char *r = build_repl(rep, val + ms, me - ms);
            APP(val, ms);
            APP(r, strlen(r));
            APP(val + me, len - me);
            free(r);
        } else {
            APP(val, len);
        }
    } else {
        size_t i = 0;
        while (i <= len) {
            int found = 0;
            size_t ms = 0, me = 0;
            for (size_t s = i; s <= len && !found; s++) {
                for (size_t e = len; ; e--) {
                    if (match_range(val, s, e, pat)) { ms = s; me = e; found = 1; break; }
                    if (e == s) break;
                }
            }
            if (!found) { APP(val + i, len - i); break; }
            APP(val + i, ms - i);
            char *r = build_repl(rep, val + ms, me - ms);
            APP(r, strlen(r));
            free(r);
            if (!global) { APP(val + me, len - me); break; }
            if (me == ms) {           /* empty match — advance one byte */
                if (ms < len) APP(val + ms, 1);
                i = ms + 1;
            } else {
                i = me;
            }
        }
    }
#undef APP
    out[olen] = '\0';
    return out;
}

/* ${v^} ${v^^} ${v,} ${v,,} — ASCII case conversion.
 * When a pattern is given, only characters matching it are converted. */
static char *case_convert(const char *val, int upper, int all, const char *pat) {
    size_t len = strlen(val);
    char *out = sh_malloc(len + 1);
    size_t o = 0, i = 0;
    int first_done = 0;
    while (val[i]) {
        int sl = utf8_seqlen((unsigned char)val[i]);
        if (sl > 6) sl = 1;
        char cbuf[8];
        memcpy(cbuf, val + i, sl);
        cbuf[sl] = '\0';

        int conv = (pat && *pat) ? sh_pattern_match(cbuf, pat) : 1;
        if (conv && !all && first_done) conv = 0;

        if (conv && sl == 1) {
            unsigned char c = (unsigned char)val[i];
            out[o++] = (char)(upper ? toupper(c) : tolower(c));
            first_done = 1;
        } else {
            memcpy(out + o, val + i, sl);
            o += sl;
            if (conv) first_done = 1;
        }
        i += sl;
    }
    out[o] = '\0';
    return out;
}

/* expand a `${ ... }` parameter expression.  *pp points just past the
 * opening '{'; on return *pp points just past the closing '}'. */
static char *expand_braced(const char **pp) {
    Shell *sh = shell_get();
    const char *start = *pp;

    /* locate the matching close brace, honouring protection escapes
     * and nested ${ ... } */
    int depth = 1;
    const char *q = start;
    while (*q) {
        if (*q == '\\' && q[1]) { q += 2; continue; }
        if (*q == '$' && q[1] == '{') { depth++; q += 2; continue; }
        if (*q == '}') { depth--; if (depth == 0) break; }
        q++;
    }
    const char *close = q;   /* at '}' or at '\0' when unterminated */
    char *content = sh_strndup(start, close - start);
    char *result = NULL;

    /* ${#param} — length (in characters) */
    if (content[0] == '#') {
        const char *op = content + 1;
        if (!*op || (!op[1] && (op[0] == '*' || op[0] == '@'))) {
            char b[32];
            snprintf(b, sizeof(b), "%d", sh->npositional);
            result = sh_strdup(b);
        } else {
            /* ${#name[idx]} / ${#name[@]} / ${#name[*]} */
            char nm[1024];
            int nl = 0;
            while (op[nl] && name_ch((unsigned char)op[nl]) && nl < 1023) {
                nm[nl] = op[nl]; nl++;
            }
            nm[nl] = '\0';
            const char *rst = op + nl;
            if (nl > 0 && rst[0] == '[') {
                const char *cl = strchr(rst, ']');
                if (cl) {
                    char *sub = sh_strndup(rst + 1, cl - rst - 1);
                    char b[32];
                    if (strcmp(sub, "@") == 0 || strcmp(sub, "*") == 0) {
                        snprintf(b, sizeof(b), "%d", var_array_count(nm));
                        result = sh_strdup(b);
                    } else {
                        char *e;
                        long idx = strtol(sub, &e, 10);
                        if (e != sub && *e == '\0') {
                            int set;
                            char *v = var_array_get(nm, idx, &set);
                            snprintf(b, sizeof(b), "%d", utf8_count(v));
                            free(v);
                            result = sh_strdup(b);
                        } else {
                            result = sh_strdup("0");
                        }
                    }
                    free(sub);
                    goto done;
                }
            }
            int set;
            char *v = param_get(op, &set);
            char b[32];
            snprintf(b, sizeof(b), "%d", utf8_count(v));
            free(v);
            result = sh_strdup(b);
        }
        goto done;
    }

    /* ${!name} — indirect;  ${!prefix*} / ${!prefix@} — name listing */
    if (content[0] == '!') {
        const char *op = content + 1;
        int l = strlen(op);
        /* ${!name[@]} / ${!name[*]} — list of subscript indices */
        if (l >= 4 && op[l-1] == ']' &&
            (op[l-2] == '@' || op[l-2] == '*') && op[l-3] == '[') {
            char *abase = sh_strndup(op, l - 3);
            if (is_valid_name(abase)) {
                int ni = 0;
                long *idxs = var_array_indices(abase, &ni);
                size_t cap = 64, olen = 0;
                char *out = sh_malloc(cap);
                out[0] = '\0';
                for (int i = 0; i < ni; i++) {
                    char b[32];
                    int bl = snprintf(b, sizeof(b), "%ld", idxs[i]);
                    while (olen + bl + 2 > cap) { cap *= 2; out = sh_realloc(out, cap); }
                    if (i) out[olen++] = ' ';
                    memcpy(out + olen, b, bl);
                    olen += bl;
                    out[olen] = '\0';
                }
                free(idxs);
                free(abase);
                result = out;
                goto done;
            }
            free(abase);
        }
        if (l > 0 && (op[l-1] == '*' || op[l-1] == '@')) {
            int ok = 1;
            for (int i = 0; i < l - 1; i++)
                if (!name_ch((unsigned char)op[i])) { ok = 0; break; }
            if (ok) { result = list_var_names(op, l - 1); goto done; }
        }
        if (is_valid_name(op)) {
            char *inner = sh_getenv(op);
            if (!inner) {
                /* indirect through an unset variable — bash reports an error */
                fprintf(stderr, "besh: %s: invalid indirect expansion\n", op);
                sh->exit_status = 1;
                if (!sh->job_interactive) exit(1);
                result = sh_strdup("");
            } else if (!is_valid_name(inner)) {
                fprintf(stderr, "besh: %s: invalid indirect expansion\n", inner);
                sh->exit_status = 1;
                if (!sh->job_interactive) exit(1);
                result = sh_strdup("");
            } else {
                char *v = sh_getenv(inner);
                result = sh_strdup(v ? v : "");
            }
        } else {
            result = sh_strdup("");
        }
        goto done;
    }

    /* parameter name (may be a one-character special parameter) */
    char name[1024];
    int nlen = 0;
    if (content[0] && (content[0] == '?' || content[0] == '$' ||
                       content[0] == '*' || content[0] == '@')) {
        name[nlen++] = content[0];
    } else {
        while (content[nlen] && name_ch((unsigned char)content[nlen]) &&
               nlen < (int)sizeof(name) - 1)
            name[nlen] = content[nlen], nlen++;
    }
    name[nlen] = '\0';
    const char *rest = content + nlen;

    if (nlen == 0) { result = sh_strdup(""); goto done; }

    /* array subscript: ${name[idx]} / ${name[@]} / ${name[*]} */
    int has_sub = 0, arr_star = 0;
    long arr_idx = 0;
    if (rest[0] == '[') {
        const char *cl = strchr(rest, ']');
        if (cl) {
            char *sub = sh_strndup(rest + 1, cl - rest - 1);
            if (strcmp(sub, "@") == 0) { arr_star = 2; has_sub = 1; }
            else if (strcmp(sub, "*") == 0) { arr_star = 1; has_sub = 1; }
            else {
                char *e;
                long v = strtol(sub, &e, 10);
                if (e != sub && *e == '\0') { arr_idx = v; has_sub = 1; }
            }
            free(sub);
            if (has_sub) rest = cl + 1;
        }
    }

    /* ${v:-x} ${v-x} ${v:=x} ${v=x} ${v:+x} ${v+x} ${v:?x} ${v?x} */
    {
        int colon = 0;
        char opc = 0;
        const char *arg = NULL;
        if (rest[0] == ':' && (rest[1] == '-' || rest[1] == '=' ||
                               rest[1] == '+' || rest[1] == '?')) {
            colon = 1; opc = rest[1]; arg = rest + 2;
        } else if (rest[0] == '-' || rest[0] == '=' ||
                   rest[0] == '+' || rest[0] == '?') {
            opc = rest[0]; arg = rest + 1;
        }
        if (opc) {
            int set;
            char *val = param_get(name, &set);
            int use = colon ? (set ? (*val == '\0') : 1) : (!set);
            char *earg = expand_string(arg);
            switch (opc) {
            case '-':
                if (use) { result = earg; free(val); }
                else     { result = val;  free(earg); }
                break;
            case '=':
                if (use) {
                    if (is_valid_name(name)) sh_setenv(name, earg, 0);
                    result = earg; free(val);
                } else { result = val; free(earg); }
                break;
            case '+':
                if (use) { result = sh_strdup(""); free(earg); }
                else     { result = earg; }
                free(val);
                break;
            case '?':
                if (use) {
                    fprintf(stderr, "besh: %s: %s\n", name,
                            *earg ? earg : "parameter null or not set");
                    free(earg); free(val);
                    sh->exit_status = 1;
                    if (!sh->job_interactive) exit(1);
                    result = sh_strdup("");
                } else { result = val; free(earg); }
                break;
            }
            goto done;
        }
    }

    /* ${v#pat} ${v##pat} — strip shortest/longest matching prefix */
    if (rest[0] == '#') {
        int longest = (rest[1] == '#');
        char *pat = unescape_word(expand_string(rest + (longest ? 2 : 1)));
        int set;
        char *val = param_get(name, &set);
        size_t len = strlen(val), cut = 0;
        if (!longest) {
            for (size_t i = 0; i <= len; i++)
                if (match_range(val, 0, i, pat)) { cut = i; break; }
        } else {
            for (size_t i = len; ; i--) {
                if (match_range(val, 0, i, pat)) { cut = i; break; }
                if (i == 0) break;
            }
        }
        result = sh_strdup(val + cut);
        free(pat); free(val);
        goto done;
    }

    /* ${v%pat} ${v%%pat} — strip shortest/longest matching suffix */
    if (rest[0] == '%') {
        int longest = (rest[1] == '%');
        char *pat = unescape_word(expand_string(rest + (longest ? 2 : 1)));
        int set;
        char *val = param_get(name, &set);
        size_t len = strlen(val), st = len;
        if (!longest) {
            for (size_t i = len; ; i--) {
                if (match_range(val, i, len, pat)) { st = i; break; }
                if (i == 0) break;
            }
        } else {
            for (size_t i = 0; i <= len; i++)
                if (match_range(val, i, len, pat)) { st = i; break; }
        }
        result = sh_strndup(val, st);
        free(pat); free(val);
        goto done;
    }

    /* ${v/pat/rep} ${v//pat/rep} ${v/#pat/rep} ${v/%pat/rep} */
    if (rest[0] == '/') {
        const char *r = rest + 1;
        int global = 0, anchor = 0;
        if (*r == '/') { global = 1; r++; }
        else if (*r == '#') { anchor = 1; r++; }
        else if (*r == '%') { anchor = 2; r++; }
        const char *slash = NULL;
        for (const char *pp = r; *pp; pp++) {
            if (*pp == '\\' && pp[1]) { pp++; continue; }
            if (*pp == '/') { slash = pp; break; }
        }
        char *patraw = slash ? sh_strndup(r, slash - r) : sh_strdup(r);
        char *repraw = slash ? sh_strdup(slash + 1) : sh_strdup("");
        char *pat = unescape_word(expand_string(patraw));
        char *rep = expand_string(repraw);
        int set;
        char *val = param_get(name, &set);
        result = do_replace(val, pat, rep, global, anchor);
        free(patraw); free(repraw); free(pat); free(rep); free(val);
        goto done;
    }

    /* ${v^} ${v^^} ${v,} ${v,,} — case conversion */
    if (rest[0] == '^' || rest[0] == ',') {
        int upper = (rest[0] == '^');
        int all = (rest[1] == rest[0]);
        const char *pr = rest + (all ? 2 : 1);
        char *pat = *pr ? unescape_word(expand_string(pr)) : NULL;
        int set;
        char *val = param_get(name, &set);
        result = case_convert(val, upper, all, pat);
        if (pat) free(pat);
        free(val);
        goto done;
    }

    /* ${v:off} ${v:off:len} — substring (UTF-8 aware) */
    if (rest[0] == ':') {
        const char *s = rest + 1;
        while (*s == ' ' || *s == '\t') s++;
        int neg = 0;
        if (*s == '-') { neg = 1; s++; }
        else if (*s == '+') s++;
        int set;
        char *val = has_sub && !arr_star
                    ? var_array_get(name, arr_idx, &set)
                    : param_get(name, &set);
        if (!(*s >= '0' && *s <= '9')) {
            result = sh_strdup("");   /* non-numeric offset */
            free(val);
            goto done;
        }
        char *endp;
        long off = strtol(s, &endp, 10);
        s = endp;
        if (neg) off = -off;

        int has_len = 0;
        long slen = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == ':') {
            s++;
            while (*s == ' ' || *s == '\t') s++;
            int neg2 = 0;
            if (*s == '-') { neg2 = 1; s++; }
            else if (*s == '+') s++;
            slen = strtol(s, &endp, 10);
            if (neg2) slen = -slen;
            has_len = 1;
        }

        int n = utf8_count(val);
        if (off < 0) { off += n; if (off < 0) off = 0; }
        if (off > n) off = n;
        long take;
        if (!has_len) take = n - off;
        else if (slen < 0) { take = n - off + slen; if (take < 0) take = 0; }
        else take = slen;
        if (off + take > n) take = n - off;
        if (take < 0) take = 0;

        size_t b0 = utf8_byteoff(val, (int)off);
        size_t b1 = utf8_byteoff(val, (int)(off + take));
        result = sh_strndup(val + b0, b1 - b0);
        free(val);
        goto done;
    }

    /* plain ${v} — or ${name[idx]} / ${name[@]} / ${name[*]} */
    if (has_sub) {
        if (arr_star) {
            int ni = 0;
            char **vals = var_array_values(name, &ni);
            char *ifs = sh_getenv("IFS");
            char sep = (arr_star == 1 && ifs && *ifs) ? *ifs : ' ';
            size_t cap = 64, olen = 0;
            char *out = sh_malloc(cap);
            out[0] = '\0';
            for (int i = 0; i < ni; i++) {
                size_t vl = strlen(vals[i]);
                while (olen + vl + 2 > cap) { cap *= 2; out = sh_realloc(out, cap); }
                if (i) out[olen++] = sep;
                memcpy(out + olen, vals[i], vl);
                olen += vl;
                out[olen] = '\0';
            }
            var_free_list(vals, ni);
            result = out;
        } else {
            int set;
            result = var_array_get(name, arr_idx, &set);
        }
        goto done;
    }
    {
        int set;
        result = param_get(name, &set);
    }

done:
    free(content);
    *pp = (*close == '}') ? close + 1 : close;
    return result ? result : sh_strdup("");
}

/* ---- tilde expansion ----------------------------------------- */
char *tilde_expand(const char *str) {
    if (!str || str[0] != '~') return sh_strdup(str);

    char *result;
    if (str[1] == '\0' || str[1] == '/') {
        /* ~ or ~/path → $HOME */
        const char *home = sh_getenv("HOME");
        if (!home) home = "/root";
        if (str[1] == '/') {
            result = sh_malloc(strlen(home) + strlen(str + 1) + 2);
            sprintf(result, "%s%s", home, str + 1);
        } else {
            result = sh_strdup(home);
        }
    } else {
        /* ~user/path */
        const char *rest = strchr(str + 1, '/');
        char username[256];
        if (rest) {
            int len = rest - str - 1;
            if (len >= 255) len = 255;
            memcpy(username, str + 1, len);
            username[len] = '\0';
        } else {
            strncpy(username, str + 1, sizeof(username) - 1);
        }

        struct passwd *pw = getpwnam(username);
        if (pw) {
            if (rest)
                { int sl = snprintf(NULL, 0, "%s%s", pw->pw_dir, rest) + 1;
                  result = sh_malloc(sl);
                  sprintf(result, "%s%s", pw->pw_dir, rest); }
            else
                result = sh_strdup(pw->pw_dir);
        } else {
            result = sh_strdup(str);  /* no expansion */
        }
    }
    return result;
}

/* ---- variable expansion -------------------------------------- */
static char *var_expand_one(const char **pp) {
    const char *p = *pp;
    if (*p != '$') return NULL;
    p++;  /* skip $ */

    Shell *sh = shell_get();
    char name[1024];
    int nlen = 0;

    /* special variables */
    if (*p == '?') {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", sh->exit_status);
        *pp = p + 1;
        return sh_strdup(buf);
    }
    if (*p == '$') {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", getpid());
        *pp = p + 1;
        return sh_strdup(buf);
    }
    if (*p == '!') {
        char buf[32];
        /* last background pid — find most recent job */
        pid_t last_pid = 0;
        for (Job *j = sh->jobs; j; j = j->next)
            if (j->npids > 0) last_pid = j->pids[j->npids - 1];
        snprintf(buf, sizeof(buf), "%d", last_pid);
        *pp = p + 1;
        return sh_strdup(buf);
    }
    if (*p == '#') {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", sh->npositional);
        *pp = p + 1;
        return sh_strdup(buf);
    }
    if (*p >= '0' && *p <= '9') {
        int idx = *p - '0';
        *pp = p + 1;
        if (idx == 0) {
            /* $0 — shell name */
            char *shell = sh_getenv("0");
            return sh_strdup(shell ? shell : "besh");
        }
        if (idx <= sh->npositional && sh->positional[idx - 1])
            return sh_strdup(sh->positional[idx - 1]);
        return sh_strdup("");
    }
    if (*p == '*') {
        /* $* — join positional params with IFS first char */
        char *ifs = sh_getenv("IFS");
        char sep = (ifs && *ifs) ? *ifs : ' ';
        int total = 0;
        for (int i = 0; i < sh->npositional; i++)
            total += strlen(sh->positional[i]) + 1;
        char *result = sh_malloc(total + 1);
        int pos = 0;
        for (int i = 0; i < sh->npositional; i++) {
            if (i > 0) result[pos++] = sep;
            int l = strlen(sh->positional[i]);
            memcpy(result + pos, sh->positional[i], l);
            pos += l;
        }
        result[pos] = '\0';
        *pp = p + 1;
        return result;
    }
    if (*p == '@') {
        /* $@ — like $* for now */
        char *ifs = sh_getenv("IFS");
        char sep = (ifs && *ifs) ? *ifs : ' ';
        int total = 0;
        for (int i = 0; i < sh->npositional; i++)
            total += strlen(sh->positional[i]) + 1;
        char *result = sh_malloc(total + 1);
        int pos = 0;
        for (int i = 0; i < sh->npositional; i++) {
            if (i > 0) result[pos++] = sep;
            int l = strlen(sh->positional[i]);
            memcpy(result + pos, sh->positional[i], l);
            pos += l;
        }
        result[pos] = '\0';
        *pp = p + 1;
        return result;
    }

    /* ${VAR} and every ${...} string operator (handled by expand_braced) */
    if (*p == '{') {
        p++;  /* skip { */
        char *res = expand_braced(&p);
        *pp = p;
        return res;
    }

    /* $VAR — plain variable name */
    nlen = 0;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_') &&
           nlen < (int)sizeof(name) - 1) {
        name[nlen++] = *p++;
    }
    name[nlen] = '\0';

    *pp = p;
    if (nlen == 0) return sh_strdup("$");  /* bare $ */
    char *val = sh_getenv(name);
    return sh_strdup(val ? val : "");
}

/* ---- command substitution: run cmd, capture stdout ------------ */
static char *command_substitute(const char *cmd) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return sh_strdup("");

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return sh_strdup(""); }

    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        /* avoid reaping parent jobs in the child */
        Shell *sh = shell_get();
        sh->jobs = NULL;
        sh->njob = 0;
        sh->job_interactive = 0;
        fflush(NULL);   /* _exit skips stdio flush */
        _exit(execute_string(cmd));
    }

    /* parent */
    close(pipefd[1]);
    char *result = sh_malloc(256);
    int rlen = 0, rcap = 256;
    char rbuf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], rbuf, sizeof(rbuf))) > 0) {
        while (rlen + n + 1 >= rcap) { rcap *= 2; result = sh_realloc(result, rcap); }
        memcpy(result + rlen, rbuf, n);
        rlen += n;
    }
    close(pipefd[0]);
    result[rlen] = '\0';

    int status;
    waitpid(pid, &status, 0);

    /* strip trailing newlines */
    while (rlen > 0 && result[rlen-1] == '\n') result[--rlen] = '\0';
    return result;
}

/* ---- arithmetic expansion: simple recursive-descent evaluator -- */
static void arith_skip(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static long arith_expr(const char **p);

static long arith_primary(const char **p) {
    arith_skip(p);
    if (**p == '(') {
        (*p)++;
        long v = arith_expr(p);
        arith_skip(p);
        if (**p == ')') (*p)++;
        return v;
    }
    if (**p == '-') { (*p)++; return -arith_primary(p); }
    if (**p == '+') { (*p)++; return  arith_primary(p); }
    if (**p == '!') { (*p)++; return !arith_primary(p); }

    /* number literal (decimal, hex, octal) */
    if (**p >= '0' && **p <= '9') {
        char *end;
        long v = strtol(*p, &end, 0);
        *p = end;
        return v;
    }

    /* variable name → look up and convert to integer */
    char name[256];
    int nlen = 0;
    while ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') ||
           (**p >= '0' && **p <= '9') || **p == '_') {
        if (nlen < 255) name[nlen++] = **p;
        (*p)++;
    }
    name[nlen] = '\0';
    if (nlen > 0) {
        char *val = sh_getenv(name);
        if (val && *val) return strtol(val, NULL, 0);
    }
    return 0;
}

static long arith_mul(const char **p) {
    long v = arith_primary(p);
    for (;;) {
        arith_skip(p);
        if (**p == '*') { (*p)++; v *= arith_primary(p); }
        else if (**p == '/') { (*p)++; long r = arith_primary(p); v = r ? v / r : 0; }
        else if (**p == '%') { (*p)++; long r = arith_primary(p); v = r ? v % r : 0; }
        else break;
    }
    return v;
}

static long arith_expr(const char **p) {
    long v = arith_mul(p);
    for (;;) {
        arith_skip(p);
        if      (**p == '+') { (*p)++; v += arith_mul(p); }
        else if (**p == '-') { (*p)++; v -= arith_mul(p); }
        else break;
    }
    return v;
}

/* ---- full string expansion (variables, command sub, arithmetic) - */
char *expand_string(const char *str) {
    if (!str) return sh_strdup("");

    char *buf = sh_malloc(4096);
    int blen = 0, bcap = 4096;
    const char *p = str;

    while (*p) {
        if (*p == '\\' && *(p+1)) {
            char nx = *(p+1);
            if (nx == '\n') {            /* line continuation */
                p += 2;
                continue;
            }
            if (nx == '$' || nx == '`' || nx == '"' || nx == '\'' ||
                nx == '\\' || nx == '~') {
                /* protected literal — unescape it now */
                if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                buf[blen++] = nx;
                p += 2;
                continue;
            }
            if (nx == '*' || nx == '?' || nx == '[' || nx == ']' ||
                nx == '{' || nx == '}') {
                /* keep protected: brace/glob stages must still see the escape */
                if (blen + 3 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                buf[blen++] = '\\';
                buf[blen++] = nx;
                p += 2;
                continue;
            }
            /* unknown escape — copy backslash literally */
            if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            buf[blen++] = '\\';
            p++;
            continue;
        }
        /* arithmetic expansion: $(( ... )) */
        if (*p == '$' && *(p+1) == '(' && *(p+2) == '(') {
            p += 3;                       /* skip '$((' */
            long val = arith_expr(&p);
            arith_skip(&p);
            if (*p == ')') p++;          /* skip first ')' */
            if (*p == ')') p++;          /* skip second ')' */
            char numbuf[32];
            int elen = snprintf(numbuf, sizeof(numbuf), "%ld", val);
            while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, numbuf, elen);
            blen += elen;
            continue;
        }
        /* command substitution: $( ... ) */
        if (*p == '$' && *(p+1) == '(') {
            p += 2;                       /* skip '$(' */
            int depth = 1;
            char *cmd = sh_malloc(256);
            int clen = 0, ccap = 256;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') { depth--; if (depth == 0) break; }
                if (clen + 2 >= ccap) { ccap *= 2; cmd = sh_realloc(cmd, ccap); }
                cmd[clen++] = *p++;
            }
            if (*p == ')') p++;
            cmd[clen] = '\0';
            char *out = command_substitute(cmd);
            free(cmd);
            int elen = strlen(out);
            while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, out, elen);
            blen += elen;
            free(out);
            continue;
        }
        /* backtick command substitution: ` ... ` */
        if (*p == '`') {
            p++;                          /* skip opening backtick */
            char *cmd = sh_malloc(256);
            int clen = 0, ccap = 256;
            while (*p && *p != '`') {
                if (*p == '\\' && *(p+1)) {
                    p++;
                    if (clen + 2 >= ccap) { ccap *= 2; cmd = sh_realloc(cmd, ccap); }
                    cmd[clen++] = *p++;
                } else {
                    if (clen + 2 >= ccap) { ccap *= 2; cmd = sh_realloc(cmd, ccap); }
                    cmd[clen++] = *p++;
                }
            }
            if (*p == '`') p++;
            cmd[clen] = '\0';
            char *out = command_substitute(cmd);
            free(cmd);
            int elen = strlen(out);
            while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, out, elen);
            blen += elen;
            free(out);
            continue;
        }
        if (*p == '$') {
            /* regular variable expansion: $VAR, ${VAR}, $?, $$, etc. */
            char *exp = var_expand_one(&p);
            if (exp) {
                int elen = strlen(exp);
                while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                memcpy(buf + blen, exp, elen);
                blen += elen;
                free(exp);
            }
            continue;
        }
        if (*p == '~' && p == str &&
            (*(p+1) == '\0' || *(p+1) == '/')) {
            char *exp = tilde_expand(p);
            int elen = strlen(exp);
            while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, exp, elen);
            blen += elen;
            free(exp);
            /* tilde_expand() consumed the whole word (p == str) */
            p += strlen(p);
            continue;
        }
        if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
        buf[blen++] = *p++;
    }
    buf[blen] = '\0';
    return buf;
}

/* ---- quote-protection removal --------------------------------- *
 * The lexer protects every literal special character with a backslash.
 * expand_string() already unescapes the expansion set ($ ` " ' \ ~), so
 * what is left here is the glob/brace set, which must survive until the
 * glob decision has been made.  unescape_word() is the final step. */
char *unescape_word(const char *s) {
    if (!s) return sh_strdup("");
    char *out = sh_malloc(strlen(s) + 1);
    int o = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\\' && *(p+1) &&
            (*(p+1) == '*' || *(p+1) == '?' || *(p+1) == '[' ||
             *(p+1) == ']' || *(p+1) == '{' || *(p+1) == '}')) {
            out[o++] = *(p+1);
            p++;
            continue;
        }
        out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

/* does the word contain a glob character that is *not* backslash-escaped? */
static int has_unescaped_glob(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '\\' && *(p+1)) { p++; continue; }
        if (*p == '*' || *p == '?' || *p == '[') return 1;
    }
    return 0;
}

/* remove *all* quote-protection backslashes from a lexer token.
 * Used for contexts that do not undergo expansion (redirection file
 * names, here-document delimiters) where quotes are simply removed. */
char *unescape_token(const char *s) {
    if (!s) return sh_strdup("");
    char *out = sh_malloc(strlen(s) + 1);
    int o = 0;
    for (const char *p = s; *p; ) {
        char n = *(p+1);
        if (*p == '\\' && n &&
            (n == '$' || n == '`' || n == '"' || n == '\'' || n == '\\' ||
             n == '~' || n == '*' || n == '?' || n == '[' || n == ']' ||
             n == '{' || n == '}')) {
            out[o++] = n;
            p += 2;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return out;
}

/* apply brace expansion / globbing / protection-removal to one fully
 * expanded string and append the resulting word(s) to `result`.
 * Takes ownership of `expanded`. */
static void add_one_word(Shell *sh, char **result, int *nresult, char *expanded) {
    int bc;
    char **br = brace_expand(expanded, &bc);
    free(expanded);

    for (int b = 0; b < bc && *nresult < MAX_ARGS - 1; b++) {
        /* if noglob is set, or the word has no active glob char,
         * keep it as a literal (after removing quote protection) */
        if (sh->opt_noglob || !has_unescaped_glob(br[b])) {
            result[(*nresult)++] = unescape_word(br[b]);
            free(br[b]);
            continue;
        }

        int gcount = 0;
        char **globs = glob_expand(br[b], &gcount);
        if (gcount > 0) {
            for (int j = 0; j < gcount && *nresult < MAX_ARGS - 1; j++)
                result[(*nresult)++] = globs[j];
            free(globs);
            free(br[b]);
        } else {
            /* no match — keep literal */
            result[(*nresult)++] = unescape_word(br[b]);
            free(br[b]);
        }
    }
    free(br);
}

/* Split a raw token at unquoted "$@" / "${@}" references.
 * Returns the number of references k and stores k+1 raw segments. */
static int split_at_refs(const char *s, char ***segs_out) {
    int cap = 4, n = 0, k = 0;
    char **segs = sh_malloc(cap * sizeof(char *));
    const char *start = s;
    for (const char *p = s; *p; ) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (*p == '$' && p[1] == '@') {
            if (n + 2 > cap) { cap *= 2; segs = sh_realloc(segs, cap * sizeof(char *)); }
            segs[n++] = sh_strndup(start, p - start);
            k++; p += 2; start = p;
            continue;
        }
        if (*p == '$' && p[1] == '{' && p[2] == '@' && p[3] == '}') {
            if (n + 2 > cap) { cap *= 2; segs = sh_realloc(segs, cap * sizeof(char *)); }
            segs[n++] = sh_strndup(start, p - start);
            k++; p += 4; start = p;
            continue;
        }
        p++;
    }
    if (n + 1 > cap) { segs = sh_realloc(segs, (n + 1) * sizeof(char *)); }
    segs[n++] = sh_strdup(start);
    *segs_out = segs;
    return k;
}

/* ---- IFS word splitting --------------------------------------- */
/* Split `s` on the characters in $IFS (IFS whitespace runs collapse and
 * are trimmed; non-whitespace IFS characters delimit one field each).
 * Returns a malloc'd array of malloc'd fields in *n. */
static char **split_ifs(const char *s, int *n) {
    char *ifs = sh_getenv("IFS");
    if (!ifs) ifs = " \t\n";
    if (!*ifs) {
        char **r = sh_malloc(sizeof(char *));
        r[0] = sh_strdup(s);
        *n = 1;
        return r;
    }
    int isifs[256] = {0}, isws[256] = {0};
    for (char *p = ifs; *p; p++) {
        isifs[(unsigned char)*p] = 1;
        if (*p == ' ' || *p == '\t' || *p == '\n')
            isws[(unsigned char)*p] = 1;
    }
    int cap = 8, m = 0;
    char **out = sh_malloc(cap * sizeof(char *));
    const char *p = s;
    while (*p) {
        while (*p && isifs[(unsigned char)*p] && isws[(unsigned char)*p]) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !isifs[(unsigned char)*p]) p++;
        if (m >= cap) { cap *= 2; out = sh_realloc(out, cap * sizeof(char *)); }
        out[m++] = sh_strndup(start, p - start);
        if (*p && isifs[(unsigned char)*p]) {
            if (!isws[(unsigned char)*p]) p++;      /* one non-ws delimiter */
            else while (*p && isifs[(unsigned char)*p] &&
                        isws[(unsigned char)*p]) p++;
        }
    }
    out[m] = NULL;
    *n = m;
    return out;
}

static void append_str(char **buf, int *len, int *cap, const char *s) {
    size_t l = strlen(s);
    while (*len + (int)l + 1 > *cap) { *cap *= 2; *buf = sh_realloc(*buf, *cap); }
    memcpy(*buf + *len, s, l);
    *len += l;
    (*buf)[*len] = '\0';
}

/* append one fully-expanded field; quoted words stay whole (no split/glob),
 * unquoted ones undergo IFS splitting + pathname expansion.  Takes
 * ownership of `w`. */
static void emit_field(Shell *sh, char **result, int *nresult, char *w, int q) {
    if (q) {
        if (*nresult >= MAX_ARGS - 1) { free(w); return; }
        result[(*nresult)++] = unescape_word(w);
        free(w);
    } else {
        int nf = 0;
        char **f = split_ifs(w, &nf);
        free(w);
        for (int i = 0; i < nf && *nresult < MAX_ARGS - 1; i++)
            add_one_word(sh, result, nresult, f[i]);
        free(f);
    }
}

/* expand a token, splitting on IFS unless it was quoted; takes ownership */
static void add_expanded_words(Shell *sh, char **result, int *nresult,
                               char *expanded, int quoted) {
    emit_field(sh, result, nresult, expanded, quoted);
}

/* ================================================================
 *  Array references  ${name[@]} / ${name[*]} / ${!name[@]} / ${#name[@]}
 * ================================================================ */

/* If `s` contains a `${ ... [@] ... }` / `${ ... [*] ... }` reference,
 * return a pointer to its '$'; otherwise NULL. */
static const char *find_array_ref(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == '$' && p[1] == '{') {
            const char *q = p + 2;
            int depth = 1;
            while (*q && depth > 0) {
                if (*q == '\\' && q[1]) { q += 2; continue; }
                if (*q == '$' && q[1] == '{') { depth++; q += 2; continue; }
                if (*q == '[' && (q[1] == '@' || q[1] == '*') && q[2] == ']')
                    return p;
                if (*q == '}') { depth--; if (depth == 0) break; }
                q++;
            }
        }
    }
    return NULL;
}

/* Parse `${[#][!]name[@|*][:off[:len]]}` starting at `s` (the '$').
 * Returns a pointer just past '}' or NULL on mismatch. */
static const char *parse_array_ref(const char *s, char **base, int *star,
                                   int *count_flag, int *bang,
                                   long *off, int *has_off,
                                   long *len, int *has_len) {
    *star = 0; *count_flag = 0; *bang = 0; *has_off = 0; *has_len = 0;
    *off = 0; *len = 0; *base = NULL;
    if (s[0] != '$' || s[1] != '{') return NULL;
    const char *p = s + 2;
    if (*p == '#') { *count_flag = 1; p++; }
    else if (*p == '!') { *bang = 1; p++; }
    char nm[512];
    int nl = 0;
    while (p[nl] && name_ch((unsigned char)p[nl]) && nl < 511) {
        nm[nl] = p[nl]; nl++;
    }
    nm[nl] = '\0';
    if (nl == 0 || p[nl] != '[') return NULL;
    const char *q = p + nl + 1;
    if (*q == '@') *star = 2;
    else if (*q == '*') *star = 1;
    else return NULL;
    q++;
    if (*q != ']') return NULL;
    q++;
    if (*q == ':') {
        q++;
        while (*q == ' ' || *q == '\t') q++;
        int neg = 0;
        if (*q == '-') { neg = 1; q++; }
        else if (*q == '+') q++;
        if (!(*q >= '0' && *q <= '9')) return NULL;
        char *e;
        long v = strtol(q, &e, 10);
        q = e;
        if (neg) v = -v;
        *off = v; *has_off = 1;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            int n2 = 0;
            if (*q == '-') { n2 = 1; q++; }
            else if (*q == '+') q++;
            if (!(*q >= '0' && *q <= '9')) return NULL;
            *len = strtol(q, &e, 10);
            q = e;
            if (n2) *len = -*len;
            *has_len = 1;
        }
    }
    if (*q != '}') return NULL;
    *base = sh_strdup(nm);
    return q + 1;
}

/* expand one token that contains array references */
static void expand_array_token(Shell *sh, const char *tok, int q,
                               char **result, int *nresult) {
    int ccap = 64, clen = 0;
    char *cur = sh_malloc(ccap);
    cur[0] = '\0';
    int force_last = 0;
    const char *p = tok;

    while (*p) {
        const char *ar = find_array_ref(p);
        if (!ar) {
            char *e = expand_string(p);
            append_str(&cur, &clen, &ccap, e);
            free(e);
            break;
        }
        if (ar > p) {
            char *chunk = sh_strndup(p, ar - p);
            char *e = expand_string(chunk);
            free(chunk);
            append_str(&cur, &clen, &ccap, e);
            free(e);
        }
        char *base = NULL;
        int star = 0, cflag = 0, bang = 0, has_off = 0, has_len = 0;
        long off = 0, slen = 0;
        const char *end = parse_array_ref(ar, &base, &star, &cflag, &bang,
                                          &off, &has_off, &slen, &has_len);
        if (!end || !base || !*base) {
            append_str(&cur, &clen, &ccap, "$");
            free(base);
            p = ar + 1;
            continue;
        }

        int ne = 0;
        char **vals = NULL;
        if (cflag) {
            char b[32];
            snprintf(b, sizeof(b), "%d", var_array_count(base));
            append_str(&cur, &clen, &ccap, b);
            free(base);
            p = end;
            continue;
        } else if (bang) {
            int ni = 0;
            long *idxs = var_array_indices(base, &ni);
            vals = sh_malloc((ni ? ni : 1) * sizeof(char *));
            for (int i = 0; i < ni; i++) {
                char b[32];
                snprintf(b, sizeof(b), "%ld", idxs[i]);
                vals[i] = sh_strdup(b);
            }
            free(idxs);
            ne = ni;
        } else {
            vals = var_array_values(base, &ne);
        }

        if (has_off) {
            int start = (int)off;
            if (start < 0) start += ne;
            if (start < 0) start = 0;
            if (start > ne) start = ne;
            int endi = ne;
            if (has_len) {
                endi = (slen < 0) ? ne + (int)slen : start + (int)slen;
                if (endi < start) endi = start;
                if (endi > ne) endi = ne;
            }
            char **nv = sh_malloc((ne ? ne : 1) * sizeof(char *));
            int m = 0;
            for (int i = start; i < endi; i++) nv[m++] = vals[i];
            for (int i = 0; i < start; i++) free(vals[i]);
            for (int i = endi; i < ne; i++) free(vals[i]);
            free(vals);
            vals = nv;
            ne = m;
        }

        if (star == 1) {
            /* ${a[*]} — join with the first IFS character */
            char *ifs = sh_getenv("IFS");
            char sep = (ifs && *ifs) ? *ifs : ' ';
            for (int i = 0; i < ne; i++) {
                if (i) { char sd[2] = { sep, '\0' };
                         append_str(&cur, &clen, &ccap, sd); }
                append_str(&cur, &clen, &ccap, vals[i]);
            }
        } else {
            /* ${a[@]} — one word per element */
            for (int i = 0; i < ne; i++) {
                if (i < ne - 1) {
                    char *w = sh_malloc(strlen(cur) + strlen(vals[i]) + 1);
                    sprintf(w, "%s%s", cur, vals[i]);
                    clen = 0; cur[0] = '\0';
                    emit_field(sh, result, nresult, w, q);
                } else {
                    clen = 0; cur[0] = '\0';
                    append_str(&cur, &clen, &ccap, vals[i]);
                    force_last = 1;
                }
            }
        }
        var_free_list(vals, ne);
        free(base);
        p = end;
    }

    if (clen > 0 || force_last)
        emit_field(sh, result, nresult, cur, q);
    else
        free(cur);
}

/* ---- expand an array of words ---------------------------------- */
char **expand_words_q(char **words, int *quoted, int *count) {
    Shell *sh = shell_get();
    char **result = sh_malloc(sizeof(char *) * MAX_ARGS);
    int nresult = 0;

    for (int i = 0; i < *count; i++) {
        int q = quoted ? quoted[i] : 0;

        /* an unquoted array reference expands to several words */
        if (find_array_ref(words[i])) {
            expand_array_token(sh, words[i], q, result, &nresult);
            continue;
        }

        /* "$@" / ${@} must produce one word per positional parameter;
         * any surrounding literal text attaches to the first/last word. */
        char **segs = NULL;
        int k = split_at_refs(words[i], &segs);
        if (k == 0) {
            free(segs);
            add_expanded_words(sh, result, &nresult, expand_string(words[i]), q);
            continue;
        }

        char **eseg = sh_malloc((k + 1) * sizeof(char *));
        for (int j = 0; j <= k; j++) eseg[j] = expand_string(segs[j]);

        int m = sh->npositional;
        if (m == 0) {
            /* no positional parameters: "$@" vanishes; "x$@y" keeps x y */
            size_t tot = 1;
            for (int j = 0; j <= k; j++) tot += strlen(eseg[j]);
            char *all = sh_malloc(tot);
            size_t o = 0;
            for (int j = 0; j <= k; j++) {
                size_t l = strlen(eseg[j]);
                memcpy(all + o, eseg[j], l);
                o += l;
            }
            all[o] = '\0';
            if (o > 0) add_expanded_words(sh, result, &nresult, all, q);
            else free(all);
        } else {
            int cap = m + 4, nw = 0;
            char **out = sh_malloc(cap * sizeof(char *));
            out[nw++] = sh_strdup(eseg[0]);
            for (int t = 1; t <= k; t++) {
                /* positional[0] continues the current (last) word */
                char *last = out[nw - 1];
                size_t l1 = strlen(last), l2 = strlen(sh->positional[0]);
                char *np = sh_malloc(l1 + l2 + 1);
                memcpy(np, last, l1);
                memcpy(np + l1, sh->positional[0], l2);
                np[l1 + l2] = '\0';
                free(last);
                out[nw - 1] = np;
                /* remaining parameters become separate words */
                for (int j = 1; j < m; j++) {
                    if (nw >= cap) { cap *= 2; out = sh_realloc(out, cap * sizeof(char *)); }
                    out[nw++] = sh_strdup(sh->positional[j]);
                }
                /* the following segment attaches to the last word */
                last = out[nw - 1];
                size_t l3 = strlen(last), l4 = strlen(eseg[t]);
                char *np2 = sh_malloc(l3 + l4 + 1);
                memcpy(np2, last, l3);
                memcpy(np2 + l3, eseg[t], l4);
                np2[l3 + l4] = '\0';
                free(last);
                out[nw - 1] = np2;
            }
            for (int j = 0; j < nw; j++)
                add_expanded_words(sh, result, &nresult, out[j], q);
            free(out);
        }

        for (int j = 0; j <= k; j++) { free(eseg[j]); free(segs[j]); }
        free(eseg);
        free(segs);
    }

    *count = nresult;
    result[nresult] = NULL;
    return result;
}

char **expand_words(char **words, int *count) {
    return expand_words_q(words, NULL, count);
}

/* ---- wildcard globbing with glob(3) -------------------------- */
/* growable string list used by the globstar walker */
typedef struct {
    char **items;
    int    count;
    int    cap;
} StrList;

static void strlist_add(StrList *sl, const char *s) {
    if (sl->count >= sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 16;
        sl->items = sh_realloc(sl->items, sl->cap * sizeof(char *));
    }
    sl->items[sl->count++] = sh_strdup(s);
}

/* join base + name handling leading-slash base correctly */
static void path_join(char *out, size_t outsz, const char *base, const char *name) {
    if (!base || !*base)
        snprintf(out, outsz, "%s", name);
    else if (base[strlen(base) - 1] == '/')
        snprintf(out, outsz, "%s%s", base, name);
    else
        snprintf(out, outsz, "%s/%s", base, name);
}

/* recursive walker implementing zsh-style '**' (globstar) */
static void globstar_walk(const char *base, char **parts, int nparts, int idx,
                          StrList *out) {
    if (idx >= nparts) {
        strlist_add(out, base);
        return;
    }
    const char *part = parts[idx];

    if (strcmp(part, "**") == 0) {
        /* zero directories then the rest of the pattern */
        globstar_walk(base, parts, nparts, idx + 1, out);
        /* one or more directories: descend into every subdir */
        DIR *d = opendir(*base ? base : ".");
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            struct stat st;
            char path[MAX_PATH];
            path_join(path, sizeof(path), base, e->d_name);
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
                globstar_walk(path, parts, nparts, idx, out);
        }
        closedir(d);
        return;
    }

    int is_last = (idx == nparts - 1);
    int has_wild = (strchr(part, '*') || strchr(part, '?') || strchr(part, '['));

    if (!has_wild) {
        /* literal name */
        char path[MAX_PATH];
        path_join(path, sizeof(path), base, part);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (is_last)
                strlist_add(out, path);
            else if (S_ISDIR(st.st_mode))
                globstar_walk(path, parts, nparts, idx + 1, out);
        }
        return;
    }

    DIR *d = opendir(*base ? base : ".");
    if (!d) return;
    struct dirent *e;
    int flags = (part[0] == '.') ? 0 : FNM_PERIOD;  /* '*' skips dotfiles */
    while ((e = readdir(d))) {
        if (fnmatch(part, e->d_name, flags) != 0) continue;
        char path[MAX_PATH];
        path_join(path, sizeof(path), base, e->d_name);
        if (is_last) {
            strlist_add(out, path);
        } else {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
                globstar_walk(path, parts, nparts, idx + 1, out);
        }
    }
    closedir(d);
}

/* glob with globstar ('**' matches zero or more directories) */
static char **globstar_glob(const char *pattern, int *count) {
    int is_abs = (pattern[0] == '/');
    const char *p = pattern;
    if (is_abs) p++;
    if (*p == '\0') { *count = 0; return NULL; }

    char *copy = sh_strdup(p);
    char *parts[512];
    int nparts = 0;
    char *save = NULL;
    char *tok = strtok_r(copy, "/", &save);
    while (tok && nparts < 511) {
        parts[nparts++] = tok;
        tok = strtok_r(NULL, "/", &save);
    }

    StrList out = { NULL, 0, 0 };
    const char *base = is_abs ? "/" : "";
    globstar_walk(base, parts, nparts, 0, &out);

    free(copy);

    if (out.count == 0) { *count = 0; free(out.items); return NULL; }
    out.items[out.count] = NULL;
    *count = out.count;
    return out.items;
}

char **glob_expand(const char *pattern, int *count) {
    Shell *sh = shell_get();

    /* globstar: '**' anywhere enables recursive matching */
    if (sh->opt_globstar && strstr(pattern, "**"))
        return globstar_glob(pattern, count);

    glob_t gl;
    int flags = GLOB_TILDE | GLOB_BRACE | GLOB_MARK;

    int rc = glob(pattern, flags, NULL, &gl);
    if (rc != 0) {
        *count = 0;
        globfree(&gl);
        return NULL;
    }

    char **result = sh_malloc((gl.gl_pathc + 1) * sizeof(char *));
    for (size_t i = 0; i < gl.gl_pathc; i++)
        result[i] = sh_strdup(gl.gl_pathv[i]);
    *count = gl.gl_pathc;
    result[*count] = NULL;

    globfree(&gl);
    return result;
}

/* ================================================================
 *  Brace expansion — {a,b,c} → a b c  (zsh/ksh feature)
 *  Supports multiple groups and nesting.
 * ================================================================ */
static char **brace_rec(const char *s, int *count) {
    char **one = sh_malloc(2 * sizeof(char *));
    one[0] = sh_strdup(s);
    one[1] = NULL;
    *count = 1;

    /* find first unescaped '{' */
    const char *start = NULL;
    for (const char *q = s; *q; q++) {
        if (*q == '\\') { q++; continue; }
        if (*q == '{') { start = q; break; }
    }
    if (!start) return one;

    /* find matching '}' (nesting-aware) */
    int depth = 0;
    const char *end = NULL;
    for (const char *q = start; *q; q++) {
        if (*q == '\\') { q++; continue; }
        if (*q == '{') depth++;
        else if (*q == '}') {
            depth--;
            if (depth == 0) { end = q; break; }
        }
    }
    if (!end) return one;  /* unbalanced — leave literal */

    /* split the inside on top-level commas */
    int inner_len = end - start - 1;
    char *inner = sh_strndup(start + 1, inner_len);

    char *opts[512];
    int nopts = 0;
    int cur = 0, d = 0;
    for (int i = 0; i < inner_len; i++) {
        char c = inner[i];
        if (c == '\\') { i++; continue; }
        if (c == '{') d++;
        else if (c == '}') d--;
        else if (c == ',' && d == 0) {
            inner[i] = '\0';
            opts[nopts++] = inner + cur;
            cur = i + 1;
            if (nopts >= 511) break;
        }
    }
    if (nopts == 0) {
        /* no commas — support numeric/alpha ranges: {1..5}, {a..c} */
        int lo = 0, hi = 0;
        char cl = 0, ch = 0;
        int is_num = 0, is_alpha = 0;
        if (sscanf(inner, "%d..%d", &lo, &hi) == 2 && lo <= hi)
            is_num = 1;
        else if (sscanf(inner, "%c..%c", &cl, &ch) == 2 && cl <= ch)
            is_alpha = 1;

        if (is_num || is_alpha) {
            char *prefix = sh_strndup(s, start - s);
            char *suffix = sh_strdup(end + 1);

            char **out = sh_malloc(2 * sizeof(char *));
            int nout = 0, cap = 2;

            if (is_num) {
                char num[32];
                for (int v = lo; v <= hi; v++) {
                    snprintf(num, sizeof(num), "%d", v);
                    if (nout >= cap) { cap *= 2; out = sh_realloc(out, cap * sizeof(char *)); }
                    size_t plen = strlen(prefix), nlen = strlen(num), slen = strlen(suffix);
                    char *combined = sh_malloc(plen + nlen + slen + 1);
                    memcpy(combined, prefix, plen);
                    memcpy(combined + plen, num, nlen);
                    memcpy(combined + plen + nlen, suffix, slen);
                    combined[plen + nlen + slen] = '\0';
                    out[nout++] = combined;
                }
            } else {
                char chbuf[2] = { 0, 0 };
                for (char v = cl; v <= ch; v++) {
                    chbuf[0] = v;
                    if (nout >= cap) { cap *= 2; out = sh_realloc(out, cap * sizeof(char *)); }
                    size_t plen = strlen(prefix), nlen = 1, slen = strlen(suffix);
                    char *combined = sh_malloc(plen + nlen + slen + 1);
                    memcpy(combined, prefix, plen);
                    memcpy(combined + plen, chbuf, 1);
                    memcpy(combined + plen + nlen, suffix, slen);
                    combined[plen + nlen + slen] = '\0';
                    out[nout++] = combined;
                }
            }
            out[nout] = NULL;

            free(prefix);
            free(suffix);
            free(inner);
            free(one[0]);
            free(one);
            *count = nout;
            return out;
        }

        free(inner);
        return one;
    }
    opts[nopts++] = inner + cur;

    /* build combinations and recurse */
    char *prefix = sh_strndup(s, start - s);
    char *suffix = sh_strdup(end + 1);

    char **out = sh_malloc(2 * sizeof(char *));
    int nout = 0, cap = 2;

    for (int i = 0; i < nopts; i++) {
        size_t plen = strlen(prefix), olen = strlen(opts[i]), slen = strlen(suffix);
        char *combined = sh_malloc(plen + olen + slen + 1);
        memcpy(combined, prefix, plen);
        memcpy(combined + plen, opts[i], olen);
        memcpy(combined + plen + olen, suffix, slen);
        combined[plen + olen + slen] = '\0';

        int subc;
        char **sub = brace_rec(combined, &subc);
        free(combined);
        for (int j = 0; j < subc; j++) {
            if (nout >= cap) { cap *= 2; out = sh_realloc(out, cap * sizeof(char *)); }
            out[nout++] = sub[j];
        }
        free(sub);
    }
    out[nout] = NULL;

    free(prefix);
    free(suffix);
    free(inner);
    free(one[0]);
    free(one);
    *count = nout;
    return out;
}

char **brace_expand(const char *str, int *count) {
    return brace_rec(str, count);
}

/* ---- variable name lookup (for export -p etc.) ---------------- */
char *var_expand(const char *name) {
    return sh_strdup(sh_getenv(name) ? sh_getenv(name) : "");
}
