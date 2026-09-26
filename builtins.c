/* ================================================================
 *  builtins.c — built-in shell commands for besh
 *
 *  cd, echo, export, unset, alias, unalias, source, exit, pwd,
 *  type, jobs, fg, bg, history, set, read, test/[, true, false,
 *  exec, shift, times, trap, umask, help
 * ================================================================ */

#include "shell.h"
#include <ctype.h>
#include <regex.h>

/* getcwd() is declared warn_unused_result, which turns every call into a
 * warning.  All our callers either know the buffer is large enough or fall
 * back to a sensible default, so funnel them through one place that
 * handles failure explicitly.  Never returns NULL: on failure the caller's
 * buffer is left holding an empty string. */
static char *proj_getcwd(char *buf, size_t size) {
    if (getcwd(buf, size) == NULL) {
        if (size > 0) buf[0] = '\0';
    }
    return buf;
}

/* ================================================================
 *  cd [dir]  — change working directory
 *  cd -      — previous directory (OLDPWD)
 *  cd +N / -N — directory stack entry (zsh)
 * ================================================================ */
int cd_to(const char *dir) {
    Shell *sh = shell_get();

    if (!dir || !*dir) {
        dir = sh_getenv("HOME");
        if (!dir) { fprintf(stderr, "besh: cd: HOME not set\n"); return 1; }
    }
    if (strcmp(dir, "-") == 0) {
        const char *old = sh_getenv("OLDPWD");
        if (!old) { fprintf(stderr, "besh: cd: OLDPWD not set\n"); return 1; }
        printf("%s\n", old);
        dir = old;
    }

    char *expanded = tilde_expand(dir);

    char oldpwd[MAX_PATH];
    proj_getcwd(oldpwd, sizeof(oldpwd));

    if (chdir(expanded) < 0) {
        fprintf(stderr, "besh: cd: %s: %s\n", expanded, strerror(errno));
        free(expanded);
        return 1;
    }
    free(expanded);

    char newpwd[MAX_PATH];
    proj_getcwd(newpwd, sizeof(newpwd));
    snprintf(sh->cwd, sizeof(sh->cwd), "%s", newpwd);

    sh_setenv("OLDPWD", oldpwd, 1);
    sh_setenv("PWD", newpwd, 1);
    return 0;
}

int builtin_cd(int argc, char **argv) {
    Shell *sh = shell_get();

    if (argc > 2) {
        fprintf(stderr, "besh: cd: too many arguments\n");
        return 1;
    }

    if (argc == 2) {
        const char *arg = argv[1];
        /* zsh-style directory stack: cd -N / cd +N */
        if (arg[0] == '-' && arg[1] && arg[1] >= '0' && arg[1] <= '9') {
            int n = atoi(arg + 1);
            if (n <= 0 || n >= sh->ndirs) {
                fprintf(stderr, "besh: cd: %s: no such directory in stack\n", arg);
                return 1;
            }
            return cd_to(sh->dirs[n]);
        }
        if (arg[0] == '+' && arg[1] && arg[1] >= '0' && arg[1] <= '9') {
            int n = atoi(arg + 1);
            int idx = sh->ndirs - 1 - n;
            if (idx < 0 || idx >= sh->ndirs) {
                fprintf(stderr, "besh: cd: %s: no such directory in stack\n", arg);
                return 1;
            }
            return cd_to(sh->dirs[idx]);
        }
        return cd_to(arg);
    }

    return cd_to(NULL);
}

/* ================================================================
 *  echo [-n] [-e] [-E] [args...]
 * ================================================================ */
int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int interpret_escapes = 0;
    int i = 1;
    int first_arg;

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
    first_arg = i;

    for (; i < argc; i++) {
        if (i > first_arg) putchar(' ');

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
 *  export [-p] [-n] [name[=value]]...  — set/clear environment vars
 *  export          — list exported variables
 *  export -p       — bash-style declaration list
 *  export -n NAME  — keep NAME but remove its exported attribute
 * ================================================================ */
int builtin_export(int argc, char **argv) {
    Shell *sh = shell_get();

    if (argc == 1) {
        /* print all exported variables */
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

    int unexport = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            for (int j = 0; j < sh->nvars; j++) {
                if (sh->vars[j].exported)
                    printf("export %s=\"%s\"\n", sh->vars[j].name,
                           sh->vars[j].value ? sh->vars[j].value : "");
            }
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0) { unexport = 1; continue; }
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (argv[i][0] == '-' && argv[i][1]) continue;  /* ignore other flags */
        break;
    }

    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = sh_strndup(argv[i], eq - argv[i]);
            char *val = sh_strdup(eq + 1);
            if (unexport) {
                sh_setenv(name, val, 0);   /* value only, no export */
                for (int j = 0; j < sh->nvars; j++)
                    if (strcmp(sh->vars[j].name, name) == 0)
                        sh->vars[j].exported = 0;
                unsetenv(name);
            } else {
                sh_setenv(name, val, 1);
            }
            free(name);
            free(val);
        } else {
            /* mark existing var as exported (or clear with -n) */
            int found = 0;
            for (int j = 0; j < sh->nvars; j++) {
                if (strcmp(sh->vars[j].name, argv[i]) == 0) {
                    if (unexport) {
                        sh->vars[j].exported = 0;
                        unsetenv(argv[i]);
                    } else {
                        sh->vars[j].exported = 1;
                        if (sh->vars[j].value)
                            setenv(argv[i], sh->vars[j].value, 1);
                    }
                    found = 1;
                    break;
                }
            }
            if (!found && !unexport) {
                /* create empty exported var */
                sh_setenv(argv[i], "", 1);
            }
        }
    }
    return 0;
}

/* ================================================================
 *  unset [-f] [-v] name...
 *  Array-aware: `unset a`, `unset 'a[@]'` (whole array) and
 *  `unset 'a[1]'` (single element) are all supported.
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

        if (var_mode) {
            char *base = NULL;
            long idx = 0;
            int star = 0;
            int kind = var_parse_subscript(argv[i], &base, &idx, &star);
            if (kind == 2) {
                if (base) var_array_clear(base);
            } else if (kind == 1) {
                if (base) var_array_unset(base, idx);
            } else {
                if (base && var_is_array(base)) var_array_clear(base);
                else sh_unsetenv(argv[i]);
            }
            free(base);
        }
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
 *  abbr — fish-style abbreviations (expand on space/enter)
 *  abbr                       — list all
 *  abbr name=value            — define
 *  abbr -a name value...      — define (multi-word value)
 *  abbr --erase name...       — remove
 * ================================================================ */
void abbr_add(const char *name, const char *value) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nabbrs; i++) {
        if (strcmp(sh->abbrs[i].name, name) == 0) {
            free(sh->abbrs[i].value);
            sh->abbrs[i].value = sh_strdup(value);
            return;
        }
    }
    if (sh->nabbrs >= sh->abbrs_cap) {
        sh->abbrs_cap = sh->abbrs_cap ? sh->abbrs_cap * 2 : 64;
        sh->abbrs = sh_realloc(sh->abbrs, sh->abbrs_cap * sizeof(Alias));
    }
    sh->abbrs[sh->nabbrs].name  = sh_strdup(name);
    sh->abbrs[sh->nabbrs].value = sh_strdup(value);
    sh->nabbrs++;
}

char *abbr_find(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nabbrs; i++)
        if (strcmp(sh->abbrs[i].name, name) == 0)
            return sh->abbrs[i].value;
    return NULL;
}

int abbr_erase(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; i < sh->nabbrs; i++) {
        if (strcmp(sh->abbrs[i].name, name) == 0) {
            free(sh->abbrs[i].name);
            free(sh->abbrs[i].value);
            memmove(&sh->abbrs[i], &sh->abbrs[i+1],
                    (sh->nabbrs - i - 1) * sizeof(Alias));
            sh->nabbrs--;
            return 0;
        }
    }
    return 1;
}

int builtin_abbr(int argc, char **argv) {
    Shell *sh = shell_get();

    if (argc == 1) {
        for (int i = 0; i < sh->nabbrs; i++)
            printf("abbr %s %s\n", sh->abbrs[i].name, sh->abbrs[i].value);
        return 0;
    }

    if (strcmp(argv[1], "--erase") == 0 || strcmp(argv[1], "-e") == 0 ||
        strcmp(argv[1], "-a") == 0) {
        for (int i = 2; i < argc; i++) {
            if (argv[1][1] == 'a') {
                /* abbr -a name value... */
                if (i + 1 >= argc) {
                    fprintf(stderr, "besh: abbr: missing value for %s\n", argv[i]);
                    continue;
                }
                char val[MAX_LINE] = "";
                for (int j = i + 1; j < argc; j++) {
                    if (j > i + 1) strcat(val, " ");
                    strncat(val, argv[j], MAX_LINE - strlen(val) - 1);
                }
                abbr_add(argv[i], val);
                i = argc;
            } else {
                abbr_erase(argv[i]);
            }
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = sh_strndup(argv[i], eq - argv[i]);
            char *val  = sh_strdup(eq + 1);
            /* strip surrounding quotes like alias does */
            int vlen = strlen(val);
            if (vlen >= 2 && ((val[0] == '\'' && val[vlen-1] == '\'') ||
                              (val[0] == '"' && val[vlen-1] == '"'))) {
                val[vlen-1] = '\0';
                memmove(val, val + 1, vlen - 1);
            }
            abbr_add(name, val);
            free(name);
            free(val);
        } else {
            char *v = abbr_find(argv[i]);
            if (v) printf("abbr %s %s\n", argv[i], v);
            else {
                fprintf(stderr, "besh: abbr: %s: no such abbreviation\n", argv[i]);
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================
 *  Directory stack (zsh-style)
 *  dirs [-v]  — print the stack (index 0 = current dir)
 *  pushd [dir] — cd to dir, pushing the old one; no arg swaps
 *  popd       — cd to the directory below the top, drop the top
 * ================================================================ */
void dirs_push(const char *dir) {
    Shell *sh = shell_get();
    if (sh->ndirs >= sh->dirs_cap) {
        sh->dirs_cap = sh->dirs_cap ? sh->dirs_cap * 2 : 16;
        sh->dirs = sh_realloc(sh->dirs, sh->dirs_cap * sizeof(char *));
    }
    memmove(sh->dirs + 1, sh->dirs, sh->ndirs * sizeof(char *));
    sh->dirs[0] = sh_strdup(dir);
    sh->ndirs++;
}

void dirs_print(int verbose) {
    Shell *sh = shell_get();
    if (verbose) {
        for (int i = 0; i < sh->ndirs; i++) {
            char *h = sh_getenv("HOME");
            const char *d = sh->dirs[i];
            if (h && strncmp(d, h, strlen(h)) == 0)
                printf("%d\t~%s\n", i, d + strlen(h));
            else
                printf("%d\t%s\n", i, d);
        }
    } else {
        for (int i = 0; i < sh->ndirs; i++)
            printf("%s ", sh->dirs[i]);
        printf("\n");
    }
}

int builtin_dirs(int argc, char **argv) {
    Shell *sh = shell_get();
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-c") == 0) {
            for (int j = 0; j < sh->ndirs; j++) free(sh->dirs[j]);
            sh->ndirs = 0;
            return 0;
        }
    }
    dirs_print(verbose);
    return 0;
}

int builtin_pushd(int argc, char **argv) {
    Shell *sh = shell_get();
    char cur[MAX_PATH];
    proj_getcwd(cur, sizeof(cur));

    if (argc == 1) {
        /* swap top two entries */
        if (sh->ndirs < 2) {
            fprintf(stderr, "besh: pushd: no other directory\n");
            return 1;
        }
        char target[MAX_PATH];
        snprintf(target, sizeof(target), "%s", sh->dirs[1]);
        if (cd_to(target)) return 1;
        free(sh->dirs[1]);
        sh->dirs[1] = sh_strdup(cur);
        dirs_print(0);
        return 0;
    }

    if (cd_to(argv[1])) return 1;

    char newcur[MAX_PATH];
    proj_getcwd(newcur, sizeof(newcur));
    dirs_push(newcur);
    dirs_print(0);
    return 0;
}

int builtin_popd(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *sh = shell_get();
    if (sh->ndirs < 2) {
        fprintf(stderr, "besh: popd: directory stack empty\n");
        return 1;
    }
    char target[MAX_PATH];
    snprintf(target, sizeof(target), "%s", sh->dirs[1]);
    if (cd_to(target)) return 1;

    free(sh->dirs[0]);
    memmove(sh->dirs, sh->dirs + 1, (sh->ndirs - 1) * sizeof(char *));
    sh->ndirs--;
    dirs_print(0);
    return 0;
}

/* ================================================================
 *  setopt / unsetopt — zsh-style option management
 *  setopt                      — print all option states
 *  setopt name...              — enable options
 *  unsetopt name...            — disable options
 * ================================================================ */
typedef struct { const char *name; size_t offset; } OptionEntry;

static OptionEntry option_table[] = {
    {"autocd",          offsetof(Shell, opt_autocd)},
    {"globstar",        offsetof(Shell, opt_globstar)},
    {"autosuggest",     offsetof(Shell, opt_autosuggest)},
    {"syntaxhighlight", offsetof(Shell, opt_syntaxhighlight)},
    {"histignoredups",  offsetof(Shell, opt_histignoredups)},
    {"noclobber",       offsetof(Shell, opt_noclobber)},
    {"allexport",       offsetof(Shell, opt_allexport)},
    {"xtrace",          offsetof(Shell, opt_xtrace)},
    {"verbose",         offsetof(Shell, opt_verbose)},
    {"noglob",          offsetof(Shell, opt_noglob)},
    {"errexit",         offsetof(Shell, opt_errexit)},
    {"nounset",         offsetof(Shell, opt_nounset)},
    {"pipefail",        offsetof(Shell, opt_pipefail)},
    {NULL, 0}
};

static int *option_flag(const char *name) {
    Shell *sh = shell_get();
    for (int i = 0; option_table[i].name; i++) {
        if (strcmp(name, option_table[i].name) == 0)
            return (int *)((char *)sh + option_table[i].offset);
    }
    return NULL;
}

static void options_print(void) {
    Shell *sh = shell_get();
    for (int i = 0; option_table[i].name; i++) {
        int *ptr = (int *)((char *)sh + option_table[i].offset);
        if (*ptr)
            printf("setopt %s\n", option_table[i].name);
        else
            printf("unsetopt %s\n", option_table[i].name);
    }
}

int builtin_setopt(int argc, char **argv) {
    if (argc == 1) { options_print(); return 0; }
    for (int i = 1; i < argc; i++) {
        int *p = option_flag(argv[i]);
        if (!p) { fprintf(stderr, "besh: setopt: unknown option: %s\n", argv[i]); return 1; }
        *p = 1;
    }
    return 0;
}

int builtin_unsetopt(int argc, char **argv) {
    if (argc == 1) { options_print(); return 0; }
    for (int i = 1; i < argc; i++) {
        int *p = option_flag(argv[i]);
        if (!p) { fprintf(stderr, "besh: unsetopt: unknown option: %s\n", argv[i]); return 1; }
        *p = 0;
    }
    return 0;
}

/* ================================================================
 *  Variable declarations: readonly / declare / typeset / local
 * ================================================================ */

/* Split "name", "name=value", "name+=value", "name[idx]=value" or
 * "name=( ... )".  Returns 1 for an assignment form, 0 for a bare name. */
static int bi_assign_split(const char *word, char **name, char **value,
                           int *op, int *is_lit, char **inner) {
    *name = NULL; *value = NULL; *inner = NULL; *op = 0; *is_lit = 0;
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
    int o = 0;
    if (*p == '+') { if (p[1] != '=') return 0; o = 1; p++; }
    else if (*p != '=') return 0;

    *name = sh_strndup(word, p - word);
    p++;                              /* past '=' */
    if (*p == '(') {
        size_t l = strlen(word);
        if (l > (size_t)(p - word) + 1 && word[l - 1] == ')') {
            *is_lit = 1;
            *inner = sh_strndup(p + 1, l - (p - word) - 2);
        }
    } else {
        *value = sh_strdup(p);
    }
    *op = o;
    return 1;
}

/* find the base (non-element) Var entry for `name` */
static Var *bi_find_base(const char *name) {
    Shell *sh = shell_get();
    char *base = var_base_of(name);
    Var *res = NULL;
    for (int i = 0; i < sh->nvars; i++)
        if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0) {
            res = &sh->vars[i];
            break;
        }
    free(base);
    return res;
}

/* apply -a/-r/-x attributes to the base variable, creating it if needed */
static void bi_apply_attrs(const char *name, int a, int r, int x) {
    Shell *sh = shell_get();
    char *base = var_base_of(name);
    if (a) var_set_array_attr(base, 1);
    Var *v = NULL;
    for (int i = 0; i < sh->nvars; i++)
        if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0) {
            v = &sh->vars[i];
            break;
        }
    if (!v && (r || x)) {
        sh_setenv(base, "", 0);
        for (int i = 0; i < sh->nvars; i++)
            if (!sh->vars[i].is_element && strcmp(sh->vars[i].name, base) == 0) {
                v = &sh->vars[i];
                break;
            }
    }
    if (v) {
        if (x) { v->exported = 1; setenv(base, v->value ? v->value : "", 1); }
        if (r) v->readonly = 1;
    }
    free(base);
}

/* print one variable bash-style (`declare -a a=(...)` etc.) */
static int bi_print_var(const char *name) {
    Shell *sh = shell_get();
    Var *base = bi_find_base(name);
    int isarr = var_is_array(name);
    if (!base && !isarr) return 0;

    char flags[8];
    int f = 0;
    flags[f++] = '-';
    if (isarr) flags[f++] = 'a';
    if (base && base->readonly) flags[f++] = 'r';
    if (base && base->exported) flags[f++] = 'x';
    if (f == 1) flags[f++] = '-';
    flags[f] = '\0';

    if (isarr) {
        int ni = 0;
        long *idxs = var_array_indices(name, &ni);
        if (ni == 0) {
            printf("declare %s %s\n", flags, name);
        } else {
            printf("declare %s %s=(", flags, name);
            for (int i = 0; i < ni; i++) {
                char *v = var_array_get(name, idxs[i], NULL);
                if (i) printf(" ");
                printf("[%ld]=\"%s\"", idxs[i], v);
                free(v);
            }
            printf(")\n");
        }
        free(idxs);
    } else {
        printf("declare %s %s=\"%s\"\n", flags, name,
               base->value ? base->value : "");
    }
    (void)sh;
    return 1;
}

/* shared implementation for declare / typeset / local */
static int declare_impl(int argc, char **argv, int is_local) {
    Shell *sh = shell_get();
    if (is_local && sh->nscopes == 0) {
        fprintf(stderr, "besh: local: can only be used in a function\n");
        return 1;
    }

    int opt_a = 0, opt_r = 0, opt_x = 0, opt_p = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        for (char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'a': opt_a = 1; break;
            case 'r': opt_r = 1; break;
            case 'x': opt_x = 1; break;
            case 'p': opt_p = 1; break;
            case 'i': break;              /* accepted (integer attr) */
            case 'g': break;              /* accepted */
            default:
                fprintf(stderr, "besh: %s: -%c: invalid option\n",
                        is_local ? "local" : "declare", *p);
                return 1;
            }
        }
    }

    /* declare -p / declare (no names) — print variables */
    if (opt_p || i >= argc) {
        if (i >= argc) {
            for (int k = 0; k < sh->nvars; k++) {
                if (sh->vars[k].is_element) continue;
                bi_print_var(sh->vars[k].name);
            }
            return 0;
        }
        int rc = 0;
        for (; i < argc; i++) {
            if (!bi_print_var(argv[i])) {
                fprintf(stderr, "besh: declare: %s: not found\n", argv[i]);
                rc = 1;
            }
        }
        return rc;
    }

    int rc = 0;
    for (; i < argc; i++) {
        char *name = NULL, *value = NULL, *inner = NULL;
        int op = 0, is_lit = 0;
        if (bi_assign_split(argv[i], &name, &value, &op, &is_lit, &inner)) {
            if (is_local) scope_declare(name);
            if (opt_a) var_set_array_attr(name, 1);
            if (is_lit) {
                Lexer *lx = lexer_new(inner);
                char *toks[MAX_ARGS];
                int qt[MAX_ARGS];
                int nt = 0, t;
                while ((t = lexer_next(lx)) == TOK_WORD && nt < MAX_ARGS - 1) {
                    toks[nt] = sh_strdup(lx->token_text);
                    qt[nt] = lx->token_quoted;
                    nt++;
                }
                lexer_free(lx);
                int nw = nt;
                char **ex = expand_words_q(toks, qt, &nw);
                for (int k = 0; k < nt; k++) free(toks[k]);
                char *base = NULL;
                long idx = 0;
                int star = 0;
                int kind = var_parse_subscript(name, &base, &idx, &star);
                if (!base) base = sh_strdup(name);
                if (kind != 1) {
                    long start = 0;
                    if (op) {
                        int ni = 0;
                        long *idxs = var_array_indices(base, &ni);
                        if (ni > 0) start = idxs[ni - 1] + 1;
                        free(idxs);
                    } else {
                        var_array_clear(base);
                    }
                    for (int k = 0; k < nw; k++)
                        var_array_set(base, start + k, ex[k]);
                } else {
                    for (int k = 0; k < nw; k++)
                        var_array_set(base, idx + k, ex[k]);
                }
                var_set_array_attr(base, 1);
                for (int k = 0; k < nw; k++) free(ex[k]);
                free(ex);
                free(base);
            } else {
                sh_assign(name, value ? value : "", opt_x, op);
            }
            bi_apply_attrs(name, opt_a, opt_r, opt_x);
            free(name); free(value); free(inner);
        } else {
            if (is_local) scope_declare(argv[i]);
            bi_apply_attrs(argv[i], opt_a, opt_r, opt_x);
        }
    }
    return rc;
}

int builtin_declare(int argc, char **argv) { return declare_impl(argc, argv, 0); }
int builtin_typeset(int argc, char **argv) { return declare_impl(argc, argv, 0); }
int builtin_local(int argc, char **argv)   { return declare_impl(argc, argv, 1); }

/* ================================================================
 *  readonly [-a] [-p] [name[=value]]...  — mark variables read-only
 * ================================================================ */
int builtin_readonly(int argc, char **argv) {
    Shell *sh = shell_get();

    int opt_a = 0, opt_p = 0, i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        for (char *p = argv[i] + 1; *p; p++) {
            if (*p == 'a') opt_a = 1;
            else if (*p == 'p') opt_p = 1;
        }
    }

    if (opt_p || i >= argc) {
        for (int k = 0; k < sh->nvars; k++) {
            if (sh->vars[k].is_element) continue;
            if (sh->vars[k].readonly) bi_print_var(sh->vars[k].name);
        }
        return 0;
    }

    for (; i < argc; i++) {
        char *name = NULL, *value = NULL, *inner = NULL;
        int op = 0, is_lit = 0;
        if (bi_assign_split(argv[i], &name, &value, &op, &is_lit, &inner)) {
            if (is_lit) {
                Lexer *lx = lexer_new(inner);
                char *toks[MAX_ARGS];
                int qt[MAX_ARGS];
                int nt = 0, t;
                while ((t = lexer_next(lx)) == TOK_WORD && nt < MAX_ARGS - 1) {
                    toks[nt] = sh_strdup(lx->token_text);
                    qt[nt] = lx->token_quoted;
                    nt++;
                }
                lexer_free(lx);
                int nw = nt;
                char **ex = expand_words_q(toks, qt, &nw);
                for (int k = 0; k < nt; k++) free(toks[k]);
                var_array_clear(name);
                for (int k = 0; k < nw; k++) var_array_set(name, k, ex[k]);
                var_set_array_attr(name, 1);
                for (int k = 0; k < nw; k++) free(ex[k]);
                free(ex);
            } else {
                sh_assign(name, value ? value : "", 0, op);
            }
            bi_apply_attrs(name, opt_a, 1, 0);
            free(name); free(value); free(inner);
        } else {
            bi_apply_attrs(argv[i], opt_a, 1, 0);
        }
    }
    return 0;
}

/* ================================================================
 *  source filename [args...]  (or  . filename)
 *
 *  The file is executed with the streaming reader (sh_run_stream), which
 *  accumulates physical lines until a logical command is complete — so
 *  multi-line if/for/while and function definitions work, unlike the old
 *  per-line execute_string() loop.
 *
 *  bash positional-parameter semantics: while the file runs, $1..$n are
 *  set to the extra arguments given to `source` (and restored afterwards).
 *  $? is the status of the last command executed from the file.
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

    Shell *sh = shell_get();

    /* save the caller's positional parameters */
    char **old_pos = sh->positional;
    int    old_npos = sh->npositional;

    /* install the source arguments as the new $1..$n */
    sh->positional = NULL;
    sh->npositional = 0;
    if (argc > 2) {
        int n = argc - 2;
        sh->positional = sh_malloc(n * sizeof(char *));
        for (int i = 0; i < n; i++)
            sh->positional[i] = sh_strdup(argv[i + 2]);
        sh->npositional = n;
    }

    int ret = sh_run_stream(f);
    fclose(f);

    /* free the temporary positional parameters and restore the originals */
    for (int i = 0; i < sh->npositional; i++)
        free(sh->positional[i]);
    free(sh->positional);
    sh->positional = old_pos;
    sh->npositional = old_npos;

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
    sigchld_block();
    for (int i = 0; i < j->npids; i++) {
        if (j->pids[i] != 0) {
            int status;
            waitpid(j->pids[i], &status, WUNTRACED);
        }
    }
    sigchld_unblock();

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
 *  history [-c] [-d offset] [-a] [-r] [-w] [n]
 *    (no option)  list all entries with line numbers
 *    history N    list the last N entries (negative N: drop |N| oldest)
 *    HISTTIMEFORMAT, if set, is used as a strftime() prefix per entry.
 * ================================================================ */
int builtin_history(int argc, char **argv) {
    Shell *sh = shell_get();

    int i = 1;
    int show_n = -1;            /* `history -N` shorthand for the last N */
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "-c") == 0) { history_clear(); return 0; }
        if (strcmp(argv[i], "-a") == 0) { history_append(); return 0; }
        if (strcmp(argv[i], "-r") == 0) { history_read(); return 0; }
        if (strcmp(argv[i], "-w") == 0) { history_write(); return 0; }
        if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "besh: history: -d: option requires an argument\n");
                return 1;
            }
            /* the argument is a history *position* (1-based, exactly as
             * printed by `history`), not a list offset */
            int pos = atoi(argv[i + 1]);
            if (history_delete(pos - 1) != 0) {
                fprintf(stderr, "besh: history: %s: history position out of range\n",
                        argv[i + 1]);
                return 1;
            }
            return 0;
        }
        /* `history -N` — show the last N entries */
        if (argv[i][1] >= '0' && argv[i][1] <= '9') {
            const char *d = argv[i] + 1;
            if (strspn(d, "0123456789") == strlen(d)) {
                show_n = atoi(d);
                i++;
                break;
            }
        }
        fprintf(stderr, "besh: history: %s: invalid option\n", argv[i]);
        return 1;
    }

    int start, count;
    if (show_n >= 0) {
        start = sh->nhist - show_n;
        if (start < 0) start = 0;
        count = sh->nhist - start;
    } else {
        start = 0;
        count = sh->nhist;
        if (i < argc) {
            int n = atoi(argv[i]);
            if (n < 0) {
                start = sh->nhist + n;
                if (start < 0) start = 0;
                count = sh->nhist - start;
            } else {
                start = sh->nhist - n;
                if (start < 0) start = 0;
                count = sh->nhist - start;
            }
        }
    }

    const char *fmt = sh_getenv("HISTTIMEFORMAT");
    for (int k = start; k < sh->nhist && count > 0; k++, count--) {
        char tb[256];
        tb[0] = '\0';
        if (fmt && *fmt && sh->hist_time[k] > 0) {
            time_t t = (time_t)sh->hist_time[k];
            struct tm *tm = localtime(&t);
            if (tm) strftime(tb, sizeof(tb), fmt, tm);
        }
        printf("%5d  %s%s\n", k + 1, tb, sh->history[k]);
    }
    return 0;
}

/* ================================================================
 *  fc [-l] [-n] [-r] [-s [old=new] [cmd]] [-e [editor] [first] [last]]
 *    -l           list history entries (line numbers, no timestamps)
 *    -s           re-execute a history entry, optionally after old=new
 *    -e [editor]  edit the selected entries, then execute them
 *  editor defaults to $FCEDIT, then $EDITOR, then vi.
 * ================================================================ */
static int looks_like_range(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-') s++;
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

/* Resolve a first/last spec to a 0-based history index (N = 1-based
 * absolute, -N = from the end, otherwise a prefix search, newest first). */
static int hist_resolve(const char *spec, int *out) {
    Shell *sh = shell_get();
    if (!spec || !*spec) return 0;
    char *end;
    long v = strtol(spec, &end, 10);
    if (end != spec && *end == '\0') {
        int idx = (v > 0) ? (int)v - 1 : (v < 0) ? sh->nhist + (int)v : -1;
        if (idx < 0 || idx >= sh->nhist) return 0;
        *out = idx;
        return 1;
    }
    for (int i = sh->nhist - 1; i >= 0; i--) {
        if (strncmp(sh->history[i], spec, strlen(spec)) == 0) {
            *out = i;
            return 1;
        }
    }
    return 0;
}

/* first-occurrence `old=new` substitution */
static char *hist_substitute(const char *cmd, const char *subst) {
    const char *eq = strchr(subst, '=');
    if (!eq) return sh_strdup(cmd);
    char *old = sh_strndup(subst, eq - subst);
    const char *new = eq + 1;
    const char *pos = strstr(cmd, old);
    if (!pos) { free(old); return sh_strdup(cmd); }
    int pre = pos - cmd;
    const char *tail = pos + strlen(old);
    char *res = sh_malloc(pre + strlen(new) + strlen(tail) + 1);
    memcpy(res, cmd, pre);
    memcpy(res + pre, new, strlen(new));
    strcpy(res + pre + strlen(new), tail);
    free(old);
    return res;
}

/* fc must not leave *itself* at the tail of the history list: bash
 * replaces the `fc …` entry with the command it actually ran.  The REPL
 * has already pushed the `fc …` line by the time this builtin runs, so
 * drop that trailing entry before recording the substituted command.
 * Without this, `fc -s` with no argument would re-run the literal text
 * `fc -s` for ever.  Returns 0 when an entry was dropped. */
static int hist_drop_trailing_fc(void) {
    Shell *sh = shell_get();
    if (sh->nhist == 0) return 0;
    const char *last = sh->history[sh->nhist - 1];
    while (*last == ' ' || *last == '\t') last++;
    if (strncmp(last, "fc ", 3) != 0 && strcmp(last, "fc") != 0)
        return 0;
    free(sh->history[sh->nhist - 1]);
    sh->history[sh->nhist - 1] = NULL;
    sh->hist_time[sh->nhist - 1] = 0;
    sh->nhist--;
    return 1;
}

int builtin_fc(int argc, char **argv) {
    Shell *sh = shell_get();
    int list = 0, silent = 0, edit = 0;
    const char *editor = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-l") == 0) { list = 1; i++; continue; }
        if (strcmp(argv[i], "-s") == 0) { silent = 1; i++; continue; }
        if (strcmp(argv[i], "-n") == 0) { i++; continue; }   /* accepted */
        if (strcmp(argv[i], "-r") == 0) { i++; continue; }   /* accepted */
        if (strcmp(argv[i], "-e") == 0) {
            edit = 1;
            i++;
            /* "-e editor" unless the next token already looks like a range */
            if (i < argc && !looks_like_range(argv[i])) editor = argv[i++];
            continue;
        }
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        break;
    }

    const char *argfirst = (i < argc) ? argv[i++] : NULL;
    const char *arglast  = (i < argc) ? argv[i++] : NULL;
    int first = -1, last = -1;

    /* Resolve the selection while the `fc …` line is still present, then
     * remove it so the chosen entry is what ends up at the tail. */
    if (silent) {
        const char *subst = NULL, *cmdspec = NULL;
        if (argfirst) {
            if (strchr(argfirst, '=')) { subst = argfirst; cmdspec = arglast; }
            else cmdspec = argfirst;
        }
        int idx = -1;
        /* bash: `fc -s` with no operand means the *previous* command, so
         * look past the trailing `fc …` entry first. */
        hist_drop_trailing_fc();
        if (sh->nhist == 0) {
            fprintf(stderr, "besh: fc: no command found\n");
            return 1;
        }
        idx = sh->nhist - 1;
        if (cmdspec && !hist_resolve(cmdspec, &idx)) {
            fprintf(stderr, "besh: fc: %s: history specification out of range\n", cmdspec);
            return 1;
        }
        char *cmd = subst ? hist_substitute(sh->history[idx], subst)
                          : sh_strdup(sh->history[idx]);
        printf("%s\n", cmd);
        history_add(cmd);
        int ret = execute_string(cmd);
        free(cmd);
        return ret;
    }

    if (list || edit) {
        /* `fc -e` (like `fc -s`) must resolve its selection *before* the
         * trailing `fc …` line is counted, otherwise `-1` — and the
         * implicit "last command" — both point at fc itself. */
        if (edit) hist_drop_trailing_fc();
        if (argfirst) {
            if (!hist_resolve(argfirst, &first)) {
                fprintf(stderr, "besh: fc: %s: history specification out of range\n", argfirst);
                return 1;
            }
            if (arglast) {
                if (!hist_resolve(arglast, &last)) {
                    fprintf(stderr, "besh: fc: %s: history specification out of range\n", arglast);
                    return 1;
                }
            } else {
                last = first;
            }
        } else {
            last = sh->nhist - 1;
            first = list ? (last - 15) : last;
            if (first < 0) first = 0;
        }
        if (sh->nhist == 0 || last < 0) {
            fprintf(stderr, "besh: fc: no command found\n");
            return 1;
        }
        if (first > last) { int t = first; first = last; last = t; }

        if (list) {
            for (int k = first; k <= last && k < sh->nhist; k++)
                printf("%5d  %s\n", k + 1, sh->history[k]);
            return 0;
        }

        /* -e: dump selection to a temp file, run the editor, then execute */
        const char *ed = editor;
        if (!ed || !*ed) ed = sh_getenv("FCEDIT");
        if (!ed || !*ed) ed = sh_getenv("EDITOR");
        if (!ed || !*ed) ed = "vi";

        char tmpl[] = "/tmp/besh_fcXXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) { perror("besh: fc"); return 1; }
        FILE *fp = fdopen(fd, "w");
        if (fp) {
            for (int k = first; k <= last && k < sh->nhist; k++)
                fprintf(fp, "%s\n", sh->history[k]);
            fclose(fp);
        } else {
            close(fd);
        }

        char cmdline[MAX_PATH + 64];
        snprintf(cmdline, sizeof(cmdline), "%s %s", ed, tmpl);
        int sysret = system(cmdline);
        (void)sysret;

        FILE *rf = fopen(tmpl, "r");
        int ret = 0;
        if (rf) {
            ret = sh_run_stream(rf);
            fclose(rf);
        }
        unlink(tmpl);
        return ret;
    }

    /* no mode flag: list the last entry (bash would invoke an editor) */
    if (sh->nhist == 0) {
        fprintf(stderr, "besh: fc: no command found\n");
        return 1;
    }
    first = last = sh->nhist - 1;
    printf("%5d  %s\n", first + 1, sh->history[first]);
    return 0;
}

/* ================================================================
 *  Programmable completion: `compgen` + `complete`
 *
 *  A registered CompSpec maps a command name to one of these candidate
 *  sources (wordlist / -F function / -A action).  Tab completion in the
 *  line editor consults compspec_find() before falling back to filenames.
 *
 *  A completion function may either
 *    (a) print candidates, one per line, to standard output, or
 *    (b) set the shell variable COMPREPLY to a space-separated list.
 *  Both are supported; COMPREPLY wins when it is non-empty.  The function
 *  is invoked with the current word as $1.
 * ================================================================ */
typedef struct { char **v; int n; int cap; } CandList;

static void cand_add(CandList *l, const char *s) {
    if (!s || !*s) return;
    if (l->n >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = sh_realloc(l->v, l->cap * sizeof(char *));
    }
    l->v[l->n++] = sh_strdup(s);
}
static int cand_has(const CandList *l, const char *s) {
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i], s) == 0) return 1;
    return 0;
}
static void cand_add_unique(CandList *l, const char *s) {
    if (!s || !*s) return;
    if (cand_has(l, s)) return;
    cand_add(l, s);
}
static void cand_add_filtered(CandList *l, const char *s, const char *prefix) {
    if (!s || !*s) return;
    if (prefix && *prefix && strncmp(s, prefix, strlen(prefix)) != 0) return;
    cand_add_unique(l, s);
}

static const char *comp_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "while",
    "until", "do", "done", "in", "function", "select", "time", "coproc",
    "return", "break", "continue", NULL
};

/* external commands found in PATH + builtins/functions/aliases */
static void gen_commands(CandList *l, const char *prefix) {
    Shell *sh = shell_get();
    for (int i = 0; i < builtin_count(); i++)
        cand_add_filtered(l, builtin_name(i), prefix);
    for (int i = 0; i < sh->nfuncs; i++)
        cand_add_filtered(l, sh->funcs[i].name, prefix);
    for (int i = 0; i < sh->naliases; i++)
        cand_add_filtered(l, sh->aliases[i].name, prefix);

    char *path = sh_getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    char *pc = sh_strdup(path);
    char *save = NULL;
    for (char *dir = strtok_r(pc, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        if (!*dir) dir = ".";
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' &&
                (!prefix || prefix[0] != '.')) continue;
            if (prefix && *prefix &&
                strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (access(full, X_OK) == 0)
                cand_add_unique(l, e->d_name);
        }
        closedir(d);
    }
    free(pc);
}

/* files (or directories only) matching `prefix`, honouring a path prefix */
static void gen_files(CandList *l, const char *prefix, int dirs_only) {
    char dir[MAX_PATH];
    char lead[MAX_PATH + 2] = "";   /* the part of prefix we reproduce */
    const char *base = prefix;
    const char *slash = strrchr(prefix, '/');
    if (slash) {
        int dlen = (int)(slash - prefix);
        if (dlen == 0) {
            snprintf(dir, sizeof(dir), "/");
            snprintf(lead, sizeof(lead), "/");
        } else {
            snprintf(dir, sizeof(dir), "%.*s", dlen, prefix);
            snprintf(lead, sizeof(lead), "%.*s/", dlen, prefix);
        }
        base = slash + 1;
    } else {
        snprintf(dir, sizeof(dir), ".");
    }

    char *expanded = NULL;
    if (dir[0] == '~') {
        expanded = tilde_expand(dir);
        if (expanded) { snprintf(dir, sizeof(dir), "%s", expanded); free(expanded); }
    }

    DIR *d = opendir(dir);
    if (!d) return;
    size_t bl = strlen(base);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            if (bl == 0 || base[0] != '.') continue;
        }
        if (strncmp(e->d_name, base, bl) != 0) continue;
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        int isdir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        if (dirs_only && !isdir) continue;
        char cand[MAX_PATH + 4];
        snprintf(cand, sizeof(cand), "%s%s%s", lead, e->d_name, isdir ? "/" : "");
        cand_add_unique(l, cand);
    }
    closedir(d);
}

/* run a completion function, capturing stdout, then collect candidates */
static void run_comp_func(const char *func, const char *prefix, CandList *l) {
    char tmpl[] = "/tmp/besh_compXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return;

    /* single-quote the current word for the function's $1 */
    char q[1024];
    int qo = 0;
    q[qo++] = '\'';
    for (const char *p = prefix; *p && qo < (int)sizeof(q) - 6; p++) {
        if (*p == '\'') { memcpy(q + qo, "'\\''", 4); qo += 4; }
        else q[qo++] = *p;
    }
    q[qo++] = '\'';
    q[qo] = '\0';

    char cmd[2048];
    if ((size_t)(qo + strlen(func) + 2) >= sizeof(cmd)) {
        close(fd);
        unlink(tmpl);
        return;
    }
    snprintf(cmd, sizeof(cmd), "%s %s", func, q);

    /* capture the function's stdout into the temp file.  Flush any pending
     * stdout (e.g. the banner's trailing reset) to the real tty first, so
     * it does not pollute the captured candidates. */
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(fd, STDOUT_FILENO);
    close(fd);
    sh_unsetenv("COMPREPLY");     /* detect whether the function sets it */
    execute_string(cmd);
    fflush(stdout);
    if (saved >= 0) { dup2(saved, STDOUT_FILENO); close(saved); }

    char *creply = sh_getenv("COMPREPLY");
    if (creply && *creply) {
        char *cr = sh_strdup(creply);
        char *save = NULL;
        for (char *w = strtok_r(cr, " \t\n", &save); w;
             w = strtok_r(NULL, " \t\n", &save))
            cand_add_filtered(l, w, prefix);
        free(cr);
    } else {
        FILE *f = fopen(tmpl, "r");
        if (f) {
            char buf[MAX_LINE];
            while (fgets(buf, sizeof(buf), f)) {
                size_t len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                    buf[--len] = '\0';
                if (len > 0) cand_add_filtered(l, buf, prefix);
            }
            fclose(f);
        }
    }
    unlink(tmpl);
}

int compgen_generate(const char *action, const char *wordlist, const char *func,
                     const char *prefix, char ***out, int *n) {
    CandList l;
    memset(&l, 0, sizeof(l));
    if (!prefix) prefix = "";
    Shell *sh = shell_get();

    if (wordlist) {
        char *wl = sh_strdup(wordlist);
        char *save = NULL;
        for (char *w = strtok_r(wl, " \t\n", &save); w;
             w = strtok_r(NULL, " \t\n", &save))
            cand_add_filtered(&l, w, prefix);
        free(wl);
    }

    if (action) {
        if (strcmp(action, "builtin") == 0) {
            for (int i = 0; i < builtin_count(); i++)
                cand_add_filtered(&l, builtin_name(i), prefix);
        } else if (strcmp(action, "function") == 0) {
            for (int i = 0; i < sh->nfuncs; i++)
                cand_add_filtered(&l, sh->funcs[i].name, prefix);
        } else if (strcmp(action, "alias") == 0) {
            for (int i = 0; i < sh->naliases; i++)
                cand_add_filtered(&l, sh->aliases[i].name, prefix);
        } else if (strcmp(action, "variable") == 0) {
            for (int i = 0; i < sh->nvars; i++)
                cand_add_filtered(&l, sh->vars[i].name, prefix);
        } else if (strcmp(action, "keyword") == 0) {
            for (int i = 0; comp_keywords[i]; i++)
                cand_add_filtered(&l, comp_keywords[i], prefix);
        } else if (strcmp(action, "file") == 0) {
            gen_files(&l, prefix, 0);
        } else if (strcmp(action, "directory") == 0 ||
                   strcmp(action, "dir") == 0) {
            gen_files(&l, prefix, 1);
        } else if (strcmp(action, "command") == 0) {
            gen_commands(&l, prefix);
        }
        /* unknown actions simply produce nothing */
    }

    if (func) run_comp_func(func, prefix, &l);

    *out = l.v;
    *n = l.n;
    return l.n;
}

void compgen_free(char **matches, int n) {
    if (!matches) return;
    for (int i = 0; i < n; i++) free(matches[i]);
    free(matches);
}

/* ---- completion rule registry --------------------------------- */
CompSpec *compspec_find(const char *name) {
    Shell *sh = shell_get();
    for (CompSpec *c = sh->compspecs; c; c = c->next)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

int compspec_add(const char *name, const char *wordlist,
                 const char *func, const char *action) {
    Shell *sh = shell_get();
    CompSpec *c = compspec_find(name);
    if (!c) {
        c = sh_malloc(sizeof(CompSpec));
        memset(c, 0, sizeof(*c));
        c->name = sh_strdup(name);
        c->next = sh->compspecs;
        sh->compspecs = c;
    }
    free(c->wordlist); c->wordlist = wordlist ? sh_strdup(wordlist) : NULL;
    free(c->func);     c->func     = func     ? sh_strdup(func)     : NULL;
    free(c->action);   c->action   = action   ? sh_strdup(action)   : NULL;
    return 0;
}

int compspec_remove(const char *name) {
    Shell *sh = shell_get();
    CompSpec *prev = NULL;
    for (CompSpec *c = sh->compspecs; c; prev = c, c = c->next) {
        if (strcmp(c->name, name) == 0) {
            if (prev) prev->next = c->next;
            else sh->compspecs = c->next;
            free(c->name); free(c->wordlist); free(c->func); free(c->action);
            free(c);
            return 0;
        }
    }
    return 1;
}

void compspec_free_all(void) {
    Shell *sh = shell_get();
    CompSpec *c = sh->compspecs;
    while (c) {
        CompSpec *next = c->next;
        free(c->name); free(c->wordlist); free(c->func); free(c->action);
        free(c);
        c = next;
    }
    sh->compspecs = NULL;
}

int builtin_compgen(int argc, char **argv) {
    const char *action = NULL, *wordlist = NULL, *func = NULL, *prefix = "";
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-A") == 0 && i + 1 < argc) { action = argv[++i]; i++; continue; }
        if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) { wordlist = argv[++i]; i++; continue; }
        if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) { func = argv[++i]; i++; continue; }
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-P") == 0 ||
             strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "-X") == 0 ||
             strcmp(argv[i], "-G") == 0) && i + 1 < argc) { i += 2; continue; }
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        break;
    }
    if (i < argc) prefix = argv[i];

    char **out = NULL;
    int n = 0;
    compgen_generate(action, wordlist, func, prefix, &out, &n);
    for (int k = 0; k < n; k++)
        printf("%s\n", out[k]);
    compgen_free(out, n);
    return n > 0 ? 0 : 1;
}

int builtin_complete(int argc, char **argv) {
    Shell *sh = shell_get();
    const char *wordlist = NULL, *func = NULL, *action = NULL;
    char *cmds[256];
    int ncmd = 0;
    int print = 0, remove = 0;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-p") == 0) { print = 1; i++; continue; }
        if (strcmp(argv[i], "-r") == 0) { remove = 1; i++; continue; }
        if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) { wordlist = argv[++i]; i++; continue; }
        if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) { func = argv[++i]; i++; continue; }
        if (strcmp(argv[i], "-A") == 0 && i + 1 < argc) { action = argv[++i]; i++; continue; }
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-G") == 0 ||
             strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "-S") == 0 ||
             strcmp(argv[i], "-X") == 0) && i + 1 < argc) { i += 2; continue; }
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        break;
    }
    for (; i < argc && ncmd < 256; i++)
        cmds[ncmd++] = argv[i];

    if (print) {
        for (CompSpec *c = sh->compspecs; c; c = c->next) {
            printf("complete");
            if (c->wordlist) printf(" -W '%s'", c->wordlist);
            if (c->func)     printf(" -F %s", c->func);
            if (c->action)   printf(" -A %s", c->action);
            printf(" %s\n", c->name);
        }
        return 0;
    }

    if (remove) {
        if (ncmd == 0) compspec_free_all();
        else for (int k = 0; k < ncmd; k++) compspec_remove(cmds[k]);
        return 0;
    }

    if (ncmd == 0) {
        fprintf(stderr, "besh: complete: usage: complete [-W wordlist] "
                        "[-F func] [-A action] [-p] [-r] [name ...]\n");
        return 1;
    }

    for (int k = 0; k < ncmd; k++)
        compspec_add(cmds[k], wordlist, func, action);
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
        if (strcmp(argv[i], "--") == 0) {
            /* `set -- a b c` replaces the positional parameters */
            for (int k = 0; k < sh->npositional; k++) free(sh->positional[k]);
            free(sh->positional);
            int n = argc - i - 1;
            sh->positional = (n > 0) ? sh_malloc(n * sizeof(char *)) : NULL;
            for (int k = 0; k < n; k++)
                sh->positional[k] = sh_strdup(argv[i + 1 + k]);
            sh->npositional = n;
            return 0;
        }
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
        if (strcmp(argv[i], "-e") == 0) { sh->opt_errexit = 1; continue; }
        if (strcmp(argv[i], "+e") == 0) { sh->opt_errexit = 0; continue; }
        if (strcmp(argv[i], "-u") == 0) { sh->opt_nounset = 1; continue; }
        if (strcmp(argv[i], "+u") == 0) { sh->opt_nounset = 0; continue; }
        /* combined single-letter flags such as `set -eu` / `set +eu` */
        if ((argv[i][0] == '-' || argv[i][0] == '+') && argv[i][1] &&
            argv[i][1] != '-' && strlen(argv[i]) > 2) {
            int val = (argv[i][0] == '-');
            int handled = 1;
            for (int k = 1; argv[i][k]; k++) {
                switch (argv[i][k]) {
                case 'e': sh->opt_errexit = val; break;
                case 'u': sh->opt_nounset = val; break;
                case 'x': sh->opt_xtrace  = val; break;
                case 'v': sh->opt_verbose = val; break;
                case 'f': sh->opt_noglob  = val; break;
                case 'a': sh->opt_allexport = val; break;
                case 'C': sh->opt_noclobber = val; break;
                default:
                    fprintf(stderr, "besh: set: unknown option: -%c\n", argv[i][k]);
                    handled = 0;
                    break;
                }
            }
            if (handled) continue;
        }
        /* zsh-style: set -o name / set +o name */
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "+o") == 0) {
            int val = (argv[i][0] == '+') ? 0 : 1;
            if (i + 1 < argc) {
                int *p = option_flag(argv[++i]);
                if (p) *p = val;
                else fprintf(stderr, "besh: set: unknown option: %s\n", argv[i]);
            } else {
                options_print();
            }
            continue;
        }
    }
    return 0;
}

/* ================================================================
 *  read [-p prompt] [-r] var...  — read a line, split on IFS
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
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) { ++i; continue; }
        break;
    }

    if (prompt) {
        fprintf(stderr, "%s", prompt);
        fflush(stderr);
    }

    char buf[MAX_LINE];
    /* Read straight from fd 0 rather than through the stdio `stdin`
     * buffer.  Redirections are installed with dup2() behind stdio's
     * back, so a FILE * left in its end-of-file state (which is exactly
     * what happens after a `while ... < <(...)` loop drains its pipe)
     * would keep reporting EOF even though fd 0 now points at a fresh
     * pipe.  Reading raw bytes avoids that stale-state trap entirely. */
    {
        size_t n = 0;
        int got_any = 0;
        while (n < sizeof(buf) - 1) {
            char ch;
            ssize_t r = read(STDIN_FILENO, &ch, 1);
            if (r < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (r == 0) break;              /* EOF */
            got_any = 1;
            if (ch == '\n') break;
            buf[n++] = ch;
        }
        if (!got_any) return 1;             /* EOF, nothing read */
        buf[n] = '\0';
        /* a partial last line still counts as a line */
    }

    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';

    if (!raw) {
        /* fold \<newline> continuations and strip unescaped backslashes */
        char out[MAX_LINE];
        int o = 0;
        for (size_t k = 0; k < len; k++) {
            if (buf[k] == '\\' && k + 1 < len && buf[k+1] == '\n') { k++; continue; }
            out[o++] = buf[k];
        }
        out[o] = '\0';
        len = o;
        memcpy(buf, out, o + 1);
    }

    if (i >= argc) {
        /* no variable given → REPLY */
        sh_setenv("REPLY", buf, 0);
        return 0;
    }

    int nvars = argc - i;

    /* tokenize on the first IFS char (default: whitespace) */
    const char *ifs = sh_getenv("IFS");
    char sep[3] = " \t";
    if (ifs && *ifs) {
        snprintf(sep, sizeof(sep), "%c", *ifs);
        if (strchr(ifs, ' ')) { sep[0] = ' '; sep[1] = '\t'; sep[2] = '\0'; }
    }

    char *tokens[MAX_LINE];
    int ntok = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, sep, &save);
    while (tok && ntok < MAX_LINE) {
        tokens[ntok++] = tok;
        tok = strtok_r(NULL, sep, &save);
    }

    int v;
    for (v = 0; v < nvars; v++) {
        if (v < ntok - 1) {
            sh_setenv(argv[i + v], tokens[v], 0);
        } else if (v == nvars - 1) {
            /* last variable gets the remainder */
            char joined[MAX_LINE] = "";
            for (int k = v; k < ntok; k++) {
                if (k > v) strcat(joined, " ");
                strncat(joined, tokens[k], MAX_LINE - strlen(joined) - 1);
            }
            sh_setenv(argv[i + v], joined, 0);
        } else {
            sh_setenv(argv[i + v], "", 0);
        }
    }
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

/* ================================================================
 *  [[ ... ]]  — compound conditional expression
 *
 *  The lexer hands us the raw text between the brackets as argv[0]
 *  prefixed with '\001'.  Grammar (bash-compatible):
 *
 *    expr   := or
 *    or     := and ( '||' and )*
 *    and    := not ( '&&' not )*
 *    not    := '!' not | primary
 *    primary:= '(' expr ')'
 *            | unary_op word
 *            | word bin_op word        (bin_op includes =~ regex)
 *            | word                    (true when non-empty)
 * ================================================================ */
typedef struct {
    char **toks;
    int   *quoted;      /* parallel to toks: 1 if the token was quoted */
    int    n;
    int    pos;
} DBTok;

static int db_peek(DBTok *t) { return t->pos < t->n ? 1 : 0; }
static const char *db_cur(DBTok *t) { return t->pos < t->n ? t->toks[t->pos] : NULL; }
static int db_cur_quoted(DBTok *t) { return t->pos < t->n ? t->quoted[t->pos] : 0; }
static void db_next(DBTok *t) { if (t->pos < t->n) t->pos++; }

/* does the current token equal one of these literal operators? */
static int db_is_op(DBTok *t, const char *a, const char *b, const char *c) {
    const char *s = db_cur(t);
    if (!s) return 0;
    if (a && strcmp(s, a) == 0) return 1;
    if (b && strcmp(s, b) == 0) return 1;
    if (c && strcmp(s, c) == 0) return 1;
    return 0;
}

static int db_expr(DBTok *t);

/* numeric comparison used by [[ -eq ]] etc. */
static int db_num_cmp(const char *l, const char *r, const char *op) {
    char *e1, *e2;
    long a = strtol(l, &e1, 10);
    long b = strtol(r, &e2, 10);
    if (*e1 || *e2) {                 /* not integers → false, like bash */
        return 0;
    }
    if (strcmp(op, "-eq") == 0) return a == b;
    if (strcmp(op, "-ne") == 0) return a != b;
    if (strcmp(op, "-lt") == 0) return a <  b;
    if (strcmp(op, "-le") == 0) return a <= b;
    if (strcmp(op, "-gt") == 0) return a >  b;
    if (strcmp(op, "-ge") == 0) return a >= b;
    return 0;
}

/* [[ left =~ regex ]] — POSIX ERE via the system regex engine */
static int db_regex_match(const char *str, const char *pat) {
    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED) != 0) return 0;
    int ok = (regexec(&re, str, 0, NULL, 0) == 0);
    regfree(&re);
    return ok;
}

static int db_primary(DBTok *t) {
    if (!db_peek(t)) return 0;
    const char *s = db_cur(t);

    if (strcmp(s, "(") == 0) {
        db_next(t);
        int v = db_expr(t);
        if (db_is_op(t, ")", NULL, NULL)) db_next(t);
        return v;
    }

    /* unary operators on files / strings — the table must list every op
     * test_unary() understands, otherwise the token is taken for an
     * operand and the expression fails to parse (`[[ -n x ]]`). */
    if (s[0] == '-' && strlen(s) == 2 &&
        strchr("abcdefghkLnprstuwxzGLNOSvR", s[1])) {
        char op[3];
        snprintf(op, sizeof(op), "%s", s);
        db_next(t);
        const char *arg = db_cur(t);
        if (!arg) arg = "";
        db_next(t);
        return test_unary(op, arg) ? 1 : 0;
    }

    /* bare word: could be a binary expression or a plain non-empty test */
    const char *left = s;
    /* build the expanded form now, so `[[ $x == y ]]` compares values */
    char *lexp = expand_string_no_split(left);
    db_next(t);

    if (db_peek(t)) {
        const char *op = db_cur(t);
        /* binary string operators */
        if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 ||
            strcmp(op, "!=") == 0 || strcmp(op, "=~") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
            strcmp(op, "-eq") == 0 || strcmp(op, "-ne") == 0 ||
            strcmp(op, "-lt") == 0 || strcmp(op, "-le") == 0 ||
            strcmp(op, "-gt") == 0 || strcmp(op, "-ge") == 0 ||
            strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 ||
            strcmp(op, "-ef") == 0) {
            db_next(t);
            const char *rtok = db_cur(t);
            int rquoted = db_cur_quoted(t);
            if (!rtok) rtok = "";
            /* For == / != the RHS is a GLOB PATTERN — but only when it was
             * not quoted.  `[[ x == "a*" ]]` compares against the literal
             * `a*`, so escape the metacharacters in that case. */
            char *rexp;
            if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 ||
                strcmp(op, "!=") == 0) {
                if (rquoted) {
                    /* escape every glob metacharacter so the match is literal */
                    size_t len = strlen(rtok);
                    char *esc = sh_malloc(len * 2 + 1);
                    size_t o = 0;
                    for (size_t k = 0; k < len; k++) {
                        if (rtok[k] == '*' || rtok[k] == '?' ||
                            rtok[k] == '[' || rtok[k] == '\\')
                            esc[o++] = '\\';
                        esc[o++] = rtok[k];
                    }
                    esc[o] = '\0';
                    rexp = esc;
                } else {
                    rexp = sh_strdup(rtok);
                }
            } else {
                rexp = expand_string_no_split(rtok);
            }
            db_next(t);

            int r;
            if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0)
                r = sh_pattern_match(lexp, rexp);
            else if (strcmp(op, "!=") == 0)
                r = !sh_pattern_match(lexp, rexp);
            else if (strcmp(op, "=~") == 0)
                r = db_regex_match(lexp, rexp);
            else if (strcmp(op, "<") == 0)  r = strcmp(lexp, rexp) <  0;
            else if (strcmp(op, ">") == 0)  r = strcmp(lexp, rexp) >  0;
            else if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 ||
                     strcmp(op, "-ef") == 0) {
                struct stat s1, s2;
                int ok1 = (stat(lexp, &s1) == 0);
                int ok2 = (stat(rexp, &s2) == 0);
                if (strcmp(op, "-ef") == 0)
                    r = ok1 && ok2 && s1.st_dev == s2.st_dev &&
                        s1.st_ino == s2.st_ino;
                else if (strcmp(op, "-nt") == 0)
                    r = ok1 && ok2 && s1.st_mtime > s2.st_mtime;
                else
                    r = ok1 && ok2 && s1.st_mtime < s2.st_mtime;
            }
            else r = db_num_cmp(lexp, rexp, op);

            free(lexp); free(rexp);
            return r;
        }
    }
    int r = (*lexp != '\0');
    free(lexp);
    return r;
}

static int db_not(DBTok *t) {
    if (db_is_op(t, "!", NULL, NULL)) { db_next(t); return !db_not(t); }
    return db_primary(t);
}

static int db_and(DBTok *t) {
    int v = db_not(t);
    while (db_is_op(t, "&&", "-a", NULL)) { db_next(t); int r = db_not(t); v = (v && r); }
    return v;
}

static int db_expr(DBTok *t) {
    int v = db_and(t);
    while (db_is_op(t, "||", "-o", NULL)) { db_next(t); int r = db_and(t); v = (v || r); }
    return v;
}

/* Split the raw `[[` body into tokens, removing quotes and honouring
 * backslash escapes.  Spaces separate tokens; quoting glues them.
 * Tokens that were quoted on the right-hand side of == / != must be
 * matched literally, so each token also records whether it was quoted. */
static int db_tokenize(const char *s, char ***out, int **quoted_out) {
    int cap = 16, n = 0;
    char **v = sh_malloc((size_t)cap * sizeof(char *));
    int  *qv = sh_malloc((size_t)cap * sizeof(int));
    int i = 0;
    while (s[i]) {
        while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n') i++;
        if (!s[i]) break;
        char *buf = sh_malloc(strlen(s) + 1);
        int bl = 0;
        int was_quoted = 0;
        /* multi-character operators are handled by the quote-aware scan
         * below; a run of operator chars becomes ONE token so that `==`,
         * `!=`, `=~`, `&&`, `||` survive intact. */
        while (s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != '\n') {
            char c = s[i];
            if (c == '\\' && s[i + 1]) {
                buf[bl++] = s[i + 1]; i += 2; was_quoted = 1; continue;
            }
            if (c == '\'' || c == '"') {
                char q = c;
                was_quoted = 1;
                i++;
                while (s[i] && s[i] != q) {
                    if (q == '"' && s[i] == '\\' && s[i + 1]) {
                        buf[bl++] = s[i + 1]; i += 2; continue;
                    }
                    buf[bl++] = s[i++];
                }
                if (s[i] == q) i++;
                continue;
            }
            buf[bl++] = s[i++];
        }
        buf[bl] = '\0';
        if (n >= cap) {
            cap *= 2;
            v  = sh_realloc(v,  (size_t)cap * sizeof(char *));
            qv = sh_realloc(qv, (size_t)cap * sizeof(int));
        }
        v[n] = buf;
        qv[n] = was_quoted;
        n++;
    }
    v[n] = NULL;
    *out = v;
    if (quoted_out) *quoted_out = qv;
    return n;
}

/* Evaluate a `[[ ... ]]` condition whose raw body is `cond`.
 * Returns 0 (true) / 1 (false) to match builtin return convention. */
static int db_evaluate(const char *cond) {
    char **toks = NULL;
    int   *tq   = NULL;
    int n = db_tokenize(cond, &toks, &tq);
    if (n == 0) { free(toks); free(tq); return 1; }   /* `[[ ]]` is false */
    DBTok t = { toks, tq, n, 0 };
    int r = db_expr(&t);
    /* leftover tokens mean a syntax error: bash reports and returns 2 */
    int leftover = (t.pos < t.n);
    for (int i = 0; i < n; i++) free(toks[i]);
    free(toks);
    free(tq);
    if (leftover) {
        fprintf(stderr, "besh: [[: syntax error near `%s'\n", cond);
        return 2;
    }
    return r ? 0 : 1;
}

int builtin_test(int argc, char **argv) {
    /* `[[ ... ]]`: the lexer passes the whole condition as one word
     * prefixed with \001 (see lex_double_bracket). */
    if (argc >= 1 && argv[0] && argv[0][0] == '\001') {
        /* the condition may still have been split if it was expanded,
         * so re-join any trailing argv entries defensively */
        return db_evaluate(argv[0] + 1);
    }

    /* `[` is `test` with a trailing `]` and otherwise identical semantics.
     * Both forms have the command name in argv[0], so drop exactly one
     * leading word in each case — the old code only did this for `[` and
     * therefore mis-counted every `test a -op b` invocation (argc 4 was
     * never handled and fell through to the syntax-error return). */
    int is_bracket = (strcmp(argv[0], "[") == 0);
    int effective_argc = argc - 1;
    char **effective_argv = argv + 1;

    if (is_bracket) {
        if (effective_argc > 0 &&
            strcmp(effective_argv[effective_argc - 1], "]") != 0) {
            fprintf(stderr, "besh: [: missing `]'\n");
            return 2;
        }
        effective_argc--;            /* discard the closing `]` */
        if (effective_argc < 0) effective_argc = 0;
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
 *  shift [n]  — shift positional parameters ($1..$n → $1..)
 * ================================================================ */
int builtin_shift(int argc, char **argv) {
    Shell *sh = shell_get();
    int n = 1;
    if (argc > 1) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*end != '\0' || v < 0) {
            fprintf(stderr, "besh: shift: %s: numeric argument required\n", argv[1]);
            return 1;
        }
        n = (int)v;
    }
    if (n > sh->npositional) n = sh->npositional;

    for (int i = 0; i < n; i++)
        free(sh->positional[i]);
    memmove(sh->positional, sh->positional + n,
            (sh->npositional - n) * sizeof(char *));
    sh->npositional -= n;
    if (sh->npositional == 0) {
        free(sh->positional);
        sh->positional = NULL;
    }
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
        const char *t = argv[1];
        if (strcmp(t, "cd") == 0)
            printf("cd: cd [dir]\n    Change the current directory to DIR.\n");
        else if (strcmp(t, "echo") == 0)
            printf("echo: echo [-neE] [arg ...]\n    Print arguments to stdout.\n");
        else if (strcmp(t, "fc") == 0)
            printf("fc: fc [-l] [-n] [-r] [-s [old=new] [cmd]] [-e [editor] [first] [last]]\n"
                   "    -l list history entries        -s re-execute an entry\n"
                   "    -e edit the selection then run it\n"
                   "    Editor comes from the -e option, then $FCEDIT, then $EDITOR, then vi.\n");
        else if (strcmp(t, "history") == 0)
            printf("history: history [-c] [-d pos] [-a] [-r] [-w] [-N]\n"
                   "    -c clear   -d pos delete entry at 1-based position pos\n"
                   "    -a append  -r read   -w write   -N show the last N entries\n"
                   "    Set $HISTTIMEFORMAT to render timestamps (e.g. \"%%F %%T \").\n");
        else if (strcmp(t, "complete") == 0)
            printf("complete: complete [-W wordlist] [-F func] [-A action] [name ...]\n"
                   "    complete -p              list registered rules\n"
                   "    complete -r [name ...]   remove one (or all) rules\n"
                   "    A -F function may print candidates one per line, or set\n"
                   "    COMPREPLY.  Tab in the line editor consults these rules\n"
                   "    before falling back to filename completion.\n");
        else if (strcmp(t, "compgen") == 0)
            printf("compgen: compgen [-W wordlist] [-F func] [-A action] [word]\n"
                   "    -A command|builtin|function|alias|variable|keyword|file|directory\n"
                   "    Prints one match per line; exits 1 when there are none.\n");
        else if (strcmp(t, "declare") == 0 || strcmp(t, "typeset") == 0)
            printf("%s: %s [-a|-A] [-r] [-x] [name[=value] ...]\n"
                   "    Declare variables.  -a array, -A associative, -r readonly,\n"
                   "    -x export, -p print declarations, -f functions.\n", t, t);
        else if (strcmp(t, "local") == 0)
            printf("local: local [name[=value] ...]\n"
                   "    Declare variables scoped to the enclosing function.\n");
        else if (strcmp(t, "set") == 0)
            printf("set: set [-eufxvaC] [--] [arg ...]\n"
                   "    -e errexit  -u nounset  -f noglob  -x xtrace\n"
                   "    -v verbose  -a allexport  -C noclobber\n"
                   "    set -- a b c replaces the positional parameters.\n");
        else
            printf("besh: help: no help for %s\n", t);
    } else {
        /* Generate the list straight from the registry so it can never
         * drift out of sync when a builtin is added or removed. */
        int n = builtin_count();
        printf("besh built-in commands (%d):\n", n);
        int col = 0;
        for (int i = 0; i < n; i++) {
            const char *nm = builtin_name(i);
            printf("  %-9s", nm);
            if (++col % 7 == 0) printf("\n");
        }
        if (col % 7 != 0) printf("\n");
        printf("Type 'help name' for more info.\n");
        printf("\nfish/zsh features: autosuggestions (right-arrow/Tab),\n");
        printf("  syntax highlighting, abbr, Ctrl-R reverse search,\n");
        printf("  autocd, globstar '**', brace {a,b} expansion, dirs stack,\n");
        printf("  process substitution <(...) / >(...), { ...; } groups,\n");
        printf("  compound redirections, fd duplication (>&N), and the\n");
        printf("  zsh-style command_not_found_handler hook.\n");
        printf("  Programmable completion: complete / compgen (+ Tab).\n");
        printf("  History: fc -l / -s / -e, timestamps via HISTTIMEFORMAT.\n");
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
    sh->return_request = 1;   /* unwind the enclosing function / sourced file */
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
    {"fc",      builtin_fc},
    {"compgen", builtin_compgen},
    {"complete",builtin_complete},
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
    {"abbr",    builtin_abbr},
    {"pushd",   builtin_pushd},
    {"popd",    builtin_popd},
    {"dirs",    builtin_dirs},
    {"setopt",  builtin_setopt},
    {"unsetopt",builtin_unsetopt},
    {"readonly",builtin_readonly},
    {"declare", builtin_declare},
    {"typeset", builtin_typeset},
    {"local",   builtin_local},
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

/* number of builtins / name of the i-th builtin (for compgen -A builtin) */
int builtin_count(void) {
    int n = 0;
    while (builtins[n].name) n++;
    return n;
}

const char *builtin_name(int i) {
    if (i < 0 || i >= builtin_count()) return NULL;
    return builtins[i].name;
}
