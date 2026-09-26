#ifndef SHELL_H
#define SHELL_H

/* ================================================================
 *  besh — a bash-compatible shell written in C
 *  Full-featured Unix shell with job control, line editing,
 *  pipelines, redirections, expansion, and built-in commands.
 * ================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <termios.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <fnmatch.h>
#include <pwd.h>
#include <setjmp.h>
#include <sys/times.h>
#include <time.h>
#include <stddef.h>

/* -------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------- */
#define MAX_ARGS        2048
#define MAX_PATH        4096
#define MAX_PSUB        128   /* pending process-substitution children */
#define MAX_LINE        65536
#define MAX_HISTORY     2000
#define MAX_JOBS         512
#define MAX_ALIASES      512
#define MAX_VARS        1024
#define MAX_FUNCTIONS    256
#define HEREDOC_BUF     4096
#define TAB_WIDTH          8

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX    256
#endif

/* -------------------------------------------------------------------
 *  Token types — produced by the lexer
 * ------------------------------------------------------------------- */
typedef enum {
    TOK_WORD = 256,       /* plain word / identifier           */
    TOK_PIPE,             /* |                                  */
    TOK_AND,              /* &&                                 */
    TOK_OR,               /* ||                                 */
    TOK_SEMI,             /* ;                                  */
    TOK_DSEMI,            /* ;; (case separator)               */
    TOK_BG,               /* & (background)                     */
    TOK_LREDIR,           /* <                                  */
    TOK_RREDIR,           /* >                                  */
    TOK_APPEND,           /* >>                                 */
    TOK_RREDIR2,          /* >|  (force overwrite, noclobber)  */
    TOK_ERRREDIR,         /* 2>                                 */
    TOK_ERRAPPEND,        /* 2>>                                */
    TOK_ERRDUP,           /* 2>&  (dup fd onto stderr)          */
    TOK_BOTHREDIR,        /* &>  or  >&  (stdout+stderr)       */
    TOK_LPAREN,           /* (                                  */
    TOK_RPAREN,           /* )                                  */
    TOK_LBRACE,           /* {  (brace group)                   */
    TOK_RBRACE,           /* }                                  */
    TOK_DLESS,            /* <<  (here-document)                */
    TOK_DLESSDASH,        /* <<- (here-doc strip leading tabs)  */
    TOK_NEWLINE,          /* logical newline                    */
    TOK_EOF,              /* end of input                       */
} TokenType;

/* -------------------------------------------------------------------
 *  AST node types
 * ------------------------------------------------------------------- */
typedef enum {
    NODE_COMMAND   = 1,   /* simple command: argv + redirs      */
    NODE_PIPELINE,        /* left | right                       */
    NODE_LIST,            /* left ; right                       */
    NODE_AND,             /* left && right                      */
    NODE_OR,              /* left || right                      */
    NODE_BG,              /* child &                            */
    NODE_SUBSHELL,        /* ( child )                          */
    NODE_BRACEGROUP,      /* { child; }  (same shell)           */
    NODE_NOT,             /* ! child                            */
    NODE_FUNCDEF,         /* name() { body; }                   */
    NODE_FOR,             /* for var in list; do body; done     */
    NODE_WHILE,           /* while cond; do body; done          */
    NODE_IF,              /* if cond; then body; [elif...] fi   */
    NODE_CASE,            /* case word in pat) body;; ... esac  */
    NODE_REDIRECT,        /* child with redirections            */
} NodeType;

/* -------------------------------------------------------------------
 *  Redirection descriptor
 * ------------------------------------------------------------------- */
typedef enum {
    REDIR_IN       = 0,   /* < filename                        */
    REDIR_OUT,            /* > filename                        */
    REDIR_APPEND,         /* >> filename                       */
    REDIR_ERR,            /* 2> filename                       */
    REDIR_ERRAPPEND,      /* 2>> filename                      */
    REDIR_BOTH,           /* &> filename                       */
    REDIR_HEREDOC,        /* << DELIM                          */
    REDIR_HEREDOC_DASH,   /* <<- DELIM                         */
    REDIR_DUPIN,          /* <&n   (dup fd n to stdin)         */
    REDIR_DUPOUT,         /* >&n   (dup fd n to stdout)        */
    REDIR_CLOSE,          /* >&- / <&- (close the fd)          */
    REDIR_CLOBBER,        /* >|    (force overwrite)           */
} RedirType;

typedef struct Redir {
    RedirType     type;
    char         *filename;     /* file name or heredoc delim   */
    char         *heredoc;      /* content for here-document    */
    int            fd;          /* fd number for dup redirs     */
    int            src_fd;      /* source fd (stdin/stdout/err) */
    int            quoted;      /* 1 if heredoc delim was quoted */
    struct Redir *next;
} Redir;

/* -------------------------------------------------------------------
 *  AST node
 * ------------------------------------------------------------------- */
typedef struct ASTNode {
    NodeType       type;

    /* NODE_COMMAND */
    char         **argv;
    int           *argv_quoted;   /* parallel to argv: 1 if word was quoted */
    int            argc;
    int            argv_cap;
    Redir         *redirs;

    /* binary nodes (PIPELINE, LIST, AND, OR) */
    struct ASTNode *left;
    struct ASTNode *right;

    /* NODE_FUNCDEF */
    char          *func_name;
    struct ASTNode *func_body;

    /* NODE_FOR / NODE_WHILE / NODE_IF */
    struct ASTNode *cond;         /* for-list, while-cond, if-cond  */
    struct ASTNode *body;         /* do-body, then-body             */
    struct ASTNode *else_body;    /* elif / else body (IF only)     */

    /* NODE_CASE */
    char          *case_word;
    char         **case_patterns;
    struct ASTNode **case_bodies;
    int            case_count;

    /* source location for error messages */
    int            lineno;
} ASTNode;

/* -------------------------------------------------------------------
 *  Job / background-process tracking
 * ------------------------------------------------------------------- */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE,
} JobStatus;

typedef struct Job {
    int         id;           /* job number (%1, %2, …)          */
    pid_t       pgid;         /* process group id                 */
    pid_t      *pids;         /* all pids in this pipeline        */
    int         npids;
    char       *command;      /* original command line            */
    JobStatus   status;
    struct Job *next;
} Job;

/* -------------------------------------------------------------------
 *  Alias entry
 * ------------------------------------------------------------------- */
typedef struct Alias {
    char *name;
    char *value;
} Alias;

/* -------------------------------------------------------------------
 *  Shell variable entry
 * ------------------------------------------------------------------- */
typedef struct Var {
    char *name;
    char *value;
    int   exported;     /* 1 = in environment for children */
    int   readonly;
    int   array;        /* 1 = variable has the array attribute */
    int   is_element;   /* 1 = name is of the form base[index]  */
    long  index;        /* element index when is_element        */
} Var;

/* Snapshot of one variable entry saved by a function-local scope */
typedef struct SavedVar {
    char *name;
    char *value;        /* NULL if the variable did not exist   */
    int   existed;
    int   exported;
    int   readonly;
    int   array;
    int   is_element;
    long  index;
} SavedVar;

/* A function-call frame: saved variables restored on return */
typedef struct ScopeFrame {
    SavedVar *saved;
    int       nsaved;
    int       saved_cap;
    char    **locals;   /* base names already declared local    */
    int       nlocals;
    int       locals_cap;
} ScopeFrame;

/* -------------------------------------------------------------------
 *  Shell function
 * ------------------------------------------------------------------- */
typedef struct Function {
    char    *name;
    ASTNode *body;
} Function;

/* -------------------------------------------------------------------
 *  Programmable completion spec  (complete builtin)
 *  Stored as a linked list on the Shell — never a fixed global array.
 * ------------------------------------------------------------------- */
typedef struct CompSpec {
    char            *name;      /* command name the rule applies to   */
    char            *wordlist;  /* -W 'list'                          */
    char            *func;      /* -F funcname                        */
    char            *action;    /* -A type (command/builtin/file/...) */
    struct CompSpec *next;
} CompSpec;

/* -------------------------------------------------------------------
 *  Global shell state
 * ------------------------------------------------------------------- */
typedef struct Shell {
    /* environment & shell variables */
    Var         *vars;
    int          nvars;
    int          vars_cap;

    /* aliases */
    Alias       *aliases;
    int          naliases;
    int          aliases_cap;

    /* abbreviations (fish-style, expand on space/enter) */
    Alias       *abbrs;
    int          nabbrs;
    int          abbrs_cap;

    /* directory stack (zsh-style pushd/popd/dirs) */
    char       **dirs;
    int          ndirs;
    int          dirs_cap;

    /* functions */
    Function    *funcs;
    int          nfuncs;
    int          funcs_cap;

    /* history */
    char       **history;
    int          nhist;
    int          hist_cap;
    int          hist_pos;      /* cursor in history browse      */
    char        *hist_file;
    long        *hist_time;     /* parallel epoch-second stamps  */
    int          hist_time_cap;
    int          hist_written;  /* entries already written to file */

    /* programmable completion rules */
    CompSpec    *compspecs;

    /* job control */
    Job         *jobs;
    int          njob;
    pid_t        shell_pgid;
    int          job_interactive;

    /* terminal */
    int          term_fd;
    struct termios orig_termios;
    struct termios shell_termios;

    /* state */
    int          exit_status;   /* $?                             */
    int          running;
    char         cwd[MAX_PATH];
    char         prompt[8192];
    int          linenum;

    /* options (set -o / +o, shopt, setopt) */
    int          opt_noclobber;  /* >| needed to overwrite        */
    int          opt_allexport;  /* auto-export all vars          */
    int          opt_xtrace;     /* set -x: print commands        */
    int          opt_verbose;    /* set -v: print input           */
    int          opt_noglob;     /* set -f: disable globbing      */
    int          opt_autocd;     /* zsh/fish: type a dir to cd    */
    int          opt_globstar;   /* zsh: '**' recursive glob      */
    int          opt_autosuggest;/* fish: history autosuggestion  */
    int          opt_syntaxhighlight; /* fish: colored input      */
    int          opt_histignoredups;  /* skip duplicate history   */
    int          opt_errexit;    /* set -e: exit on failed command */
    int          opt_nounset;    /* set -u: error on unset variable */
    int          opt_pipefail;   /* set -o pipefail: pipeline status */
    int          in_condition;   /* >0 while evaluating if/while/until
                                  * conditions and &&/|| operands, where
                                  * errexit must stay suppressed */
    int          exit_request;   /* set by `set -e`; REPL unwinds  */
    int          last_exempt;    /* last executed node was exempt from
                                  * errexit (&& / || / ! ) — lets an
                                  * enclosing NODE_LIST stay quiet */

    /* process substitution: pids of the children spawned for <(...) /
     * >(...).  They are reaped like background jobs so they do not turn
     * into zombies, and `wait` can be used to synchronise with them. */
    int          psub_pids[MAX_PSUB];
    int          npsub;

    /* line-editor state */
    char        *line_buf;
    int          line_len;
    int          line_cap;
    int          line_pos;       /* cursor position in buffer     */
    char        *hist_search;    /* prefix for up-arrow search     */
    char        *suggestion;     /* autosuggestion suffix (0=none) */
    int          line_interrupted; /* Ctrl-C aborted the current line */
    int          term_in_raw;    /* 1 while the tty is in raw mode */

    /* positional parameters ($1, $2, ...) */
    char       **positional;
    int          npositional;

    /* loop control: break/continue signaling */
    int          break_request;     /* 1 = break innermost loop */
    int          continue_request;  /* 1 = continue innermost loop */

    /* return signaling (function body / sourced file) */
    int          return_request;    /* 1 = unwind current function */

    /* function-call scope stack (local variables) */
    ScopeFrame  *scopes;
    int          nscopes;
    int          scopes_cap;
} Shell;

/* -------------------------------------------------------------------
 *  Globa state accessor
 * ------------------------------------------------------------------- */
Shell *shell_get(void);
void   shell_init(void);
void   shell_destroy(void);

/* -------------------------------------------------------------------
 *  lexer.c
 * ------------------------------------------------------------------- */
typedef struct {
    const char *input;
    int         pos;
    int         len;
    int         lineno;
    int         token_type;
    char       *token_text;
    int         token_quoted;    /* 1 if token came from quotes   */
    int         token_fd;        /* fd number for `N>`/`N<` tokens,
                                  * -1 when the token carried none  */
} Lexer;

Lexer *lexer_new(const char *input);
void   lexer_free(Lexer *l);
int    lexer_next(Lexer *l);     /* returns token type            */
char  *lexer_heredoc(Lexer *l, const char *delim, int strip_tabs);

/* -------------------------------------------------------------------
 *  parser.c
 * ------------------------------------------------------------------- */
ASTNode *parse_complete(Lexer *l);
void     ast_free(ASTNode *node);
void     ast_print(ASTNode *node, int indent);

/* -------------------------------------------------------------------
 *  executor.c
 * ------------------------------------------------------------------- */
int  execute_node(ASTNode *node, int *piped_fds);
int  execute_pipeline(ASTNode *pipeline);
int  execute_command(ASTNode *node);
int  execute_string(const char *cmd);

/* -------------------------------------------------------------------
 *  builtins.c
 * ------------------------------------------------------------------- */
int  builtin_cd(int argc, char **argv);
int  builtin_echo(int argc, char **argv);
int  builtin_export(int argc, char **argv);
int  builtin_unset(int argc, char **argv);
int  builtin_alias(int argc, char **argv);
int  builtin_unalias(int argc, char **argv);
int  builtin_source(int argc, char **argv);
int  builtin_exit(int argc, char **argv);
int  builtin_pwd(int argc, char **argv);
int  builtin_type(int argc, char **argv);
int  builtin_jobs(int argc, char **argv);
int  builtin_fg(int argc, char **argv);
int  builtin_bg(int argc, char **argv);
int  builtin_history(int argc, char **argv);
int  builtin_fc(int argc, char **argv);
int  builtin_compgen(int argc, char **argv);
int  builtin_complete(int argc, char **argv);
int  builtin_set(int argc, char **argv);
int  builtin_read(int argc, char **argv);
int  builtin_test(int argc, char **argv);
int  builtin_true(int argc, char **argv);
int  builtin_false(int argc, char **argv);
int  builtin_exec(int argc, char **argv);
int  builtin_wait(int argc, char **argv);
int  builtin_shift(int argc, char **argv);
int  builtin_times(int argc, char **argv);
int  builtin_trap(int argc, char **argv);
int  builtin_umask(int argc, char **argv);
int  builtin_abbr(int argc, char **argv);
int  builtin_pushd(int argc, char **argv);
int  builtin_popd(int argc, char **argv);
int  builtin_dirs(int argc, char **argv);
int  builtin_setopt(int argc, char **argv);
int  builtin_unsetopt(int argc, char **argv);
int  builtin_readonly(int argc, char **argv);
int  builtin_declare(int argc, char **argv);
int  builtin_local(int argc, char **argv);
int  builtin_typeset(int argc, char **argv);

typedef int (*builtin_fn)(int, char **);
builtin_fn builtin_lookup(const char *name);
int        builtin_is(const char *name);

/* --- cd / directory stack helpers --- */
int  cd_to(const char *dir);
void dirs_push(const char *dir);
void dirs_print(int verbose);
void abbr_add(const char *name, const char *value);
char *abbr_find(const char *name);
int  abbr_erase(const char *name);

/* -------------------------------------------------------------------
 *  expand.c
 * ------------------------------------------------------------------- */
char  *expand_string(const char *str);
/* expand without word splitting / brace / pathname expansion —
 * the result is exactly one word (used by [[ ]], case, assignments) */
char  *expand_string_no_split(const char *str);
char **expand_words(char **words, int *count);
char **expand_words_q(char **words, int *quoted, int *count);
/* fork the inner list of a `<(...)` / `>(...)` word and return a
 * /dev/fd/N pathname; *pid receives the child pid */
char *expand_process_sub(const char *word, int *pid);
char  *unescape_word(const char *str);
char  *unescape_token(const char *str);
char  *tilde_expand(const char *str);
char **glob_expand(const char *pattern, int *count);
char **brace_expand(const char *str, int *count);
char  *var_expand(const char *name);
/* glob-style pattern match used by ${var#pat}, case, [[ a == pat ]] */
int    sh_pattern_match(const char *str, const char *pattern);

/* -------------------------------------------------------------------
 *  signal.c  (parts in main.c)
 * ------------------------------------------------------------------- */
void signals_setup(void);
void signals_restore(void);
void signals_block(void);
void signals_unblock(void);
void sigchld_block(void);
void sigchld_unblock(void);

/* -------------------------------------------------------------------
 *  job control (in executor.c)
 * ------------------------------------------------------------------- */
void  job_add(pid_t pgid, pid_t *pids, int npids, const char *cmd);
void  job_update(pid_t pid, int status);
void  job_remove(pid_t pgid);
Job  *job_find_by_pgid(pid_t pgid);
Job  *job_find_by_id(int id);
void  job_print(Job *j);
void  job_cleanup(void);
void  job_notify(void);

/* -------------------------------------------------------------------
 *  utility helpers
 * ------------------------------------------------------------------- */
char  *sh_strdup(const char *s);
void  *sh_malloc(size_t n);
void  *sh_realloc(void *p, size_t n);
char  *sh_strndup(const char *s, size_t n);
/* sh_trim() trims leading/trailing blanks IN PLACE and returns a pointer
 * to the first non-blank character — i.e. an INTERNAL pointer into `s`
 * (or `s + k`).  The result must NEVER be passed to free(); free the
 * original buffer instead. */
char  *sh_trim(char *s);
int    sh_is_whitespace(int c);
int    sh_is_special_char(int c);
char  *sh_getenv(const char *name);
void   sh_setenv(const char *name, const char *value, int export);
void   sh_unsetenv(const char *name);
/* ---- array variables (indexed) -------------------------------- */
/* The base name and its elements are separate Var entries: element k of
 * `a` is stored under the literal name `a[k]`, while a scalar mirror `a`
 * always holds element 0 (so `$a` == `${a[0]}`). */
int    var_base_len(const char *name);          /* len up to '[' or all */
char  *var_base_of(const char *name);            /* malloc'd base name  */
int    var_parse_subscript(const char *name, char **base_out, long *idx,
                           int *star);            /* name / name[i] / name[@] */
int    var_is_array(const char *base);
void   var_set_array_attr(const char *base, int on);
void   var_array_set(const char *base, long idx, const char *value);
char  *var_array_get(const char *base, long idx, int *is_set);
int    var_array_unset(const char *base, long idx);
void   var_array_clear(const char *base);        /* elements + scalar   */
char **var_array_values(const char *base, int *n);
long  *var_array_indices(const char *base, int *n);
int    var_array_count(const char *base);
void   var_free_list(char **v, int n);
/* assign to a name that may carry an array subscript (no word splitting) */
void   sh_assign(const char *name, const char *value, int export_flag,
                 int append);
/* ---- function-local scopes ------------------------------------ */
void   scope_push(void);
void   scope_pop(void);
void   scope_declare(const char *name);          /* save current value  */
int    scope_in_function(void);
char  *resolve_path(const char *cmd);
void   history_save(void);
void   history_load(void);

/* Returns 0 when `input` is a complete command, otherwise a positive
 * value telling the caller how to join the next physical line:
 *   1 = join with '\n'          (trailing \ | && || do then { ( …)
 *   2 = join with NO separator  (unterminated quote — see note in main.c)
 * Used by the interactive REPL (multi-line editing) and by `source`. */
int    sh_input_incomplete(const char *input);
/* Read a whole script/stream from `f` and execute it, accumulating
 * physical lines until each logical command is complete (so multi-line
 * if/for/while and function definitions work).  Used by the script-file
 * entry point, `source` and ~/.beshrc.  Returns the last status. */
int    sh_run_stream(FILE *f);

/* --- history helpers (implemented in main.c) --- */
void history_add(const char *line);      /* add (respect dups/HISTSIZE)  */
void history_clear(void);                /* history -c                   */
int  history_delete(int idx);            /* history -d (0-based index)    */
void history_append(void);               /* history -a                   */
void history_read(void);                 /* history -r                   */
void history_write(void);                /* history -w                   */

/* --- programmable completion (implemented in builtins.c) --- */
CompSpec   *compspec_find(const char *name);
int         compspec_add(const char *name, const char *wordlist,
                         const char *func, const char *action);
int         compspec_remove(const char *name);
void        compspec_free_all(void);
int         compgen_generate(const char *action, const char *wordlist,
                             const char *func, const char *prefix,
                             char ***out, int *n);
void        compgen_free(char **matches, int n);
int         builtin_count(void);
const char *builtin_name(int i);

#endif /* SHELL_H */
