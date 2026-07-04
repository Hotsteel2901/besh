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

/* ---- forward declarations ------------------------------------ */
static int execute_node_internal(ASTNode *node, int *pipe_in, int *pipe_out,
                                 int async);
static int wait_for_pid(pid_t pid);
static int setup_redirections(Redir *redirs);

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
    do {
        w = waitpid(pid, &status, WUNTRACED);
    } while (w < 0 && errno == EINTR);

    if (w < 0) {
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
        int target_fd = (r->src_fd >= 0) ? r->src_fd : STDOUT_FILENO;

        switch (r->type) {
        case REDIR_IN:
            fd = open(r->filename, O_RDONLY);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_OUT: {
            int flags = O_WRONLY | O_CREAT | O_TRUNC;
            if (sh->opt_noclobber) flags |= O_EXCL;
            fd = open(r->filename, flags, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;
        }

        case REDIR_APPEND:
            fd = open(r->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_CLOBBER:
            fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, target_fd);
            close(fd);
            break;

        case REDIR_ERR:
            fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;

        case REDIR_ERRAPPEND:
            fd = open(r->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
            dup2(fd, STDERR_FILENO);
            close(fd);
            break;

        case REDIR_BOTH:
            fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r->filename); return -1; }
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

        default:
            break;
        }
    }

    return 0;
}

/* ---- helper: detect NAME=value assignment word ---------------- */
static int is_assignment(const char *word) {
    if (!word || !(*word == '_' || (*word >= 'a' && *word <= 'z') ||
                   (*word >= 'A' && *word <= 'Z')))
        return 0;
    const char *p = word + 1;
    while (*p && (*p == '_' || (*p >= 'a' && *p <= 'z') ||
                  (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
        p++;
    return *p == '=';
}

/* ---- execute a single simple command ------------------------- */
int execute_command(ASTNode *node) {
    if (!node || node->type != NODE_COMMAND) return 1;
    if (node->argc == 0) return 0;

    /* ---- expand variables in all arguments ---- */
    char **expanded_argv = sh_malloc((node->argc + 1) * sizeof(char *));
    for (int i = 0; i < node->argc; i++) {
        expanded_argv[i] = expand_string(node->argv[i]);
    }
    expanded_argv[node->argc] = NULL;

    Shell *sh = shell_get();

    /* ---- detect leading variable assignments (NAME=value ...) ---- */
    int n_assign = 0;
    while (n_assign < node->argc && is_assignment(expanded_argv[n_assign]))
        n_assign++;

    int cmd_start = n_assign;  /* index of first non-assignment word */

    if (n_assign == node->argc) {
        /* all words are assignments — set shell variables */
        int ret = 0;
        for (int i = 0; i < n_assign; i++) {
            char *eq = strchr(expanded_argv[i], '=');
            *eq = '\0';
            sh_setenv(expanded_argv[i], eq + 1, sh->opt_allexport);
            *eq = '=';
        }
        sh->exit_status = ret;
        for (int j = 0; j < node->argc; j++) free(expanded_argv[j]);
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
    int cmd_argc = node->argc - cmd_start;

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

            int ret = execute_node_internal(sh->funcs[i].body,
                                            NULL, NULL, 0);

            /* free function positional parameters */
            for (int j = 0; j < fnargs; j++)
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
            for (int j = 0; j < node->argc; j++) free(expanded_argv[j]);
            free(expanded_argv);
            return ret;
        }
    }

    /* check builtins */
    builtin_fn bf = builtin_lookup(cmd_argv[0]);
    if (bf) {
        /* handle redirections for builtins */
        int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;

        /* set up redirections */
        for (Redir *r = node->redirs; r; r = r->next) {
            switch (r->type) {
            case REDIR_IN:
                if (saved_stdin < 0) saved_stdin = dup(STDIN_FILENO);
                { int fd = open(r->filename, O_RDONLY);
                  if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); } }
                break;
            case REDIR_OUT:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                { int fd = open(r->filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); } }
                break;
            case REDIR_APPEND:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                { int fd = open(r->filename, O_WRONLY|O_CREAT|O_APPEND, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); } }
                break;
            case REDIR_ERR:
                if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                { int fd = open(r->filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); } }
                break;
            case REDIR_BOTH:
                if (saved_stdout < 0) saved_stdout = dup(STDOUT_FILENO);
                if (saved_stderr < 0) saved_stderr = dup(STDERR_FILENO);
                { int fd = open(r->filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                  if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); } }
                break;
            default: break;
            }
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
        for (int j = 0; j < node->argc; j++) free(expanded_argv[j]);
        free(expanded_argv);
        return ret;
    }

    /* external command — fork and exec */
    pid_t pid = fork();
    if (pid < 0) {
        perror("besh: fork");
        for (int j = 0; j < node->argc; j++) free(expanded_argv[j]);
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
    for (int j = 0; j < node->argc; j++) free(expanded_argv[j]);
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
                builtin_fn bf = builtin_lookup(cmds[i]->argv[0]);
                if (bf) {
                    int r = bf(cmds[i]->argc, cmds[i]->argv);
                    _exit(r);
                }
                char *path = resolve_path(cmds[i]->argv[0]);
                if (!path) path = cmds[i]->argv[0];
                execvp(path, cmds[i]->argv);
                fprintf(stderr, "besh: %s: %s\n", cmds[i]->argv[0], strerror(errno));
                _exit(127);
            }
            _exit(0);
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
    for (int i = 0; i < ncmds; i++) {
        int status = wait_for_pid(pids[i]);
        if (i == ncmds - 1) last_status = status;
    }

    /* restore foreground */
    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, sh->shell_pgid);
    }

    sh->exit_status = last_status;
    return last_status;
}

/* ---- internal execution dispatcher --------------------------- */
static int execute_node_internal(ASTNode *node, int *pipe_in, int *pipe_out,
                                 int async) {
    Shell *sh = shell_get();
    if (!node) return 0;

    int ret = 0;

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

    case NODE_LIST:
        execute_node_internal(node->left, NULL, NULL, async);
        if (sh->break_request || sh->continue_request) break;
        ret = execute_node_internal(node->right, NULL, NULL, async);
        break;

    case NODE_AND:
        ret = execute_node_internal(node->left, NULL, NULL, async);
        if (ret == 0) {
            ret = execute_node_internal(node->right, NULL, NULL, async);
        }
        break;

    case NODE_OR:
        ret = execute_node_internal(node->left, NULL, NULL, async);
        if (ret != 0) {
            ret = execute_node_internal(node->right, NULL, NULL, async);
        }
        break;

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
        ret = execute_node_internal(node->left, NULL, NULL, async);
        ret = (ret == 0) ? 1 : 0;
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

    case NODE_FUNCDEF:
        /* function already registered at parse time */
        ret = 0;
        break;

    case NODE_IF: {
        int cond_ret = execute_node_internal(node->cond, NULL, NULL, 0);
        if (cond_ret == 0) {
            ret = execute_node_internal(node->body, NULL, NULL, 0);
        } else if (node->else_body) {
            ret = execute_node_internal(node->else_body, NULL, NULL, 0);
        }
        break;
    }

    case NODE_FOR: {
        char *var_name = node->func_name;  /* variable name stored here */
        if (node->cond && node->cond->type == NODE_COMMAND) {
            for (int i = 0; i < node->cond->argc; i++) {
                sh_setenv(var_name, node->cond->argv[i], 0);
                ret = execute_node_internal(node->body, NULL, NULL, 0);
                if (sh->break_request) {
                    sh->break_request = 0;
                    break;
                }
                if (sh->continue_request) {
                    sh->continue_request = 0;
                    continue;
                }
            }
        }
        break;
    }

    case NODE_WHILE: {
        int is_until = (node->argc == 1);
        while (1) {
            int cond_ret = execute_node_internal(node->cond, NULL, NULL, 0);
            if (is_until) { if (cond_ret == 0) break; }
            else          { if (cond_ret != 0) break; }
            ret = execute_node_internal(node->body, NULL, NULL, 0);
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

    sh->exit_status = ret;
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
