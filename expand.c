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

    /* ${VAR} or ${VAR:-default} or ${VAR:=default} */
    if (*p == '{') {
        p++;  /* skip { */
        nlen = 0;
        while (*p && *p != '}' && *p != ':' && *p != '-' && *p != '=' &&
               *p != '+' && *p != '?' &&
               nlen < (int)sizeof(name) - 1) {
            name[nlen++] = *p++;
        }
        name[nlen] = '\0';

        /* handle modifiers: :-  :=  :+  :?  */
        if (*p == ':') {
            p++;
            char mod = *p++;  /* - = + ? */
            /* collect default value */
            char def[4096]; int dlen = 0;
            while (*p && *p != '}' && dlen < (int)sizeof(def) - 1)
                def[dlen++] = *p++;
            def[dlen] = '\0';

            char *val = sh_getenv(name);
            int use_default = (!val || !*val);

            switch (mod) {
            case '-':  /* ${VAR:-default} */
                *pp = (*p == '}') ? p + 1 : p;
                return use_default ? sh_strdup(def) : sh_strdup(val);
            case '=':  /* ${VAR:=default} — assign if unset/empty */
                if (use_default) {
                    sh_setenv(name, def, 1);
                    *pp = (*p == '}') ? p + 1 : p;
                    return sh_strdup(def);
                }
                *pp = (*p == '}') ? p + 1 : p;
                return sh_strdup(val);
            case '+':  /* ${VAR:+replacement} — use replacement if set */
                *pp = (*p == '}') ? p + 1 : p;
                return use_default ? sh_strdup("") : sh_strdup(def);
            case '?':  /* ${VAR:?error} — error if unset */
                if (use_default) {
                    fprintf(stderr, "besh: %s: %s\n", name,
                            dlen > 0 ? def : "parameter null or not set");
                }
                *pp = (*p == '}') ? p + 1 : p;
                return use_default ? sh_strdup("") : sh_strdup(val);
            }
        }

        if (*p == '}') p++;
        *pp = p;
        char *v = sh_getenv(name);
        return sh_strdup(v ? v : "");
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
            if (nx == '$' || nx == '`' || nx == '"' || nx == '\\' || nx == '\n') {
                p++;
                if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                buf[blen++] = *p++;
                continue;
            }
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
        if (*p == '~' && (p == str || *(p-1) == ' ') &&
            (*(p+1) == '\0' || *(p+1) == '/' || *(p+1) == ' ')) {
            char *exp = tilde_expand(p);
            int elen = strlen(exp);
            while (blen + elen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            memcpy(buf + blen, exp, elen);
            blen += elen;
            free(exp);
            /* advance past the tilde pattern */
            p++; while (*p && *p != '/' && *p != ' ' && *p != ':') p++;
            continue;
        }
        if (*p == '\'') {
            /* skip single-quoted sections as-is */
            p++;
            while (*p && *p != '\'') {
                if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
                buf[blen++] = *p++;
            }
            if (*p) p++;
            continue;
        }
        if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
        buf[blen++] = *p++;
    }
    buf[blen] = '\0';
    return buf;
}

/* ---- expand an array of words ---------------------------------- */
char **expand_words(char **words, int *count) {
    Shell *sh = shell_get();
    char **result = sh_malloc(sizeof(char *) * MAX_ARGS);
    int nresult = 0;

    for (int i = 0; i < *count; i++) {
        char *expanded = expand_string(words[i]);

        /* brace expansion {a,b,c} */
        int bc;
        char **br = brace_expand(expanded, &bc);
        free(expanded);

        for (int b = 0; b < bc && nresult < MAX_ARGS - 1; b++) {
            /* if noglob is set, skip globbing */
            if (sh->opt_noglob) {
                result[nresult++] = br[b];
                continue;
            }

            /* check if the word contains glob characters */
            int has_glob = 0;
            for (char *p = br[b]; *p && !has_glob; p++)
                if (*p == '*' || *p == '?' || *p == '[') has_glob = 1;

            if (has_glob) {
                int gcount = 0;
                char **globs = glob_expand(br[b], &gcount);
                if (gcount > 0) {
                    for (int j = 0; j < gcount && nresult < MAX_ARGS - 1; j++)
                        result[nresult++] = globs[j];
                    free(globs);
                    free(br[b]);
                } else {
                    /* no match — keep literal */
                    result[nresult++] = br[b];
                }
            } else {
                result[nresult++] = br[b];
            }
        }
        free(br);
    }

    *count = nresult;
    result[nresult] = NULL;
    return result;
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
