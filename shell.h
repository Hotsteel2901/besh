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

/* -------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------- */
#define MAX_ARGS        2048
#define MAX_PATH        4096
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
    TOK_BOTHREDIR,        /* &>  or  >&  (stdout+stderr)       */
    TOK_LPAREN,           /* (                                  */
    TOK_RPAREN,           /* )                                  */
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
} Var;

/* -------------------------------------------------------------------
 *  Shell function
 * ------------------------------------------------------------------- */
typedef struct Function {
    char    *name;
    ASTNode *body;
} Function;

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
    char         prompt[256];
    int          linenum;

    /* options (set -o / +o, shopt) */
    int          opt_noclobber;  /* >| needed to overwrite        */
    int          opt_allexport;  /* auto-export all vars          */
    int          opt_xtrace;     /* set -x: print commands        */
    int          opt_verbose;    /* set -v: print input           */
    int          opt_noglob;     /* set -f: disable globbing      */

    /* line-editor state */
    char        *line_buf;
    int          line_len;
    int          line_cap;
    int          line_pos;       /* cursor position in buffer     */

    /* positional parameters ($1, $2, ...) */
    char       **positional;
    int          npositional;

    /* loop control: break/continue signaling */
    int          break_request;     /* 1 = break innermost loop */
    int          continue_request;  /* 1 = continue innermost loop */
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

typedef int (*builtin_fn)(int, char **);
builtin_fn builtin_lookup(const char *name);
int        builtin_is(const char *name);

/* -------------------------------------------------------------------
 *  expand.c
 * ------------------------------------------------------------------- */
char  *expand_string(const char *str);
char **expand_words(char **words, int *count);
char  *tilde_expand(const char *str);
char **glob_expand(const char *pattern, int *count);
char  *var_expand(const char *name);

/* -------------------------------------------------------------------
 *  signal.c  (parts in main.c)
 * ------------------------------------------------------------------- */
void signals_setup(void);
void signals_restore(void);
void signals_block(void);
void signals_unblock(void);

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
char  *sh_trim(char *s);
int    sh_is_whitespace(int c);
int    sh_is_special_char(int c);
char  *sh_getenv(const char *name);
void   sh_setenv(const char *name, const char *value, int export);
void   sh_unsetenv(const char *name);
char  *resolve_path(const char *cmd);
void   history_save(void);
void   history_load(void);

#endif /* SHELL_H */
