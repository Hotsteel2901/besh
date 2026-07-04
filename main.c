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
            perror("besh: setpgid");
            exit(1);
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

/* redraw the line from cursor position */
static void line_refresh(void) {
    Shell *sh = shell_get();
    int pos = 0;
    static char buf[65536];
    char saved;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\x1b[K");
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", sh->prompt);
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", sh->line_buf);
    write(sh->term_fd, buf, pos);

    pos = 0;
    saved = sh->line_buf[sh->line_pos];
    sh->line_buf[sh->line_pos] = '\0';
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r");
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", sh->prompt);
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", sh->line_buf);
    write(sh->term_fd, buf, pos);
    sh->line_buf[sh->line_pos] = saved;
}

/* insert char at cursor */
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

/* delete char at cursor (Delete key) */
static void line_delete_at_cursor(void) {
    Shell *sh = shell_get();
    if (sh->line_pos < sh->line_len) {
        memmove(sh->line_buf + sh->line_pos,
                sh->line_buf + sh->line_pos + 1,
                sh->line_len - sh->line_pos);
        sh->line_len--;
    }
}

/* backspace */
static void line_backspace(void) {
    Shell *sh = shell_get();
    if (sh->line_pos > 0) {
        memmove(sh->line_buf + sh->line_pos - 1,
                sh->line_buf + sh->line_pos,
                sh->line_len - sh->line_pos + 1);
        sh->line_pos--;
        sh->line_len--;
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

/* history navigation */
static void history_up(void) {
    Shell *sh = shell_get();
    if (sh->nhist == 0 || sh->hist_pos <= 0) return;
    if (sh->hist_pos == sh->nhist) {
        /* save current line before browsing */
        free(sh->history[sh->nhist]);  /* temporary slot */
        sh->history[sh->nhist] = sh_strdup(sh->line_buf);
    }
    sh->hist_pos--;
    strncpy(sh->line_buf, sh->history[sh->hist_pos], sh->line_cap - 1);
    sh->line_len = strlen(sh->line_buf);
    sh->line_pos = sh->line_len;
}

static void history_down(void) {
    Shell *sh = shell_get();
    if (sh->hist_pos >= sh->nhist) return;
    sh->hist_pos++;
    if (sh->hist_pos == sh->nhist) {
        /* restore saved line */
        strncpy(sh->line_buf, sh->history[sh->nhist], sh->line_cap - 1);
    } else {
        strncpy(sh->line_buf, sh->history[sh->hist_pos], sh->line_cap - 1);
    }
    sh->line_len = strlen(sh->line_buf);
    sh->line_pos = sh->line_len;
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

    /* allocate history temp slot */
    if (sh->nhist >= sh->hist_cap) {
        sh->hist_cap = sh->hist_cap ? sh->hist_cap * 2 : MAX_HISTORY;
        sh->history = sh_realloc(sh->history, (sh->hist_cap + 1) * sizeof(char *));
    }
    /* temporary slot at history[nhist] for saving current line */
    if (sh->history[sh->nhist] == NULL || sh->hist_pos != sh->nhist) {
        /* ensure we have buffer space */
    }

    line_refresh();

    for (;;) {
        int key = read_key();
        if (key < 0) { write(sh->term_fd, "\r\n", 2); return NULL; }

        switch (key) {
        case '\r': case '\n':  /* Enter */
            write(sh->term_fd, "\r\n", 2);
            sh->line_buf[sh->line_len] = '\0';
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

        case 1:   /* Ctrl-A — beginning of line */
            sh->line_pos = 0;
            line_refresh();
            break;

        case 5:   /* Ctrl-E — end of line */
            sh->line_pos = sh->line_len;
            line_refresh();
            break;

        case 2:   /* Ctrl-B — back one char (left) */
            if (sh->line_pos > 0) { sh->line_pos--; line_refresh(); }
            break;

        case 6:   /* Ctrl-F — forward one char (right) */
            if (sh->line_pos < sh->line_len) { sh->line_pos++; line_refresh(); }
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
            line_refresh();
            break;

        case 12:  /* Ctrl-L — clear screen */
            write(sh->term_fd, "\x1b[2J\x1b[H", 7);
            line_refresh();
            break;

        case '\t':  /* Tab completion */
            line_complete();
            line_refresh();
            break;

        case 127: case '\b':  /* Backspace */
            line_backspace();
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
            if (sh->line_pos < sh->line_len) { sh->line_pos++; line_refresh(); }
            break;

        case 256 + 'D':  /* Left arrow */
            if (sh->line_pos > 0) { sh->line_pos--; line_refresh(); }
            break;

        case 256 + 'H':  /* Home */
            sh->line_pos = 0;
            line_refresh();
            break;

        case 256 + 'F':  /* End */
            sh->line_pos = sh->line_len;
            line_refresh();
            break;

        case 256 + 127:  /* Delete */
            line_delete_at_cursor();
            line_refresh();
            break;

        default:
            if (key >= 32 && key < 127) {
                /* printable ASCII */
                line_insert((char)key);
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

/* ---- prompt builder ------------------------------------------ */
static void build_prompt(void) {
    Shell *sh = shell_get();
    char host[HOST_NAME_MAX + 1] = "localhost";
    char *user = sh_getenv("USER");
    if (!user) user = "unknown";
    gethostname(host, sizeof(host));
    char *h = strchr(host, '.');
    if (h) *h = '\0';

    /* get basename of cwd */
    const char *cwd = sh->cwd;
    const char *base = strrchr(cwd, '/');
    if (base && base[1]) base++; else base = cwd;

    snprintf(sh->prompt, sizeof(sh->prompt),
             "\x1b[1;32m%s@%s\x1b[0m:\x1b[1;34m%s\x1b[0m$ ",
             user, host, base);
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

    while (sh->running) {
        /* check for completed background jobs */
        job_notify();

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
