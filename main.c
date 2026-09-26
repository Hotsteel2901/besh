/* ================================================================
 *  main.c — entry point, signal handling, line editor, REPL loop
 * ================================================================ */

#include "shell.h"

/* ---- global shell singleton ---------------------------------- */
static Shell _shell;
Shell *shell_get(void) { return &_shell; }

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
    sh->vars[sh->nvars].name     = sh_strdup(name);
    sh->vars[sh->nvars].value    = sh_strdup(value);
    sh->vars[sh->nvars].exported = export_flag;
    sh->vars[sh->nvars].readonly = 0;
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

/* ---- PATH resolution ----------------------------------------- */
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
    write(STDOUT_FILENO, "\n", 1);
    /* if no foreground job, just redraw prompt */
    sh->line_pos = 0;
    sh->line_len = 0;
    sh->line_buf[0] = '\0';
    if (sh->running) write(STDOUT_FILENO, sh->prompt, strlen(sh->prompt));
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
static void term_raw(void) {
    Shell *sh = shell_get();
    tcgetattr(sh->term_fd, &sh->orig_termios);
    sh->shell_termios = sh->orig_termios;
    sh->shell_termios.c_lflag &= ~(ICANON | ECHO | ISIG);
    sh->shell_termios.c_cc[VMIN]  = 1;
    sh->shell_termios.c_cc[VTIME] = 0;
    tcsetattr(sh->term_fd, TCSANOW, &sh->shell_termios);
}

static void term_restore(void) {
    Shell *sh = shell_get();
    tcsetattr(sh->term_fd, TCSANOW, &sh->orig_termios);
}

/* read one key.  returns the byte for simple keys;
 * for escape sequences we return a synthetic code 256+ */
static int read_key(void) {
    Shell *sh = shell_get();
    unsigned char c;
    if (read(sh->term_fd, &c, 1) != 1) return -1;
    if (c != 27) return c;    /* 27 = ESC */

    /* escape sequence — try to read more with timeout */
    unsigned char seq[8];
    ssize_t n = read(sh->term_fd, seq, sizeof(seq));
    if (n <= 0) return 27;    /* bare ESC */

    if (seq[0] == '[') {
        /* CSI sequences */
        if (n >= 2) {
            switch (seq[1]) {
            case 'A': return 256 + 'A';  /* Up    */
            case 'B': return 256 + 'B';  /* Down  */
            case 'C': return 256 + 'C';  /* Right */
            case 'D': return 256 + 'D';  /* Left  */
            case 'H': return 256 + 'H';  /* Home  */
            case 'F': return 256 + 'F';  /* End   */
            case '3':                    /* Delete */
                if (n >= 3 && seq[2] == '~') return 256 + 127;
                break;
            case '5':                    /* PgUp   */
                if (n >= 3 && seq[2] == '~') return 256 + 'U';
                break;
            case '6':                    /* PgDn   */
                if (n >= 3 && seq[2] == '~') return 256 + 'V';
                break;
            case '1':                    /* Home (alternate) */
                if (n >= 3 && seq[2] == '~') return 256 + 'H';
                break;
            case '4':                    /* End (alternate)  */
                if (n >= 3 && seq[2] == '~') return 256 + 'F';
                break;
            case '7':                    /* Home (urxvt) */
                if (n >= 3 && seq[2] == '~') return 256 + 'H';
                break;
            case '8':                    /* End (urxvt)  */
                if (n >= 3 && seq[2] == '~') return 256 + 'F';
                break;
            }
        }
        return 27;   /* unrecognised escape, return bare ESC */
    }
    if (seq[0] == 'O') {
        /* SS3 sequences */
        if (n >= 2) {
            switch (seq[1]) {
            case 'H': return 256 + 'H';  /* Home */
            case 'F': return 256 + 'F';  /* End  */
            }
        }
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
            if (o + 8 >= (int)sizeof(ob)) { write(fd, ob, o); o = 0; }
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

        if (o + wlen + 32 >= (int)sizeof(ob)) { write(fd, ob, o); o = 0; }
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
    write(fd, ob, o);
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
    write(sh->term_fd, buf, pos);
    pos = 0;

    /* write the input (with fish-style syntax highlighting) */
    if (sh->opt_syntaxhighlight)
        write_highlighted(sh->term_fd, sh->line_buf);
    else
        write(sh->term_fd, sh->line_buf, sh->line_len);

    /* fish-style autosuggestion (dim, after the cursor when at EOL) */
    int sugg = 0;
    if (sh->opt_autosuggest && sh->line_pos == sh->line_len)
        sugg = autosuggest_update();
    if (sugg > 0) {
        static char sbuf[131072];
        int n = snprintf(sbuf, sizeof(sbuf), "\x1b[2m%s\x1b[22m", sh->suggestion);
        write(sh->term_fd, sbuf, n);
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
        write(sh->term_fd, buf, n);
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
    write(sh->term_fd, "\a", 1);  /* no more matches */
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

/* tab completion — basic filename completion */
static void line_complete(void) {
    Shell *sh = shell_get();
    /* find the word being completed */
    int start = sh->line_pos;
    while (start > 0 && sh->line_buf[start - 1] != ' ' &&
           sh->line_buf[start - 1] != '|' && sh->line_buf[start - 1] != '&' &&
           sh->line_buf[start - 1] != ';' && sh->line_buf[start - 1] != '<' &&
           sh->line_buf[start - 1] != '>' && sh->line_buf[start - 1] != '(')
        start--;

    int wlen = sh->line_pos - start;
    if (wlen == 0) return;

    char *word = sh_strndup(sh->line_buf + start, wlen);
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

    if (nmatch == 0) {
        /* no matches — beep */
        write(sh->term_fd, "\a", 1);
        goto done;
    }
    if (nmatch == 1) {
        /* single match — complete */
        char *suffix = matches[0] + flen;
        for (char *p = suffix; *p; p++) line_insert(*p);
        goto done;
    }

    /* multiple matches — find common prefix */
    char common[4096];
    strncpy(common, matches[0], sizeof(common) - 1);
    for (int i = 1; i < nmatch; i++) {
        int j = 0;
        while (common[j] && matches[i][j] && common[j] == matches[i][j]) j++;
        common[j] = '\0';
    }
    int cmlen = strlen(common);
    if (cmlen > flen) {
        /* insert common prefix */
        for (int i = flen; common[i]; i++) line_insert(common[i]);
    } else {
        /* show possibilities */
        write(sh->term_fd, "\r\n", 2);
        for (int i = 0; i < nmatch; i++) {
            write(sh->term_fd, matches[i], strlen(matches[i]));
            write(sh->term_fd, "  ", 2);
            if ((i + 1) % 8 == 0) write(sh->term_fd, "\r\n", 2);
        }
        if (nmatch % 8 != 0) write(sh->term_fd, "\r\n", 2);
        line_refresh();
    }

done:
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
        write(sh->term_fd, buf, pos);

        int key = read_key();

        if (key == '\r' || key == '\n') {       /* accept */
            write(sh->term_fd, "\r\n", 2);
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

    /* allocate history temp slot */
    if (sh->nhist >= sh->hist_cap) {
        sh->hist_cap = sh->hist_cap ? sh->hist_cap * 2 : MAX_HISTORY;
        sh->history = sh_realloc(sh->history, (sh->hist_cap + 1) * sizeof(char *));
    }
    /* temporary slot at history[nhist] for saving the line being browsed */
    sh->history[sh->nhist] = NULL;

    line_refresh();

    for (;;) {
        int key = read_key();
        if (key < 0) { write(sh->term_fd, "\r\n", 2); return NULL; }

        switch (key) {
        case '\r': case '\n':  /* Enter */
            write(sh->term_fd, "\r\n", 2);
            sh->line_buf[sh->line_len] = '\0';
            /* fish: expand an abbreviation at the end of the line */
            if (sh->line_len > 0 && abbr_expand_at_cursor()) {
                sh->line_buf[sh->line_len] = '\0';
            }
            history_search_reset();
            /* add to history */
            if (sh->line_len > 0) {
                if (sh->nhist >= sh->hist_cap) {
                    sh->hist_cap = sh->hist_cap ? sh->hist_cap * 2 : MAX_HISTORY;
                    sh->history = sh_realloc(sh->history,
                        (sh->hist_cap + 1) * sizeof(char *));
                }
                /* don't duplicate consecutive identical lines */
                if (sh->nhist == 0 ||
                    strcmp(sh->history[sh->nhist - 1], sh->line_buf) != 0) {
                    sh->history[sh->nhist] = sh_strdup(sh->line_buf);
                    sh->nhist++;
                }
            }
            return sh_strdup(sh->line_buf);

        case 4:   /* Ctrl-D — EOF on empty line */
            if (sh->line_len == 0) {
                write(sh->term_fd, "\r\n", 2);
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

        case 23:  /* Ctrl-W — kill word backward */
            {
                int p = sh->line_pos;
                while (p > 0 && sh->line_buf[p - 1] == ' ') p--;
                while (p > 0 && sh->line_buf[p - 1] != ' ') p--;
                int n = sh->line_pos - p;
                if (n > 0) {
                    memmove(sh->line_buf + p, sh->line_buf + sh->line_pos,
                            sh->line_len - sh->line_pos + 1);
                    sh->line_len -= n;
                    sh->line_pos = p;
                }
            }
            history_search_reset();
            line_refresh();
            break;

        case 12:  /* Ctrl-L — clear screen */
            write(sh->term_fd, "\x1b[2J\x1b[H", 7);
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

/* ---- history file -------------------------------------------- */
static void history_load_from_file(void) {
    Shell *sh = shell_get();
    if (!sh->hist_file) return;
    FILE *f = fopen(sh->hist_file, "r");
    if (!f) return;
    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;
        if (sh->nhist >= sh->hist_cap) {
            sh->hist_cap = sh->hist_cap ? sh->hist_cap * 2 : MAX_HISTORY;
            sh->history = sh_realloc(sh->history,
                (sh->hist_cap + 1) * sizeof(char *));
        }
        sh->history[sh->nhist++] = sh_strdup(buf);
    }
    fclose(f);
}

void history_save(void) {
    Shell *sh = shell_get();
    if (!sh->hist_file) return;
    FILE *f = fopen(sh->hist_file, "w");
    if (!f) return;
    int start = sh->nhist > MAX_HISTORY ? sh->nhist - MAX_HISTORY : 0;
    for (int i = start; i < sh->nhist; i++)
        fprintf(f, "%s\n", sh->history[i]);
    fclose(f);
}

void history_load(void) {
    history_load_from_file();
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

    getcwd(sh->cwd, sizeof(sh->cwd));
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
    sh->nhist = 0;

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
    free(sh->hist_file);
    /* free vars */
    for (int i = 0; i < sh->nvars; i++) {
        free(sh->vars[i].name);
        free(sh->vars[i].value);
    }
    free(sh->vars);
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
    /* free line buf */
    free(sh->line_buf);
    /* job list handled async */
}

/* ---- prompt builder (bash PS1 + zsh prompt escapes) ---------- */
/* returns a malloc'd git branch name, or NULL when not in a repo */
static char *get_git_branch(void) {
    char dir[MAX_PATH];
    if (!getcwd(dir, sizeof(dir))) return NULL;

    for (;;) {
        struct stat st;
        char gitdir[MAX_PATH + 32];
        snprintf(gitdir, sizeof(gitdir), "%s/.git", dir);
        if (stat(gitdir, &st) == 0) {
            char head[MAX_PATH + 32];
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
        /* go up one level */
        char *s = strrchr(dir, '/');
        if (!s) break;
        if (s == dir) {
            dir[1] = '\0';   /* at the filesystem root */
            if (strlen(dir) > 0 && s == dir && dir[1] == '\0') break;
        } else {
            *s = '\0';
        }
        if (dir[0] == '\0' || (dir[1] == '\0' && dir[0] == '/')) break;
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

/* ---- run the startup file (~/.beshrc), if present ------------- */
static void source_rc(void) {
    const char *home = sh_getenv("HOME");
    if (!home) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.beshrc", home);

    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0 && buf[0] != '#')
            execute_string(buf);
    }
    fclose(f);
}

/* ================================================================
 *  MAIN — REPL loop
 * ================================================================ */
int main(int argc, char **argv) {
    shell_init();
    Shell *sh = shell_get();

    /* handle -c option */
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        /* non-interactive: execute command string */
        sh->job_interactive = 0;
        int ret = execute_string(argv[2]);
        shell_destroy();
        return ret;
    }

    /* handle script file */
    if (argc >= 2 && argv[1][0] != '-') {
        sh->job_interactive = 0;
        FILE *f = fopen(argv[1], "r");
        if (!f) { perror(argv[1]); shell_destroy(); return 1; }
        char buf[MAX_LINE];
        int ret = 0;
        while (fgets(buf, sizeof(buf), f)) {
            sh->linenum++;
            /* handle multi-line continuation */
            char *line = sh_strdup(buf);
            char *s = line;
            /* strip trailing newline */
            size_t l = strlen(s);
            while (l > 0 && (s[l-1] == '\n' || s[l-1] == '\r')) s[--l] = '\0';
            /* handle backslash continuation */
            while (l > 0 && s[l-1] == '\\') {
                s[l-1] = '\0';
                if (fgets(buf, sizeof(buf), f)) {
                    sh->linenum++;
                    size_t bl = strlen(buf);
                    while (bl > 0 && (buf[bl-1] == '\n' || buf[bl-1] == '\r'))
                        buf[--bl] = '\0';
                    s = sh_realloc(s, l + bl + 2);
                    strcat(s, "\n");
                    strcat(s, buf);
                    l = strlen(s);
                } else break;
            }
            ret = execute_string(s);
            free(s);
        }
        fclose(f);
        shell_destroy();
        return ret;
    }

    /* interactive mode */
    if (!sh->job_interactive) {
        /* stdin is not a tty — read all input at once */
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
            ret = execute_string(buf);
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

    source_rc();   /* load ~/.beshrc (aliases, abbrs, setopt, PROMPT) */

    while (sh->running) {
        /* check for completed background jobs */
        job_notify();

        /* full-screen apps (vim/less) restore the tty to canonical mode
         * when they exit — re-enter raw mode for the line editor */
        term_raw();

        build_prompt();
        char *line = read_line();

        if (!line) {
            /* EOF */
            printf("exit\n");
            break;
        }

        char *trimmed = sh_trim(sh_strdup(line));
        free(line);

        if (*trimmed == '\0' || *trimmed == '#') {
            free(trimmed);
            continue;
        }

        sh->linenum++;
        sh->exit_status = execute_string(trimmed);
        free(trimmed);
    }

    shell_destroy();
    return sh->exit_status;
}
