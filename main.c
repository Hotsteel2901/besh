/* ================================================================
 *  main.c — entry point, signal handling, line editor, REPL loop
 * ================================================================ */

#include "shell.h"

/* ---- global shell singleton ---------------------------------- */
static Shell _shell;
Shell *shell_get(void) { return &_shell; }

/* ---- history storage helpers (defined further down) ---------- */
static void hist_ensure_cap(int need);

/* ---- line-editor output helper (defined further down) -------- */
/* A thin wrapper around write(2) so the many fire-and-forget writes in
 * the editor do not each need a hand-rolled unused-result dance. */
static void em_write(int fd, const char *s, size_t n);

/* ---- utility allocators (used everywhere) -------------------- */
void *sh_malloc(size_t n) {
    void *p = malloc(n);
    if (!p) { perror("besh: malloc"); exit(1); }
    return p;
}
void *sh_realloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { perror("besh: realloc"); exit(1); }
    return q;
}
char *sh_strdup(const char *s) {
    if (!s) return NULL;
    char *d = strdup(s);
    if (!d) { perror("besh: strdup"); exit(1); }
    return d;
}
char *sh_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *d = strndup(s, n);
    if (!d) { perror("besh: strndup"); exit(1); }
    return d;
}
/* Trim leading/trailing blanks in place.  NOTE: this returns an internal
 * pointer into `s` (or s+k), NOT a new allocation — never free() the
 * return value (free the original buffer instead). */
char *sh_trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    *(end + 1) = '\0';
    return s;
}
int sh_is_whitespace(int c) { return c == ' ' || c == '\t' || c == '\n'; }
int sh_is_special_char(int c) {
    return (c == '|' || c == '&' || c == ';' || c == '<' ||
            c == '>' || c == '(' || c == ')' || c == '\n' || c == '\0');
}

/* ---- environment variable helpers ---------------------------- */
char *sh_getenv(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nvars; i++)
        if (strcmp(sh->vars[i].name, name) == 0)
            return sh->vars[i].value;
    return getenv(name);  /* fallback to system environ */
}
void sh_setenv(const char *name, const char *value, int export_flag) {
    Shell *sh = shell_get();
    /* look for existing */
    for (int i = 0; i < sh->nvars; i++) {
        if (strcmp(sh->vars[i].name, name) == 0) {
            if (sh->vars[i].readonly) return;
            free(sh->vars[i].value);
            sh->vars[i].value = sh_strdup(value);
            sh->vars[i].exported = export_flag || sh->vars[i].exported;
            if (sh->vars[i].exported) setenv(name, value, 1);
            return;
        }
    }
    /* add new */
    if (sh->nvars >= sh->vars_cap) {
        sh->vars_cap = sh->vars_cap ? sh->vars_cap * 2 : 128;
        sh->vars = sh_realloc(sh->vars, sh->vars_cap * sizeof(Var));
    }
    sh->vars[sh->nvars].name       = sh_strdup(name);
    sh->vars[sh->nvars].value      = sh_strdup(value);
    sh->vars[sh->nvars].exported   = export_flag;
    sh->vars[sh->nvars].readonly   = 0;
    sh->vars[sh->nvars].array      = 0;
    sh->vars[sh->nvars].is_element = 0;
    sh->vars[sh->nvars].index      = 0;
    sh->nvars++;
    if (export_flag) setenv(name, value, 1);
}
void sh_unsetenv(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nvars; i++) {
        if (strcmp(sh->vars[i].name, name) == 0) {
            if (sh->vars[i].readonly) return;
            free(sh->vars[i].name);
            free(sh->vars[i].value);
            /* shift remaining */
            memmove(&sh->vars[i], &sh->vars[i+1],
                    (sh->nvars - i - 1) * sizeof(Var));
            sh->nvars--;
            unsetenv(name);
            return;
        }
    }
    unsetenv(name);
}

/* ================================================================
 *  Indexed arrays
 *
 *  Element k of array `a` lives in its own Var entry named "a[k]".
 *  A scalar entry "a" mirrors element 0 so that $a == ${a[0]}.
 * ================================================================ */

int var_base_len(const char *name) {
    int i = 0;
    while (name && name[i] && name[i] != '[') i++;
    return i;
}

char *var_base_of(const char *name) {
    return sh_strndup(name, var_base_len(name));
}

/* Parse "name", "name[idx]", "name[@]" or "name[*]".
 *   0 = plain name, 1 = indexed element, 2 = whole array (@ / *)  */
int var_parse_subscript(const char *name, char **base_out, long *idx,
                        int *star) {
    *base_out = NULL; *idx = 0; *star = 0;
    if (!name || !*name) return 0;
    int bl = var_base_len(name);
    if (bl == 0) return 0;
    if (name[bl] != '[') { *base_out = sh_strndup(name, bl); return 0; }
    const char *close = strchr(name + bl, ']');
    if (!close) { *base_out = sh_strndup(name, bl); return 0; }
    char *sub = sh_strndup(name + bl + 1, close - (name + bl + 1));
    if (strcmp(sub, "@") == 0) { *star = 2; free(sub);
        *base_out = sh_strndup(name, bl); return 2; }
    if (strcmp(sub, "*") == 0) { *star = 1; free(sub);
        *base_out = sh_strndup(name, bl); return 2; }
    char *end;
    long v = strtol(sub, &end, 10);
    int ok = (end != sub && *end == '\0');
    free(sub);
    *base_out = sh_strndup(name, bl);
    if (!ok) return 0;
    *idx = v;
    return 1;
}

static Var *var_find_entry(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nvars; i++)
        if (strcmp(sh->vars[i].name, name) == 0)
            return &sh->vars[i];
    return NULL;
}

int var_is_array(const char *base) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nvars; i++) {
        if (sh->vars[i].is_element &&
            (int)strlen(base) == var_base_len(sh->vars[i].name) &&
            strncmp(sh->vars[i].name, base, strlen(base)) == 0)
            return 1;
        if (!sh->vars[i].is_element && sh->vars[i].array &&
            strcmp(sh->vars[i].name, base) == 0)
            return 1;
    }
    return 0;
}

void var_set_array_attr(const char *base, int on) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nvars; i++) {
        if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0) {
            sh->vars[i].array = on;
            return;
        }
    }
    if (!on) return;
    /* create the mirror/scalar entry so the attribute sticks */
    if (sh->nvars >= sh->vars_cap) {
        sh->vars_cap = sh->vars_cap ? sh->vars_cap * 2 : 128;
        sh->vars = sh_realloc(sh->vars, sh->vars_cap * sizeof(Var));
    }
    sh->vars[sh->nvars].name       = sh_strdup(base);
    sh->vars[sh->nvars].value      = sh_strdup("");
    sh->vars[sh->nvars].exported   = 0;
    sh->vars[sh->nvars].readonly   = 0;
    sh->vars[sh->nvars].array      = 1;
    sh->vars[sh->nvars].is_element = 0;
    sh->vars[sh->nvars].index      = 0;
    sh->nvars++;
}

static void var_set_value_raw(Var *v, const char *val) {
    free(v->value);
    v->value = sh_strdup(val);
}

void var_array_set(const char *base, long idx, const char *value) {
    Shell *sh = shell_get();
    /* honour readonly on the base variable */
    Var *b = var_find_entry(base);
    if (b && b->readonly) return;

    char nm[1024];
    snprintf(nm, sizeof(nm), "%s[%ld]", base, idx);
    Var *e = var_find_entry(nm);
    if (e) {
        var_set_value_raw(e, value);
    } else {
        if (sh->nvars >= sh->vars_cap) {
            sh->vars_cap = sh->vars_cap ? sh->vars_cap * 2 : 128;
            sh->vars = sh_realloc(sh->vars, sh->vars_cap * sizeof(Var));
        }
        sh->vars[sh->nvars].name       = sh_strdup(nm);
        sh->vars[sh->nvars].value      = sh_strdup(value);
        sh->vars[sh->nvars].exported   = 0;
        sh->vars[sh->nvars].readonly   = b ? b->readonly : 0;
        sh->vars[sh->nvars].array      = 0;
        sh->vars[sh->nvars].is_element = 1;
        sh->vars[sh->nvars].index      = idx;
        sh->nvars++;
    }

    /* mark the base as an array */
    if (!b || !b->array) var_set_array_attr(base, 1);

    /* mirror element 0 into the scalar entry */
    if (idx == 0) {
        Var *m = var_find_entry(base);
        if (m && !m->readonly) var_set_value_raw(m, value);
    }
}

char *var_array_get(const char *base, long idx, int *is_set) {
    if (is_set) *is_set = 0;
    char nm[1024];
    snprintf(nm, sizeof(nm), "%s[%ld]", base, idx);
    Var *e = var_find_entry(nm);
    if (e) { if (is_set) *is_set = 1; return sh_strdup(e->value ? e->value : ""); }
    if (idx == 0) {
        Var *b = var_find_entry(base);
        if (b) { if (is_set) *is_set = 1; return sh_strdup(b->value ? b->value : ""); }
    }
    return sh_strdup("");
}

int var_array_unset(const char *base, long idx) {
    Shell *sh = shell_get();
    char nm[1024];
    snprintf(nm, sizeof(nm), "%s[%ld]", base, idx);
    int found = 0;
    for (int i = 0; i < sh->nvars; i++) {
        if (strcmp(sh->vars[i].name, nm) == 0) {
            if (sh->vars[i].readonly) return 1;
            free(sh->vars[i].name);
            free(sh->vars[i].value);
            memmove(&sh->vars[i], &sh->vars[i+1],
                    (sh->nvars - i - 1) * sizeof(Var));
            sh->nvars--;
            found = 1;
            break;
        }
    }
    if (idx == 0) {
        for (int i = 0; i < sh->nvars; i++) {
            if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0) {
                if (sh->vars[i].readonly) return 1;
                free(sh->vars[i].name);
                free(sh->vars[i].value);
                memmove(&sh->vars[i], &sh->vars[i+1],
                        (sh->nvars - i - 1) * sizeof(Var));
                sh->nvars--;
                found = 1;
                break;
            }
        }
    }
    return found;
}

/* remove all element entries plus the scalar mirror */
void var_array_clear(const char *base) {
    Shell *sh = shell_get();
    int bl = strlen(base);
    for (int i = 0; i < sh->nvars; ) {
        int match = 0;
        if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0)
            match = 1;
        else if (sh->vars[i].is_element && sh->vars[i].index >= 0 &&
                 (int)var_base_len(sh->vars[i].name) == bl &&
                 strncmp(sh->vars[i].name, base, bl) == 0)
            match = 1;
        if (match) {
            if (sh->vars[i].readonly) { i++; continue; }
            free(sh->vars[i].name);
            free(sh->vars[i].value);
            memmove(&sh->vars[i], &sh->vars[i+1],
                    (sh->nvars - i - 1) * sizeof(Var));
            sh->nvars--;
        } else i++;
    }
}

/* collect element indices of `base` in ascending order (malloc'd) */
long *var_array_indices(const char *base, int *n) {
    Shell *sh = shell_get();
    int bl = strlen(base);
    long tmp[4096];
    int m = 0;
    for (int i = 0; i < sh->nvars && m < 4096; i++) {
        if (!sh->vars[i].is_element) continue;
        if ((int)var_base_len(sh->vars[i].name) != bl) continue;
        if (strncmp(sh->vars[i].name, base, bl) != 0) continue;
        tmp[m++] = sh->vars[i].index;
    }
    /* scalar-only variable behaves like a 1-element array */
    if (m == 0) {
        for (int i = 0; i < sh->nvars; i++)
            if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0)
                { tmp[m++] = 0; break; }
    }
    for (int i = 1; i < m; i++) {           /* insertion sort */
        long v = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = v;
    }
    long *out = sh_malloc((m ? m : 1) * sizeof(long));
    for (int i = 0; i < m; i++) out[i] = tmp[i];
    *n = m;
    return out;
}

char **var_array_values(const char *base, int *n) {
    int ni = 0;
    long *idxs = var_array_indices(base, &ni);
    char **out = sh_malloc((ni ? ni : 1) * sizeof(char *));
    for (int i = 0; i < ni; i++) {
        char *v = var_array_get(base, idxs[i], NULL);
        out[i] = v;
    }
    free(idxs);
    *n = ni;
    return out;
}

int var_array_count(const char *base) {
    int n = 0;
    long *idxs = var_array_indices(base, &n);
    free(idxs);
    return n;
}

void var_free_list(char **v, int n) {
    if (!v) return;
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

/* ================================================================
 *  Function-local scopes
 * ================================================================ */

int scope_in_function(void) {
    return shell_get()->nscopes > 0;
}

void scope_push(void) {
    Shell *sh = shell_get();
    if (sh->nscopes >= sh->scopes_cap) {
        sh->scopes_cap = sh->scopes_cap ? sh->scopes_cap * 2 : 16;
        sh->scopes = sh_realloc(sh->scopes, sh->scopes_cap * sizeof(ScopeFrame));
    }
    memset(&sh->scopes[sh->nscopes], 0, sizeof(ScopeFrame));
    sh->nscopes++;
}

static void frame_save(ScopeFrame *fr, const Var *v) {
    if (fr->nsaved >= fr->saved_cap) {
        fr->saved_cap = fr->saved_cap ? fr->saved_cap * 2 : 16;
        fr->saved = sh_realloc(fr->saved, fr->saved_cap * sizeof(SavedVar));
    }
    SavedVar *s = &fr->saved[fr->nsaved++];
    s->name       = sh_strdup(v->name);
    s->value      = v->value ? sh_strdup(v->value) : NULL;
    s->existed    = 1;
    s->exported   = v->exported;
    s->readonly   = v->readonly;
    s->array      = v->array;
    s->is_element = v->is_element;
    s->index      = v->index;
}

/* declare `name` local to the current function frame: snapshot and remove
 * the existing variable (base + all elements) if present. */
void scope_declare(const char *name) {
    Shell *sh = shell_get();
    if (sh->nscopes == 0) return;
    ScopeFrame *fr = &sh->scopes[sh->nscopes - 1];
    char *base = var_base_of(name);
    int bl = strlen(base);

    for (int i = 0; i < fr->nlocals; i++)
        if (strcmp(fr->locals[i], base) == 0) { free(base); return; }

    if (fr->nlocals >= fr->locals_cap) {
        fr->locals_cap = fr->locals_cap ? fr->locals_cap * 2 : 8;
        fr->locals = sh_realloc(fr->locals,
                                fr->locals_cap * sizeof(char *));
    }
    fr->locals[fr->nlocals++] = base;

    for (int i = 0; i < sh->nvars; ) {
        int match = 0;
        if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0)
            match = 1;
        else if (sh->vars[i].is_element &&
                 (int)var_base_len(sh->vars[i].name) == bl &&
                 strncmp(sh->vars[i].name, base, bl) == 0)
            match = 1;
        if (match) {
            frame_save(fr, &sh->vars[i]);
            free(sh->vars[i].name);
            free(sh->vars[i].value);
            memmove(&sh->vars[i], &sh->vars[i+1],
                    (sh->nvars - i - 1) * sizeof(Var));
            sh->nvars--;
        } else i++;
    }
}

void scope_pop(void) {
    Shell *sh = shell_get();
    if (sh->nscopes == 0) return;
    ScopeFrame *fr = &sh->scopes[--sh->nscopes];

    /* drop anything the function left behind for these names */
    for (int i = 0; i < fr->nlocals; i++) {
        const char *base = fr->locals[i];
        int bl = strlen(base);
        for (int j = 0; j < sh->nvars; ) {
            int match = 0;
            if (!sh->vars[j].is_element && strcmp(sh->vars[j].name, base) == 0)
                match = 1;
            else if (sh->vars[j].is_element &&
                     (int)var_base_len(sh->vars[j].name) == bl &&
                     strncmp(sh->vars[j].name, base, bl) == 0)
                match = 1;
            if (match) {
                free(sh->vars[j].name);
                free(sh->vars[j].value);
                memmove(&sh->vars[j], &sh->vars[j+1],
                        (sh->nvars - j - 1) * sizeof(Var));
                sh->nvars--;
            } else j++;
        }
    }

    /* restore the snapshots (in order) */
    for (int i = 0; i < fr->nsaved; i++) {
        SavedVar *s = &fr->saved[i];
        if (s->existed) {
            if (sh->nvars >= sh->vars_cap) {
                sh->vars_cap = sh->vars_cap ? sh->vars_cap * 2 : 128;
                sh->vars = sh_realloc(sh->vars, sh->vars_cap * sizeof(Var));
            }
            Var *v = &sh->vars[sh->nvars++];
            v->name       = sh_strdup(s->name);
            v->value      = sh_strdup(s->value ? s->value : "");
            v->exported   = s->exported;
            v->readonly   = s->readonly;
            v->array      = s->array;
            v->is_element = s->is_element;
            v->index      = s->index;
            if (s->exported && !s->is_element)
                setenv(s->name, s->value ? s->value : "", 1);
        }
        free(s->name);
        free(s->value);
    }
    free(fr->saved);
    for (int i = 0; i < fr->nlocals; i++) free(fr->locals[i]);
    free(fr->locals);
    memset(fr, 0, sizeof(*fr));
}

/* assign to a (possibly array) variable name without word splitting */
void sh_assign(const char *name, const char *value, int export_flag, int append) {
    char *base = NULL;
    long idx = 0;
    int star = 0;
    int kind = var_parse_subscript(name, &base, &idx, &star);
    if (!base) base = sh_strdup(name);

    if (kind == 1) {
        if (append) {
            char *old = var_array_get(base, idx, NULL);
            char *nv = sh_malloc(strlen(old) + strlen(value) + 1);
            sprintf(nv, "%s%s", old, value);
            free(old);
            var_array_set(base, idx, nv);
            free(nv);
        } else {
            var_array_set(base, idx, value);
        }
    } else if (kind == 2) {
        var_array_set(base, 0, value);
    } else {
        if (var_is_array(base)) {
            if (append) {
                char *old = var_array_get(base, 0, NULL);
                char *nv = sh_malloc(strlen(old) + strlen(value) + 1);
                sprintf(nv, "%s%s", old, value);
                free(old);
                var_array_set(base, 0, nv);
                free(nv);
            } else {
                var_array_set(base, 0, value);
            }
        } else {
            if (append) {
                char *old = sh_getenv(base);
                char *nv = sh_malloc((old ? strlen(old) : 0) + strlen(value) + 1);
                sprintf(nv, "%s%s", old ? old : "", value);
                sh_setenv(base, nv, export_flag);
                free(nv);
            } else {
                sh_setenv(base, value, export_flag);
            }
        }
    }
    free(base);
}
char *resolve_path(const char *cmd) {
    if (!cmd || !*cmd) return NULL;
    /* absolute or relative path — check directly */
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) return sh_strdup(cmd);
        return NULL;
    }
    /* search PATH */
    char *path = sh_getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    char *path_copy = sh_strdup(path);
    char *save = NULL;
    char *dir = strtok_r(path_copy, ":", &save);
    static char full[MAX_PATH];
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) { free(path_copy); return sh_strdup(full); }
        dir = strtok_r(NULL, ":", &save);
    }
    free(path_copy);
    return NULL;
}

/* ================================================================
 *  SIGNAL HANDLING
 * ================================================================ */
static void sigint_handler(int sig) {
    (void)sig;
    Shell *sh = shell_get();
    em_write(STDOUT_FILENO, "\n", 1);
    /* if no foreground job, just redraw prompt */
    sh->line_pos = 0;
    sh->line_len = 0;
    sh->line_buf[0] = '\0';
    if (sh->running) em_write(STDOUT_FILENO, sh->prompt, strlen(sh->prompt));
}

static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        job_update(pid, status);
    }
    errno = saved_errno;
}

static void sigtstp_handler(int sig) {
    (void)sig;
    /* suspend the shell itself if it's a login shell */
    signal(SIGTSTP, SIG_DFL);
    kill(getpid(), SIGTSTP);
}

void signals_setup(void) {
    Shell *sh = shell_get();
    sh->shell_pgid = getpid();
    if (sh->job_interactive) {
        /* put shell in its own process group */
        while (tcgetpgrp(sh->term_fd) != (sh->shell_pgid = getpgrp()))
            kill(-sh->shell_pgid, SIGTTIN);

        signal(SIGINT,  SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);

        sh->shell_pgid = getpid();
        if (setpgid(sh->shell_pgid, sh->shell_pgid) < 0) {
            /* EPERM means we are already a session leader with our own
             * process group (e.g. started under script/pty) — fine. */
            if (errno != EPERM) {
                perror("besh: setpgid");
                exit(1);
            }
        }
        tcsetpgrp(sh->term_fd, sh->shell_pgid);

        signal(SIGINT,  sigint_handler);
        signal(SIGCHLD, sigchld_handler);
        signal(SIGTSTP, sigtstp_handler);
    }
}

void signals_restore(void) {
    Shell *sh = shell_get();
    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, sh->shell_pgid);
        tcsetattr(sh->term_fd, TCSANOW, &sh->orig_termios);
    }
}

void signals_block(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGINT);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

void signals_unblock(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGINT);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/* block/unblock only SIGCHLD — used so the signal handler cannot reap a
 * foreground child while we are explicitly waiting on it (avoids ECHILD) */
void sigchld_block(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

void sigchld_unblock(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/* ================================================================
 *  UTF-8 aware helpers
 *  - utf8_decode : one code point + number of bytes consumed
 *  - cp_width    : terminal display width (1 / 2 / 0)
 *  - index helpers so every edit acts on whole characters
 * ================================================================ */
typedef struct { int lo, hi; } URange;

/* East-Asian Wide / Fullwidth code points → width 2 */
static const URange width2_ranges[] = {
    {0x1100, 0x115F}, {0x2329, 0x232A}, {0x2E80, 0x303E},
    {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF},
    {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
    {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF01, 0xFF60},
    {0xFFE0, 0xFFE6}, {0x1F300, 0x1F64F}, {0x1F900, 0x1F9FF},
    {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};
/* combining marks / zero-width → width 0 */
static const URange width0_ranges[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD},
    {0x0610, 0x061A}, {0x064B, 0x065F}, {0x1AB0, 0x1AFF},
    {0x1DC0, 0x1DFF}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0x200B, 0x200F},
};

static int in_ranges(const URange *r, int n, int cp) {
    for (int i = 0; i < n; i++)
        if (cp >= r[i].lo && cp <= r[i].hi) return 1;
    return 0;
}

static int utf8_decode(const char *s, int *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    int n, v;
    if      ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
    else { *cp = c; return 1; }                 /* invalid byte */
    for (int i = 1; i < n; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) { *cp = c; return 1; }   /* truncated */
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return n;
}

static int cp_width(int cp) {
    if (cp < 32) return 0;
    if (in_ranges(width0_ranges, (int)(sizeof(width0_ranges)/sizeof(URange)), cp))
        return 0;
    if (in_ranges(width2_ranges, (int)(sizeof(width2_ranges)/sizeof(URange)), cp))
        return 2;
    return 1;
}

/* display width of the first `len` bytes of s */
static int utf8_width(const char *s, int len) {
    int w = 0, i = 0;
    while (i < len) {
        int cp;
        int n = utf8_decode(s + i, &cp);
        if (n <= 0) n = 1;
        w += cp_width(cp);
        i += n;
    }
    return w;
}

/* start index of the character before byte offset `pos` */
static int utf8_prev_index(const char *s, int pos) {
    if (pos <= 0) return 0;
    int i = pos - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

/* start index of the character after byte offset `pos` */
static int utf8_next_index(const char *s, int len, int pos) {
    if (pos >= len) return len;
    int i = pos + 1;
    while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

/* ================================================================
 *  LINE EDITOR  (readline-style using termios + VT100 escapes)
 * ================================================================ */
/* Enter raw mode.  The cooked settings are captured only once (the first
 * time we go raw) so that a later term_restore() always restores the real
 * terminal state even if a full-screen program clobbered termios while a
 * command was running.  Calling this while already raw simply re-applies
 * the raw settings — it does NOT overwrite orig_termios. */
static void term_raw(void) {
    Shell *sh = shell_get();
    if (!sh->term_in_raw) {
        tcgetattr(sh->term_fd, &sh->orig_termios);
        sh->shell_termios = sh->orig_termios;
        sh->shell_termios.c_lflag &= ~(ICANON | ECHO | ISIG);
        sh->shell_termios.c_cc[VMIN]  = 1;
        sh->shell_termios.c_cc[VTIME] = 0;
        sh->term_in_raw = 1;
    }
    tcsetattr(sh->term_fd, TCSANOW, &sh->shell_termios);
}

/* Restore cooked mode (no-op when we are not in raw mode). */
static void term_restore(void) {
    Shell *sh = shell_get();
    if (sh->term_in_raw) {
        tcsetattr(sh->term_fd, TCSANOW, &sh->orig_termios);
        sh->term_in_raw = 0;
    }
}

/* read one byte from the terminal; returns -1 on EOF/error */
static int read_byte(void) {
    Shell *sh = shell_get();
    unsigned char c;
    if (read(sh->term_fd, &c, 1) != 1) return -1;
    return c;
}

/* read one key.  returns the byte for simple keys;
 * for escape sequences we return a synthetic code 256+.
 *
 * Bytes are consumed one at a time so that several escape sequences that
 * arrive together (fast arrow presses, a pasted block) are not swallowed
 * into a single read and lost. */
static int read_key(void) {
    int c = read_byte();
    if (c < 0) return -1;
    if (c != 27) return c;    /* 27 = ESC */

    int b = read_byte();
    if (b < 0) return 27;     /* bare ESC */

    if (b == '[') {
        /* CSI sequences */
        int d = read_byte();
        if (d < 0) return 27;
        switch (d) {
        case 'A': return 256 + 'A';  /* Up    */
        case 'B': return 256 + 'B';  /* Down  */
        case 'C': return 256 + 'C';  /* Right */
        case 'D': return 256 + 'D';  /* Left  */
        case 'H': return 256 + 'H';  /* Home  */
        case 'F': return 256 + 'F';  /* End   */
        case '3':                    /* Delete */
        case '5':                    /* PgUp   */
        case '6':                    /* PgDn   */
        case '1':                    /* Home (alternate) */
        case '4':                    /* End (alternate)  */
        case '7':                    /* Home (urxvt)     */
        case '8': {                  /* End (urxvt)      */
            int t = read_byte();
            if (t != '~') return 27;
            if (d == '3') return 256 + 127;
            if (d == '5') return 256 + 'U';
            if (d == '6') return 256 + 'V';
            if (d == '1' || d == '7') return 256 + 'H';
            return 256 + 'F';        /* '4' or '8' */
        }
        }
        return 27;   /* unrecognised escape, return bare ESC */
    }
    if (b == 'O') {
        /* SS3 sequences */
        int d = read_byte();
        if (d == 'H') return 256 + 'H';  /* Home */
        if (d == 'F') return 256 + 'F';  /* End  */
        return 27;
    }
    return 27;
}

/* ================================================================
 *  FISH-STYLE SYNTAX HIGHLIGHTING
 *  Colors commands (green=valid, red=invalid), options (yellow),
 *  directories (blue), variables (cyan), numbers (magenta),
 *  and quoted strings.
 * ================================================================ */
static void line_insert(char c);   /* defined below (used by suggestion) */

static int hl_is_known_command(const char *w) {
    Shell *sh = shell_get();
    if (!w || !*w) return 0;
    if (builtin_is(w)) return 1;
    for (int i = 0; i < sh->nfuncs; i++)
        if (strcmp(sh->funcs[i].name, w) == 0) return 1;
    for (int i = 0; i < sh->naliases; i++)
        if (strcmp(sh->aliases[i].name, w) == 0) return 1;
    if (strchr(w, '/')) return access(w, X_OK) == 0;
    char *p = resolve_path(w);
    if (p) { free(p); return 1; }
    return 0;
}

/* read one "word" starting at p, honoring quotes/escapes; returns end ptr */
static const char *hl_scan_word(const char *p) {
    char quote = 0;
    while (*p) {
        char c = *p;
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && p[1]) p++;
        } else {
            if (c == '\'' || c == '"') quote = c;
            else if (c == '\\' && p[1]) { p++; }
            else if (sh_is_whitespace(c)) break;
        }
        p++;
    }
    return p;
}

static void write_highlighted(int fd, const char *line) {
    static char ob[131072];
    int o = 0;
    const char *p = line;
    int first = 1;   /* command position */

    while (*p) {
        if (*p == ' ' || *p == '\t') {
            if (o + 8 >= (int)sizeof(ob)) { em_write(fd, ob, o); o = 0; }
            ob[o++] = *p++;
            continue;
        }

        const char *end = hl_scan_word(p);
        int wlen = end - p;
        char *word = sh_strndup(p, wlen);

        const char *color;
        if (first) {
            color = hl_is_known_command(word) ? "\x1b[1;32m" : "\x1b[1;31m";
        } else if (word[0] == '-' && word[1] && word[1] != '\0') {
            color = "\x1b[0;33m";                 /* options */
        } else if (word[0] == '$' && word[1]) {
            color = "\x1b[0;36m";                 /* variables */
        } else if (word[0] == '\'' || word[0] == '"') {
            color = "\x1b[0;32m";                 /* quoted strings */
        } else if (strchr(word, '<') || strchr(word, '>')) {
            color = "\x1b[1;35m";                 /* redirections */
        } else {
            int numeric = 1;
            for (const char *q = word; *q; q++) {
                if (!(*q >= '0' && *q <= '9')) { numeric = 0; break; }
            }
            if (numeric) color = "\x1b[0;35m";    /* numbers */
            else {
                struct stat st;
                if (stat(word, &st) == 0 && S_ISDIR(st.st_mode))
                    color = "\x1b[1;34m";         /* directories */
                else
                    color = NULL;                 /* normal */
            }
        }

        if (o + wlen + 32 >= (int)sizeof(ob)) { em_write(fd, ob, o); o = 0; }
        if (color) {
            int n = snprintf(ob + o, sizeof(ob) - o, "%s", color);
            o += n;
        }
        memcpy(ob + o, p, wlen);
        o += wlen;
        if (color) {
            int n = snprintf(ob + o, sizeof(ob) - o, "\x1b[0m");
            o += n;
        }

        free(word);
        p = end;
        first = 0;
    }
    ob[o] = '\0';
    em_write(fd, ob, o);
}

/* ================================================================
 *  FISH-STYLE AUTOSUGGESTIONS
 *  Suggests the most recent history entry matching the prefix.
 * ================================================================ */
static int autosuggest_update(void) {
    Shell *sh = shell_get();
    free(sh->suggestion);
    sh->suggestion = NULL;
    if (sh->line_len == 0) return 0;

    for (int i = sh->nhist - 1; i >= 0; i--) {
        if (strncmp(sh->history[i], sh->line_buf, sh->line_len) == 0 &&
            (size_t)sh->line_len < strlen(sh->history[i])) {
            sh->suggestion = sh_strdup(sh->history[i] + sh->line_len);
            return strlen(sh->suggestion);
        }
    }
    return 0;
}

static void accept_suggestion(void) {
    Shell *sh = shell_get();
    if (!sh->suggestion) return;
    for (char *q = sh->suggestion; *q; q++) line_insert(*q);
    free(sh->suggestion);
    sh->suggestion = NULL;
}

/* expand a fish-style abbreviation right before the cursor */
static int abbr_expand_at_cursor(void) {
    Shell *sh = shell_get();
    int start = sh->line_pos;
    while (start > 0 && sh->line_buf[start - 1] != ' ' &&
           sh->line_buf[start - 1] != '\t' && sh->line_buf[start - 1] != '|' &&
           sh->line_buf[start - 1] != '&' && sh->line_buf[start - 1] != ';' &&
           sh->line_buf[start - 1] != '<' && sh->line_buf[start - 1] != '>')
        start--;

    int wlen = sh->line_pos - start;
    if (wlen == 0) return 0;

    char *word = sh_strndup(sh->line_buf + start, wlen);
    char *val = abbr_find(word);
    free(word);
    if (!val) return 0;

    int vlen = strlen(val);
    if (vlen == 0) return 0;

    /* replace the abbreviation with its expansion */
    memmove(sh->line_buf + start + vlen,
            sh->line_buf + sh->line_pos,
            sh->line_len - sh->line_pos + 1);
    memcpy(sh->line_buf + start, val, vlen);
    sh->line_len += vlen - wlen;
    sh->line_pos = start + vlen;
    return 1;
}

/* redraw the line from cursor position */
static void line_refresh(void) {
    Shell *sh = shell_get();
    int pos = 0;
    static char buf[131072];

    /* clear the line and write the prompt */
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\x1b[K%s", sh->prompt);
    em_write(sh->term_fd, buf, pos);
    pos = 0;

    /* write the input (with fish-style syntax highlighting) */
    if (sh->opt_syntaxhighlight)
        write_highlighted(sh->term_fd, sh->line_buf);
    else
        em_write(sh->term_fd, sh->line_buf, sh->line_len);

    /* fish-style autosuggestion (dim, after the cursor when at EOL) */
    int sugg = 0;
    if (sh->opt_autosuggest && sh->line_pos == sh->line_len)
        sugg = autosuggest_update();
    if (sugg > 0) {
        static char sbuf[131072];
        int n = snprintf(sbuf, sizeof(sbuf), "\x1b[2m%s\x1b[22m", sh->suggestion);
        em_write(sh->term_fd, sbuf, n);
    }

    /* reposition the cursor using cursor-left escapes.
     * The distance is measured in terminal columns (display width),
     * not bytes, so wide/combining characters line up correctly. */
    int nleft = utf8_width(sh->line_buf + sh->line_pos,
                           sh->line_len - sh->line_pos);
    if (sugg > 0)
        nleft += utf8_width(sh->suggestion, strlen(sh->suggestion));
    if (nleft > 0) {
        int n = snprintf(buf, sizeof(buf), "\x1b[%dD", nleft);
        em_write(sh->term_fd, buf, n);
    }
}

/* insert a byte at cursor */
static void line_insert(char c) {
    Shell *sh = shell_get();
    if (sh->line_len + 2 >= sh->line_cap) {
        sh->line_cap = sh->line_cap ? sh->line_cap * 2 : 1024;
        sh->line_buf = sh_realloc(sh->line_buf, sh->line_cap);
    }
    memmove(sh->line_buf + sh->line_pos + 1,
            sh->line_buf + sh->line_pos,
            sh->line_len - sh->line_pos + 1);
    sh->line_buf[sh->line_pos] = c;
    sh->line_pos++;
    sh->line_len++;
}

/* insert a whole (possibly multibyte) character at cursor */
static void line_insert_str(const char *s, int n) {
    Shell *sh = shell_get();
    if (sh->line_len + n + 1 >= sh->line_cap) {
        while (sh->line_len + n + 1 >= sh->line_cap)
            sh->line_cap = sh->line_cap ? sh->line_cap * 2 : 1024;
        sh->line_buf = sh_realloc(sh->line_buf, sh->line_cap);
    }
    memmove(sh->line_buf + sh->line_pos + n,
            sh->line_buf + sh->line_pos,
            sh->line_len - sh->line_pos + 1);
    memcpy(sh->line_buf + sh->line_pos, s, n);
    sh->line_pos += n;
    sh->line_len += n;
}

/* delete char at cursor (Delete key) — whole character */
static void line_delete_at_cursor(void) {
    Shell *sh = shell_get();
    if (sh->line_pos < sh->line_len) {
        int next = utf8_next_index(sh->line_buf, sh->line_len, sh->line_pos);
        int n = next - sh->line_pos;
        memmove(sh->line_buf + sh->line_pos,
                sh->line_buf + next,
                sh->line_len - next + 1);
        sh->line_len -= n;
    }
}

/* backspace — delete the whole character before the cursor */
static void line_backspace(void) {
    Shell *sh = shell_get();
    if (sh->line_pos > 0) {
        int start = utf8_prev_index(sh->line_buf, sh->line_pos);
        int n = sh->line_pos - start;
        memmove(sh->line_buf + start,
                sh->line_buf + sh->line_pos,
                sh->line_len - sh->line_pos + 1);
        sh->line_pos = start;
        sh->line_len -= n;
    }
}

/* kill from cursor to end of line */
static void line_kill_to_end(void) {
    Shell *sh = shell_get();
    sh->line_buf[sh->line_pos] = '\0';
    sh->line_len = sh->line_pos;
}

/* kill from start to cursor */
static void line_kill_to_start(void) {
    Shell *sh = shell_get();
    int n = sh->line_len - sh->line_pos;
    memmove(sh->line_buf, sh->line_buf + sh->line_pos, n + 1);
    sh->line_len = n;
    sh->line_pos = 0;
}

/* Ctrl-W — delete the word before the cursor.  Stepping is done a whole
 * character at a time so multibyte (CJK) text is never cut in half. */
static void line_kill_word_back(void) {
    Shell *sh = shell_get();
    int p = sh->line_pos;
    /* skip trailing blanks */
    while (p > 0 && (sh->line_buf[p - 1] == ' ' || sh->line_buf[p - 1] == '\t'))
        p--;
    /* eat characters until the next separator, always on a char boundary */
    while (p > 0) {
        int prev = utf8_prev_index(sh->line_buf, p);
        char c = sh->line_buf[prev];
        if (c == ' ' || c == '\t') break;
        p = prev;
    }
    int n = sh->line_pos - p;
    if (n > 0) {
        memmove(sh->line_buf + p, sh->line_buf + sh->line_pos,
                sh->line_len - sh->line_pos + 1);
        sh->line_len -= n;
        sh->line_pos = p;
    }
}

/* history navigation with fish-style prefix search:
 * if the current line is non-empty, ↑ recalls older commands that
 * start with it; once recalled, ↓ walks forward or restores. */
static void history_up(void) {
    Shell *sh = shell_get();
    if (sh->nhist == 0) return;

    /* start a new search on the first ↑ with a non-empty line */
    if (sh->hist_search == NULL && sh->line_len > 0) {
        sh->hist_search = sh_strdup(sh->line_buf);
        free(sh->history[sh->nhist]);
        sh->history[sh->nhist] = sh_strdup(sh->line_buf);
        sh->hist_pos = sh->nhist;
    }

    int i = sh->hist_pos - 1;
    while (i >= 0) {
        if (sh->hist_search == NULL ||
            strncmp(sh->history[i], sh->hist_search,
                    strlen(sh->hist_search)) == 0) {
            sh->hist_pos = i;
            strncpy(sh->line_buf, sh->history[i], sh->line_cap - 1);
            sh->line_len = strlen(sh->line_buf);
            sh->line_pos = sh->line_len;
            return;
        }
        i--;
    }
    em_write(sh->term_fd, "\a", 1);  /* no more matches */
}

static void history_down(void) {
    Shell *sh = shell_get();
    if (sh->hist_pos >= sh->nhist) return;

    int i = sh->hist_pos + 1;
    if (sh->hist_search) {
        while (i < sh->nhist) {
            if (strncmp(sh->history[i], sh->hist_search,
                        strlen(sh->hist_search)) == 0) {
                sh->hist_pos = i;
                strncpy(sh->line_buf, sh->history[i], sh->line_cap - 1);
                sh->line_len = strlen(sh->line_buf);
                sh->line_pos = sh->line_len;
                return;
            }
            i++;
        }
        /* walked past all matches — restore the typed line */
        sh->hist_pos = sh->nhist;
        strncpy(sh->line_buf, sh->history[sh->nhist], sh->line_cap - 1);
        sh->line_len = strlen(sh->line_buf);
        sh->line_pos = sh->line_len;
        free(sh->hist_search);
        sh->hist_search = NULL;
        return;
    }

    if (i == sh->nhist) {
        /* restore the line typed before browsing */
        sh->hist_pos = i;
        strncpy(sh->line_buf, sh->history[sh->nhist], sh->line_cap - 1);
    } else {
        sh->hist_pos = i;
        strncpy(sh->line_buf, sh->history[i], sh->line_cap - 1);
    }
    sh->line_len = strlen(sh->line_buf);
    sh->line_pos = sh->line_len;
}

static void history_search_reset(void) {
    Shell *sh = shell_get();
    if (sh->hist_search) {
        free(sh->hist_search);
        sh->hist_search = NULL;
    }
    sh->hist_pos = sh->nhist;
}

/* em_write() wrapper whose ignored result does not trip -Wunused-result */
static void em_write(int fd, const char *s, size_t n) {
    ssize_t r = write(fd, s, n);
    (void)r;
}

/* Apply a list of completion candidates: complete a single match, insert
 * the longest common prefix, or list the possibilities.  `skip` is how
 * many leading bytes of every candidate are already present in the word
 * being completed (so only the remainder gets inserted). */
static void line_apply_matches(char **matches, int nmatch, int skip) {
    Shell *sh = shell_get();

    if (nmatch == 0) {
        em_write(sh->term_fd, "\a", 1);
        return;
    }
    if (nmatch == 1) {
        if (skip > (int)strlen(matches[0])) skip = strlen(matches[0]);
        for (const char *p = matches[0] + skip; *p; p++) line_insert(*p);
        return;
    }

    /* multiple matches — longest common prefix (in bytes; candidates that
     * share a prefix are ASCII-safe) */
    char common[4096];
    snprintf(common, sizeof(common), "%s", matches[0]);
    for (int i = 1; i < nmatch; i++) {
        int j = 0;
        while (common[j] && matches[i][j] && common[j] == matches[i][j]) j++;
        common[j] = '\0';
    }
    int cmlen = strlen(common);
    if (cmlen > skip) {
        for (int i = skip; common[i]; i++) line_insert(common[i]);
        return;
    }

    /* nothing more to add — show possibilities in columns */
    em_write(sh->term_fd, "\r\n", 2);
    int maxw = 0;
    for (int i = 0; i < nmatch; i++) {
        int l = strlen(matches[i]);
        if (l > maxw) maxw = l;
    }
    int col = 0;
    for (int i = 0; i < nmatch; i++) {
        int l = strlen(matches[i]);
        em_write(sh->term_fd, matches[i], l);
        for (int k = l; k < maxw + 2; k++) em_write(sh->term_fd, " ", 1);
        if (++col % 8 == 0) em_write(sh->term_fd, "\r\n", 2);
    }
    if (col % 8 != 0) em_write(sh->term_fd, "\r\n", 2);
    line_refresh();
}

/* tab completion — programmable completion first, else filenames */
static void line_complete(void) {
    Shell *sh = shell_get();
    /* find the word being completed */
    int start = sh->line_pos;
    while (start > 0 && sh->line_buf[start - 1] != ' ' &&
           sh->line_buf[start - 1] != '\t' &&
           sh->line_buf[start - 1] != '|' && sh->line_buf[start - 1] != '&' &&
           sh->line_buf[start - 1] != ';' && sh->line_buf[start - 1] != '<' &&
           sh->line_buf[start - 1] != '>' && sh->line_buf[start - 1] != '(')
        start--;

    int wlen = sh->line_pos - start;
    if (wlen == 0) {
        em_write(sh->term_fd, "\a", 1);
        return;
    }
    char *word = sh_strndup(sh->line_buf + start, wlen);

    /* are we completing the command word itself (first token on line)? */
    int cmd_pos = 1;
    for (int i = 0; i < start; i++)
        if (sh->line_buf[i] != ' ' && sh->line_buf[i] != '\t') { cmd_pos = 0; break; }

    /* registered completion rule for the current command takes priority */
    if (!cmd_pos) {
        int cstart = 0;
        while (cstart < start &&
               (sh->line_buf[cstart] == ' ' || sh->line_buf[cstart] == '\t'))
            cstart++;
        int cend = cstart;
        while (cend < start && sh->line_buf[cend] != ' ' &&
               sh->line_buf[cend] != '\t')
            cend++;
        char *cmdname = sh_strndup(sh->line_buf + cstart, cend - cstart);
        CompSpec *cs = compspec_find(cmdname);
        free(cmdname);
        if (cs) {
            char **cands = NULL;
            int nc = 0;
            compgen_generate(cs->action, cs->wordlist, cs->func, word,
                             &cands, &nc);
            if (nc > 0) {
                line_apply_matches(cands, nc, (int)strlen(word));
                compgen_free(cands, nc);
                free(word);
                return;
            }
            compgen_free(cands, nc);
            /* no candidates — fall through to filename completion */
        }
    }

    char *file_part = NULL;
    char dir_path[MAX_PATH] = ".";

    /* split into directory and file prefix */
    char *slash = strrchr(word, '/');
    if (slash) {
        file_part = sh_strdup(slash + 1);
        int dlen = slash - word;
        if (dlen == 0) {
            strcpy(dir_path, "/");
        } else {
            strncpy(dir_path, word, dlen);
            dir_path[dlen] = '\0';
        }
        /* expand ~ in dir part */
        if (dir_path[0] == '~') {
            char *expanded = tilde_expand(dir_path);
            strncpy(dir_path, expanded, MAX_PATH - 1);
            free(expanded);
        }
    } else {
        file_part = sh_strdup(word);
        /* if word starts with ~, expand for the dir listing */
        if (word[0] == '~') {
            char *expanded = tilde_expand(word);
            if (expanded) {
                /* find the last slash to split */
                char *s = strrchr(expanded, '/');
                if (s) {
                    *s = '\0';
                    strncpy(dir_path, expanded, MAX_PATH - 1);
                    file_part = sh_strdup(s + 1);
                } else {
                    strncpy(dir_path, expanded, MAX_PATH - 1);
                    file_part = sh_strdup("");
                }
                free(expanded);
            }
        }
    }

    DIR *dir = opendir(dir_path);
    if (!dir) { free(word); free(file_part); return; }

    char *matches[1024];
    int nmatch = 0;
    int flen = strlen(file_part);
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL && nmatch < 1023) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            /* show dotfiles only if prefix starts with . */
            if (flen == 0 || file_part[0] != '.') continue;
        }
        if (strncmp(ent->d_name, file_part, flen) == 0) {
            matches[nmatch] = sh_strdup(ent->d_name);
            /* if it's a directory, append / */
            struct stat st;
            char fullp[MAX_PATH];
            snprintf(fullp, sizeof(fullp), "%s/%s", dir_path, ent->d_name);
            if (stat(fullp, &st) == 0 && S_ISDIR(st.st_mode)) {
                int sl = strlen(matches[nmatch]);
                matches[nmatch] = sh_realloc(matches[nmatch], sl + 2);
                matches[nmatch][sl] = '/';
                matches[nmatch][sl + 1] = '\0';
            }
            nmatch++;
        }
    }
    closedir(dir);

    line_apply_matches(matches, nmatch, flen);

    for (int i = 0; i < nmatch; i++) free(matches[i]);
    free(word);
    free(file_part);
}

/* ================================================================
 *  CTRL-R — incremental reverse history search (zsh/readline)
 * ================================================================ */
static int reverse_search(void) {
    Shell *sh = shell_get();
    char pattern[MAX_LINE] = "";
    int plen = 0;
    char *saved = sh_strdup(sh->line_buf);
    int cur = sh->nhist;      /* current match index */
    static char buf[131072];

    for (;;) {
        /* find the newest entry containing the pattern */
        cur = sh->nhist;
        if (plen > 0) {
            for (int i = sh->nhist - 1; i >= 0; i--) {
                if (strstr(sh->history[i], pattern)) { cur = i; break; }
            }
        }

        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "\r\x1b[K\x1b[1;36m(reverse-i-search)`%s'\x1b[0m: ",
                        pattern);
        if (cur < sh->nhist)
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", sh->history[cur]);
        else
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", saved);
        em_write(sh->term_fd, buf, pos);

        int key = read_key();

        if (key == '\r' || key == '\n') {       /* accept */
            em_write(sh->term_fd, "\r\n", 2);
            if (cur < sh->nhist) {
                strncpy(sh->line_buf, sh->history[cur], sh->line_cap - 1);
            } else {
                strncpy(sh->line_buf, saved, sh->line_cap - 1);
            }
            sh->line_len = strlen(sh->line_buf);
            sh->line_pos = sh->line_len;
            free(saved);
            return 1;
        }
        if (key == 7 || key == 3 || key == 27) {/* Ctrl-G/Ctrl-C/ESC: cancel */
            strncpy(sh->line_buf, saved, sh->line_cap - 1);
            sh->line_len = strlen(saved);
            sh->line_pos = sh->line_len;
            free(saved);
            line_refresh();
            return 0;
        }
        if (key == 18) {                        /* Ctrl-R: previous match */
            if (plen > 0) {
                for (int i = cur - 1; i >= 0; i--) {
                    if (strstr(sh->history[i], pattern)) { cur = i; break; }
                }
            }
            continue;
        }
        if (key == 127 || key == 8 || key == '\b') { /* backspace */
            if (plen > 0) pattern[--plen] = '\0';
            else {                              /* empty → cancel */
                strncpy(sh->line_buf, saved, sh->line_cap - 1);
                sh->line_len = strlen(saved);
                sh->line_pos = sh->line_len;
                free(saved);
                line_refresh();
                return 0;
            }
            continue;
        }
        if (key >= 32 && key < 127) {           /* printable */
            if (plen < (int)sizeof(pattern) - 1)
                pattern[plen++] = (char)key;
            pattern[plen] = '\0';
            continue;
        }
    }
}

/* main line-editor loop — returns a malloc'd line, or NULL on EOF */
static char *read_line(void) {
    Shell *sh = shell_get();

    /* allocate line buffer if needed */
    if (!sh->line_buf) {
        sh->line_cap = 1024;
        sh->line_buf = sh_malloc(sh->line_cap);
    }
    sh->line_buf[0] = '\0';
    sh->line_len = 0;
    sh->line_pos = 0;
    sh->hist_pos = sh->nhist;
    history_search_reset();

    /* reserve the temporary slot at history[nhist] used while browsing */
    hist_ensure_cap(sh->nhist + 2);
    free(sh->history[sh->nhist]);
    sh->history[sh->nhist] = NULL;

    line_refresh();

    for (;;) {
        int key = read_key();
        if (key < 0) { em_write(sh->term_fd, "\r\n", 2); return NULL; }

        switch (key) {
        case '\r': case '\n':  /* Enter */
            em_write(sh->term_fd, "\r\n", 2);
            sh->line_buf[sh->line_len] = '\0';
            /* fish: expand an abbreviation at the end of the line */
            if (sh->line_len > 0 && abbr_expand_at_cursor()) {
                sh->line_buf[sh->line_len] = '\0';
            }
            history_search_reset();
            /* drop the temporary browsing slot, then record the line */
            free(sh->history[sh->nhist]);
            sh->history[sh->nhist] = NULL;
            history_add(sh->line_buf);
            return sh_strdup(sh->line_buf);

        case 3:   /* Ctrl-C — abort the current input line */
            em_write(sh->term_fd, "^C\r\n", 4);
            sh->line_buf[0] = '\0';
            sh->line_len = 0;
            sh->line_pos = 0;
            history_search_reset();
            sh->line_interrupted = 1;   /* REPL discards any partial input */
            return sh_strdup("");

        case 4:   /* Ctrl-D — EOF on empty line */
            if (sh->line_len == 0) {
                em_write(sh->term_fd, "\r\n", 2);
                return NULL;
            }
            line_delete_at_cursor();
            line_refresh();
            break;

        case 18:  /* Ctrl-R — incremental reverse history search */
            reverse_search();
            line_refresh();
            break;

        case 1:   /* Ctrl-A — beginning of line */
            sh->line_pos = 0;
            line_refresh();
            break;

        case 5:   /* Ctrl-E — end of line */
            sh->line_pos = sh->line_len;
            line_refresh();
            break;

        case 2:   /* Ctrl-B — back one char (left) */
            if (sh->line_pos > 0) {
                sh->line_pos = utf8_prev_index(sh->line_buf, sh->line_pos);
                line_refresh();
            }
            break;

        case 6:   /* Ctrl-F — forward, or accept a suggestion */
            if (sh->line_pos == sh->line_len)
                accept_suggestion();
            else
                sh->line_pos = utf8_next_index(sh->line_buf, sh->line_len, sh->line_pos);
            line_refresh();
            break;

        case 14:  /* Ctrl-N — next history (down) */
            history_down();
            line_refresh();
            break;

        case 16:  /* Ctrl-P — previous history (up) */
            history_up();
            line_refresh();
            break;

        case 11:  /* Ctrl-K — kill to end of line */
            line_kill_to_end();
            line_refresh();
            break;

        case 21:  /* Ctrl-U — kill to start of line */
            line_kill_to_start();
            history_search_reset();
            line_refresh();
            break;

        case 23:  /* Ctrl-W — kill word backward (character-aware) */
            line_kill_word_back();
            history_search_reset();
            line_refresh();
            break;

        case 12:  /* Ctrl-L — clear screen */
            em_write(sh->term_fd, "\x1b[2J\x1b[H", 7);
            line_refresh();
            break;

        case '\t':  /* Tab: accept a suggestion, else complete */
            if (sh->opt_autosuggest && sh->suggestion) {
                accept_suggestion();
            } else {
                line_complete();
            }
            line_refresh();
            break;

        case 127: case '\b':  /* Backspace */
            line_backspace();
            history_search_reset();
            line_refresh();
            break;

        case 256 + 'A':  /* Up arrow */
            history_up();
            line_refresh();
            break;

        case 256 + 'B':  /* Down arrow */
            history_down();
            line_refresh();
            break;

        case 256 + 'C':  /* Right arrow */
            if (sh->line_pos == sh->line_len)
                accept_suggestion();
            else
                sh->line_pos = utf8_next_index(sh->line_buf, sh->line_len, sh->line_pos);
            line_refresh();
            break;

        case 256 + 'D':  /* Left arrow */
            if (sh->line_pos > 0) {
                sh->line_pos = utf8_prev_index(sh->line_buf, sh->line_pos);
                line_refresh();
            }
            break;

        case 256 + 'H':  /* Home */
            sh->line_pos = 0;
            line_refresh();
            break;

        case 256 + 'F':  /* End — also accept a suggestion */
            accept_suggestion();
            sh->line_pos = sh->line_len;
            line_refresh();
            break;

        case 256 + 127:  /* Delete */
            line_delete_at_cursor();
            history_search_reset();
            line_refresh();
            break;

        default:
            if (key >= 32 && key < 256) {
                /* printable ASCII and the first byte of a UTF-8 char.
                 * For a multibyte character, read the continuation bytes
                 * here so the whole code point is inserted atomically. */
                if (key == ' ')
                    abbr_expand_at_cursor();
                history_search_reset();
                unsigned char first = (unsigned char)key;
                char seq[4];
                int n = 1;
                seq[0] = (char)first;
                int need = 0;
                if      ((first & 0xE0) == 0xC0) need = 2;
                else if ((first & 0xF0) == 0xE0) need = 3;
                else if ((first & 0xF8) == 0xF0) need = 4;
                while (n < need) {
                    unsigned char cb;
                    if (read(sh->term_fd, &cb, 1) != 1) break;
                    seq[n++] = (char)cb;
                }
                line_insert_str(seq, n);
                line_refresh();
            }
            /* ignore other control chars */
            break;
        }
    }
}

/* ================================================================
 *  INPUT COMPLETENESS  (multi-line / continuation detection)
 *
 *  Scans a logical command buffer and decides whether it is complete.
 *  It is a deliberately lightweight scanner: it tracks quote state,
 *  backslash continuations, (), {} depth and the block keywords
 *  (if/fi, do/done, case/esac) so the REPL can keep reading with PS2.
 *  Returns 0 = complete, 1 = need more (join with '\n'), 2 = need more
 *  because of an unterminated quote (join without a separator).
 * ================================================================ */
static int all_digits_str(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

static void incompleteness_word(const char *w,
                                int *n_if, int *n_fi, int *n_do, int *n_done,
                                int *n_case, int *n_esac, int *n_head,
                                int *cmd_pos, int *last_op) {
    if (!w || !*w) return;
    if (*cmd_pos) {
        if      (strcmp(w, "if")   == 0) (*n_if)++;
        else if (strcmp(w, "fi")   == 0) (*n_fi)++;
        else if (strcmp(w, "do")   == 0) { (*n_do)++; if (*n_head > 0) (*n_head)--; }
        else if (strcmp(w, "done") == 0) (*n_done)++;
        else if (strcmp(w, "case") == 0) (*n_case)++;
        else if (strcmp(w, "esac") == 0) (*n_esac)++;
        /* `for x in …` / `while cond` / `until cond` / `select x in …`
         * are not complete until their `do` arrives, which may be on a
         * later line (`for x in a b c<newline>do<newline>…`). */
        else if (strcmp(w, "for") == 0 || strcmp(w, "while") == 0 ||
                 strcmp(w, "until") == 0 || strcmp(w, "select") == 0)
            (*n_head)++;
    }
    /* words that open a new command slot on the same line */
    if (strcmp(w, "then") == 0 || strcmp(w, "do") == 0 ||
        strcmp(w, "else") == 0 || strcmp(w, "elif") == 0)
        *cmd_pos = 1;
    else
        *cmd_pos = 0;
    *last_op = 0;
}

int sh_input_incomplete(const char *input) {
    if (!input) return 0;
    int n = (int)strlen(input);

    int in_s = 0, in_d = 0, esc = 0;
    int paren = 0, brace = 0;
    int n_if = 0, n_fi = 0, n_do = 0, n_done = 0, n_case = 0, n_esac = 0;
    int n_head = 0;   /* for/while/until/select awaiting their `do` */
    int cmd_pos = 1;
    int last_op = 0;
    int trailing_esc = 0;
    char word[128];
    int wl = 0;
    char last_word[128];
    last_word[0] = '\0';

    /* Redirection operators already seen for the command being scanned.
     * `&>`/`>&`/`>|` are complete on their own; a bare `>`/`<` opens a slot
     * for the target word, which may turn out to be a heredoc delimiter. */
    /* Pending here-document awaiting its body.  A single `<<` operator may
     * declare more than one here-doc on one line (`cat <<A <<B`), so this is
     * a bounded FIFO of delimiters. */
    char hd_delim[4][128];
    int  hd_strip[4];
    int  hd_n = 0;          /* delimiters queued, not yet satisfied */
    int  hd_head = 0;       /* current delimiter index              */
    int  hd_scan = 0;       /* 1 = we are consuming a here-doc body */

    for (int i = 0; i < n; i++) {
        char c = input[i];

        /* ---- inside a here-document body ---------------------------- */
        if (hd_scan) {
            int ls = i;
            while (i + 1 < n && input[i] != '\n') i++;
            if (input[i] != '\n') break;          /* body still open */
            int le = i;                           /* exclusive end    */
            const char *body = input + ls;
            int blen = le - ls;
            if (hd_strip[hd_head]) {              /* <<- skips tabs   */
                while (blen > 0 && *body == '\t') { body++; blen--; }
            }
            int dlen = (int)strlen(hd_delim[hd_head]);
            if (blen == dlen && memcmp(body, hd_delim[hd_head],
                                       (size_t)dlen) == 0) {
                /* this body is closed — move on to the next queued one */
                hd_head++;
                if (hd_head >= hd_n) {            /* all closed       */
                    hd_n = hd_head = 0;
                    hd_scan = 0;
                }
            }
            /* a non-matching line simply belongs to the here-doc body */
            continue;
        }

        if (esc) {
            esc = 0;
            if (wl < (int)sizeof(word) - 1) word[wl++] = c;
            continue;
        }
        if (in_s) {
            if (c == '\'') in_s = 0;
            continue;
        }
        if (in_d) {
            if (c == '\\') { esc = 1; continue; }
            if (c == '"') in_d = 0;
            continue;
        }
        if (c == '\\') { esc = 1; continue; }
        if (c == '\'') {
            word[wl] = '\0';
            if (hd_n > hd_head && hd_delim[hd_n - 1][0] == '\0' && wl) {
                /* a quoted delimiter disables expansion inside the body */
                snprintf(hd_delim[hd_n - 1], sizeof(hd_delim[0]), "%s", word);
            }
            incompleteness_word(word, &n_if, &n_fi, &n_do,
                &n_done, &n_case, &n_esac, &n_head, &cmd_pos, &last_op);
            if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
            wl = 0;
            in_s = 1;
            continue;
        }
        if (c == '"') {
            word[wl] = '\0';
            if (hd_n > hd_head && hd_delim[hd_n - 1][0] == '\0' && wl) {
                snprintf(hd_delim[hd_n - 1], sizeof(hd_delim[0]), "%s", word);
            }
            incompleteness_word(word, &n_if, &n_fi, &n_do,
                &n_done, &n_case, &n_esac, &n_head, &cmd_pos, &last_op);
            if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
            wl = 0;
            in_d = 1;
            continue;
        }
        if (c == '#' && wl == 0) {          /* comment to end of line */
            while (i + 1 < n && input[i + 1] != '\n') i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            word[wl] = '\0';
            if (hd_n > hd_head && hd_delim[hd_n - 1][0] == '\0' && wl)
                snprintf(hd_delim[hd_n - 1], sizeof(hd_delim[0]), "%s", word);
            incompleteness_word(word, &n_if, &n_fi, &n_do, &n_done,
                &n_case, &n_esac, &n_head, &cmd_pos, &last_op);
            if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
            wl = 0;
            if (c == '\n') {
                /* a `<<` operator whose delimiter never appeared is a
                 * syntax error, not a continuation — drop the queue */
                if (hd_n > hd_head && hd_delim[hd_n - 1][0] == '\0') hd_n = hd_head;
                cmd_pos = 1; last_op = 0;
                if (hd_n > hd_head) hd_scan = 1;   /* body starts next line */
            }
            continue;
        }
        /* operators */
        if (c == '|') {
            word[wl] = '\0'; incompleteness_word(word, &n_if, &n_fi, &n_do,
                &n_done, &n_case, &n_esac, &n_head, &cmd_pos, &last_op);
            if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
            wl = 0;
            if (i + 1 < n && input[i + 1] == '|') i++;   /* || */
            cmd_pos = 1; last_op = 1;
            continue;
        }
        if (c == '&') {
            word[wl] = '\0'; incompleteness_word(word, &n_if, &n_fi, &n_do,
                &n_done, &n_case, &n_esac, &n_head, &cmd_pos, &last_op);
            if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
            wl = 0;
            if (i + 1 < n && input[i + 1] == '&') { i++; cmd_pos = 1; last_op = 1; }
            else { cmd_pos = 1; last_op = 0; }       /* background */
            continue;
        }
        if (c == '(') { paren++; cmd_pos = 1; last_op = 0; continue; }
        if (c == ')') { if (paren > 0) paren--; cmd_pos = 0; last_op = 0; continue; }
        if (c == '{') { brace++; cmd_pos = 1; last_op = 0; continue; }
        if (c == '}') { if (brace > 0) brace--; cmd_pos = 0; last_op = 0; continue; }
        if (c == ';') { cmd_pos = 1; last_op = 0; continue; }

        /* redirection operators — `<<` never matches `<<<` (here-string,
         * which has no body and therefore needs no continuation) */
        if (c == '<' && i + 2 < n && input[i + 1] == '<' &&
            input[i + 2] == '<') {
            i += 2;                       /* consume "<<<" */
            wl = 0;                       /* its word is not a delimiter */
            cmd_pos = 0; last_op = 0;
            continue;
        }
        if (c == '<' && i + 1 < n && input[i + 1] == '<') {
            i++;                          /* consume "<<"  */
            if (i + 1 < n && input[i + 1] == '-') { i++; hd_strip[hd_n] = 1; }
            else hd_strip[hd_n] = 0;
            hd_delim[hd_n][0] = '\0';
            wl = 0;                       /* start collecting the delimiter word */
            hd_n++;                       /* bounded below by the queue size */
            cmd_pos = 0; last_op = 0;
            continue;
        }
        if (c == '>') {
            if (i + 1 < n && input[i + 1] == '>') i++;        /* >>  */
            else if (i + 1 < n && input[i + 1] == '|') i++;    /* >|  */
            wl = 0;
            cmd_pos = 0; last_op = 0;
            continue;
        }
        if (c == '<') {
            wl = 0;                       /* plain input redirection */
            cmd_pos = 0; last_op = 0;
            continue;
        }

        if (wl < (int)sizeof(word) - 1) word[wl++] = c;
    }
    word[wl] = '\0';
    /* a `<<` delimiter that ran to end-of-input still counts */
    if (hd_n > hd_head && hd_delim[hd_n - 1][0] == '\0' && wl)
        snprintf(hd_delim[hd_n - 1], sizeof(hd_delim[0]), "%s", word);
    incompleteness_word(word, &n_if, &n_fi, &n_do, &n_done, &n_case,
                        &n_esac, &n_head, &cmd_pos, &last_op);
    if (wl) snprintf(last_word, sizeof(last_word), "%s", word);
    trailing_esc = esc;

    /* An unterminated here-document keeps the input open.  This is what
     * makes multi-line scripts and `source` work: the reader must not hand
     * a `cat <<EOF` line to the parser before its body has arrived. */
    if (hd_n > 0) return 1;

    if (in_s || in_d) return 2;
    if (trailing_esc || paren > 0 || brace > 0 || last_op) return 1;
    if (n_head > 0 || n_if > n_fi || n_do > n_done || n_case > n_esac) return 1;
    if (strcmp(last_word, "then") == 0 || strcmp(last_word, "do") == 0 ||
        strcmp(last_word, "else") == 0 || strcmp(last_word, "elif") == 0 ||
        strcmp(last_word, "in") == 0)
        return 1;
    return 0;
}

/* ================================================================
 *  HISTORY  (timestamped, bash-compatible file format)
 * ================================================================ */
/* Effective HISTSIZE / HISTFILESIZE (positive integers, else fallback). */
static long hist_limit(const char *name, long dflt) {
    char *v = sh_getenv(name);
    if (!v || !*v) return dflt;
    char *end;
    long n = strtol(v, &end, 10);
    if (*end != '\0' || n <= 0) return dflt;
    return n;
}

/* Make sure history[] (and the parallel time array) can hold `need` slots. */
static void hist_ensure_cap(int need) {
    Shell *sh = shell_get();
    if (sh->history == NULL) {
        sh->hist_cap = MAX_HISTORY;
        sh->history = sh_malloc((sh->hist_cap + 1) * sizeof(char *));
    }
    if (need <= sh->hist_cap && need <= sh->hist_time_cap) return;
    int old = sh->hist_cap;
    while (sh->hist_cap < need) sh->hist_cap = sh->hist_cap * 2;
    sh->history = sh_realloc(sh->history, (sh->hist_cap + 1) * sizeof(char *));
    sh->hist_time = sh_realloc(sh->hist_time, sh->hist_cap * sizeof(long));
    for (int i = old; i < sh->hist_cap; i++) {
        sh->history[i] = NULL;
        sh->hist_time[i] = 0;
    }
    sh->hist_time_cap = sh->hist_cap;
}

/* Drop the `drop` oldest entries (HISTSIZE enforcement). */
static void history_trim_front(int drop) {
    Shell *sh = shell_get();
    if (drop <= 0) return;
    if (drop > sh->nhist) drop = sh->nhist;
    for (int i = 0; i < drop; i++) free(sh->history[i]);
    memmove(sh->history, sh->history + drop,
            (sh->nhist - drop) * sizeof(char *));
    memmove(sh->hist_time, sh->hist_time + drop,
            (sh->nhist - drop) * sizeof(long));
    sh->nhist -= drop;
    sh->hist_written -= drop;
    if (sh->hist_written < 0) sh->hist_written = 0;
}

void history_add(const char *line) {
    Shell *sh = shell_get();
    if (!line || !*line) return;
    if (sh->opt_histignoredups && sh->nhist > 0 &&
        strcmp(sh->history[sh->nhist - 1], line) == 0)
        return;
    hist_ensure_cap(sh->nhist + 2);
    sh->history[sh->nhist] = sh_strdup(line);
    sh->hist_time[sh->nhist] = (long)time(NULL);
    sh->nhist++;
    long hsize = hist_limit("HISTSIZE", MAX_HISTORY);
    if ((long)sh->nhist > hsize)
        history_trim_front(sh->nhist - (int)hsize);
}

void history_clear(void) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nhist; i++) {
        free(sh->history[i]);
        sh->history[i] = NULL;
    }
    /* The line editor keeps a scratch entry one past the end (it holds the
     * half-typed line while the user browses with the arrow keys).  That
     * slot must be released too, otherwise the editor's own free() on the
     * next Enter would hit the same pointer twice. */
    free(sh->history[sh->nhist]);
    sh->history[sh->nhist] = NULL;
    sh->nhist = 0;
    sh->hist_written = 0;
    sh->hist_pos = 0;
}

/* delete entry at 0-based index `idx` */
int history_delete(int idx) {
    Shell *sh = shell_get();
    if (idx < 0 || idx >= sh->nhist) return 1;
    free(sh->history[idx]);
    /* Drop the scratch slot too: after the shift it would otherwise refer
     * to an entry that is still live, and the editor's next Enter would
     * free that live pointer. */
    free(sh->history[sh->nhist]);
    sh->history[sh->nhist] = NULL;
    memmove(&sh->history[idx], &sh->history[idx + 1],
            (sh->nhist - idx - 1) * sizeof(char *));
    memmove(&sh->hist_time[idx], &sh->hist_time[idx + 1],
            (sh->nhist - idx - 1) * sizeof(long));
    sh->nhist--;
    sh->history[sh->nhist] = NULL;
    if (sh->hist_written > idx) sh->hist_written--;
    return 0;
}

void history_write(void) {
    Shell *sh = shell_get();
    if (!sh->hist_file) return;
    FILE *f = fopen(sh->hist_file, "w");
    if (!f) return;
    long fsize = hist_limit("HISTFILESIZE", MAX_HISTORY);
    int start = 0;
    if ((long)sh->nhist > fsize) start = sh->nhist - (int)fsize;
    for (int i = start; i < sh->nhist; i++)
        fprintf(f, "#%ld\n%s\n", sh->hist_time[i], sh->history[i]);
    fclose(f);
    sh->hist_written = sh->nhist;
}

void history_append(void) {
    Shell *sh = shell_get();
    if (!sh->hist_file) return;
    if (sh->hist_written >= sh->nhist) return;
    FILE *f = fopen(sh->hist_file, "a");
    if (!f) return;
    for (int i = sh->hist_written; i < sh->nhist; i++)
        fprintf(f, "#%ld\n%s\n", sh->hist_time[i], sh->history[i]);
    fclose(f);
    sh->hist_written = sh->nhist;
}

/* read the history file and append its entries to the in-memory list */
static void history_load_from_file(void) {
    Shell *sh = shell_get();
    if (!sh->hist_file) return;
    FILE *f = fopen(sh->hist_file, "r");
    if (!f) return;
    char buf[MAX_LINE];
    long pending = 0;
    int have_pending = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;
        if (buf[0] == '#' && all_digits_str(buf + 1)) {   /* #<epoch> */
            pending = strtol(buf + 1, NULL, 10);
            have_pending = 1;
            continue;
        }
        hist_ensure_cap(sh->nhist + 2);
        sh->history[sh->nhist] = sh_strdup(buf);
        sh->hist_time[sh->nhist] = have_pending ? pending : 0;
        sh->nhist++;
        have_pending = 0;
        pending = 0;
    }
    fclose(f);
    long hsize = hist_limit("HISTSIZE", MAX_HISTORY);
    if ((long)sh->nhist > hsize)
        history_trim_front(sh->nhist - (int)hsize);
}

void history_read(void) {
    history_load_from_file();
    Shell *sh = shell_get();
    /* entries just read are considered already stored, so a later -a
     * only appends commands entered after the read */
    sh->hist_written = sh->nhist;
}

void history_save(void) {
    /* Persist the whole in-memory history (bash-like overwrite on exit). */
    history_write();
}

void history_load(void) {
    history_load_from_file();
    Shell *sh = shell_get();
    /* everything already on disk counts as written */
    sh->hist_written = sh->nhist;
}

/* ---- shell init / destroy ------------------------------------ */
void shell_init(void) {
    Shell *sh = shell_get();
    memset(sh, 0, sizeof(*sh));

    sh->term_fd = STDIN_FILENO;
    sh->running = 1;
    sh->job_interactive = isatty(STDIN_FILENO);
    sh->linenum = 0;
    sh->opt_noclobber = 0;
    sh->opt_allexport = 0;
    sh->opt_noglob = 0;
    sh->opt_autocd = 1;            /* fish/zsh: implicit cd */
    sh->opt_globstar = 1;          /* zsh: '**' recursive glob */
    sh->opt_autosuggest = 1;       /* fish: history suggestions */
    sh->opt_syntaxhighlight = 1;   /* fish: colored input */
    sh->opt_histignoredups = 1;    /* skip consecutive duplicates */

    if (getcwd(sh->cwd, sizeof(sh->cwd)) == NULL)
        sh->cwd[0] = '\0';
    snprintf(sh->prompt, sizeof(sh->prompt), "\x1b[1;32mbesh\x1b[0m:\x1b[1;34m\\W\x1b[0m$ ");

    /* init variable storage */
    sh->vars_cap = 128;
    sh->vars = sh_malloc(sh->vars_cap * sizeof(Var));
    sh->nvars = 0;

    /* import environ */
    extern char **environ;
    for (char **ep = environ; *ep; ep++) {
        char *eq = strchr(*ep, '=');
        if (eq) {
            char *name = sh_strndup(*ep, eq - *ep);
            char *val  = sh_strdup(eq + 1);
            sh_setenv(name, val, 1);
            free(name);
            free(val);
        }
    }

    /* set up default variables */
    sh_setenv("SHELL", "besh", 1);
    {
        char pidbuf[32];
        snprintf(pidbuf, sizeof(pidbuf), "%d", getpid());
        sh_setenv("$", pidbuf, 0);
    }
    sh_setenv("?", "0", 0);
    sh_setenv("IFS", " \t\n", 1);

    /* history file */
    const char *home = sh_getenv("HOME");
    if (home) {
        char hf[MAX_PATH];
        snprintf(hf, sizeof(hf), "%s/.besh_history", home);
        sh->hist_file = sh_strdup(hf);
    }
    sh->hist_cap = MAX_HISTORY;
    sh->history = sh_malloc((sh->hist_cap + 1) * sizeof(char *));
    sh->hist_time = sh_malloc(sh->hist_cap * sizeof(long));
    sh->hist_time_cap = sh->hist_cap;
    for (int i = 0; i <= sh->hist_cap; i++) sh->history[i] = NULL;
    for (int i = 0; i < sh->hist_cap; i++) sh->hist_time[i] = 0;
    sh->nhist = 0;
    sh->hist_written = 0;

    /* job list */
    sh->jobs = NULL;
    sh->njob = 0;

    /* alias storage */
    sh->aliases_cap = 64;
    sh->aliases = sh_malloc(sh->aliases_cap * sizeof(Alias));
    sh->naliases = 0;

    /* abbreviation storage */
    sh->abbrs_cap = 64;
    sh->abbrs = sh_malloc(sh->abbrs_cap * sizeof(Alias));
    sh->nabbrs = 0;

    /* directory stack */
    sh->dirs_cap = 16;
    sh->dirs = sh_malloc(sh->dirs_cap * sizeof(char *));
    sh->ndirs = 0;
    dirs_push(sh->cwd);   /* entry 0 is always the current directory */

    /* function storage */
    sh->funcs_cap = 64;
    sh->funcs = sh_malloc(sh->funcs_cap * sizeof(Function));
    sh->nfuncs = 0;

    if (sh->job_interactive) {
        term_raw();
        signals_setup();
        history_load();
    }
}

void shell_destroy(void) {
    Shell *sh = shell_get();
    if (sh->job_interactive) {
        history_save();
        term_restore();
    }
    /* free history */
    for (int i = 0; i < sh->nhist; i++) free(sh->history[i]);
    free(sh->history);
    free(sh->hist_time);
    free(sh->hist_file);
    /* free positional parameters (set by `-c`/script invocation) */
    for (int i = 0; i < sh->npositional; i++) free(sh->positional[i]);
    free(sh->positional);
    /* free vars */
    for (int i = 0; i < sh->nvars; i++) {
        free(sh->vars[i].name);
        free(sh->vars[i].value);
    }
    free(sh->vars);
    /* free any function-call scope frames */
    for (int i = 0; i < sh->nscopes; i++) {
        ScopeFrame *fr = &sh->scopes[i];
        for (int j = 0; j < fr->nsaved; j++) {
            free(fr->saved[j].name);
            free(fr->saved[j].value);
        }
        free(fr->saved);
        for (int j = 0; j < fr->nlocals; j++) free(fr->locals[j]);
        free(fr->locals);
    }
    free(sh->scopes);
    /* free aliases */
    for (int i = 0; i < sh->naliases; i++) {
        free(sh->aliases[i].name);
        free(sh->aliases[i].value);
    }
    free(sh->aliases);
    /* free abbreviations */
    for (int i = 0; i < sh->nabbrs; i++) {
        free(sh->abbrs[i].name);
        free(sh->abbrs[i].value);
    }
    free(sh->abbrs);
    /* free directory stack */
    for (int i = 0; i < sh->ndirs; i++)
        free(sh->dirs[i]);
    free(sh->dirs);
    free(sh->hist_search);
    free(sh->suggestion);
    /* free functions */
    for (int i = 0; i < sh->nfuncs; i++) {
        free(sh->funcs[i].name);
        ast_free(sh->funcs[i].body);
    }
    free(sh->funcs);
    /* free completion rules */
    compspec_free_all();
    /* free line buf */
    free(sh->line_buf);
    /* job list handled async */
}

/* ---- prompt builder (bash PS1 + zsh prompt escapes) ---------- */
/* Read `<gitdir>/HEAD` and return the branch name, "(detached)" for a
 * detached HEAD, or NULL if HEAD cannot be read.  `gitdir` may be either
 * a real directory or the path of a `.git` file's target. */
static char *git_head_branch(const char *gitdir) {
    char head[MAX_PATH + 64];
    snprintf(head, sizeof(head), "%s/HEAD", gitdir);
    FILE *f = fopen(head, "r");
    if (!f) return NULL;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return NULL; }
    fclose(f);
    char *ref = strstr(line, "refs/heads/");
    if (ref) {
        ref += strlen("refs/heads/");
        char *nl = strchr(ref, '\n');
        if (nl) *nl = '\0';
        if (*ref) return sh_strdup(ref);
    }
    return sh_strdup("(detached)");
}

/* Resolve the git directory that applies to `dir`.  Handles the normal
 * case (.git is a directory) and the submodule/worktree case where .git
 * is a text file containing `gitdir: <path>` (which may be relative).
 * Returns a malloc'd path or NULL. */
static char *git_dir_for(const char *dir) {
    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%s/.git", dir);

    struct stat st;
    if (stat(path, &st) != 0) return NULL;

    if (S_ISDIR(st.st_mode)) return sh_strdup(path);

    /* .git is a file: read `gitdir: <path>` */
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char line[MAX_PATH + 64];
    char *res = NULL;
    if (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "gitdir:", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '/') {
                res = sh_strdup(p);                     /* absolute */
            } else {
                /* relative to the directory that holds the .git file */
                char abs[MAX_PATH + 64];
                snprintf(abs, sizeof(abs), "%s/%s", dir, p);
                res = sh_strdup(abs);
            }
        }
    }
    fclose(f);
    return res;
}

/* returns a malloc'd git branch name, or NULL when not in a repo */
static char *get_git_branch(void) {
    char dir[MAX_PATH];
    if (!getcwd(dir, sizeof(dir))) return NULL;

    for (;;) {
        char *gitdir = git_dir_for(dir);
        if (gitdir) {
            char *branch = git_head_branch(gitdir);
            free(gitdir);
            return branch;   /* NULL only if HEAD is unreadable */
        }
        if (strcmp(dir, "/") == 0) break;
        char *s = strrchr(dir, '/');
        if (!s) break;
        if (s == dir) dir[1] = '\0';   /* "/x" -> "/" */
        else *s = '\0';
    }
    return NULL;
}

/* zsh-style prompt escapes:
 *   %n user, %m short host, %M full host, %~ cwd w/ ~, %d full cwd,
 *   %# '#' if root else '%', %? last exit status (when nonzero),
 *   %g git branch (when in a repo), %h history line number
 * bash-style: \u \h \W \w \$ \t \# \n \e */
static void prompt_render(const char *fmt, char *out, size_t outsz) {
    Shell *sh = shell_get();
    char host[HOST_NAME_MAX + 1] = "";
    char fqdn[HOST_NAME_MAX + 1] = "";
    gethostname(fqdn, sizeof(fqdn));
    snprintf(host, sizeof(host), "%s", fqdn);
    char *dot = strchr(host, '.');
    if (dot) *dot = '\0';

    const char *user = sh_getenv("USER");
    if (!user) user = "besh";
    int root = (geteuid() == 0);

    const char *cwd = sh->cwd;
    char tilde_cwd[MAX_PATH] = "";
    const char *home = sh_getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0 &&
        (cwd[strlen(home)] == '/' || cwd[strlen(home)] == '\0')) {
        snprintf(tilde_cwd, sizeof(tilde_cwd), "~%s", cwd + strlen(home));
    } else {
        snprintf(tilde_cwd, sizeof(tilde_cwd), "%s", cwd);
    }

    const char *base = strrchr(cwd, '/');
    if (base && base[1]) base++; else base = cwd;

    char branch[256] = "";
    char *gb = get_git_branch();
    if (gb) {
        snprintf(branch, sizeof(branch), "\x1b[1;33m ( %s )\x1b[0m", gb);
        free(gb);
    }

    int o = 0;
    for (const char *p = fmt; *p && o < (int)outsz - 64; p++) {
        if (*p == '%') {
            char n = *(p + 1);
            switch (n) {
            case 'n': o += snprintf(out+o, outsz-o, "%s", user); p++; continue;
            case 'm': o += snprintf(out+o, outsz-o, "%s", host); p++; continue;
            case 'M': o += snprintf(out+o, outsz-o, "%s", fqdn); p++; continue;
            case '~': o += snprintf(out+o, outsz-o, "%s", tilde_cwd); p++; continue;
            case 'd': o += snprintf(out+o, outsz-o, "%s", cwd); p++; continue;
            case '#':
                o += snprintf(out+o, outsz-o, "%c", root ? '#' : '%');
                p++; continue;
            case '?':
                if (sh->exit_status != 0)
                    o += snprintf(out+o, outsz-o, "\x1b[1;31m %d\x1b[0m",
                                  sh->exit_status);
                p++; continue;
            case 'g':
                o += snprintf(out+o, outsz-o, "%s", branch); p++; continue;
            case 'h':
                o += snprintf(out+o, outsz-o, "%d", sh->nhist + 1);
                p++; continue;
            case '%': o += snprintf(out+o, outsz-o, "%%"); p++; continue;
            case '\0': out[o++] = '%'; p--; continue;
            default:
                out[o++] = '%'; continue;
            }
        }
        if (*p == '\\') {
            char n = *(p + 1);
            switch (n) {
            case 'u': o += snprintf(out+o, outsz-o, "%s", user); p++; continue;
            case 'h': o += snprintf(out+o, outsz-o, "%s", host); p++; continue;
            case 'W': o += snprintf(out+o, outsz-o, "%s", base); p++; continue;
            case 'w': o += snprintf(out+o, outsz-o, "%s", tilde_cwd); p++; continue;
            case 't':
                { char t[16]; time_t now = time(NULL);
                  strftime(t, sizeof(t), "%H:%M:%S", localtime(&now));
                  o += snprintf(out+o, outsz-o, "%s", t); }
                p++; continue;
            case '$': o += snprintf(out+o, outsz-o, "%c", root ? '#' : '$');
                p++; continue;
            case '#': o += snprintf(out+o, outsz-o, "%d", sh->nhist + 1);
                p++; continue;
            case 'n': o += snprintf(out+o, outsz-o, "\n"); p++; continue;
            case 'e': o += snprintf(out+o, outsz-o, "\x1b"); p++; continue;
            case '\\': out[o++] = '\\'; p++; continue;
            case '\0': out[o++] = '\\'; p--; continue;
            default: out[o++] = '\\'; continue;
            }
        }
        out[o++] = *p;
    }
    out[o] = '\0';
}

static void build_prompt(void) {
    Shell *sh = shell_get();
    const char *fmt = sh_getenv("PROMPT");
    if (!fmt || !*fmt) fmt = sh_getenv("PS1");
    if (!fmt || !*fmt)
        fmt = "\x1b[1;32mbesh\x1b[0m \x1b[1;36m%~\x1b[0m%g\x1b[0m%?%# ";
    prompt_render(fmt, sh->prompt, sizeof(sh->prompt));
}

/* ---- execute a whole stream, one complete logical command at a time -- */
int sh_run_stream(FILE *f) {
    Shell *sh = shell_get();
    char *acc = NULL;
    size_t acclen = 0;
    int prev_kind = 0;
    int ret = sh->exit_status;
    char buf[MAX_LINE];

    while (fgets(buf, sizeof(buf), f)) {
        sh->linenum++;
        size_t l = strlen(buf);
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
            buf[--l] = '\0';

        const char *sep = "";
        if (acclen > 0) sep = (prev_kind == 2) ? "" : "\n";
        size_t seplen = strlen(sep);
        acc = sh_realloc(acc, acclen + seplen + l + 1);
        memcpy(acc + acclen, sep, seplen);
        memcpy(acc + acclen + seplen, buf, l);
        acclen += seplen + l;
        acc[acclen] = '\0';

        prev_kind = sh_input_incomplete(acc);
        if (prev_kind == 0) {
            sh->in_condition = 0;      /* never leak condition state */
            ret = execute_string(acc);
            free(acc);
            acc = NULL;
            acclen = 0;
            /* `set -e`: a failing statement ends the whole script */
            if (sh->exit_request) {
                sh->exit_request = 0;
                break;
            }
            /* a `return` in a sourced file stops the remaining statements */
            if (sh->return_request) {
                sh->return_request = 0;
                break;
            }
        }
        if (!sh->running) break;
    }
    if (acc) {
        if (acclen > 0) ret = execute_string(acc);
        free(acc);
    }
    return ret;
}

/* Set $0 and the positional parameters from argv[first..].  `zero_default`
 * is used for $0 when no name argument is present (bash: "besh"). */
static void set_positional(char **argv, int argc, int first,
                           const char *zero_default) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->npositional; i++) free(sh->positional[i]);
    free(sh->positional);
    sh->positional = NULL;
    sh->npositional = 0;

    const char *zero = zero_default;
    int pos_start = first;
    if (argc > first) {          /* first trailing arg becomes $0 */
        zero = argv[first];
        pos_start = first + 1;
    }
    sh_setenv("0", zero, 0);

    int n = argc - pos_start;
    if (n > 0) {
        sh->positional = sh_malloc(n * sizeof(char *));
        for (int i = 0; i < n; i++)
            sh->positional[i] = sh_strdup(argv[pos_start + i]);
        sh->npositional = n;
    }
}

/* ---- run the startup file (~/.beshrc), if present ------------- */
static void source_rc(void) {
    const char *home = sh_getenv("HOME");
    if (!home) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.beshrc", home);

    FILE *f = fopen(path, "r");
    if (!f) return;
    sh_run_stream(f);
    fclose(f);
}

/* ================================================================
 *  MAIN — REPL loop
 * ================================================================ */
int main(int argc, char **argv) {
    shell_init();
    Shell *sh = shell_get();

    /* -c command [name [args...]] : run the command string.
     * $0 is the first trailing word (default "besh"), $1.. follow it —
     * exactly like bash.  This must be tested before the script-file
     * branch so "besh -c cmd a b" is never mistaken for a script run. */
    if (argc >= 2 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fprintf(stderr, "besh: -c: option requires an argument\n");
            shell_destroy();
            return 2;
        }
        sh->job_interactive = 0;
        set_positional(argv, argc, 3, "besh");
        int ret = execute_string(argv[2]);
        /* `set -e` must leave a non-zero status behind: unwind() only
         * skips the *remaining* statements, the last status is still the
         * failure that triggered the exit (bash exits with that code). */
        if (sh->exit_request) {
            sh->exit_request = 0;
            if (ret == 0) ret = sh->exit_status;
            if (ret == 0) ret = 1;
        }
        shell_destroy();
        return ret;
    }

    /* script file [args...] : $0 is the script path, $1.. the args */
    if (argc >= 2 && argv[1][0] != '-') {
        sh->job_interactive = 0;
        set_positional(argv, argc, 1, argv[1]);
        FILE *f = fopen(argv[1], "r");
        if (!f) { perror(argv[1]); shell_destroy(); return 1; }
        int ret = sh_run_stream(f);
        fclose(f);
        shell_destroy();
        return ret;
    }

    /* interactive mode */
    if (!sh->job_interactive) {
        /* stdin is not a tty — read all input at once, then run it through
         * the streaming executor.  Going through sh_run_stream() (rather
         * than a single execute_string call) is what makes `set -e` able
         * to abandon the remaining statements and keeps multi-line
         * constructs and here-documents working for piped input. */
        char *buf = NULL;
        size_t cap = 0, len = 0;
        char chunk[8192];
        ssize_t n;
        while ((n = read(STDIN_FILENO, chunk, sizeof(chunk))) > 0) {
            while (len + (size_t)n + 1 > cap) {
                cap = cap ? cap * 2 : 8192;
                buf = sh_realloc(buf, cap);
            }
            memcpy(buf + len, chunk, n);
            len += n;
        }
        int ret = 0;
        if (buf && len > 0) {
            buf[len] = '\0';
            FILE *mem = fmemopen(buf, len, "r");
            if (mem) {
                ret = sh_run_stream(mem);
                fclose(mem);
            } else {
                ret = execute_string(buf);
            }
        }
        free(buf);
        shell_destroy();
        return ret;
    }

    /* ---- interactive REPL ---- */
    printf("\x1b[1;36m"
           "╔══════════════════════════════════════════════╗\n"
           "║   besh — the bash-compatible shell in C      ║\n"
           "║   type 'help' for builtins, Ctrl-D to exit   ║\n"
           "╚══════════════════════════════════════════════╝\n"
           "\x1b[0m");
    fflush(stdout);   /* emit the reset now, not with the first command */

    source_rc();   /* load ~/.beshrc (aliases, abbrs, setopt, PROMPT) */

    /* Logical input buffer for multi-line editing.  `prev_kind` is the
     * joining mode reported by sh_input_incomplete() after the last line. */
    char  *multi = NULL;
    size_t mlen = 0;
    int    prev_kind = 0;

    while (sh->running) {
        /* check for completed background jobs */
        job_notify();

        /* re-enter raw mode for the line editor (a running full-screen
         * program may have changed the terminal settings) */
        term_raw();

        /* PS2 for continuation lines, PS1 otherwise */
        if (multi) {
            const char *ps2 = sh_getenv("PS2");
            snprintf(sh->prompt, sizeof(sh->prompt), "%s",
                     (ps2 && *ps2) ? ps2 : "> ");
        } else {
            build_prompt();
        }

        char *line = read_line();
        if (!line) {
            /* EOF */
            printf("exit\n");
            break;
        }

        if (sh->line_interrupted) {
            /* Ctrl-C — discard the whole (possibly multi-line) input */
            sh->line_interrupted = 0;
            free(line);
            free(multi);
            multi = NULL;
            mlen = 0;
            prev_kind = 0;
            continue;
        }

        /* append this physical line to the logical buffer */
        {
            const char *sep = "";
            if (multi) sep = (prev_kind == 2) ? "" : "\n";
            size_t seplen = strlen(sep);
            size_t l = strlen(line);
            multi = sh_realloc(multi, mlen + seplen + l + 2);
            memcpy(multi + mlen, sep, seplen);
            memcpy(multi + mlen + seplen, line, l);
            mlen += seplen + l;
            multi[mlen] = '\0';
        }
        free(line);

        prev_kind = sh_input_incomplete(multi);
        if (prev_kind != 0) {
            /* command is unfinished — read the next line with PS2 */
            continue;
        }

        /* complete logical line ready to run */
        char *dup = sh_strdup(multi);
        char *trimmed = sh_trim(dup);   /* internal pointer — free(dup) */
        free(multi);
        multi = NULL;
        mlen = 0;
        prev_kind = 0;

        if (*trimmed == '\0' || *trimmed == '#') {
            free(dup);
            continue;
        }

        sh->linenum++;
        {
            /* Run the command with the tty in cooked mode and let it share
             * our foreground process group, so interactive programs (cat,
             * read, vim) get a sane line discipline.  The executor only
             * splits off a separate process group / grabs the terminal
             * when job_interactive is set, which is wrong for simple tty
             * commands, so clear it for the duration of the command. */
            int saved_jc = sh->job_interactive;
            sh->job_interactive = 0;
            term_restore();
            sh->in_condition = 0;     /* never leak condition state */
            sh->exit_status = execute_string(trimmed);
            sh->job_interactive = saved_jc;
            sh->return_request = 0;   /* drop a stray top-level return */
            /* `set -e` at the top level leaves the shell, like bash -e */
            if (sh->exit_request) {
                sh->exit_request = 0;
                sh->running = 0;
            }
        }
        free(dup);
    }

    free(multi);   /* in case we stopped mid-continuation */
    shell_destroy();
    return sh->exit_status;
}
