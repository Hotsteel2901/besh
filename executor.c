/* ================================================================
 *  executor.c — AST execution engine for besh
 *
 *  Walks the AST and executes commands:
 *   - Simple commands: fork + execvp (or run builtin)
 *   - Pipelines: create pipe, fork children, connect fds
 *   - Lists: execute left, then right
 *   - AND/OR: conditional execution based on exit status
 *   - Background: fork and don't wait
 *   - Subshells: fork and execute in child
 *   - Redirections: dup2 fds before exec
 *   - Job control: track background processes
 * ================================================================ */

#include "shell.h"
#include <ctype.h>

/* ---- forward declarations ------------------------------------ */
static int execute_node_internal(ASTNode *node, int *pipe_in, int *pipe_out,
                                 int async);
static int wait_for_pid(pid_t pid);
static int setup_redirections(Redir *redirs);

/* A redirection target may itself be a process substitution
 * (`cmd > >(sink)` / `cmd < <(source)`).  Such a filename is kept by the
 * parser with a leading \002 / \003 marker; fork the inner list here and
 * return the /dev/fd/N pathname the redirection should actually open.
 * For ordinary filenames the input pointer is returned unchanged.
 *
 * *psub_fd receives the raw descriptor when a process substitution was
 * created, or -1 otherwise.  The caller is responsible for closing it:
 * bash keeps the pipe end only long enough for the redirection to be
 * installed, and leaking it would keep the writer's pipe from ever
 * seeing EOF (so the next `while ... < <(...)` would read nothing). */
static const char *redir_resolve_name(const char *name, int *psub_fd) {
    if (psub_fd) *psub_fd = -1;
    if (name && (name[0] == '\002' || name[0] == '\003')) {
        int pid = -1;
        char *path = expand_process_sub(name, &pid);
        if (path) {
            Shell *sh = shell_get();
            if (pid > 0 && sh->npsub < MAX_PSUB)
                sh->psub_pids[sh->npsub++] = pid;
            if (psub_fd) {
                /* the pathname is /dev/fd/N — recover N */
                const char *slash = strrchr(path, '/');
                if (slash && slash[1]) *psub_fd = atoi(slash + 1);
            }
            return path;                 /* freed by the caller after use */
        }
        return "/dev/null";
    }
    return name;
}

/* ---- job management ------------------------------------------ */
void job_add(pid_t pgid, pid_t *pids, int npids, const char *cmd) {
    Shell *sh = shell_get();
    Job *j = sh_malloc(sizeof(Job));
    j->id = ++sh->njob;
    j->pgid = pgid;
    j->pids = sh_malloc(npids * sizeof(pid_t));
    memcpy(j->pids, pids, npids * sizeof(pid_t));
    j->npids = npids;
    j->command = sh_strdup(cmd ? cmd : "");
    j->status = JOB_RUNNING;
    j->next = sh->jobs;
    sh->jobs = j;
}

void job_update(pid_t pid, int status) {
    Shell *sh = shell_get();
    for (Job *j = sh->jobs; j; j = j->next) {
        for (int i = 0; i < j->npids; i++) {
            if (j->pids[i] == pid) {
                if (WIFSTOPPED(status))
                    j->status = JOB_STOPPED;
                else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    /* mark this pid as done */
                    j->pids[i] = 0;
                    /* check if all pids in job are done */
                    int all_done = 1;
                    for (int k = 0; k < j->npids; k++) {
                        if (j->pids[k] != 0) { all_done = 0; break; }
                    }
                    if (all_done) j->status = JOB_DONE;
                }
                return;
            }
        }
    }
}

void job_remove(pid_t pgid) {
    Shell *sh = shell_get();
    Job *prev = NULL;
    for (Job *j = sh->jobs; j; prev = j, j = j->next) {
        if (j->pgid == pgid) {
            if (prev) prev->next = j->next;
            else sh->jobs = j->next;
            free(j->pids);
            free(j->command);
            free(j);
            return;
        }
    }
}

Job *job_find_by_pgid(pid_t pgid) {
    Shell *sh = shell_get();
    for (Job *j = sh->jobs; j; j = j->next)
        if (j->pgid == pgid) return j;
    return NULL;
}

Job *job_find_by_id(int id) {
    Shell *sh = shell_get();
    for (Job *j = sh->jobs; j; j = j->next)
        if (j->id == id) return j;
    return NULL;
}

void job_print(Job *j) {
    if (!j) return;
    const char *status_str = "Running";
    if (j->status == JOB_STOPPED) status_str = "Stopped";
    else if (j->status == JOB_DONE) status_str = "Done";
    fprintf(stderr, "[%d] %s\t\t%s\n", j->id, status_str, j->command);
}

void job_cleanup(void) {
    Shell *sh = shell_get();
    Job *prev = NULL;
    Job *j = sh->jobs;
    while (j) {
        Job *next = j->next;
        if (j->status == JOB_DONE) {
            if (prev) prev->next = next;
            else sh->jobs = next;
            free(j->pids);
            free(j->command);
            free(j);
        } else {
            prev = j;
        }
        j = next;
    }
}

void job_notify(void) {
    Shell *sh = shell_get();
    Job *prev = NULL;
    Job *j = sh->jobs;
    int changed = 0;

    while (j) {
        Job *next = j->next;
        if (j->status == JOB_DONE) {
            fprintf(stderr, "[%d]  Done\t\t%s\n", j->id, j->command);
            if (prev) prev->next = next;
            else sh->jobs = next;
            free(j->pids);
            free(j->command);
            free(j);
            changed = 1;
        } else {
            prev = j;
        }
        j = next;
    }
    if (changed && sh->job_interactive)
        job_cleanup();
}

/* ---- wait for a process -------------------------------------- */
static int wait_for_pid(pid_t pid) {
    int status;
    pid_t w;
    sigchld_block();
    do {
        w = waitpid(pid, &status, WUNTRACED);
    } while (w < 0 && errno == EINTR);
    sigchld_unblock();

    if (w < 0) {
        if (errno == ECHILD) {
            /* already reaped by the SIGCHLD handler — treat as success */
            return 0;
        }
        perror("besh: waitpid");
        return 1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "besh: process %d terminated by signal %d\n",
                pid, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    if (WIFSTOPPED(status)) {
        /* job stopped — keep tracking */
        return 146;  /* suspend marker */
    }
    return status;
}

/* ---- set up redirections ------------------------------------- */
static int setup_redirections(Redir *redirs) {
    Shell *sh = shell_get();

    for (Redir *r = redirs; r; r = r->next) {
        int fd = -1;
        int psub_fd = -1;
        int target_fd = (r->src_fd >= 0) ? r->src_fd : STDOUT_FILENO;
        const char *rname = redir_resolve_name(r->filename, &psub_fd);

        switch (r->type) {
        case REDIR_IN:
            fd = open(rname, O_RDONLY);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_OUT: {
            int flags = O_WRONLY | O_CREAT | O_TRUNC;
            if (sh->opt_noclobber) flags |= O_EXCL;
            fd = open(rname, flags, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;
        }

        case REDIR_APPEND:
            fd = open(rname, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_CLOBBER:
            fd = open(rname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_ERR:
            fd = open(rname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;

        case REDIR_ERRAPPEND:
            fd = open(rname, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;

        case REDIR_BOTH:
            fd = open(rname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(rname); return -1; }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;

        case REDIR_HEREDOC:
        case REDIR_HEREDOC_DASH: {
            int hpipe[2];
            if (pipe(hpipe) < 0) { perror("pipe"); return -1; }
            if (r->heredoc) {
                char *content;
                if (r->quoted)
                    content = sh_strdup(r->heredoc);
                else
                    content = expand_string(r->heredoc);
                write(hpipe[1], content, strlen(content));
                free(content);
            }
            close(hpipe[1]);
            dup2(hpipe[0], target_fd);
            close(hpipe[0]);
            break;
        }

        case REDIR_DUPIN:
            /* <&n — duplicate fd n onto the target */
            dup2(r->fd, target_fd);
            break;

        case REDIR_DUPOUT:
            /* >&n — duplicate fd n onto the target */
            dup2(r->fd, target_fd);
            break;

        case REDIR_CLOSE:
            /* >&- / <&- — close the target fd */
            close(target_fd);
            break;

        default:
            break;
        }

        /* The redirection is installed on its target fd now, so the
         * temporary descriptor (and the pipe end behind a process
         * substitution) can be released.  Keeping it open would stop the
         * writer from ever seeing EOF. */
        if (psub_fd >= 0) close(psub_fd);
    }

    return 0;
}

/* ---- helper: detect NAME=value assignment word ---------------- */
/* Recognise "name=", "name+=", "name[idx]=", "name[idx]+=".  Returns the
 * length of the assignment prefix (up to and including '=' / '+='), or 0
 * if `word` is not an assignment; *op is set to 1 for '+='. */
static int assign_prefix_len(const char *word, int *op) {
    if (op) *op = 0;
    if (!word) return 0;
    unsigned char c0 = (unsigned char)word[0];
    if (!(c0 == '_' || isalpha(c0))) return 0;
    const char *p = word + 1;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    if (*p == '[') {
        const char *cl = strchr(p, ']');
        if (!cl) return 0;
        p = cl + 1;
    }
    if (*p == '+') {
        if (p[1] == '=') { if (op) *op = 1; return (int)(p - word) + 2; }
        return 0;
    }
    if (*p == '=') return (int)(p - word) + 1;
    return 0;
}

static int is_assignment(const char *word) {
    return assign_prefix_len(word, NULL) > 0;
}

/* name[..]+?=( ... ) — a whole array literal kept as one lexer token */
static int is_array_literal(const char *word) {
    int op;
    int n = assign_prefix_len(word, &op);
    if (n <= 0) return 0;
    if (word[n] != '(') return 0;
    size_t l = strlen(word);
    return l > (size_t)n + 1 && word[l - 1] == ')';
}

/* evaluate `a=(one two three)` / `a+=(four)` */
static void exec_array_literal(const char *word, int export_flag) {
    (void)export_flag;
    int op;
    int n = assign_prefix_len(word, &op);       /* index of '(' */
    char *namepart = sh_strndup(word, n - 1 - (op ? 1 : 0));
    char *inner = sh_strndup(word + n + 1, strlen(word) - n - 2);

    /* lex the element list (preserving per-element quoting) */
    Lexer *lx = lexer_new(inner);
    char *toks[MAX_ARGS];
    int qt[MAX_ARGS];
    int nt = 0;
    int t;
    while ((t = lexer_next(lx)) == TOK_WORD && nt < MAX_ARGS - 1) {
        toks[nt] = sh_strdup(lx->token_text);
        qt[nt] = lx->token_quoted;
        nt++;
    }
    lexer_free(lx);

    int nw = nt;
    char **ex = expand_words_q(toks, qt, &nw);
    for (int i = 0; i < nt; i++) free(toks[i]);

    char *base = NULL;
    long idx = 0;
    int star = 0;
    int kind = var_parse_subscript(namepart, &base, &idx, &star);
    if (!base) base = sh_strdup(namepart);

    if (kind == 1) {
        for (int i = 0; i < nw; i++) var_array_set(base, idx + i, ex[i]);
    } else {
        long start = 0;
        if (op) {
            int ni = 0;
            long *idxs = var_array_indices(base, &ni);
            if (ni > 0) start = idxs[ni - 1] + 1;
            free(idxs);
        } else {
            var_array_clear(base);
        }
        for (int i = 0; i < nw; i++) var_array_set(base, start + i, ex[i]);
    }
    var_set_array_attr(base, 1);

    for (int i = 0; i < nw; i++) free(ex[i]);
    free(ex);
    free(base);
    free(inner);
    free(namepart);
}

/* ---- execute a single simple command ------------------------- */
int execute_command(ASTNode *node) {
    if (!node || node->type != NODE_COMMAND) return 1;
    if (node->argc == 0) return 0;

    Shell *sh = shell_get();

    /* a lone array literal is a complete assignment */
    if (node->argc == 1 && is_array_literal(node->argv[0])) {
        exec_array_literal(node->argv[0], sh->opt_allexport);
        sh->exit_status = 0;
        return 0;
    }

    /* ---- expand variables, braces and globs in all arguments ---- */
    int expanded_argc = node->argc;
    char **words = sh_malloc((node->argc + 1) * sizeof(char *));
    int *qwords = sh_malloc((node->argc + 1) * sizeof(int));
    for (int i = 0; i < node->argc; i++) {
        words[i] = sh_strdup(node->argv[i]);
        qwords[i] = node->argv_quoted ? node->argv_quoted[i] : 0;
    }
    words[node->argc] = NULL;

    char **expanded_argv = expand_words_q(words, qwords, &expanded_argc);
    for (int i = 0; i < node->argc; i++) free(words[i]);
    free(words);
    free(qwords);

    /* ---- detect leading variable assignments (NAME=value ...) ---- */
    int n_assign = 0;
    while (n_assign < expanded_argc && is_assignment(expanded_argv[n_assign]))
        n_assign++;

    int cmd_start = n_assign;  /* index of first non-assignment word */

    if (n_assign == expanded_argc) {
        /* all words are assignments — set shell variables */
        int ret = 0;
        for (int i = 0; i < n_assign; i++) {
            int op;
            int n = assign_prefix_len(expanded_argv[i], &op);
            char *nm = sh_strndup(expanded_argv[i], n - 1 - (op ? 1 : 0));
            sh_assign(nm, expanded_argv[i] + n, sh->opt_allexport, op);
            free(nm);
        }
        sh->exit_status = ret;
        for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
        free(expanded_argv);
        return ret;
    }

    /* prefix assignments: temporarily set env for the command */
    if (n_assign > 0) {
        for (int i = 0; i < n_assign; i++) {
            char *eq = strchr(expanded_argv[i], '=');
            *eq = '\0';
            setenv(expanded_argv[i], eq + 1, 1);
            *eq = '=';
        }
    }

    /* ---- effective argv starts after assignments ---- */
    char **cmd_argv = expanded_argv + cmd_start;
    int cmd_argc = expanded_argc - cmd_start;

    /* first, check if it's a shell function */
    for (int i = 0; i < sh->nfuncs; i++) {
        if (strcmp(cmd_argv[0], sh->funcs[i].name) == 0) {
            /* save old positional parameters */
            char **old_pos = sh->positional;
            int old_npos = sh->npositional;

            /* set new positional parameters from function args */
            int fnargs = cmd_argc - 1;
            if (fnargs > 0) {
                sh->positional = sh_malloc(fnargs * sizeof(char *));
                for (int j = 0; j < fnargs; j++)
                    sh->positional[j] = sh_strdup(cmd_argv[j + 1]);
            } else {
                sh->positional = NULL;
            }
            sh->npositional = fnargs;

            /* push a function-call scope for `local` variables */
            scope_push();

            int ret = execute_node_internal(sh->funcs[i].body,
                                            NULL, NULL, 0);

            /* discard locals and restore the caller's variables */
            scope_pop();
            sh->return_request = 0;

            /* free function positional parameters
             * (shift may have consumed some of them) */
            for (int j = 0; j < sh->npositional; j++)
                free(sh->positional[j]);
            free(sh->positional);

            /* restore old positional parameters */
            sh->positional = old_pos;
            sh->npositional = old_npos;

            sh->exit_status = ret;
            /* restore env from prefix assignments */
            for (int i2 = 0; i2 < n_assign; i2++) {
                char *eq = strchr(expanded_argv[i2], '=');
                *eq = '\0';
                char *old = sh_getenv(expanded_argv[i2]);
                if (old) setenv(expanded_argv[i2], old, 1);
                else unsetenv(expanded_argv[i2]);
                *eq = '=';
            }
            for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
            free(expanded_argv);
            return ret;
        }
    }

    /* check builtins */
    builtin_fn bf = builtin_lookup(cmd_argv[0]);
    /* a `[[ ... ]]` condition word (marker \001) is handled by builtin_test
     * regardless of its literal text */
    if (!bf && cmd_argv[0] && cmd_argv[0][0] == '\001')
        bf = builtin_lookup("test");
    if (bf) {
        /* handle redirections for builtins */
        int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;

        /* set up redirections */
        for (Redir *r = node->redirs; r; r = r->next) {
            int bpsub_fd = -1;
            const char *rname = redir_resolve_name(r->filename, &bpsub_fd);
            switch (r->type) {
            case REDIR_IN:
                if (saved_stdin < 0) saved_stdin = dup(STDIN_FILENO);
                { int fd = open(rname, O_RDONLY);
                  if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); } }
                break;
            case REDIR_OUT:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); } }
                break;
            case REDIR_APPEND:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_APPEND, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); } }
                break;
            case REDIR_ERR:
                if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); } }
                break;
            case REDIR_BOTH:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); } }
                break;
            case REDIR_ERRAPPEND:
                if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_APPEND, 0644);
                  if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); } }
                break;
            case REDIR_CLOBBER:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                { int fd = open(rname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); } }
                break;
            case REDIR_DUPIN:
            case REDIR_DUPOUT:
                if (r->src_fd == STDIN_FILENO) {
                    if (saved_stdin < 0) saved_stdin = dup(STDIN_FILENO);
                    dup2(r->fd, STDIN_FILENO);
                } else if (r->src_fd == STDERR_FILENO) {
                    if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                    dup2(r->fd, STDERR_FILENO);
                } else {
                    if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                    dup2(r->fd, STDOUT_FILENO);
                }
                break;
            case REDIR_CLOSE:
                if (r->src_fd == STDIN_FILENO) {
                    if (saved_stdin < 0) saved_stdin = dup(STDIN_FILENO);
                    close(STDIN_FILENO);
                } else if (r->src_fd == STDERR_FILENO) {
                    if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                    close(STDERR_FILENO);
                } else {
                    if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                    close(STDOUT_FILENO);
                }
                break;
            default: break;
            }
            if (bpsub_fd >= 0) close(bpsub_fd);
        }

        int ret = bf(cmd_argc, cmd_argv);

        /* restore fds */
        if (saved_stdin >= 0)  { dup2(saved_stdin, STDIN_FILENO); close(saved_stdin); }
        if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stderr >= 0) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }

        /* restore env from prefix assignments */
        if (n_assign > 0) {
            for (int i = 0; i < n_assign; i++) {
                char *eq = strchr(expanded_argv[i], '=');
                *eq = '\0';
                char *old = sh_getenv(expanded_argv[i]);
                if (old) setenv(expanded_argv[i], old, 1);
                else unsetenv(expanded_argv[i]);
                *eq = '=';
            }
        }

        sh->exit_status = ret;
        for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
        free(expanded_argv);
        return ret;
    }

    /* autocd (fish/zsh): typing a directory name changes into it */
    if (sh->opt_autocd && cmd_argv[0][0] != '/' && strchr(cmd_argv[0], '/') == NULL &&
        !resolve_path(cmd_argv[0])) {
        struct stat st;
        if (stat(cmd_argv[0], &st) == 0 && S_ISDIR(st.st_mode)) {
            int r = cd_to(cmd_argv[0]);
            /* restore env from prefix assignments */
            for (int i2 = 0; i2 < n_assign; i2++) {
                char *eq = strchr(expanded_argv[i2], '=');
                *eq = '\0';
                char *old = sh_getenv(expanded_argv[i2]);
                if (old) setenv(expanded_argv[i2], old, 1);
                else unsetenv(expanded_argv[i2]);
                *eq = '=';
            }
            for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
            free(expanded_argv);
            sh->exit_status = r;
            return r;
        }
    }

    /* external command — fork and exec */
    pid_t pid = fork();
    if (pid < 0) {
        perror("besh: fork");
        for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
        free(expanded_argv);
        return 1;
    }
    if (pid == 0) {
        /* child */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        /* set up redirections */
        if (node->redirs) {
            if (setup_redirections(node->redirs) < 0) _exit(1);
        }

        /* resolve command path */
        char *path = resolve_path(cmd_argv[0]);
        if (!path)
            path = cmd_argv[0];

        execvp(path, cmd_argv);

        /* exec failed */
        fprintf(stderr, "besh: %s: %s\n", cmd_argv[0], strerror(errno));
        _exit(127);
    }

    /* parent */
    /* set process group for job control */
    if (sh->job_interactive) {
        setpgid(pid, pid);
    }

    int ret = wait_for_pid(pid);

    /* restore env from prefix assignments */
    if (n_assign > 0) {
        for (int i = 0; i < n_assign; i++) {
            char *eq = strchr(expanded_argv[i], '=');
            *eq = '\0';
            char *old = sh_getenv(expanded_argv[i]);
            if (old) setenv(expanded_argv[i], old, 1);
            else unsetenv(expanded_argv[i]);
            *eq = '=';
        }
    }

    sh->exit_status = ret;
    for (int j = 0; j < expanded_argc; j++) free(expanded_argv[j]);
    free(expanded_argv);
    return ret;
}

/* ---- execute a pipeline -------------------------------------- */
int execute_pipeline(ASTNode *pipeline) {
    if (!pipeline) return 0;
    if (pipeline->type != NODE_PIPELINE) {
        return execute_node_internal(pipeline, NULL, NULL, 0);
    }

    Shell *sh = shell_get();

    /* collect all commands in the pipeline */
    ASTNode *cmds[256];
    int ncmds = 0;
    ASTNode *cur = pipeline;
    while (cur && cur->type == NODE_PIPELINE && ncmds < 255) {
        cmds[ncmds++] = cur->right;
        cur = cur->left;
    }
    if (cur) cmds[ncmds++] = cur;
    /* reverse to original order */
    for (int i = 0; i < ncmds / 2; i++) {
        ASTNode *tmp = cmds[i];
        cmds[i] = cmds[ncmds - 1 - i];
        cmds[ncmds - 1 - i] = tmp;
    }

    /* create pipes */
    int pipes[256][2];
    pid_t pids[256] = {0};

    for (int i = 0; i < ncmds - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return 1; }
    }

    for (int i = 0; i < ncmds; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            /* child */
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            /* stdin from previous pipe */
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            /* stdout to next pipe */
            if (i < ncmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* close all pipe fds */
            for (int j = 0; j < ncmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* set up redirections */
            if (cmds[i]->type == NODE_COMMAND && cmds[i]->redirs) {
                setup_redirections(cmds[i]->redirs);
            }

            /* execute the command */
            if (cmds[i]->type == NODE_COMMAND && cmds[i]->argc > 0) {
                /* expand variables / globs in the pipeline child */
                int wc = cmds[i]->argc;
                char **w = sh_malloc((cmds[i]->argc + 1) * sizeof(char *));
                int *wq = sh_malloc((cmds[i]->argc + 1) * sizeof(int));
                for (int a = 0; a < cmds[i]->argc; a++) {
                    w[a] = sh_strdup(cmds[i]->argv[a]);
                    wq[a] = cmds[i]->argv_quoted ? cmds[i]->argv_quoted[a] : 0;
                }
                w[cmds[i]->argc] = NULL;
                char **ex = expand_words_q(w, wq, &wc);
                for (int a = 0; a < cmds[i]->argc; a++) free(w[a]);
                free(w);
                free(wq);

                builtin_fn bf = builtin_lookup(ex[0]);
                if (bf) {
                    int r = bf(wc, ex);
                    for (int a = 0; a < wc; a++) free(ex[a]);
                    free(ex);
                    fflush(NULL);   /* _exit skips stdio flush */
                    _exit(r);
                }
                char *path = resolve_path(ex[0]);
                if (!path) path = ex[0];
                execvp(path, ex);
                fprintf(stderr, "besh: %s: %s\n", ex[0], strerror(errno));
                _exit(127);
            }

            /* Anything that is not a simple command — a while/for/if/
             * case/group/subshell/function call — used to fall through to
             * the _exit(0) below and silently do nothing, which broke the
             * very common `producer | while read l; do ... done` idiom.
             * Run it here in the forked child instead: the process already
             * has the right stdin/stdout wired to the pipe, and exiting
             * afterwards gives the pipeline its left-to-right semantics.
             * NB: only commands running in a child may run here — a
             * non-async child inherits parent memory but that is fine, its
             * results are discarded like any other pipeline stage. */
            {
                /* never hijack the shell while it is interactive here */
                int saved_jc = sh->job_interactive;
                sh->job_interactive = 0;
                int r = execute_node_internal(cmds[i], NULL, NULL, 0);
                sh->job_interactive = saved_jc;
                fflush(NULL);
                _exit(r);
            }
        }
        pids[i] = pid;

        /* set process group for first command */
        if (i == 0 && sh->job_interactive) {
            setpgid(pid, pid);
        } else if (sh->job_interactive) {
            setpgid(pid, pids[0]);
        }
    }

    /* close all pipe fds in parent */
    for (int i = 0; i < ncmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* bring pipeline to foreground */
    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, pids[0]);
    }

    /* wait for all children */
    int last_status = 0;
    int pipefail_status = 0;      /* last non-zero, for set -o pipefail */
    for (int i = 0; i < ncmds; i++) {
        int status = wait_for_pid(pids[i]);
        if (i == ncmds - 1) last_status = status;
        if (status != 0) pipefail_status = status;
    }

    /* restore foreground */
    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, sh->shell_pgid);
    }

    /* `set -o pipefail`: the pipeline fails if ANY stage failed, using the
     * right-most non-zero status — plain bash semantics. */
    if (sh->opt_pipefail && pipefail_status != 0)
        last_status = pipefail_status;

    sh->exit_status = last_status;
    return last_status;
}

/* ---- internal execution dispatcher --------------------------- */
/* Compound commands can carry redirections too (`while ...; done < f`,
 * `{ ...; } 2>&1`, `for i in ...; do ...; done > out`).  They must apply
 * for the whole construct, so they are installed before the body runs and
 * undone afterwards — unlike a simple command, which keeps them for the
 * lifetime of its forked child.
 *
 * Returns 0 on success (the saved fds are written to *save_in/out/err, -1
 * when not saved), or -1 if a redirection could not be opened. */
static int push_compound_redirs(Redir *redirs, int *save_in, int *save_out,
                                int *save_err) {
    *save_in = *save_out = *save_err = -1;
    if (!redirs) return 0;

    for (Redir *r = redirs; r; r = r->next) {
        int psub_fd = -1;
        const char *name = redir_resolve_name(r->filename, &psub_fd);
        switch (r->type) {
        case REDIR_IN:
            if (*save_in < 0) *save_in = dup(STDIN_FILENO);
            { int fd = open(name, O_RDONLY);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDIN_FILENO); close(fd); }
            break;
        case REDIR_OUT:
        case REDIR_CLOBBER:
            if (*save_out < 0) *save_out = dup(STDOUT_FILENO);
            { int fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0644);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDOUT_FILENO); close(fd); }
            break;
        case REDIR_APPEND:
            if (*save_out < 0) *save_out = dup(STDOUT_FILENO);
            { int fd = open(name, O_WRONLY|O_CREAT|O_APPEND, 0644);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDOUT_FILENO); close(fd); }
            break;
        case REDIR_ERR:
            if (*save_err < 0) *save_err = dup(STDERR_FILENO);
            { int fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0644);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDERR_FILENO); close(fd); }
            break;
        case REDIR_ERRAPPEND:
            if (*save_err < 0) *save_err = dup(STDERR_FILENO);
            { int fd = open(name, O_WRONLY|O_CREAT|O_APPEND, 0644);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDERR_FILENO); close(fd); }
            break;
        case REDIR_BOTH:
            if (*save_out < 0) *save_out = dup(STDOUT_FILENO);
            if (*save_err < 0) *save_err = dup(STDERR_FILENO);
            { int fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0644);
              if (fd < 0) { perror(name); return -1; }
              dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
            break;
        case REDIR_DUPIN:
        case REDIR_DUPOUT: {
            int tf = (r->src_fd == STDIN_FILENO)  ? STDIN_FILENO
                   : (r->src_fd == STDERR_FILENO) ? STDERR_FILENO
                                                  : STDOUT_FILENO;
            if (tf == STDIN_FILENO)       { if (*save_in  < 0) *save_in  = dup(tf); }
            else if (tf == STDERR_FILENO) { if (*save_err < 0) *save_err = dup(tf); }
            else                          { if (*save_out < 0) *save_out = dup(tf); }
            dup2(r->fd, tf);
            break;
        }
        case REDIR_CLOSE: {
            int tf = (r->src_fd == STDIN_FILENO)  ? STDIN_FILENO
                   : (r->src_fd == STDERR_FILENO) ? STDERR_FILENO
                                                  : STDOUT_FILENO;
            if (tf == STDIN_FILENO)       { if (*save_in  < 0) *save_in  = dup(tf); }
            else if (tf == STDERR_FILENO) { if (*save_err < 0) *save_err = dup(tf); }
            else                          { if (*save_out < 0) *save_out = dup(tf); }
            close(tf);
            break;
        }
        default:
            break;
        }
        /* release the temporary fd / process-substitution pipe end */
        if (psub_fd >= 0) close(psub_fd);
    }
    return 0;
}

static void pop_compound_redirs(int save_in, int save_out, int save_err) {
    if (save_in  >= 0) { dup2(save_in,  STDIN_FILENO);  close(save_in);  }
    if (save_out >= 0) { dup2(save_out, STDOUT_FILENO); close(save_out); }
    if (save_err >= 0) { dup2(save_err, STDERR_FILENO); close(save_err); }
}

static int execute_node_internal(ASTNode *node, int *pipe_in, int *pipe_out,
                                 int async) {
    Shell *sh = shell_get();
    if (!node) return 0;

    /* Set by constructs that are inherently exempt from errexit (&& / ||
     * lists, `!` pipelines).  Reaching the generic check below with
     * `sh->exit_request` set means a *nested* construct that IS subject
     * to errexit failed, so the request must survive.  Clearing it here
     * instead would silently disable `set -e` for everything below. */
    int exempt_errexit = 0;

    /* `return` unwinds the current function body / sourced file */
    if (sh->return_request) return sh->exit_status;

    int ret = 0;

    /* Compound commands carry their own redirections; install them for the
     * whole construct.  Simple commands handle redirections internally
     * (possibly in a forked child), so they are excluded here. */
    int cr_in = -1, cr_out = -1, cr_err = -1;
    int cr_active = 0;
    switch (node->type) {
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_IF:
    case NODE_CASE:
    case NODE_SUBSHELL:
    case NODE_BRACEGROUP:
        if (node->redirs) {
            if (push_compound_redirs(node->redirs, &cr_in, &cr_out, &cr_err) < 0) {
                pop_compound_redirs(cr_in, cr_out, cr_err);
                sh->exit_status = 1;
                return 1;
            }
            cr_active = 1;
        }
        break;
    default:
        break;
    }

    switch (node->type) {

    case NODE_COMMAND: {
        /* handle pipe_in / pipe_out for pipeline children */
        if (pipe_in)  { dup2(*pipe_in, STDIN_FILENO); close(*pipe_in); }
        if (pipe_out) { dup2(*pipe_out, STDOUT_FILENO); close(*pipe_out); }
        ret = execute_command(node);
        break;
    }

    case NODE_PIPELINE:
        ret = execute_pipeline(node);
        break;

    case NODE_LIST: {
        /* Sequential list.  `a && b; c` must still run `c` even though the
         * `&&` list came back non-zero.  The parse tree is left-nested
         * (LIST(LIST(stmt, stmt), stmt)), so the exemption flag has to
         * travel outward: a list whose last-executed statement was exempt
         * is itself exempt.  Only a genuinely failing statement that is
         * NOT exempt leaves `exit_request` set for us to honour. */
        execute_node_internal(node->left, NULL, NULL, async);
        int left_exempt = sh->last_exempt;
        sh->last_exempt = 0;          /* will be re-set by the right side */
        if (sh->break_request || sh->continue_request || sh->return_request)
            break;
        if (left_exempt && sh->opt_errexit) sh->exit_request = 0;
        if (sh->exit_request) break;
        ret = execute_node_internal(node->right, NULL, NULL, async);
        /* `last_exempt` now describes the right operand; the generic reset
         * below would wipe it for a non-exempt node, so make this node
         * count as exempt when its last statement was. */
        exempt_errexit = sh->last_exempt;
        break;
    }

    case NODE_AND:
    case NODE_OR: {
        /* Both operands sit "in a condition", and so does the node itself:
         * bash does not apply errexit to a command that is part of an
         * && / || list — neither operand nor the list's own result.  The
         * counter therefore stays raised across the whole node. */
        int is_and = (node->type == NODE_AND);
        sh->in_condition++;
        ret = execute_node_internal(node->left, NULL, NULL, async);
        int left_status = ret;
        if (!sh->return_request &&
            (( is_and && ret == 0) || (!is_and && ret != 0))) {
            ret = execute_node_internal(node->right, NULL, NULL, async);
        }
        sh->in_condition--;
        if (sh->opt_errexit) sh->exit_request = 0;
        exempt_errexit = 1;
        sh->last_exempt = 1;
        /* bash nuance: the operands of an && / || list are exempt from
         * errexit, but the list's own failing status still counts when it
         * is the LAST statement.  The status is exempt only when the
         * short-circuit itself decided it — i.e. `false && x` (left failed,
         * right never ran) stays quiet, while `true && false` (right ran
         * and failed) aborts under set -e. */
        if (ret != 0) {
            int decided_by_short_circuit =
                is_and ? (left_status != 0) : (left_status == 0);
            if (!decided_by_short_circuit) sh->last_exempt = 0;
        }
        break;
    }

    case NODE_BG: {
        pid_t pid = fork();
        if (pid < 0) {
            perror("besh: fork");
            ret = 1;
        } else if (pid == 0) {
            /* child — run in background */
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            if (sh->job_interactive) {
                setpgid(0, 0);  /* new process group */
            }
            _exit(execute_node_internal(node->left, NULL, NULL, 0));
        } else {
            /* parent */
            if (sh->job_interactive) {
                setpgid(pid, pid);
            }
            pid_t pids[1] = { pid };

            /* build command string for job listing */
            char cmdstr[1024] = "";
            if (node->left && node->left->type == NODE_COMMAND && node->left->argc > 0) {
                for (int i = 0; i < node->left->argc && strlen(cmdstr) < 1000; i++) {
                    if (i > 0) strcat(cmdstr, " ");
                    strncat(cmdstr, node->left->argv[i], 1000 - strlen(cmdstr));
                }
            }
            job_add(pid, pids, 1, cmdstr);
            fprintf(stderr, "[%d] %d\n", sh->njob, pid);
            ret = 0;
        }
        break;
    }

    case NODE_NOT: {
        /* `! cmd` inverts the status, so a failure is expected control
         * flow — errexit must not fire for the negated command. */
        sh->in_condition++;
        ret = execute_node_internal(node->left, NULL, NULL, async);
        sh->in_condition--;
        if (sh->opt_errexit) sh->exit_request = 0;
        ret = (ret == 0) ? 1 : 0;
        exempt_errexit = 1;
        sh->last_exempt = 1;
        break;
    }

    case NODE_SUBSHELL: {
        pid_t pid = fork();
        if (pid < 0) {
            perror("besh: fork");
            ret = 1;
        } else if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            if (sh->job_interactive) setpgid(0, 0);
            _exit(execute_node_internal(node->left, NULL, NULL, 0));
        } else {
            if (sh->job_interactive) {
                setpgid(pid, pid);
                tcsetpgrp(sh->term_fd, pid);
            }
            ret = wait_for_pid(pid);
            if (sh->job_interactive) {
                tcsetpgrp(sh->term_fd, sh->shell_pgid);
            }
        }
        break;
    }

    case NODE_BRACEGROUP:
        /* `{ list; }` runs in the current shell — only the redirections
         * (if any) are scoped, and those are installed above.  This is
         * what makes `{ cd /tmp; pwd; }` affect the parent shell while
         * `( cd /tmp; pwd )` does not. */
        ret = execute_node_internal(node->left, NULL, NULL, 0);
        break;

    case NODE_FUNCDEF:
        /* function already registered at parse time */
        ret = 0;
        break;

    case NODE_IF: {
        sh->in_condition++;          /* suppress errexit while testing */
        int cond_ret = execute_node_internal(node->cond, NULL, NULL, 0);
        sh->in_condition--;
        if (cond_ret == 0) {
            ret = execute_node_internal(node->body, NULL, NULL, 0);
        } else if (node->else_body) {
            ret = execute_node_internal(node->else_body, NULL, NULL, 0);
        }
        break;
    }

    case NODE_FOR: {
        char *var_name = node->func_name;  /* variable name stored here */
        char **items = NULL;
        int nitems = 0;

        if (node->cond && node->cond->type == NODE_COMMAND) {
            /* expand the word list: honours $list, "${a[@]}", "$@", globs */
            int wc = node->cond->argc;
            char **w = sh_malloc((wc + 1) * sizeof(char *));
            int *wq = sh_malloc((wc + 1) * sizeof(int));
            for (int i = 0; i < wc; i++) {
                w[i] = sh_strdup(node->cond->argv[i]);
                wq[i] = node->cond->argv_quoted ? node->cond->argv_quoted[i] : 0;
            }
            w[wc] = NULL;
            items = expand_words_q(w, wq, &wc);
            nitems = wc;
            for (int i = 0; i < node->cond->argc; i++) free(w[i]);
            free(w);
            free(wq);
        } else {
            /* no `in` list: iterate over the positional parameters */
            nitems = sh->npositional;
            items = sh_malloc((nitems ? nitems : 1) * sizeof(char *));
            for (int i = 0; i < nitems; i++)
                items[i] = sh_strdup(sh->positional[i]);
        }

        for (int i = 0; i < nitems; i++) {
            sh_setenv(var_name, items[i], 0);
            ret = execute_node_internal(node->body, NULL, NULL, 0);
            if (sh->return_request) break;
            if (sh->break_request) {
                sh->break_request = 0;
                break;
            }
            if (sh->continue_request) {
                sh->continue_request = 0;
                continue;
            }
        }
        for (int i = 0; i < nitems; i++) free(items[i]);
        free(items);
        break;
    }

    case NODE_WHILE: {
        int is_until = (node->argc == 1);
        while (1) {
            sh->in_condition++;      /* suppress errexit while testing */
            int cond_ret = execute_node_internal(node->cond, NULL, NULL, 0);
            sh->in_condition--;
            if (sh->return_request) break;
            if (is_until) { if (cond_ret == 0) break; }
            else          { if (cond_ret != 0) break; }
            if (sh->exit_request) break;
            ret = execute_node_internal(node->body, NULL, NULL, 0);
            if (sh->return_request) break;
            if (sh->exit_request) break;
            if (sh->break_request) {
                sh->break_request = 0;
                break;
            }
            if (sh->continue_request) {
                sh->continue_request = 0;
                continue;
            }
        }
        break;
    }

    case NODE_CASE: {
        char *expanded = expand_string(node->case_word);
        int matched = 0;

        for (int i = 0; i < node->case_count && !matched; i++) {
            /* patterns are joined with '|' — split and fnmatch each */
            char *pat_copy = sh_strdup(node->case_patterns[i]);
            char *save = NULL;
            char *token = strtok_r(pat_copy, "|", &save);
            while (token) {
                if (fnmatch(token, expanded, 0) == 0) {
                    matched = 1;
                    break;
                }
                token = strtok_r(NULL, "|", &save);
            }
            free(pat_copy);

            if (matched) {
                ret = execute_node_internal(node->case_bodies[i], NULL, NULL, 0);
            }
        }
        free(expanded);
        break;
    }

    default:
        break;
    }

    /* undo compound-command redirections before the status propagates, so
     * a following statement writes to the shell's own stdout again */
    if (cr_active) pop_compound_redirs(cr_in, cr_out, cr_err);

    sh->exit_status = ret;

    /* ---- errexit (set -e) -------------------------------------------
     * bash does NOT trigger this when the failing command is part of a
     * condition (if/while/until, either operand of && / ||, or a `!`
     * pipeline), which is what sh->in_condition tracks.  When it does
     * fire, the shell unwinds: we flag `exit_request` so enclosing
     * constructs stop at once and the REPL / script loop terminates. */
    if (sh->opt_errexit && ret != 0 && sh->in_condition == 0 &&
        !exempt_errexit &&
        !sh->return_request && !sh->break_request && !sh->continue_request) {
        /* a failure inside a background job or a subshell child is not a
         * reason to kill the shell itself */
        if (node->type != NODE_BG) {
            sh->exit_request = 1;
        }
    }

    /* A node that is not inherently exempt clears the exemption flag, so
     * `false && true` followed by `echo` does not keep suppressing
     * errexit.  Exempt nodes set the flag themselves above. */
    if (!exempt_errexit && node->type != NODE_AND &&
        node->type != NODE_OR && node->type != NODE_NOT)
        sh->last_exempt = 0;

    return ret;
}

/* ---- execute an AST from the root ---------------------------- */
int execute_node(ASTNode *node, int *piped_fds) {
    (void)piped_fds;
    return execute_node_internal(node, NULL, NULL, 0);
}

/* ---- execute a string (parse + execute) ---------------------- */
int execute_string(const char *cmd) {
    if (!cmd || !*cmd) return 0;

    Shell *sh = shell_get();
    Lexer *l = lexer_new(cmd);

    /* handle multi-line / compound commands */
    ASTNode *ast = parse_complete(l);

    if (!ast) {
        lexer_free(l);
        return 0;
    }

    if (sh->opt_xtrace) {
        fprintf(stderr, "+ %s\n", cmd);
    }

    int ret = execute_node_internal(ast, NULL, NULL, 0);

    ast_free(ast);
    lexer_free(l);
    job_notify();
    return ret;
}
