/* ================================================================
 *  builtins.c — built-in shell commands for besh
 *
 *  cd, echo, export, unset, alias, unalias, source, exit, pwd,
 *  type, jobs, fg, bg, history, set, read, test/[, true, false,
 *  exec, shift, times, trap, umask, help
 * ================================================================ */

#include "shell.h"

/* ================================================================
 *  cd [dir]  — change working directory
 * ================================================================ */
int builtin_cd(int argc, char **argv) {
    Shell *sh = shell_get();
    const char *dir = NULL;

    if (argc > 2) {
        fprintf(stderr, "besh: cd: too many arguments\n");
        return 1;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "~") == 0)) {
        dir = sh_getenv("HOME");
        if (!dir) { fprintf(stderr, "besh: cd: HOME not set\n"); return 1; }
    } else if (argc == 2 && strcmp(argv[1], "-") == 0) {
        dir = sh_getenv("OLDPWD");
        if (!dir) { fprintf(stderr, "besh: cd: OLDPWD not set\n"); return 1; }
        printf("%s\n", dir);
    } else {
        dir = argv[1];
    }

    /* save current dir as OLDPWD */
    char oldpwd[MAX_PATH];
    getcwd(oldpwd, sizeof(oldpwd));

    if (chdir(dir) < 0) {
        fprintf(stderr, "besh: cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    char newpwd[MAX_PATH];
    getcwd(newpwd, sizeof(newpwd));
    strncpy(sh->cwd, newpwd, sizeof(sh->cwd) - 1);

    sh_setenv("OLDPWD", oldpwd, 1);
    sh_setenv("PWD", newpwd, 1);
    return 0;
}

/* ================================================================
 *  echo [-n] [-e] [-E] [args...]
 * ================================================================ */
int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int interpret_escapes = 0;
    int i = 1;

    /* parse options */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-n") == 0) {
            newline = 0; i++;
        } else if (strcmp(argv[i], "-e") == 0) {
            interpret_escapes = 1; i++;
        } else if (strcmp(argv[i], "-E") == 0) {
            interpret_escapes = 0; i++;
        } else if (strcmp(argv[i], "--") == 0) {
            i++; break;
        } else {
            break;  /* not a flag */
        }
    }

    for (; i < argc; i++) {
        if (i > 1) putchar(' ');

        if (interpret_escapes) {
            for (char *p = argv[i]; *p; p++) {
                if (*p == '\\' && *(p+1)) {
                    p++;
                    switch (*p) {
                    case 'a': putchar('\a'); break;
                    case 'b': putchar('\b'); break;
                    case 'c': return 0;        /* stop output */
                    case 'e': case 'E': putchar('\x1b'); break;
                    case 'f': putchar('\f'); break;
                    case 'n': putchar('\n'); break;
                    case 'r': putchar('\r'); break;
                    case 't': putchar('\t'); break;
                    case 'v': putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    case '0': {   /* octal */
                        int val = 0;
                        for (int j = 0; j < 3 && *(p) >= '0' && *(p) <= '7'; j++, p++)
                            val = val * 8 + (*p - '0');
                        p--;
                        putchar((char)val);
                        break;
                    }
                    case 'x': {   /* hex */
                        p++;
                        int val = 0;
                        for (int j = 0; j < 2 && ((*p >= '0' && *p <= '9') ||
                             (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'));
                             j++, p++) {
                            if (*p >= '0' && *p <= '9') val = val * 16 + (*p - '0');
                            else if (*p >= 'a' && *p <= 'f') val = val * 16 + (*p - 'a' + 10);
                            else val = val * 16 + (*p - 'A' + 10);
                        }
                        p--;
                        putchar((char)val);
                        break;
                    }
                    default:
                        putchar(*p);
                        break;
                    }
                } else {
                    putchar(*p);
                }
            }
        } else {
            printf("%s", argv[i]);
        }
    }
    if (newline) putchar('\n');
    fflush(stdout);
    return 0;
}

/* ================================================================
 *  export [name[=value]]...   — set environment variables
 *  export -p                  — print all exported variables
 * ================================================================ */
int builtin_export(int argc, char **argv) {
    if (argc == 1) {
        /* print all exported variables */
        Shell *sh = shell_get();
        for (int i = 0; i < sh->nvars; i++) {
            if (sh->vars[i].exported) {
                printf("declare -x %s", sh->vars[i].name);
                if (sh->vars[i].value && *sh->vars[i].value)
                    printf("=\"%s\"", sh->vars[i].value);
                printf("\n");
            }
        }
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "-p") == 0) {
        Shell *sh = shell_get();
        for (int i = 0; i < sh->nvars; i++) {
            if (sh->vars[i].exported)
                printf("export %s=\"%s\"\n", sh->vars[i].name,
                       sh->vars[i].value ? sh->vars[i].value : "");
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = sh_strndup(argv[i], eq - argv[i]);
            char *val = sh_strdup(eq + 1);
            sh_setenv(name, val, 1);
            free(name);
            free(val);
        } else {
            /* mark existing var as exported */
            Shell *sh = shell_get();
            int found = 0;
            for (int j = 0; j < sh->nvars; j++) {
                if (strcmp(sh->vars[j].name, argv[i]) == 0) {
                    sh->vars[j].exported = 1;
                    if (sh->vars[j].value)
                        setenv(argv[i], sh->vars[j].value, 1);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                /* create empty exported var */
                sh_setenv(argv[i], "", 1);
            }
        }
    }
    return 0;
}

/* ================================================================
 *  unset [-f] [-v] name...
 * ================================================================ */
int builtin_unset(int argc, char **argv) {
    int func_mode = 0;
    int var_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) { func_mode = 1; continue; }
        if (strcmp(argv[i], "-v") == 0) { var_mode = 1; continue; }
    }

    if (!func_mode && !var_mode) var_mode = 1;  /* default: unset variables */

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;

        if (func_mode) {
            Shell *sh = shell_get();
            for (int j = 0; j < sh->nfuncs; j++) {
                if (strcmp(sh->funcs[j].name, argv[i]) == 0) {
                    free(sh->funcs[j].name);
                    ast_free(sh->funcs[j].body);
                    memmove(&sh->funcs[j], &sh->funcs[j+1],
                            (sh->nfuncs - j - 1) * sizeof(Function));
                    sh->nfuncs--;
                    break;
                }
            }
        }

        if (var_mode)
            sh_unsetenv(argv[i]);
    }
    return 0;
}

/* ================================================================
 *  alias [name[=value]...]  — define or list aliases
 * ================================================================ */
int builtin_alias(int argc, char **argv) {
    Shell *sh = shell_get();

    if (argc == 1) {
        /* list all aliases */
        for (int i = 0; i < sh->naliases; i++)
            printf("alias %s='%s'\n", sh->aliases[i].name, sh->aliases[i].value);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = sh_strndup(argv[i], eq - argv[i]);
            char *val = sh_strdup(eq + 1);

            /* strip surrounding quotes */
            int vlen = strlen(val);
            if (vlen >= 2 && ((val[0] == '\'' && val[vlen-1] == '\'') ||
                              (val[0] == '"' && val[vlen-1] == '"'))) {
                val[vlen-1] = '\0';
                memmove(val, val + 1, vlen - 1);
            }

            /* update or add */
            int found = 0;
            for (int j = 0; j < sh->naliases; j++) {
                if (strcmp(sh->aliases[j].name, name) == 0) {
                    free(sh->aliases[j].value);
                    sh->aliases[j].value = val;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (sh->naliases >= sh->aliases_cap) {
                    sh->aliases_cap *= 2;
                    sh->aliases = sh_realloc(sh->aliases,
                        sh->aliases_cap * sizeof(Alias));
                }
                sh->aliases[sh->naliases].name = name;
                sh->aliases[sh->naliases].value = val;
                sh->naliases++;
            } else {
                free(name);
            }
        } else {
            /* print specific alias */
            for (int j = 0; j < sh->naliases; j++) {
                if (strcmp(sh->aliases[j].name, argv[i]) == 0) {
                    printf("alias %s='%s'\n", sh->aliases[j].name,
                           sh->aliases[j].value);
                    break;
                }
            }
        }
    }
    return 0;
}

/* ================================================================
 *  unalias name...
 * ================================================================ */
int builtin_unalias(int argc, char **argv) {
    Shell *sh = shell_get();
    for (int i = 1; i < argc; i++) {
        for (int j = 0; j < sh->naliases; j++) {
            if (strcmp(sh->aliases[j].name, argv[i]) == 0) {
                free(sh->aliases[j].name);
                free(sh->aliases[j].value);
                memmove(&sh->aliases[j], &sh->aliases[j+1],
                        (sh->naliases - j - 1) * sizeof(Alias));
                sh->naliases--;
                break;
            }
        }
    }
    return 0;
}

/* ================================================================
 *  source filename  (or  . filename)
 * ================================================================ */
int builtin_source(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "besh: source: filename argument required\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "besh: source: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    char buf[MAX_LINE];
    int ret = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            ret = execute_string(buf);
    }
    fclose(f);
    return ret;
}

/* ================================================================
 *  exit [n]
 * ================================================================ */
int builtin_exit(int argc, char **argv) {
    Shell *sh = shell_get();
    int code = (argc > 1) ? atoi(argv[1]) : sh->exit_status;
    if (sh->job_interactive) {
        /* clean up */
        history_save();
    }
    sh->running = 0;
    /* return code — main() will use it */
    sh->exit_status = code;
    return code;
}

/* ================================================================
 *  pwd  — print working directory
 * ================================================================ */
int builtin_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *sh = shell_get();
    printf("%s\n", sh->cwd);
    return 0;
}

/* ================================================================
 *  type name...  — show how a command would be interpreted
 * ================================================================ */
int builtin_type(int argc, char **argv) {
    if (argc < 2) return 0;

    Shell *sh = shell_get();
    for (int i = 1; i < argc; i++) {
        /* check builtins */
        if (builtin_is(argv[i])) {
            printf("%s is a shell builtin\n", argv[i]);
            continue;
        }
        /* check aliases */
        int is_alias = 0;
        for (int j = 0; j < sh->naliases; j++) {
            if (strcmp(sh->aliases[j].name, argv[i]) == 0) {
                printf("%s is aliased to `%s'\n", argv[i], sh->aliases[j].value);
                is_alias = 1;
                break;
            }
        }
        if (is_alias) continue;
        /* check functions */
        int is_func = 0;
        for (int j = 0; j < sh->nfuncs; j++) {
            if (strcmp(sh->funcs[j].name, argv[i]) == 0) {
                printf("%s is a function\n", argv[i]);
                is_func = 1;
                break;
            }
        }
        if (is_func) continue;
        /* check PATH */
        char *path = resolve_path(argv[i]);
        if (path) {
            printf("%s is %s\n", argv[i], path);
            free(path);
        } else {
            printf("besh: type: %s: not found\n", argv[i]);
        }
    }
    return 0;
}

/* ================================================================
 *  jobs  — list background jobs
 * ================================================================ */
int builtin_jobs(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *sh = shell_get();
    job_cleanup();
    for (Job *j = sh->jobs; j; j = j->next)
        job_print(j);
    return 0;
}

/* ================================================================
 *  fg [job_id]  — bring job to foreground
 * ================================================================ */
int builtin_fg(int argc, char **argv) {
    Shell *sh = shell_get();
    Job *j = NULL;

    if (argc > 1) {
        char *arg = argv[1];
        if (arg[0] == '%') arg++;
        int id = atoi(arg);
        j = job_find_by_id(id);
    } else {
        /* find most recent job */
        j = sh->jobs;
    }

    if (!j) {
        fprintf(stderr, "besh: fg: no such job\n");
        return 1;
    }

    /* bring to foreground */
    pid_t pgid = j->pgid;
    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, pgid);
    }

    /* send SIGCONT if stopped */
    if (j->status == JOB_STOPPED) {
        kill(-pgid, SIGCONT);
    }

    /* wait for all pids */
    for (int i = 0; i < j->npids; i++) {
        if (j->pids[i] != 0) {
            int status;
            waitpid(j->pids[i], &status, WUNTRACED);
        }
    }

    if (sh->job_interactive) {
        tcsetpgrp(sh->term_fd, sh->shell_pgid);
    }

    job_remove(pgid);
    return 0;
}

/* ================================================================
 *  bg [job_id]  — resume stopped job in background
 * ================================================================ */
int builtin_bg(int argc, char **argv) {
    Shell *sh = shell_get();
    Job *j = NULL;

    if (argc > 1) {
        char *arg = argv[1];
        if (arg[0] == '%') arg++;
        int id = atoi(arg);
        j = job_find_by_id(id);
    } else {
        /* find most recent stopped job */
        for (Job *jj = sh->jobs; jj; jj = jj->next) {
            if (jj->status == JOB_STOPPED) { j = jj; break; }
        }
    }

    if (!j) {
        fprintf(stderr, "besh: bg: no such job\n");
        return 1;
    }

    if (j->status == JOB_STOPPED) {
        kill(-j->pgid, SIGCONT);
        j->status = JOB_RUNNING;
        printf("[%d] %s &\n", j->id, j->command);
    }

    return 0;
}

/* ================================================================
 *  history  — display command history
 * ================================================================ */
int builtin_history(int argc, char **argv) {
    Shell *sh = shell_get();
    int start = 0;
    int count = sh->nhist;

    if (argc > 1) {
        count = atoi(argv[1]);
        if (count < 0) {
            start = sh->nhist + count;
            if (start < 0) start = 0;
            count = sh->nhist - start;
        } else {
            start = sh->nhist - count;
            if (start < 0) start = 0;
        }
    }

    for (int i = start; i < sh->nhist && count > 0; i++, count--) {
        printf("%5d  %s\n", i + 1, sh->history[i]);
    }
    return 0;
}

/* ================================================================
 *  set [--] [options]  — set shell options
 * ================================================================ */
int builtin_set(int argc, char **argv) {
    Shell *sh = shell_get();

    if (argc == 1) {
        /* print all variables */
        for (int i = 0; i < sh->nvars; i++) {
            printf("%s=%s\n", sh->vars[i].name,
                   sh->vars[i].value ? sh->vars[i].value : "");
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) continue;
        if (strcmp(argv[i], "-x") == 0) { sh->opt_xtrace = 1; continue; }
        if (strcmp(argv[i], "+x") == 0) { sh->opt_xtrace = 0; continue; }
        if (strcmp(argv[i], "-v") == 0) { sh->opt_verbose = 1; continue; }
        if (strcmp(argv[i], "+v") == 0) { sh->opt_verbose = 0; continue; }
        if (strcmp(argv[i], "-f") == 0) { sh->opt_noglob = 1; continue; }
        if (strcmp(argv[i], "+f") == 0) { sh->opt_noglob = 0; continue; }
        if (strcmp(argv[i], "-C") == 0) { sh->opt_noclobber = 1; continue; }
        if (strcmp(argv[i], "+C") == 0) { sh->opt_noclobber = 0; continue; }
        if (strcmp(argv[i], "-a") == 0) { sh->opt_allexport = 1; continue; }
        if (strcmp(argv[i], "+a") == 0) { sh->opt_allexport = 0; continue; }
    }
    return 0;
}

/* ================================================================
 *  read [-p prompt] [-r] var  — read a line from stdin
 * ================================================================ */
int builtin_read(int argc, char **argv) {
    int raw = 0;
    char *prompt = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) { raw = 1; continue; }
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            prompt = argv[++i];
            continue;
        }
        break;
    }

    if (i >= argc) {
        fprintf(stderr, "besh: read: variable name required\n");
        return 1;
    }

    if (prompt) {
        fprintf(stderr, "%s", prompt);
        fflush(stderr);
    }

    char buf[MAX_LINE];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return 1;  /* EOF */
    }

    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';

    if (!raw) {
        /* strip backslash escapes */
        /* (simplified — just strip trailing backslash) */
        if (len > 0 && buf[len-1] == '\\') buf[--len] = '\0';
    }

    sh_setenv(argv[i], buf, 0);
    return 0;
}

/* ================================================================
 *  test / [  expression  ]
 * ================================================================ */
static int test_unary(const char *op, const char *arg) {
    struct stat st;
    if (strcmp(op, "-z") == 0) return (arg == NULL || *arg == '\0');
    if (strcmp(op, "-n") == 0) return (arg != NULL && *arg != '\0');
    if (strcmp(op, "-e") == 0) return stat(arg, &st) == 0;
    if (strcmp(op, "-f") == 0) return stat(arg, &st) == 0 && S_ISREG(st.st_mode);
    if (strcmp(op, "-d") == 0) return stat(arg, &st) == 0 && S_ISDIR(st.st_mode);
    if (strcmp(op, "-x") == 0) return access(arg, X_OK) == 0;
    if (strcmp(op, "-r") == 0) return access(arg, R_OK) == 0;
    if (strcmp(op, "-w") == 0) return access(arg, W_OK) == 0;
    if (strcmp(op, "-s") == 0) return stat(arg, &st) == 0 && st.st_size > 0;
    if (strcmp(op, "-L") == 0 || strcmp(op, "-h") == 0)
        return lstat(arg, &st) == 0 && S_ISLNK(st.st_mode);
    return 0;
}

int builtin_test(int argc, char **argv) {
    /* handle the '[' form — last argument must be ']' */
    int is_bracket = (strcmp(argv[0], "[") == 0);
    int effective_argc = is_bracket ? argc - 2 : argc;
    char **effective_argv = is_bracket ? argv + 1 : argv;

    if (is_bracket && argc > 1 && strcmp(argv[argc-1], "]") != 0) {
        fprintf(stderr, "besh: [: missing `]'\n");
        return 2;
    }

    if (effective_argc <= 0) {
        /* [ ] or empty test — false */
        return 1;
    }

    if (effective_argc == 1) {
        /* just a string — true if non-empty */
        return (*effective_argv[0] == '\0') ? 1 : 0;
    }

    if (effective_argc == 2) {
        /* unary operator */
        return test_unary(effective_argv[0], effective_argv[1]) ? 0 : 1;
    }

    if (effective_argc == 3) {
        /* binary operator */
        const char *left = effective_argv[0];
        const char *op = effective_argv[1];
        const char *right = effective_argv[2];

        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
            return (strcmp(left, right) == 0) ? 0 : 1;
        if (strcmp(op, "!=") == 0)
            return (strcmp(left, right) != 0) ? 0 : 1;
        if (strcmp(op, "-eq") == 0) return (atoi(left) == atoi(right)) ? 0 : 1;
        if (strcmp(op, "-ne") == 0) return (atoi(left) != atoi(right)) ? 0 : 1;
        if (strcmp(op, "-lt") == 0) return (atoi(left) <  atoi(right)) ? 0 : 1;
        if (strcmp(op, "-le") == 0) return (atoi(left) <= atoi(right)) ? 0 : 1;
        if (strcmp(op, "-gt") == 0) return (atoi(left) >  atoi(right)) ? 0 : 1;
        if (strcmp(op, "-ge") == 0) return (atoi(left) >= atoi(right)) ? 0 : 1;

        /* string with unary op */
        if (test_unary(op, right)) return 0;

        return 1;
    }

    return 2;  /* syntax error */
}

/* ================================================================
 *  true / false
 * ================================================================ */
int builtin_true(int argc, char **argv)  { (void)argc; (void)argv; return 0; }
int builtin_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

/* ================================================================
 *  exec command  — replace shell with command
 * ================================================================ */
int builtin_exec(int argc, char **argv) {
    if (argc < 2) return 0;
    char *path = resolve_path(argv[1]);
    if (!path) path = argv[1];
    execvp(path, argv + 1);
    fprintf(stderr, "besh: exec: %s: %s\n", argv[1], strerror(errno));
    return 1;
}

/* ================================================================
 *  wait [pid]  — wait for background processes
 * ================================================================ */
int builtin_wait(int argc, char **argv) {
    (void)argc; (void)argv;
    int status;
    pid_t pid;
    int ret = 0;
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        if (WIFEXITED(status))
            ret = WEXITSTATUS(status);
    }
    Shell *sh = shell_get();
    sh->exit_status = ret;
    return ret;
}

/* ================================================================
 *  shift [n]  — shift positional parameters
 * ================================================================ */
int builtin_shift(int argc, char **argv) {
    (void)argc; (void)argv;
    /* positional parameters not fully implemented */
    return 0;
}

/* ================================================================
 *  times  — print accumulated process times
 * ================================================================ */
int builtin_times(int argc, char **argv) {
    (void)argc; (void)argv;
    struct tms buf;
    clock_t ticks = times(&buf);
    if (ticks == (clock_t)-1) { perror("times"); return 1; }
    long clk = sysconf(_SC_CLK_TCK);
    printf("%ldm%ld.%03lds %ldm%ld.%03lds\n",
           (long)(buf.tms_utime / clk / 60), (long)(buf.tms_utime / clk % 60),
           (long)(buf.tms_utime % clk * 1000 / clk),
           (long)(buf.tms_stime / clk / 60), (long)(buf.tms_stime / clk % 60),
           (long)(buf.tms_stime % clk * 1000 / clk));
    printf("%ldm%ld.%03lds %ldm%ld.%03lds\n",
           (long)(buf.tms_cutime / clk / 60), (long)(buf.tms_cutime / clk % 60),
           (long)(buf.tms_cutime % clk * 1000 / clk),
           (long)(buf.tms_cstime / clk / 60), (long)(buf.tms_cstime / clk % 60),
           (long)(buf.tms_cstime % clk * 1000 / clk));
    return 0;
}

/* ================================================================
 *  trap [action] [signal...]  — set signal handlers
 * ================================================================ */
int builtin_trap(int argc, char **argv) {
    (void)argc; (void)argv;
    if (argc == 1) {
        /* list traps */
        return 0;
    }
    /* simplified — not fully implemented */
    return 0;
}

/* ================================================================
 *  umask [mode]  — set file creation mask
 * ================================================================ */
int builtin_umask(int argc, char **argv) {
    if (argc == 1) {
        mode_t mask = umask(0);
        umask(mask);
        printf("%04o\n", mask);
        return 0;
    }
    if (argc >= 2) {
        if (strcmp(argv[1], "-S") == 0) {
            mode_t mask = umask(0);
            umask(mask);
            char ubuf[4] = "", gbuf[4] = "", obuf[4] = "";
            if (!(mask & S_IRUSR)) ubuf[0] = 'r'; else ubuf[0] = 0;
            if (!(mask & S_IWUSR)) { int l = strlen(ubuf); ubuf[l] = 'w'; ubuf[l+1] = 0; }
            if (!(mask & S_IXUSR)) { int l = strlen(ubuf); ubuf[l] = 'x'; ubuf[l+1] = 0; }
            if (!(mask & S_IRGRP)) gbuf[0] = 'r'; else gbuf[0] = 0;
            if (!(mask & S_IWGRP)) { int l = strlen(gbuf); gbuf[l] = 'w'; gbuf[l+1] = 0; }
            if (!(mask & S_IXGRP)) { int l = strlen(gbuf); gbuf[l] = 'x'; gbuf[l+1] = 0; }
            if (!(mask & S_IROTH)) obuf[0] = 'r'; else obuf[0] = 0;
            if (!(mask & S_IWOTH)) { int l = strlen(obuf); obuf[l] = 'w'; obuf[l+1] = 0; }
            if (!(mask & S_IXOTH)) { int l = strlen(obuf); obuf[l] = 'x'; obuf[l+1] = 0; }
            printf("u=%s,g=%s,o=%s\n",
                   strlen(ubuf) ? ubuf : "",
                   strlen(gbuf) ? gbuf : "",
                   strlen(obuf) ? obuf : "");
            return 0;
        }
        mode_t mode = (mode_t)strtol(argv[1], NULL, 8);
        umask(mode);
    }
    return 0;
}

/* ================================================================
 *  help  — show help for builtins
 * ================================================================ */
static int builtin_help(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "cd") == 0)
            printf("cd: cd [dir]\n    Change the current directory to DIR.\n");
        else if (strcmp(argv[1], "echo") == 0)
            printf("echo: echo [-neE] [arg ...]\n    Print arguments to stdout.\n");
        else
            printf("besh: help: no help for %s\n", argv[1]);
    } else {
        printf("besh built-in commands:\n");
        printf("  alias  bg  cd  echo  exec  exit  export  false  fg  help\n");
        printf("  history  jobs  pwd  read  set  shift  source  test  times\n");
        printf("  trap  true  type  umask  unalias  unset  [\n");
        printf("Type 'help name' for more info.\n");
    }
    return 0;
}

/* ================================================================
 *  break [n]  — exit innermost n loops
 * ================================================================ */
static int builtin_break(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *sh = shell_get();
    sh->break_request = 1;
    return 0;
}

/* ================================================================
 *  continue [n]  — skip to next iteration of innermost n loops
 * ================================================================ */
static int builtin_continue(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *sh = shell_get();
    sh->continue_request = 1;
    return 0;
}

/* ================================================================
 *  return [n]  — return from a function
 * ================================================================ */
static int builtin_return(int argc, char **argv) {
    Shell *sh = shell_get();
    int code = (argc > 1) ? atoi(argv[1]) : sh->exit_status;
    sh->exit_status = code;
    return code;
}

/* ================================================================
 *  BUILTIN LOOKUP TABLE
 * ================================================================ */
typedef struct {
    const char *name;
    builtin_fn  func;
} BuiltinEntry;

static const BuiltinEntry builtins[] = {
    {"cd",      builtin_cd},
    {"echo",    builtin_echo},
    {"export",  builtin_export},
    {"unset",   builtin_unset},
    {"alias",   builtin_alias},
    {"unalias", builtin_unalias},
    {"source",  builtin_source},
    {".",       builtin_source},
    {"exit",    builtin_exit},
    {"pwd",     builtin_pwd},
    {"type",    builtin_type},
    {"jobs",    builtin_jobs},
    {"fg",      builtin_fg},
    {"bg",      builtin_bg},
    {"history", builtin_history},
    {"set",     builtin_set},
    {"read",    builtin_read},
    {"test",    builtin_test},
    {"[",       builtin_test},
    {"true",    builtin_true},
    {"false",   builtin_false},
    {"exec",    builtin_exec},
    {"shift",   builtin_shift},
    {"times",   builtin_times},
    {"wait",    builtin_wait},
    {"trap",    builtin_trap},
    {"umask",   builtin_umask},
    {"break",   builtin_break},
    {"continue",builtin_continue},
    {"return",  builtin_return},
    {"help",    builtin_help},
    {NULL, NULL}
};

builtin_fn builtin_lookup(const char *name) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, name) == 0)
            return builtins[i].func;
    }
    return NULL;
}

int builtin_is(const char *name) {
    return builtin_lookup(name) != NULL;
}
