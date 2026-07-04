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
            p++;
            if (blen + 2 >= bcap) { bcap *= 2; buf = sh_realloc(buf, bcap); }
            buf[blen++] = *p++;
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

        /* if noglob is set, skip globbing */
        if (sh->opt_noglob) {
            result[nresult++] = expanded;
            continue;
        }

        /* check if the word contains glob characters */
        int has_glob = 0;
        for (char *p = expanded; *p && !has_glob; p++)
            if (*p == '*' || *p == '?' || *p == '[') has_glob = 1;

        if (has_glob) {
            int gcount = 0;
            char **globs = glob_expand(expanded, &gcount);
            if (gcount > 0) {
                for (int j = 0; j < gcount && nresult < MAX_ARGS - 1; j++)
                    result[nresult++] = globs[j];
                free(globs);
                free(expanded);
            } else {
                /* no match — keep literal */
                result[nresult++] = expanded;
            }
        } else {
            result[nresult++] = expanded;
        }
    }

    *count = nresult;
    result[nresult] = NULL;
    return result;
}

/* ---- wildcard globbing with glob(3) -------------------------- */
char **glob_expand(const char *pattern, int *count) {
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

/* ---- variable name lookup (for export -p etc.) ---------------- */
char *var_expand(const char *name) {
    return sh_strdup(sh_getenv(name) ? sh_getenv(name) : "");
}
