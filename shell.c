/*
    Immortal Database Access (iDA) EUROSTARS project

    ROAE shell
    A shell to interface with the Read-Only Access Engine (ROAE)

    Authors:
    Sergio Romero, Eladio Gutierrez, Oscar Plata
    University of Malaga, Spain
  
    Aug, 2023
*/

#define ROAESHELL_VERSION "v1.0 (2024112900)"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define MAX_LINE 4096

// Some extra flags. See newlib/libc/include/sys/_default_fcntl.h
#ifndef O_PATH
#define O_PATH  0x2000000
#endif
#ifndef O_TMPFILE
    #ifdef __ivm64__
    // See newlib/libc/include/sys/_default_fcntl.h
    #define O_TMPFILE   0x800000
    #else
    // See: /usr/include/x86_64-linux-gnu/bits/fcntl-linux.h
    #ifndef __O_TMPFILE
    #define __O_TMPFILE 020000000
    #endif
    #define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
    #endif
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 16
#endif

// MAX/MIN only one evaluation
#define MIN(a,b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); (_a < _b)?_a:_b;})
#define MAX(a,b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); (_a > _b)?_a:_b;})
#define SWAP(a,b) ({__typeof__(a) tmp=a; a=b; b=tmp; })

// flags for redirections
#define R_NO_REDIR 0x00
#define R_FILE_IN  0x04
#define R_FILE_OUT 0x05
#define R_FILE_ERR 0x06
#define R_RAWINPUT 0x07
#define R_APPEND   0x08
#define R_EOFINPUT 0x10
#define R_FUNC_ARG 0x20
#define REDIR_MASK 0x03
#define REDIR_BIT  0x04


////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////
static int main_argv(int argc, char *argv[]);
static int main_bn(int argc, char *argv[]);
static int main_cat(int argc, char *argv[]);
static int main_cd(int argc, char *argv[]);
static int main_chmod(int argc, char *argv[]);
static int main_close(int argc, char *argv[]);
static int main_cmp(int argc, char *argv[]);
static int main_cp(int argc, char *argv[]);
static int main_crc32(int argc, char *argv[]);
static int main_dd(int argc, char *argv[]);
static int main_dir(int argc, char *argv[]);
static int main_dn(int argc, char *argv[]);
static int main_du(int argc, char *argv[]);
static int main_dup(int argc, char *argv[]);
static int main_dup2(int argc, char *argv[]);
static int main_echo(int argc, char *argv[]);
static int main_env(int argc, char *argv[]);
static int main_export(int argc, char *argv[]);
static int main_fchmod(int argc, char *argv[]);
static int main_fchmodat(int argc, char *argv[]);
static int main_fcmp(int argc, char *argv[]);
static int main_ftruncate(int argc, char *argv[]);
static int main_glob(int argc, char *argv[]);
static int main_help(int argc, char *argv[]);
static int main_hexdump(int argc, char *argv[]);
static int main_ioctl(int argc, char *argv[]);
static int main_linkat(int argc, char *argv[]);
static int main_ln(int argc, char *argv[]);
static int main_ls(int argc, char *argv[]);
static int main_lsreel(int argc, char *argv[]);
static int main_lseek(int argc, char *argv[]);
static int main_lsof(int argc, char *argv[]);
static int main_meminfo(int argc, char *argv[]);
static int main_mkdir(int argc, char *argv[]);
static int main_mv(int argc, char *argv[]);
static int main_open(int argc, char *argv[]);
static int main_openat(int argc, char *argv[]);
static int main_pwd(int argc, char *argv[]);
static int main_read(int argc, char *argv[]);
static int main_readlink(int argc, char *argv[]);
static int main_realpath(int argc, char *argv[]);
static int main_rename(int argc, char *argv[]);
static int main_renameat(int argc, char *argv[]);
static int main_rm(int argc, char *argv[]);
static int main_rmdir(int argc, char *argv[]);
static int main_seekdir(int argc, char *argv[]);
static int main_set(int argc, char *argv[]);
static int main_stat(int argc, char *argv[]);
static int main_stty(int argc, char *argv[]);
static int main_tee(int argc, char *argv[]);
static int main_touch(int argc, char *argv[]);
static int main_tree(int argc, char *argv[]);
static int main_truncate(int argc, char *argv[]);
static int main_type(int argc, char *argv[]);
static int main_unset(int argc, char *argv[]);
static int main_umask(int argc, char *argv[]);
static int main_wc(int argc, char *argv[]);
static int main_write(int argc, char *argv[]);
static int writechars(int argc, char *argv[]);

static int main_spawn(int argc, char *argv[]);
static int main_source(int argc, char *argv[]);

// roae shell commands
static int main_roae(int argc, char *argv[]);
static int main_siard(int argc, char *argv[]);
static int main_sqlite(int argc, char *argv[]);
static int main_unzip(int argc, char *argv[]);
// end roae shell cmds

extern int main_find(int argc, char *argv[]);
extern int main_grep(int argc, char *argv[]);

#ifdef __ivm64__
extern int ivm_spawn(int argc, char *argv[]);
#endif

static void sqlite_shell_init();
// End of prototypes (extern or placed at the end of the current file
////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////
// shell types, variables and supporting functions
////////////////////////////////////////////////////////////////////////////////

char cwd[PATH_MAX];	// current working directory

typedef enum {
    COND_QUIT   = -1,
    COND_ENDL   = 0,
    COND_SEQ,
    COND_BACK,
    COND_OR,
    COND_AND,
    COND_PIPE,
} cond_t;

// Arguments
typedef struct argument {
    int argc;
    char **argv;
} argument_t;

// input argument is a struct (not a pointer)
// result is a struct (not a pointer)
static argument_t argument_copy(argument_t arg)
{
    int argc  = arg.argc;
    char **argv = (char **)malloc((argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        argv[i] = strdup(arg.argv[i]);
    }
    argv[argc] = NULL;
    return (argument_t) { argc:argc, argv:argv };
}
// End of arguments


// Redirection
typedef struct {
    char* value;
    int   type;
} redir_t;
// input argument is a struct (not a pointer)
// result is a struct (not a pointer)
static redir_t redir_copy(redir_t orig)
{
    return (redir_t) { value:strdup(orig.value), type:orig.type };
}
// End of redirection


// Redirection vector
typedef struct {
    int count;
    redir_t* redir;
} redir_vect_t;
// input argument is a struct (not a pointer)
// result is a struct (not a pointer)
static redir_vect_t redir_vect_copy(redir_vect_t orig)
{
    int count = orig.count;
    if (!count) return (redir_vect_t) { 0, NULL };
    redir_t *vect = (redir_t*)malloc(count * sizeof(redir_t));
    for (int i = 0; i < count; i++) {
        vect[i] = redir_copy(orig.redir[i]);
    }
    return (redir_vect_t) { count:count, redir:vect };
}
// End of redirection vector

// Split command
typedef struct {
    argument_t arg;
    redir_vect_t in;
    redir_vect_t out;
    redir_vect_t err;
} split_cmd_t;

static void split_cmd_clear(split_cmd_t *split_cmd)
{
    if (split_cmd->arg.argv) {
        int argc = split_cmd->arg.argc;
        for (int i = 0; i < argc; i++) free(split_cmd->arg.argv[i]);
        free(split_cmd->arg.argv);
    }
    free(split_cmd->in.redir);
    free(split_cmd->out.redir);
    free(split_cmd->err.redir);
    split_cmd->arg = (argument_t){ 0, NULL };
    split_cmd->in  = (redir_vect_t){ count:0, redir:NULL };
    split_cmd->out = (redir_vect_t){ count:0, redir:NULL };
    split_cmd->err = (redir_vect_t){ count:0, redir:NULL };
}

static void split_cmd_print_short(split_cmd_t *split_cmd, cond_t cond)
{
    FILE *stream = stdout;
    fputs(((split_cmd->arg.argc == 0) &&
            ((split_cmd->in.count > 0)||
            (split_cmd->out.count > 0)||
            (split_cmd->err.count > 0)))? "[cat] ": "", stream);
    for (int i = 0; i < split_cmd->arg.argc; i++) {
        fprintf(stream, "%s ", split_cmd->arg.argv[i]);
    }

    for (int i = 0; i < split_cmd->in.count; i++) {
        fprintf(stream, "%s %s ", (split_cmd->in.redir[i].type == R_FILE_IN)? "<": "<<<",
                split_cmd->in.redir[i].value);
    }
    for (int i = 0; i < split_cmd->out.count; i++) {
        fprintf(stream, "%s %s ", (split_cmd->out.redir[i].type & R_APPEND)? ">>": ">",
                split_cmd->out.redir[i].value);
    }
    for (int i = 0; i < split_cmd->err.count; i++) {
        fprintf(stream, "2%s %s ", (split_cmd->err.redir[i].type & R_APPEND)? ">>": ">",
                split_cmd->err.redir[i].value);
    }

    switch (cond) {
        case COND_ENDL: fprintf(stream, "\n");   break;
        case COND_PIPE: fprintf(stream, "| ");   break;
        case COND_SEQ:  fprintf(stream, "; ");   break;
        case COND_AND:  fprintf(stream, "&& ");  break;
        case COND_OR:   fprintf(stream, "|| ");  break;
        case COND_BACK: fprintf(stream, "& ");   break;
        default: break;
    }
    fflush(stream);
}

static void split_cmd_print_verbose(split_cmd_t *split_cmd, cond_t cond)
{
    FILE *stream = stderr;
    fputs("Command: ", stream);
    fputs((split_cmd->arg.argc == 0)? "[cat] ": " ", stream);
    for (int i = 0; i < split_cmd->arg.argc; i++) {
        fprintf(stream, "%s ", split_cmd->arg.argv[i]);
    }
    int count = split_cmd->in.count;
    if (count) {
        fprintf(stream, "\nInput redirections (%d): ", count);
        for (int i = 0; i < count; i++) {
            fprintf(stream, "%s %s ", (split_cmd->in.redir[i].type == R_FILE_IN)? "<": "<<<",
                    split_cmd->in.redir[i].value);
        }
    }
    count = split_cmd->out.count;
    if (count) {
        fprintf(stream, "\nOutput redirections (%d): ", count);
        for (int i = 0; i < count; i++) {
            fprintf(stream, "%s %s ", (split_cmd->out.redir[i].type & R_APPEND)? ">>": ">",
                    split_cmd->out.redir[i].value);
        }
    }
    count = split_cmd->err.count;
    if (count) {
        fprintf(stream, "\nErrors redirections: ");    
        for (int i = 0; i < count; i++) {
            fprintf(stream, "%s %s ", (split_cmd->err.redir[i].type & R_APPEND)? ">>": ">",
                    split_cmd->err.redir[i].value);
        }
    }
    fputs("\nEnd with: ", stream);
    switch (cond) {
        case COND_ENDL: fprintf(stream, "'\\n'");   break;
        case COND_PIPE: fprintf(stream, "| ");   break;
        case COND_SEQ:  fprintf(stream, "; ");   break;
        case COND_AND:  fprintf(stream, "&& ");  break;
        case COND_OR:   fprintf(stream, "|| ");  break;
        case COND_BACK: fprintf(stream, "& ");   break;
        default: break;
    }   
    fprintf(stream, "\n--\n");
    fflush(stream);
}
// End of split_cmd


// Command
typedef struct command {
    split_cmd_t readed;
    split_cmd_t parsed;
    int count_redir[3];
    cond_t cond;
    int pipe;
    struct command* next;
} command_t;

static command_t* new_command()
{
    command_t *cmd = (command_t*)malloc(sizeof(command_t));
    if (!cmd) return NULL;
    // chain in cmd_line
    cmd->cond = COND_ENDL;
    cmd->next = NULL;
    // read arena
    cmd->readed.arg = (argument_t) { 0, NULL };
    cmd->readed.in  = (redir_vect_t) { 0, NULL };
    cmd->readed.out = (redir_vect_t) { 0, NULL };
    cmd->readed.err = (redir_vect_t) { 0, NULL };
    // parse arena
    // prevent access in show_command, and avoid problems with clean_command
    cmd->parsed.arg = (argument_t){ 0, NULL };
    cmd->parsed.in  = (redir_vect_t) { 0, NULL };
    cmd->parsed.out = (redir_vect_t) { 0, NULL };
    cmd->parsed.err = (redir_vect_t) { 0, NULL };
    // execution
    cmd->count_redir[0] = 0;
    cmd->count_redir[1] = 0;
    cmd->count_redir[2] = 0;
    cmd->pipe = -1;

    return cmd;
}
// copy a clean command, only read arena
static command_t* new_command_copy(command_t *cmd)
{
    if (!cmd) return NULL;
    command_t *new = new_command();
    if (!new) return NULL;
    // chain in cmd_line
    new->cond = cmd->cond;
    // read arena
    new->readed.arg = argument_copy(cmd->readed.arg);
    new->readed.in  = redir_vect_copy(cmd->readed.in);
    new->readed.out = redir_vect_copy(cmd->readed.out);
    new->readed.err = redir_vect_copy(cmd->readed.err);
    return new;
}
// clear or delete command (depends on keep arg)
static void command_clear(command_t *cmd, int keep)
{
    // free arguments allocated by parse_command (during exec_line())
    split_cmd_clear(&cmd->parsed);

    if (keep) {
        // clear fields used during execution if cmd is kept
        cmd->count_redir[0] = 0;
        cmd->count_redir[1] = 0;
        cmd->count_redir[2] = 0;
        cmd->pipe = -1;
    } else {
        // free reusable data (allocated by get_line())
        split_cmd_clear(&cmd->readed);
        free(cmd);
    }
}
// transfer (readed) input redirections from cmd_o to cmd_d
// chain info not set: next, cond
static command_t* command_merge_input(command_t *cmd_d, command_t *cmd_o)
{
    if (!cmd_d) return NULL;
    if (!cmd_o) return cmd_d;
    split_cmd_t *scmd_o = &cmd_o->readed;
    split_cmd_t *scmd_d = &cmd_d->readed;
    int extra = scmd_o->in.count;
    if (extra) {
        int init = scmd_d->in.count;
        int count = init + extra;
        redir_t *tmp = realloc(scmd_d->in.redir, count * sizeof(redir_t *));
        if (tmp) {
            scmd_d->in.count = count;
            for (int i = 0; i < extra; i++) {
                tmp[init + i] = scmd_o->in.redir[i]; // transfer, not copy
            }
            scmd_d->in.redir = tmp;
            free(scmd_o->in.redir);
            scmd_o->in.redir = NULL;
            scmd_o->in.count = 0;
        }
    }
    return cmd_d;
}
// transfer (readed) output/error redirections from cmd_o to cmd_d
// chain info not set: next, cond
static command_t* command_merge_output(command_t *cmd_d, command_t *cmd_o)
{
    if (!cmd_d) return NULL;
    if (!cmd_o) return cmd_d;
    split_cmd_t *scmd_o = &cmd_o->readed;
    split_cmd_t *scmd_d = &cmd_d->readed;

    int extra = scmd_o->out.count;
    if (extra) {
        int init = scmd_d->out.count;
        int count = init + extra;
        redir_t *tmp = realloc(scmd_d->out.redir, count * sizeof(redir_t *));
        if (tmp) {
            scmd_d->out.count = count;
            for (int i = 0; i < extra; i++) {
                tmp[init + i] = scmd_o->out.redir[i]; // transfer, not copy
            }
            scmd_d->out.redir = tmp;
            free(scmd_o->out.redir);
            scmd_o->out.redir = NULL;
            scmd_o->out.count = 0;
        }
    }
    extra = scmd_o->err.count;
    if (extra) {
        int init = scmd_d->err.count;
        int count = init + extra;
        redir_t *tmp = realloc(scmd_d->err.redir, count * sizeof(redir_t *));
        if (tmp) {
            scmd_d->err.count = count;
            for (int i = 0; i < extra; i++) {
                tmp[init + i] = scmd_o->err.redir[i]; // transfer, not copy
            }
            scmd_d->err.redir = tmp;
            free(scmd_o->err.redir);
            scmd_o->err.redir = NULL;
            scmd_o->err.count = 0;
        }
    }
    return cmd_d;
}

// print readed command
static void command_print_short(command_t *cmd)
{
    split_cmd_print_short(&cmd->readed, cmd->cond);
}
// print parsed command
static void command_print_verbose(command_t *cmd)
{
    split_cmd_print_verbose(&cmd->parsed, cmd->cond);
}
// End of command


// Command line
typedef struct cmdline_st{
    int index;
    command_t *first_cmd;
    struct cmdline_st *next_line;
} cmdline_t;
// New command line, includes the given command, NOT A COPY!
static cmdline_t* new_cmdline(command_t *first_cmd)
{
    cmdline_t *newcmdline = (cmdline_t*)malloc(sizeof(cmdline_t));
    if (!newcmdline) return NULL;
    newcmdline->index = 0;
    newcmdline->first_cmd = first_cmd;
    newcmdline->next_line = NULL;
    return newcmdline;
}
// Copy a command line, copying each command
static cmdline_t* new_cmdline_copy(cmdline_t *cmdline)
{
    if (!cmdline || !cmdline->first_cmd) return NULL;
    command_t *cmd_o = cmdline->first_cmd;
    command_t *cmd_d = new_command_copy(cmd_o);

    cmdline_t *newline = new_cmdline(cmd_d);
    if (!newline) return NULL;
    while (cmd_o && cmd_o->cond != COND_ENDL) {
        cmd_o = cmd_o->next;
        cmd_d = cmd_d->next = new_command_copy(cmd_o);
    }
    cmd_d->next = NULL;
    return newline;
}
// print command line
static void cmdline_print(cmdline_t *cmdline)
{
    if (!cmdline) return;
    command_t *cmd = cmdline->first_cmd;
    while (cmd) {
        command_print_short(cmd);
        cmd = cmd->next;
    }
}
// print command line sequence
static void cmdline_seq_print(cmdline_t *cmdline)
{
    if (!cmdline) return;
    if (cmdline->next_line) cmdline_seq_print(cmdline->next_line);
    printf("[%d] ", cmdline->index);
    cmdline_print(cmdline);
}
// clear or delete a command line (depends on arg keep)
static void cmdline_clear(cmdline_t *cmdline, int keep)
{
    if (!cmdline) return;
    command_t *cmd = cmdline->first_cmd;

    while (cmd->cond != COND_ENDL && cmd->cond != COND_QUIT) {
        command_t *old = cmd;
        cmd = old->next;
        command_clear(old, keep);
    }
    command_clear(cmd, keep);
    if (!keep) free(cmdline);
}
// get the first command line with (id == n) from a sequence of cmdline
// return the command line or NULL
static cmdline_t* cmdline_seq_cmdline_get(cmdline_t *cmdline, int n)
{
    while (cmdline && cmdline->index != n)
        cmdline = cmdline->next_line;
    return cmdline;
}
// find the first command line with first command == string from a sequence of cmdline
// return the command line or NULL
static cmdline_t* cmdline_seq_cmdline_cmd_find(cmdline_t *cmdline, char *string)
{
    command_t *cmd;
    while (cmdline && (cmd = cmdline->first_cmd)) {
        char **argv = cmd->readed.arg.argv;
        if (argv && strcmp(argv[0], string) == 0) break;
        cmdline = cmdline->next_line;
    }
    return cmdline;
}
// find the first command line including string from a sequence of cmdline
// return the command line or NULL
static cmdline_t* cmdline_seq_cmdline_arg_find(cmdline_t *cmdline, char *string)
{
    while (cmdline) {
        command_t *cmd = cmdline->first_cmd;
        while (cmd) {
            char **argv = cmd->readed.arg.argv;
            if (argv)
                while (*argv)
                    if (strcmp(*argv++, string) == 0)
                        return cmdline;
            for (int i = 0; i < cmd->readed.in.count; i++)
                if (strcmp(cmd->readed.in.redir[i].value, string) == 0)
                    return cmdline;
            for (int i = 0; i < cmd->readed.out.count; i++)
                if (strcmp(cmd->readed.out.redir[i].value, string) == 0)
                    return cmdline;
            for (int i = 0; i < cmd->readed.err.count; i++)
                if (strcmp(cmd->readed.err.redir[i].value, string) == 0)
                    return cmdline;
            cmd = cmd->next;
        }
        cmdline = cmdline->next_line;
    }
    return cmdline;
}
// delete the first command line with (id == n) from a sequence of cmdline
// return a sequence without such command line
static cmdline_t* cmdline_seq_cmdline_delete(cmdline_t *cmdline, int n)
{
    if (!cmdline) return NULL;
    if (cmdline->index == n) {
        cmdline_t* res = cmdline->next_line;
        cmdline_clear(cmdline, 0);
        return res;
    }
    cmdline->next_line = cmdline_seq_cmdline_delete(cmdline->next_line, n);
    return cmdline;
}
// traverse a sequence of command lines and clear or delete (depend on keep)
static void cmdline_seq_clear(cmdline_t *cmdline, int keep)
{
    while (cmdline) {
        cmdline_t *next_line = cmdline->next_line;
        cmdline_clear(cmdline, keep);
        cmdline = next_line;
    }
}
// End of command line type and functions

////////////////////////////////////////////////////////////////////////////////
// End of shell types, variables and supporting functions
////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////
// Management of environment and local vars
////////////////////////////////////////////////////////////////////////////////

// Local vars management (types, variables and supporting functions)
typedef char* (localentry_t)[2];
typedef struct {
    localentry_t *var;
    int size;
    int aloc;
} localvars_t;

static void vars_set_var(localvars_t *vars, localentry_t* var) {vars->var = var;}
static localentry_t* vars_get_var(localvars_t *vars) {return vars->var;}
static void vars_set_aloc(localvars_t *vars, int aloc) {vars->aloc = aloc;}
static int vars_get_aloc(localvars_t *vars) {return vars->aloc;}
static void vars_set_size(localvars_t *vars, int size) {vars->size = size;}
static int vars_get_size(localvars_t *vars) {return vars->size;}
static void vars_set_key(localvars_t *vars, int pos, char *key) {vars->var[pos][0] = key;}
static char* vars_get_key(localvars_t *vars, int pos) {return (vars->var[pos][0]);}
static void vars_set_value(localvars_t *vars, int pos, char *val) {vars->var[pos][1] = val;}
static char* vars_get_value(localvars_t *vars, int pos) {return (vars->var[pos][1]);}

static int vars_find_key(localvars_t *vars, const char *key)
{
    localentry_t* var = vars_get_var(vars);
    int size = vars_get_size(vars);
    for (int i = 0; i < size; i++)
        if (strcmp(var[i][0], key) == 0)
            return i;
    return -1;
}

static int setvar(localvars_t *vars, const char *key, const char *value)
{
    if (!key || !*key) return 0;
    int pos = vars_find_key(vars, key);
    if (pos == -1) {
        pos = vars_get_size(vars);
        int aloc = vars_get_aloc(vars);        
        if (pos == aloc) {
            int newaloc = aloc + 10;
            localentry_t *newvar = (localentry_t*)realloc(vars_get_var(vars), newaloc*sizeof(localentry_t));
            if (newvar == NULL) {
                errno = ENOMEM;
                return -1;
            }
            vars_set_var(vars, newvar);
            vars_set_aloc(vars, aloc);
        }
        vars_set_key(vars, pos, strdup(key));
        vars_set_size(vars, pos + 1);
    } else {
        free(vars_get_value(vars, pos));
    }
    vars_set_value(vars, pos, (value)? strdup(value): NULL);
    return 0;
}

static int unsetvar(localvars_t *vars, const char *key)
{
    if (!key || !*key) return -1;
    int size = vars->size;
    int pos = vars_find_key(vars, key);
    if (pos == -1) return -1;
    free(vars_get_key(vars, pos));
    free(vars_get_value(vars, pos));
    int last = size - 1;
    if (pos < last) {
        vars_set_key(vars, pos, vars_get_key(vars, last));
        vars_set_value(vars, pos, vars_get_value(vars, last));
    }
    vars_set_size(vars, last);
    return 0;
}

static char* getvar(localvars_t *vars, const char *key)
{
    if (!key || !*key) return NULL;
    int pos = vars_find_key(vars, key);
    if (pos == -1) return NULL;
    return vars_get_value(vars, pos);
}

static char* getkey(localvars_t *vars, int pos)
{
    if (pos == -1) return NULL;
    if (pos >= vars_get_size(vars)) return NULL;
    return vars_get_key(vars, pos);
}
// End of local vars management





// Shell structure to hold local vars
static localvars_t localvars = (localvars_t){ NULL, 0, 0 };

// Environment management: userland interface
static int main_env(int argc, char *argv[])
{
    extern char **environ;
    char **env = environ;
    while (*env) printf("%s\n",*env++);
    return 0;
}

static int main_export(int argc, char *argv[])
{
    extern char **environ;
    char **env = environ;
    if (argc == 1) return main_env(0, NULL);
    if (argv[1][0] == '-') {
        if (argv[1][1] == 'p') {
            while (*env) printf("export %s\n", *env++);
            return 0;
        } else {
            fprintf(stderr,"%s: bad option: -%c\n", argv[0], argv[1][1]);
            return -1;
        }
    }
    char *key = argv[1];
    char *value = strchr(argv[1], '=');
    if (!value) {
        value = getvar(&localvars, key);
        if (!value) return -1;
    } else {
        *value++ = '\0';
        if (!*key) return -1;
    }
    int ret = setenv(key, value, 1);
    if (ret == -1) {
        char buff[256];
        snprintf(buff, 256, "%s: %s", argv[0], "setenv");
        perror(buff);
    }
    return ret;
}

static int main_set(int argc, char *argv[])
{
    if (argc == 1) {
        int size = vars_get_size(&localvars);
        for (int i = 0; i < size; i++)
            printf("%s=%s\n", vars_get_key(&localvars, i), vars_get_value(&localvars, i));
    }
    return 0;
}

static int main_unset(int argc, char *argv[])
{
    return unsetvar(&localvars, argv[1]);
}
////////////////////////////////////////////////////////////////////////////////
// End of environment
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// history
////////////////////////////////////////////////////////////////////////////////

static cmdline_t *history = NULL;
int history_index = 1;

static void history_push(cmdline_t *cmdline)
{
    if (!cmdline) return;
    cmdline->index = history_index++;
    cmdline->next_line = history;
    history = cmdline;
}

static cmdline_t* history_find_cmdline(char *inputstr)
{
    cmdline_t *cmdline = NULL;

    if (!inputstr) return NULL;

    size_t len = strnlen(inputstr, PATH_MAX-1);
    char buff[PATH_MAX];
    strncpy(buff, inputstr, len);
    buff[len] = '\0';
    char *str = buff;

    if (len < 2) return NULL;

    if (*str == '!') {   // ! => history command
        cmdline_t *foundcmdline = NULL;
        str++;
        if (*str == '!') {        // !!
            foundcmdline = cmdline_seq_cmdline_get(history, history_index - 1);
        } else if (*str == '?') { // !?string
            str++;
            foundcmdline = cmdline_seq_cmdline_arg_find(history, str);
        } else {
            char *endptr;
            int n = strtol(str, &endptr, 10);
            if (n < 0) {                // !-n
                foundcmdline = cmdline_seq_cmdline_get(history, n + history_index);
            } else if (str == endptr) { // !string
                foundcmdline = cmdline_seq_cmdline_cmd_find(history, str);
            } else {                    // !n
                foundcmdline = cmdline_seq_cmdline_get(history, n);
            }
        }
        if (foundcmdline) {
            cmdline = new_cmdline_copy(foundcmdline);
            //if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) cmdline_print(cmdline);
        } else {
            fprintf(stderr, "Event not found: '%s'\n", str);
            cmdline = new_cmdline(new_command());
        }
    }

    return cmdline;
}

static int main_history(int argc, char *argv[])
{
    if (argc == 1) {
        cmdline_seq_print(history);
    }
    else if (strcmp(argv[1], "-c") == 0) {
        cmdline_seq_clear(history, 0);
        history = NULL;
        history_index = 1;
    } else if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            fprintf(stderr,"Missing command line number\n");
            return 1;
        }
        int k = atoi(argv[2]);
        cmdline_seq_cmdline_delete(history, k);
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// End of history
////////////////////////////////////////////////////////////////////////////////






////////////////////////////////////////////////////////////////////////////////
#define READ(fdi, buf, count)\
    ({\
        ssize_t ret = read(fdi, buf, count);\
        if (ret == -1) return -1;\
        ret;\
    })

#define WRITE(fdo, buf, count)\
    ({\
        ssize_t ret = write(fdo, buf, count);\
        if (ret == -1) return -2;\
        ret;\
    })

static ssize_t COPY(int ifd, int ofd)
{
    char buf[BUFSIZ];
    ssize_t ret = 0;
    ssize_t rlen;
    struct stat statbufin, statbufout;
    if (fstat(ifd, &statbufin)==0 &&
        fstat(ofd, &statbufout)==0 &&
        !S_ISCHR(statbufin.st_mode) &&
        !S_ISCHR(statbufout.st_mode) &&
        statbufin.st_ino == statbufout.st_ino) {
        fprintf(stderr, "input file is output file\n");
        return 0;
    }
            
    while ((rlen = READ(ifd, buf, BUFSIZ)) > 0) {
        for (ssize_t off = 0; off < rlen; off += WRITE(ofd, buf+off, rlen-off));
        ret += rlen;
    }
    return ret;
}

static ssize_t COPY_ENDSTR(int ifd, int ofd, char *endstr)
{
    char buf[BUFSIZ];
    ssize_t ret = 0;
    ssize_t rlen = 0;
    ssize_t endstrlen = strnlen(endstr, BUFSIZ);
    ssize_t pos = 0;
    int line_begins = 1;
    while (READ(ifd, buf+pos, 1) > 0) {
        rlen++;
        if (buf[pos] == '\n' || rlen == BUFSIZ) {
            if (line_begins && pos == endstrlen && strncmp(endstr, buf, endstrlen) == 0) break;
            line_begins = (buf[pos] == '\n');
            for (ssize_t off = 0; off < rlen; off += WRITE(ofd, buf+off, rlen-off));
            ret += rlen;
            rlen = 0;
            pos = 0;
        } else pos++;
    }
    return ret;
}

static void WRITE_STDOUT_ISATTY(const char *prompt)
{
    (void)(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
            write(STDOUT_FILENO, prompt, strlen(prompt)));
}

static ssize_t read_stdin(char *buffer, ssize_t size)
{
    int interactive = isatty(STDIN_FILENO);
    ssize_t off = 0;
    do {
        // when processing a file, read byte by byte
        ssize_t len = read(STDIN_FILENO, buffer+off, (interactive)? size-off: 1);
        if (len == 0){
            WRITE_STDOUT_ISATTY("\nBye\n");
            return 0; // ^d was entered, end of user command stream
        } else if (len == -1){
            perror("shell: read");
            return -1;
        }
        off += len;
    } while (off < size && buffer[off-1] != '\n' && buffer[off-1] != '\0');
    //buffer[off-1]='\0';     // replacing '\n' by '\0' (not mandatory)
    return off;
}

////////////////////////////////////////////////////////////////////////////////
//
// Parser
//
////////////////////////////////////////////////////////////////////////////////

static command_t* get_command(char *inputBuffer, ssize_t size, int *offset, const char *prompt)
{
    int len;
    int ii = *offset;   // loop index for reading inputBuffer array
    int item_count = 0;
    int next = -1;
    int rtypefound = R_NO_REDIR;
    int rtype = R_NO_REDIR;
    int start = -1;
    int argi[size/2];
    int type[size/2];
    cond_t cond = COND_ENDL;
    int cnt[3];
    cnt[0] = 0;
    cnt[1] = 0;
    cnt[2] = 0;

#define QUIT                                \
         do {                               \
            command_t *cmd = new_command(); \
            if (!cmd) return NULL;          \
            cmd->cond = COND_QUIT;          \
            return cmd;                     \
        } while(0)

#define READ_STDIN(buffer, size)            \
    ({                                      \
        int ret = read_stdin(buffer, size); \
        if (ret <= 0) QUIT;                 \
        ret;                                \
    })

    // if /n is ignored then pipe/cmdand/cmdor prompt are not aplied
//    if (ii == -1 || inputBuffer[ii] == '\0') {
    // if force read when \n or \0 then extra read occur with ls; or ls& (COND_SEQ/BACK)
    if (ii == -1 || inputBuffer[ii] == '\n' || inputBuffer[ii] == '\0') {
        if (ii == -1) ii = 0;
        WRITE_STDOUT_ISATTY(prompt);
        len = READ_STDIN(&inputBuffer[ii], size-ii-1) + ii;
        inputBuffer[len] = '\0';
//TODO: remove printk debug
//printk("readed: '%s' (len: %d) (pos: %d)\n",&inputBuffer[ii],len,ii);
    } else {
        len = strnlen(&inputBuffer[ii], size-ii-1) + ii;
//printk("received: '%s' (len: %d) (pos: %d)\n",&inputBuffer[ii],len,ii);
//if (inputBuffer[len]=='\0') printk("Null terminator OK\n");
//else printk("Missing Null terminator !!!\n");
    }
    inputBuffer[size-1] = '\0'; //in any case

    int jj = ii;
    unsigned char sep = 0;
    while (ii < len) {
        unsigned char c;
        switch (inputBuffer[ii])
        {
        case '\0':
//printk("case '\\0': [isatty? %d]\n",isatty(0));
            if (!isatty(STDIN_FILENO)) QUIT;
        case '\n':                          // no more to process: line ending
            next = -1;
            cond = COND_ENDL;
        multiple:                           // no more to process: next command
            while (isblank(inputBuffer[ii])) ii++;
            if (jj + 1 < ii) {              // compact space
//printk("Compact %d -> %d\n",ii, jj+1);
                for (int cont = 0; ii + cont < len+1; cont++)
                    inputBuffer[jj + 1 + cont] = inputBuffer[ii + cont];
                if (next != -1) next = jj + 1;
            }
            if (inputBuffer[next] == '\n') {
                next = -1;
                if ((cond == COND_SEQ) || (cond == COND_BACK)) cond = COND_ENDL;
            }
//if (start != -1) printk("last item: '%s' (start: %d)\n",&inputBuffer[start], start);
//if (next != -1) printk("delayed: '%s' (next: %d)\n",&inputBuffer[next], next);
//else printk("nothing delayed\n");
            len = ii;                       // pass through
        redir:
        case '\t':                          // argument separators
        case ' ':                           // pass through
            if (start != -1) {
                inputBuffer[jj++] = '\0';
                if (rtype & REDIR_BIT) {
                   if ((rtype & REDIR_MASK) == 1) cnt[1]++;
                   else if ((rtype & REDIR_MASK) == 2) cnt[2]++;
                   else cnt[0]++;   // 00->file_in; 11->raw_input (or EOFstr)
                }
                argi[item_count] = start;
                type[item_count++] = rtype;
                start = -1;
                rtype = R_NO_REDIR;
            }
            if (rtypefound != R_NO_REDIR) {
                rtype = rtypefound;
                rtypefound = R_NO_REDIR;
            }
            break;
        case ';':
            next = jj + 1;
            cond = COND_SEQ;                // next command should be executed
            ii++;
            goto multiple;
        case '|':
            if (ii+1 < len && inputBuffer[ii+1] == '|') { // '||' logical OR
                next = jj + 2;
                cond = COND_OR;             // next cmd iff current fails
                ii+=2;
                goto multiple;
            } else {                        // '|' pipe output for next cmd
                next = jj + 1;
                cond = COND_PIPE;           // next command should be executed
                ii++;
                goto multiple;
            }
            break;
        case '&':
            if (ii+1 < len && inputBuffer[ii+1] == '&'){  // '&&' logical AND
                next = jj + 2;
                cond = COND_AND;            // next cmd iff current succeeds
                ii+=2;
                goto multiple;
            } else {                        // '&' no background -> treat as ';'
                next = jj + 1;
                cond = COND_BACK;           // next command should be executed
                ii++;
                goto multiple;
            }
            break;
        case '(':                           // an argument itself (?)
            sep = ')';
            prompt = "> ";
            goto string;
        case '"':
            sep = '"';                      // start quote (replace vars)
            prompt = "dquote> ";
            goto string;
        case '\'':
            sep = '\'';                     // start quote (literal)
            prompt = "quote> ";
        string:
            if (start == -1) start = jj;
            inputBuffer[jj++] = inputBuffer[ii++]; // copy separator: ( " '
            while (ii < len) {
                do {                        // deferred until command parsing
                    c = inputBuffer[ii++];
                    inputBuffer[jj++] = c;
                } while (c != sep && c != '\n' && c != '\0' && ii < len);
                if ((c == '\n' || c == '\0') && ii < size) { // multiline string (allow '\n' into string)
                    WRITE_STDOUT_ISATTY(prompt);
                    long tmp = READ_STDIN(&inputBuffer[ii], size-(ii));
                    len += tmp;
                    inputBuffer[jj - 1] = '\n'; // set '\n' instead of '\0'
                }
                if (c == sep) break;
            }
            ii--;
            sep = 0;
            break;
        case '\\':                          // scape character
            if (start == -1) start = jj;
            c = inputBuffer[++ii];
            inputBuffer[jj++] = c;
            if ((c == '\n' || c == '\0') && ii < size) {   // multiline command line
                WRITE_STDOUT_ISATTY("> ");
                len += READ_STDIN(&inputBuffer[ii], size-(ii));
                ii--;
                jj--;
            }
            break;
        case '<':
            rtypefound = R_FILE_IN;
            if (ii+1 < len && inputBuffer[ii+1] == '<') {
                ii++;
                rtypefound = R_RAWINPUT;
                if (ii+1 < len && inputBuffer[ii+1] != '<') {
                    rtypefound |= R_EOFINPUT;
                } else { // <<<'string in stdin'
                    ii++;
                }
            }
            goto redir; 
        case '1':
            if (start != -1) goto general;
            if (ii+1 < len && inputBuffer[ii+1] != '>') goto general;
            ii++; // pass through
        case '>':
            rtypefound = R_FILE_OUT;
            if (ii+1 < len && inputBuffer[ii+1] == '>') {
                ii++;
                rtypefound |= R_APPEND;
            }
            goto redir;            
        case '2':
            if (start != -1) goto general;
            if (ii+1 < len && inputBuffer[ii+1] != '>') goto general;
            ii++;
            rtypefound = R_FILE_ERR;
            if (ii+1 < len && inputBuffer[ii+1] == '>') {
                ii++;
                rtypefound |= R_APPEND;
            }
            goto redir;
        case '#':
            if (start == -1) {
                cond = COND_ENDL;           // comments => end of line
                len = ii;                   // exit loop
                next = -1;                  // read next line
                break;
            }                               // pass throught
        general:
        default:                            // some other character
            if (start == -1) start = jj;
            if (jj != ii) inputBuffer[jj] = inputBuffer[ii];
            jj++;
        }   // end switch
        ii++;
    }   // end while

    *offset = next;
    command_t *cmd = new_command();
    if (!cmd) return NULL;

    cmd->cond = cond;
    if (!item_count) return cmd;

    redir_t *redir_in  = (redir_t*)malloc(cnt[0] * sizeof(redir_t));
    redir_t *redir_out = (redir_t*)malloc(cnt[1] * sizeof(redir_t));
    redir_t *redir_err = (redir_t*)malloc(cnt[2] * sizeof(redir_t));

    int argc = item_count - cnt[0] - cnt[1] - cnt[2];
    char **argv = (char**)malloc((argc + 1) * sizeof(char *));

    int count_in = 0;
    int count_out = 0;
    int count_err = 0;
    int count_argv = 0;
    for (int i = 0; i < item_count; i++) {
        char *value = strdup(inputBuffer + argi[i]);
        int rtype = type[i];
        if (rtype & REDIR_BIT) {
            if ((rtype & REDIR_MASK) == 1) {
                redir_out[count_out++] = (redir_t){ value, rtype };
                continue;
            }
            if ((rtype & REDIR_MASK) == 2) {
                redir_err[count_err++] = (redir_t){ value, rtype };
                continue;
            }
            // rtype: 00->file_in; 11->raw input (or EOFstr)
            redir_in[count_in++] = (redir_t){ value, rtype };
            continue;
        }
        argv[count_argv++] = value;
    }
    // assert count_argv == argc
    argv[argc] = NULL;
    cmd->readed.arg.argc = argc;
    cmd->readed.arg.argv = argv;
    cmd->readed.in  = (redir_vect_t) { cnt[0], redir_in  };
    cmd->readed.out = (redir_vect_t) { cnt[1], redir_out };
    cmd->readed.err = (redir_vect_t) { cnt[2], redir_err };

    return cmd;
}

static cmdline_t* get_line(const char *prompt)
{
    char workbuffer[MAX_LINE];
    static int offset = -1;
    cmdline_t *newcmdline;
    
    command_t *cmd;
    do {
        cmd = get_command(workbuffer, MAX_LINE, &offset, prompt);
    } while (!cmd || (cmd->cond == COND_ENDL &&
                        cmd->readed.arg.argc +
                        cmd->readed.in.count +
                        cmd->readed.out.count +
                        cmd->readed.err.count == 0)); // avoid a void first cmd


    char **argv = cmd->readed.arg.argv; // solve history references
    cmdline_t *cmdline = (argv)? history_find_cmdline(*argv): NULL; // argv[0]

    if (cmdline) {  // found => cmdline is a copy of that one in history
        command_t *tmp = cmdline->first_cmd;
        command_merge_input(tmp, cmd);
        while(tmp->cond != COND_ENDL) tmp = tmp->next;
        command_merge_output(tmp, cmd);
        tmp->cond = cmd->cond;
        command_clear(cmd, 0);
        cmd = tmp;
        newcmdline = cmdline;
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) cmdline_print(cmdline);
    } else {
        newcmdline = new_cmdline(cmd);
    }

    while(cmd->cond != COND_ENDL && cmd->cond != COND_QUIT) {
        const char *newprompt;
        switch (cmd->cond) {
            case COND_AND: newprompt = "cmdand> "; break;
            case COND_OR:  newprompt = "cmdor> "; break;
            case COND_PIPE:newprompt = "pipe> "; break;
            default:  newprompt = prompt;
        }
        command_t *next;
        do {
            next = get_command(workbuffer, MAX_LINE, &offset, newprompt);
        } while (!next);

        argv = next->readed.arg.argv;
        cmdline_t *cmdline = (argv)? history_find_cmdline(*argv): NULL;
        if (cmdline) {  // found => cmdline is a copy of that one in history
            command_t *tmp = cmdline->first_cmd;
            free(cmdline);
            cmd->next = tmp;
            command_merge_input(tmp, next);
            while(tmp->cond != COND_ENDL) tmp = tmp->next;
            command_merge_output(tmp, next);
            tmp->cond = next->cond;
            command_clear(next, 0);
            cmd = tmp;
        } else {
            cmd = cmd->next = next;
        }

    }

    return newcmdline;
}

static int redirect_stream(int STDFILENO, const char *FILENAME, int FLAGS, mode_t MODE)
{
    int fd = openat(AT_FDCWD, FILENAME, FLAGS, MODE);
    char buff[256];
    if (fd != -1) {
        if (dup2(fd, STDFILENO) != STDFILENO) {
            snprintf(buff, 256, "%s: dup2: '%s' to fd=%d", __func__,
                                FILENAME?:"TMPFILE", STDFILENO);
            perror(buff);
        }
    } else {
        snprintf(buff, 256, "%s: open: %s", __func__, FILENAME?:"TMPFILE");
        perror(buff);
    }
    return fd;
}


static void save_default_streams(int *default_std_fd)
{
    default_std_fd[STDIN_FILENO]  = dup(STDIN_FILENO);
    default_std_fd[STDOUT_FILENO] = dup(STDOUT_FILENO);
    default_std_fd[STDERR_FILENO] = dup(STDERR_FILENO);
}

static int restore_default_stream(int fd, int std_fd)
{
    int res = std_fd;
    if (std_fd != -1) res = dup2(std_fd, fd);
    return res;
}

static void restore_default_streams(int *default_std_fd)
{
    restore_default_stream(STDERR_FILENO, default_std_fd[STDERR_FILENO]);
    restore_default_stream(STDOUT_FILENO, default_std_fd[STDOUT_FILENO]);
    restore_default_stream(STDIN_FILENO,  default_std_fd[STDIN_FILENO]);
}

static void close_default_streams(int *default_std_fd)
{
    close(default_std_fd[STDIN_FILENO]);
    close(default_std_fd[STDOUT_FILENO]);
    close(default_std_fd[STDERR_FILENO]);
    default_std_fd[STDIN_FILENO]  = -1;
    default_std_fd[STDOUT_FILENO] = -1;
    default_std_fd[STDERR_FILENO] = -1;
}


////////////////////////////////////////////////////////////////////////////////
// variable structure to save enviroment overwrited by local vars in a cmdline
static localvars_t envrestore = (localvars_t){ NULL, 0, 0 };
////////////////////////////////////////////////////////////////////////////////



static char* parse_argument(char *arg)
{
    char buff[MAX_LINE];
    int k = 0;
    char sep = 0;
    for (int j = 0; arg[j] != '\0'; j++) {
        if (arg[j] == '\'') {       // remove "'", rest is litteral
            char c;
            do {
                c = arg[++j];
                buff[k++] = c;
            } while (c != '\'' && c != '\0');
            k--;
        } else if (arg[j] == '"') { // remove '"', parse content of string
            if (sep == '"') {
                sep = 0;
            } else if (!sep) {
                sep = '"';
            } else {                // other separator: ')'
                buff[k++] = arg[j];
            }
        } else if (arg[j] == '\\') {
            j++;
            if (arg[j] == '\n') j++;
            if (arg[j] == '\0') break;
            buff[k++] = arg[j];
        } else if (arg[j] == '$'){
            j++;
            if (arg[j] == '$') {
                k += snprintf(&buff[k], MAX_LINE - k, "%s", getvar(&localvars,"$"));
            } else {
                char *tmp = strdup(&arg[j]);
                char *key = tmp;
                int cont = 0;
                if (*key == '{') {
                    key++;
                    cont++;
                }
                char c;
                do {
                    c = tmp[cont++];
                } while (c != ' ' && c != '\t' && c != '\n' && c != '\0' &&
                        c != '$' && c != '\'' && c != '"' &&
                        c != '(' && c != ')' && c != '}' && c != '*' /*&& c != '?'*/);
                cont--;
                tmp[cont] = '\0';
                char *env = getenv(key);
                if (!env) env = getvar(&localvars, key);
                if (env) k += snprintf(&buff[k], MAX_LINE - k, "%s", env);
                free(tmp);
                if (c == '}') cont++;
                j += cont-1;
            }
        } else {
            buff[k++] = arg[j];
        }
    }
    buff[k] = '\0';
    return strdup(buff);
}


// expand wildcards (*,?) in redirections
static redir_vect_t parse_redir_vector(redir_vect_t readed_redir_vect)
{
    int num_redirs = readed_redir_vect.count;
    int size = num_redirs;
    redir_t *redir_array = (redir_t*)malloc(size * sizeof(redir_t));
    int count = 0;
    for (int i = 0; i < num_redirs; i++) {
        redir_t redir = readed_redir_vect.redir[i];
        char *parsed_redir = parse_argument(redir.value);
        glob_t globbuf;
        glob(parsed_redir, GLOB_TILDE | GLOB_BRACE, NULL, &globbuf);
        if (globbuf.gl_pathc == 0) {
            redir_array[count++] = (redir_t){ parsed_redir, redir.type };
        }
        else if (globbuf.gl_pathc == 1) {
            redir_array[count++] = (redir_t){ strdup(globbuf.gl_pathv[0]), redir.type };
        }
        else {
            redir_t* newspace = (redir_t*)realloc(redir_array,
                                (size + globbuf.gl_pathc) * sizeof(redir_t));
            if (!newspace) {    // undo glob
                redir_array[count++] = (redir_t){ parsed_redir, redir.type };
                continue;                                       // next arg
            }
            redir_array = newspace;
            size += globbuf.gl_pathc;
            for (int i = 0; i < globbuf.gl_pathc; i++) {
                redir_array[count++] = (redir_t){ strdup(globbuf.gl_pathv[i]), redir.type };
            }
        }
        globfree(&globbuf);
    }
    return (redir_vect_t) { count:count, redir:redir_array };
}

// expand wildcards (*,?) in arguments
static argument_t parse_argument_vector(int start, argument_t readed_argument_vect)
{
    int argn = readed_argument_vect.argc;
    int size = argn - start + 1;
    char **argument_array = (char**)malloc(size * sizeof(char*));
    
    int argc = 0;
    for (int i = start; i < argn; i++) {
        char *parsed_arg = parse_argument(readed_argument_vect.argv[i]);
        glob_t globbuf;
        glob(parsed_arg, GLOB_TILDE | GLOB_BRACE, NULL, &globbuf);
        if (globbuf.gl_pathc == 0) {
            argument_array[argc++] = parsed_arg;
        }
        else if (globbuf.gl_pathc == 1) {
            argument_array[argc++] = strdup(globbuf.gl_pathv[0]);
        }
        else {
            char **newspace = (char**)realloc(argument_array,
                                (size + globbuf.gl_pathc) * sizeof(char*));
            if (!newspace) {
                argument_array[argc++] = parsed_arg;      // undo glob
                continue;                                       // next arg
            }
            argument_array = newspace;
            size += globbuf.gl_pathc;
            for (int i = 0; i < globbuf.gl_pathc; i++) {
                argument_array[argc++] = strdup(globbuf.gl_pathv[i]);
            }
        }
        globfree(&globbuf);
    }
    argument_array[argc] = NULL;
    return (argument_t){ argc, argument_array };
}

// Parse command from reusable data: readed arguments
// Substitute/evaluate local vars: $?, $HOME, and other redirections: <<, <<<
// Expand arguments using wildcards (*, ?)
static int parse_command(command_t *cmd)
{
    // parse redirections, do this before local vars
    cmd->parsed.in  = parse_redir_vector(cmd->readed.in );    // input
    cmd->parsed.out = parse_redir_vector(cmd->readed.out);    // output
    cmd->parsed.err = parse_redir_vector(cmd->readed.err);    // errors

    // process local vars (and store overwrited enviroment vars to recover later)
    int argn = cmd->readed.arg.argc;
    char **argv = cmd->readed.arg.argv;
    int index = argn;
    for (int i = 0; i < argn; i++) {
        char tmp[MAX_LINE];
        strcpy(tmp, argv[i]);
        char *key = tmp;
        char *value = strchr(tmp, '=');
        if (!value) {
            index = i;
            break;
        }
        *value++ = '\0';
        char *newkey = parse_argument(key);
        if (*newkey) {  // not empty
            char *restore = getenv(newkey);
            setvar(&envrestore, newkey, restore);
            char *newval = parse_argument(value);
            setenv(newkey, newval, 1);
            free(newval);
            free(newkey);
        }
    }

    // if empty command after command vars, then set command var as local vars
    if (index == argn) {
        char *key;
        while (NULL != (key = getkey(&envrestore, 0))) {
            char *val = getenv(key);
            setvar(&localvars, key, val);       // set as local vars
            val = getvar(&envrestore, key);
            if (val) {                          // restore environment
                setenv(key, val, 1);
            } else {
                unsetenv(key);
            }
            unsetvar(&envrestore, key);
        }
    }

    // current arguments once removed local vars from command
    cmd->parsed.arg  = parse_argument_vector(index, cmd->readed.arg ); // args

    return 0;
}

#define UNUSE_RESULT(code) do{ if (code) {} }while(0)
static int redir_command(command_t *cmd, int *default_std_fd)
{
    // redir standard input combined from pipe, files, raw inputs (restore if none)
    int num_redirs = cmd->parsed.in.count;
    int prefdin = cmd->pipe;
    int fdin = prefdin;
    int count = 0;
    char *mode = "r";
    fflush(stdin);
    for (int i = 0; i < num_redirs; i++) {
        int rtype = cmd->parsed.in.redir[i].type;
        char *value = cmd->parsed.in.redir[i].value;
        if (fdin == -1) {                       // first redir and no pipe
            if ((rtype & REDIR_MASK) == 0) {
                fdin = open(value, O_RDONLY);
                if (fdin != -1) count++;
            } else {                            // raw imput, create TMP
                prefdin = fdin;                 // use this TMP for the rest
                fdin = open("/tmp", O_RDWR | O_TMPFILE, S_IRUSR | S_IWUSR);
                if (fdin != -1) {
                    count++;
                    ssize_t error;
                    if (rtype & R_EOFINPUT) {
                        error = COPY_ENDSTR(STDIN_FILENO, fdin, value);
                    } else {
                        error = write(fdin, value, strlen(value));
                        if (error >= 0) error = write(fdin, "\n", 1);
                    }
                    if (error < 0) {
                        close(fdin);
                        return -1;
                    }
                }
            }
            if (fdin == -1) // still -1
            {
                char buff[256];
                snprintf(buff, 256, "redirection: open: '%s'", value);
                perror(buff);
                return -1;
            }
        } else if (prefdin == -1) {             // second redir, create TMP
            prefdin = fdin;                     // use this TMP for the rest
            fdin = open("/tmp", O_RDWR | O_TMPFILE, S_IRUSR | S_IWUSR);
            if (fdin == -1) {
                perror("redirection: open: interal TMP file");
                return -1;
            }
            lseek(prefdin, 0, SEEK_SET);
            ssize_t retval = COPY(prefdin, fdin);
            close(prefdin);
            if (retval < 0) {
                close(fdin);
                return -1;
            }
            if ((rtype & REDIR_MASK) == 0) {
                int fd = open(value, O_RDONLY);
                if (fd != -1) {
                    count++;
                    retval = COPY(fd, fdin);
                    close(fd);
                    if (retval < 0) {
                        close(fdin);
                        return -1;
                    }
                } else {
                    char buff[256];
                    snprintf(buff, 256, "redirection: open: '%s'", value);
                    perror(buff);
                    close(fdin);
                    return -1;
                }
            } else {
                count++;
                ssize_t error;
                if (rtype & R_EOFINPUT) {
                    error = COPY_ENDSTR(STDIN_FILENO, fdin, value);
                } else {
                    error = write(fdin, value, strlen(value));
                    if (error >= 0) error = write(fdin, "\n", 1);
                }
                if (error < 0) {
                    close(fdin);
                    return -1;
                }
            }
        } else if ((rtype & REDIR_MASK) == 0) {   // following file redirs, use TMP
            prefdin = open(value, O_RDONLY);
            if (prefdin == -1) {
                char buff[256];
                snprintf(buff, 256, "redirection: open: '%s'", value);
                perror(buff);
                close(fdin);
                return -1;
            }
            count++;
            COPY(prefdin, fdin);
            close(prefdin);
        } else {                                // following raw input, use same TMP
            count++;
            ssize_t error;
            if (rtype & R_EOFINPUT) {
                error = COPY_ENDSTR(STDIN_FILENO, fdin, value);
            } else {
                error = write(fdin, value, strlen(value));
                if (error >= 0) error = write(fdin, "\n", 1);
            }
            if (error < 0) {
                close(fdin);
                return -1;
            }
        }
    }
    if (fdin == -1) {
        restore_default_stream(STDIN_FILENO, default_std_fd[STDIN_FILENO]);
    } else {
        lseek(fdin, 0, SEEK_SET);
        dup2(fdin, STDIN_FILENO);
        close(fdin);
    }
    cmd->count_redir[0] = count;
    stdin = fdopen(STDIN_FILENO, mode); // FILE *stdin holds obsolete data => pipe or redirections
    setvbuf(stdin, NULL, _IONBF, 0);

    // redir standard output to file, pipe or both (restore if none)
    int fdout = -1;
    num_redirs = cmd->parsed.out.count;
    if (cmd->cond != COND_PIPE && num_redirs == 1) {
        char *value = cmd->parsed.out.redir[0].value;
        int flags = O_CREAT | O_WRONLY;
        int isAppend = cmd->parsed.out.redir[0].type & R_APPEND;
        flags |= (isAppend)? O_APPEND: O_TRUNC;
        mode = (isAppend)? "a": "w";
        fdout = redirect_stream(STDOUT_FILENO, value, flags, S_IRUSR | S_IWUSR);
        if (fdout != -1) close(fdout);
        else return -1;
    } else if (cmd->cond == COND_PIPE || num_redirs > 1) {
        fdout = redirect_stream(STDOUT_FILENO, "/tmp", O_RDWR | O_TMPFILE, S_IRUSR | S_IWUSR);
        mode = "w+"; //TODO: for stdout users (libc level), mode can be "w"
        if (fdout != -1) close(fdout);
        else return -1;
        if (cmd->cond == COND_PIPE) cmd->next->pipe = dup(STDOUT_FILENO);
    } else {
        restore_default_stream(STDOUT_FILENO, default_std_fd[STDOUT_FILENO]);
        mode = "w";
    }
    cmd->count_redir[1] = num_redirs;
    stdout = fdopen(STDOUT_FILENO, mode);
    setvbuf(stdout, NULL, _IONBF, 0);

    // redir errors to file or restore
    int fderr = -1;
    //num_redirs = cmd->parsed.count_redir[2];
    num_redirs = cmd->parsed.err.count;
    if (num_redirs == 1) {
        char *value = cmd->parsed.err.redir[0].value;
        int flags = O_CREAT | O_WRONLY;
        int isAppend = cmd->parsed.err.redir[0].type & R_APPEND;
        flags |= (isAppend)? O_APPEND: O_TRUNC;
        mode = (isAppend)? "a": "w";
        fderr = redirect_stream(STDERR_FILENO, value, flags, S_IRUSR | S_IWUSR);
        if (fderr != -1) close(fderr);
        else return -1;
    } else if (num_redirs > 1) {
        fderr = redirect_stream(STDERR_FILENO, "/tmp", O_RDWR | O_TMPFILE, S_IRUSR | S_IWUSR);
        mode = "w+"; //TODO: for stderr users (libc level), mode can be "w"
        if (fderr != -1) close(fderr);
        else return -1;
    } else {
        restore_default_stream(STDERR_FILENO, default_std_fd[STDERR_FILENO]);
        mode = "w";
    }
    cmd->count_redir[2] = num_redirs;
    stderr = fdopen(STDERR_FILENO, mode);
    setvbuf(stderr, NULL, _IONBF, 0);

    return 0;
}



static int exec_command(command_t *cmd, cond_t *condp)
{
    *condp = cmd->cond;
    if (*condp == COND_QUIT) return 0;
    
    char **argv = cmd->parsed.arg.argv;
    int argc = cmd->parsed.arg.argc;

    if (argc == 0 &&
        !cmd->count_redir[0] &&
        !cmd->count_redir[1] &&
        !cmd->count_redir[2] &&
        cmd->pipe == -1) return 0;

    //let's do it!
    int status;
    if (!argc) status = COPY(STDIN_FILENO, STDOUT_FILENO);
    else if (!strcmp("!", argv[0]))         status = main_history(argc, argv);
    else if (!strcmp(".", argv[0]))         status = main_source(argc, argv);
    else if (!strcmp("argv", argv[0]))      status = main_argv(argc, argv);
    else if (!strcmp("basename", argv[0]))  status = main_bn(argc, argv);
    else if (!strcmp("cat", argv[0]))       status = main_cat(argc, argv);
    else if (!strcmp("cd", argv[0]))        status = main_cd(argc, argv);
    else if (!strcmp("chmod", argv[0]))     status = main_chmod(argc, argv);
    else if (!strcmp("close", argv[0]))     status = main_close(argc, argv);
    else if (!strcmp("cmp", argv[0]))       status = main_cmp(argc, argv);
    else if (!strcmp("cp", argv[0]))        status = main_cp(argc, argv);
    else if (!strcmp("crc32", argv[0]))     status = main_crc32(argc, argv);
    else if (!strcmp("dd", argv[0]))        status = main_dd(argc, argv);
    else if (!strcmp("dir", argv[0]))       status = main_dir(argc, argv);
    else if (!strcmp("dirname", argv[0]))   status = main_dn(argc, argv);
    else if (!strcmp("du", argv[0]))        status = main_du(argc, argv);
    else if (!strcmp("dup", argv[0]))       status = main_dup(argc, argv);
    else if (!strcmp("dup2", argv[0]))      status = main_dup2(argc, argv);
    else if (!strcmp("echo", argv[0]))      status = main_echo(argc, argv);
    else if (!strcmp("env", argv[0]))       status = main_env(argc, argv);
    else if (!strcmp("export", argv[0]))    status = main_export(argc, argv);
    else if (!strcmp("fchmod", argv[0]))    status = main_fchmod(argc, argv);
    else if (!strcmp("fchmodat", argv[0]))  status = main_fchmodat(argc, argv);
    else if (!strcmp("fcmp", argv[0]))      status = main_fcmp(argc, argv);
    else if (!strcmp("find", argv[0]))      status = main_find(argc, argv);
    else if (!strcmp("ftruncate", argv[0])) status = main_ftruncate(argc, argv);
    else if (!strcmp("glob", argv[0]))      status = main_glob(argc, argv);
    else if (!strcmp("grep", argv[0]))      status = main_grep(argc, argv);
    else if (!strcmp("help", argv[0]))      status = main_help(argc, argv);
    else if (!strcmp("hexdump", argv[0]))   status = main_hexdump(argc, argv);
    else if (!strcmp("history", argv[0]))   status = main_history(argc, argv);
    else if (!strcmp("ioctl", argv[0]))     status = main_ioctl(argc, argv);
    else if (!strcmp("linkat", argv[0]))    status = main_linkat(argc, argv);
    else if (!strcmp("ln", argv[0]))        status = main_ln(argc, argv);
    else if (!strcmp("ls", argv[0]))        status = main_ls(argc, argv);
    else if (!strcmp("lsreel", argv[0]))    status = main_lsreel(argc, argv);
    else if (!strcmp("lseek", argv[0]))     status = main_lseek(argc, argv);
    else if (!strcmp("lsof", argv[0]))      status = main_lsof(argc, argv);
    else if (!strcmp("meminfo", argv[0]))   status = main_meminfo(argc, argv);
    else if (!strcmp("mkdir", argv[0]))     status = main_mkdir(argc, argv);
    else if (!strcmp("mv", argv[0]))        status = main_mv(argc, argv);
    else if (!strcmp("open", argv[0]))      status = main_open(argc, argv);
    else if (!strcmp("openat", argv[0]))    status = main_openat(argc, argv);
    else if (!strcmp("pwd", argv[0]))       status = main_pwd(argc, argv);
    else if (!strcmp("read", argv[0]))      status = main_read(argc, argv);
    else if (!strcmp("readlink", argv[0]))  status = main_readlink(argc, argv);
    else if (!strcmp("realpath", argv[0]))  status = main_realpath(argc, argv);
    else if (!strcmp("rename", argv[0]))    status = main_rename(argc, argv);
    else if (!strcmp("renameat", argv[0]))  status = main_renameat(argc, argv);
    else if (!strcmp("rm", argv[0]))        status = main_rm(argc, argv);
    else if (!strcmp("rmdir", argv[0]))     status = main_rmdir(argc, argv);
    else if (!strcmp("seekdir", argv[0]))   status = main_seekdir(argc, argv);
    else if (!strcmp("set", argv[0]))       status = main_set(argc, argv);
    else if (!strcmp("source", argv[0]))    status = main_source(argc, argv);
    else if (!strcmp("stat", argv[0]))      status = main_stat(argc, argv);
    else if (!strcmp("stty", argv[0]))      status = main_stty(argc, argv);
    else if (!strcmp("tee",argv[0]))        status = main_tee(argc, argv);
    else if (!strcmp("touch",argv[0]))      status = main_touch(argc, argv);
    else if (!strcmp("tree", argv[0]))      status = main_tree(argc, argv);
    else if (!strcmp("truncate", argv[0]))  status = main_truncate(argc, argv);
    else if (!strcmp("type", argv[0]))      status = main_type(argc, argv);
    else if (!strcmp("unset", argv[0]))     status = main_unset(argc, argv);
    else if (!strcmp("umask", argv[0]))     status = main_umask(argc, argv);
    else if (!strcmp("wc",argv[0]))         status = main_wc(argc, argv);
    else if (!strcmp("write", argv[0]))     status = main_write(argc, argv);
    else if (!strcmp("writechars", argv[0]))status = writechars(argc, argv);
    // roae shell commands
    else if (!strcmp("roae", argv[0]))      status = main_roae(argc, argv);
    else if (!strcmp("siard", argv[0]))     status = main_siard(argc, argv);
    else if (!strcmp("sqlite", argv[0]))    status = main_sqlite(argc, argv);
    else if (!strcmp("unzip", argv[0]))     status = main_unzip(argc, argv);
    // end roae shell cmds
    else if (!strcmp("exit", argv[0]) || !strcmp("quit", argv[0])){
        fprintf(stderr, "exit\n");
        *condp = COND_QUIT;
        status = 0;
        if (argv[1]) status = atoi(argv[1]);
    } else {                                status = main_spawn(argc, argv);}

    char number[12];    // update local var
    int id = atoi(getvar(&localvars, "$")) + 1;
    snprintf(number, 12, "%d", id);
    setvar(&localvars, "$", number);    // $$ -> command number
    snprintf(number, 12, "%d", status);
    setvar(&localvars, "?", number);    // $? -> last command status

    return status;
}

// flush stdout and stderr if there are multiple redirections
static void flush_command(command_t *cmd)
{
    // if stdout is redirected to more than one stream (files and/or pipe): lseek/COPY
    fflush(stdout);
    int count = cmd->count_redir[1];
    if ((cmd->cond == COND_PIPE && count) || count > 1) {
        for (int i = 0; i < count; i++) {
            int flags = O_CREAT | O_WRONLY;
            flags |= (cmd->parsed.out.redir[i].type & R_APPEND)? O_APPEND: O_TRUNC;
            int fd = open(cmd->parsed.out.redir[i].value, flags, S_IWUSR | S_IRUSR);
            if (fd == -1) {
                char buff[256];
                snprintf(buff, 256, "%s (out): open: %s", __func__, cmd->parsed.out.redir[i].value);
                perror(buff);
                continue;
            }
            lseek(STDOUT_FILENO, 0, SEEK_SET);
            COPY(STDOUT_FILENO, fd);
            close(fd);
        }
    }
    // if stderr is redirected to more than one stream (files): lseek/COPY
    fflush(stderr);
    count = cmd->count_redir[2];
    if (count > 1) {
        for (int i = 0; i < count; i++) {
            int flags = O_CREAT | O_WRONLY;
            flags |= (cmd->parsed.err.redir[i].type & R_APPEND)? O_APPEND: O_TRUNC;
            int fd = open(cmd->parsed.err.redir[i].value, flags, S_IWUSR | S_IRUSR);
            if (fd == -1) {
                char buff[256];
                snprintf(buff, 256, "%s (err): open: %s", __func__, cmd->parsed.out.redir[i].value);
                perror(buff);
                continue;
            }
            lseek(STDERR_FILENO, 0, SEEK_SET);
            COPY(STDERR_FILENO, fd);
            close(fd);
        }
    }
}

// Restore environment vars overwrited by command vars
static void clean_command(command_t *cmd)
{
    // restore environment (overwrited by local vars)
    char *key;
    while (NULL != (key = getkey(&envrestore, 0))) {
        char *val = getvar(&envrestore, key);
        if (val) {                      // restore environment
            setenv(key, val, 1);
        } else {
            unsetenv(key);
        }
        unsetvar(&envrestore, key);
    }
    
    command_clear(cmd, 1); // keep the readed arena
}

static cond_t exec_line(cmdline_t *cmdline, int *default_std_fd)
{
    cond_t cond;
    int status;
    command_t *nxt = cmdline->first_cmd;
    command_t *cmd;
    do {
        cmd = nxt;
        parse_command(cmd);
        //command_print_verbose(cmd);
        if (redir_command(cmd, default_std_fd) == -1) {
            clean_command(cmd);
            break;
        }
        status = exec_command(cmd, &cond);
        flush_command(cmd);
        nxt = cmd->next;
        clean_command(cmd);
    } while (cond == COND_SEQ || cond == COND_BACK || cond == COND_PIPE ||
            (cond == COND_AND && status == 0) ||
            (cond == COND_OR && status != 0));

    restore_default_streams(default_std_fd);
    return cond;
}


// -----------------------------------------------------------------------
//                            MAIN
// -----------------------------------------------------------------------
int main(void)
{
    cond_t cond;
    char prompt[PATH_MAX+3];

    int default_std_fd[3] = {-1, -1, -1};
    save_default_streams(default_std_fd);

    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        printf("Type \"help\" for a list of commands.\n");
        printf("Type \"quit\", \"exit [err]\" or ^D to exit.\n");
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    // Termios tty configuration, use ICANON|ECHO if the tty
    // where running this program has not ICANON nor ECHO.
    struct termios tty;
    ioctl(STDIN_FILENO, TCGETS, &tty);
    tty.c_lflag |= ICANON | ECHO;
    //tty.c_lflag &= ~ICANON & ~ECHO;
    ioctl(STDIN_FILENO, TCSETS, &tty);

    // Initialize sqlite shell
    sqlite_shell_init();

    setvar(&localvars, "$", "1");
    setvar(&localvars, "?", "0");
    do {
        char *ptype = getvar(&localvars, "PROMPT");
        if (ptype && atoi(ptype) == 1) {
            strcpy(prompt, "> ");
        } else {
            snprintf(prompt, PATH_MAX+3, "%s> ", getcwd(cwd, PATH_MAX));
        }
        cmdline_t *cmdline = get_line(prompt);
        if (!isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) cmdline_print(cmdline);
        cond = exec_line(cmdline, default_std_fd);
        if (isatty(STDIN_FILENO)) history_push(cmdline);
    } while (cond != COND_QUIT);

    close_default_streams(default_std_fd);

    char *errval = getvar(&localvars, "?");    // $? -> last command status
    int err = atoi(errval);
    return err;
}
// End of shell
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Source functions
////////////////////////////////////////////////////////////////////////////////
static int main_source(int argc, char *argv[])
{
    cond_t cond;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script_file>\n", argv[0]);
        return -1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: %s", argv[0], argv[1]);
        perror(errbuff);
        return -1;
    }
    dup2(fd, STDIN_FILENO);
    close(fd);

    int default_std_fd[3] = {-1, -1, -1};
    save_default_streams(default_std_fd);
    do {
        cmdline_t *cmdline = get_line("SOURCE> "); // TODO: prompt should be NULL
        cond = exec_line(cmdline, default_std_fd);
        cmdline_clear(cmdline, 0);
    } while (cond != COND_QUIT);
    close_default_streams(default_std_fd);

    char *errval = getvar(&localvars, "?");    // $? -> last command status
    int err = atoi(errval);
    return err;
}
// End of Source
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Path functions
////////////////////////////////////////////////////////////////////////////////

// Use checkpath() to check if the 'pathname' part is a valid directory where
//     create new 'basename' file or directorio
// resolve absolute file name; all but the last component must exist
char* checkpath(const char *name, char *canon_name) //TODO static (used is shell.c for test)
{
    char *p;
    const char *base_name;
    const char *pathname;
    char buff[PATH_MAX];
    struct stat st;
    
    strcpy(buff, name);
    pathname = dirname(buff);
    p = strrchr((char *)name, '/');
    if (p) {
        base_name = p+1; // base_name is '' if input name is '/'; function basename() returns '/'
    } else {
        base_name = name;
    }
    if (base_name[0] == '\0' ||                             // ''
        (base_name[0] == '.' && (base_name[1] == '\0' ||    // '.'
        (base_name[1] == '.' && base_name[2] == '\0')))) {  // '..'
        errno = EINVAL;
        return NULL;
    }
    if (!realpath(pathname, canon_name)) {
        errno = ENOENT;
        return NULL;
    }
    if (lstat(canon_name, &st) < 0) { // should never happen
        errno = ENOENT;
        return NULL;
    }
    if (! S_ISDIR(st.st_mode)) { // it's a regular file or symlink
        errno = ENOTDIR;
        return NULL;    
    }
    if (canon_name[1]) strcat(canon_name, "/"); // avoid '//'
    //if (base_name[0] != '/') strcat(canon_name, base_name); // avoid '//' if function 'basename()'nis used
    strcat(canon_name, base_name);
    return canon_name;
}

// no path components need exist or be a directory
static int resolve_softpath(char *path, char *result, char *pos)
{
    if (*path == '/') {
        *result = '/';
        pos = result+1;
        path++;
    }
    *pos = 0;
    if (!*path) return 0;
    while (1) {
        char *slash;
        slash = *path ? strchr(path,'/') : NULL;
        if (slash) *slash = 0;
        if (!path[0] || (path[0] == '.' &&
           (!path[1] || (path[1] == '.' && !path[2])))) {
            pos--;
            if (pos != result && path[0] && path[1])
                while (*--pos != '/');
        }
        else {
            strcpy(pos,path);
            pos = strchr(result,0);
        }
        if (slash) {
            *pos++ = '/';
            path = slash+1;
        }
        *pos = 0;
        if (!slash) break;
    }
    return 0;
}

// no path components need exist or be a directory
static char* softpath(const char *__restrict path, char *__restrict resolved_path)
{
    char cwd[PATH_MAX];
    char *path_copy;
    int res;
    errno = 0;
    if (!*path) {
        errno = ENOENT; /* SUSv2 */
        return NULL;
    }
    int allocated = 0;
    if (resolved_path == NULL) {
        // If  resolved_path is specified as NULL, then realpath() uses malloc(3)
        // to allocate a buffer of up to PATH_MAX bytes to hold the resolved
        // pathname, and returns a pointer to this buffer
        allocated = 1;
        resolved_path = (char * __restrict)malloc(PATH_MAX*sizeof(char));
        if (!resolved_path) return NULL;
    }
    #ifndef __ivm64__
        if (!getcwd(cwd,sizeof(cwd))) {
            if (allocated) free(resolved_path); 
            return NULL;
        }
        strcpy(resolved_path, "/");
        if (resolve_softpath(cwd, resolved_path, resolved_path)) {
            if (allocated) free(resolved_path); 
            return NULL;
        }
        strcat(resolved_path, "/");
    #else
        if (*path != '/') {
            if (!getcwd(cwd,sizeof(cwd))) {
                if (allocated) free(resolved_path); 
                return NULL;
            }
            strcpy(resolved_path, "/");
            if (resolve_softpath(cwd, resolved_path, resolved_path)){
                if (allocated) free(resolved_path); 
                return NULL;
            }
            if (strcmp(resolved_path, "/") != 0) strcat(resolved_path, "/");
        } else {
            strcpy(resolved_path, "/");
        }
    #endif
    path_copy = strdup(path);
    if (!path_copy) return NULL;
    res = resolve_softpath(path_copy, resolved_path, strchr(resolved_path,0));
    free(path_copy);
    if (res) {
        if (allocated) free(resolved_path);
        return NULL;
    }
    if (!*resolved_path) strcat(resolved_path, "/");
    return resolved_path;
}

static int main_cd(int argc, char *argv[])
{
    char *d;
    if (argc==1) {
        d = getenv("HOME");
        if (!d) d = (char *)"/";
    } else d = argv[1];

    char buff[PATH_MAX];
    softpath(d, buff);
    int c = chdir(d);
    if (c) {
        int errsv = errno;
        if (chdir(".")) strcpy(cwd, "(unreachable)");
        char errbuff[256];
        snprintf(errbuff, 256, "%s: chdir: %s", argv[0], d);
        errno = errsv;
        perror(errbuff);
    }
    d = getcwd(cwd, PATH_MAX);
    printf("Changed to dir '%s'\n", d);
    return c;
}
////////////////////////////////////////////////////////////////////////////////
// End of path function
////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////
// ACTIONS

#define SIZEDIFER -1
#define ERR_READ1 -2
#define ERR_READ2 -3

static ssize_t fcmp(int fd1, int fd2)
{
    char buf1[BUFSIZ];
    char buf2[BUFSIZ];
    struct stat st;

    if (fd1 == fd2) {
        printf("Same file descriptor\n");
        return 0;
    }

    fstat(fd1, &st);
    unsigned long ino1 = st.st_ino;
    ssize_t size1 = st.st_size;
    
    fstat(fd2, &st);
    if (ino1 == st.st_ino) {
        printf("Same file\n");
        return 0;
    }
    if (size1 != st.st_size) return SIZEDIFER;

    int count = 0;
    do {
        ssize_t count1 = 0;
        ssize_t count2 = 0;
        do {
            int nread = read(fd1, buf1, BUFSIZ);
            if (nread == -1) return ERR_READ1;
            if (nread == 0) break;
            count1 += nread;
        } while (count1 < BUFSIZ);
        do {
            int nread = read(fd2, buf2, BUFSIZ);
            if (nread == -1) return ERR_READ2;
            if (nread == 0) break;
            count2 += nread;
        } while (count2 < BUFSIZ);
        if (count1 != count2) {
            return MIN(count1, count2);
        }
        int i;
        for (i = 0; i < count1; i++)
            if (buf1[i] != buf2[i]) break;
        count += i;
        if (i < count1) return count;
    } while (count < size1);
    
    return 0;
}

static int main_fcmp(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s fildes1 fildes2\n", argv[0]);
        return -1;
    }
    char errbuff[256];
    errno = 0;
    int fd1 = strtol(argv[1], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: strtol: %s", argv[0], argv[1]);
        perror(errbuff);
        return -1;
    }
    errno = 0;
    int fd2 = strtol(argv[2], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: strtol: %s", argv[0], argv[2]);
        perror(errbuff);
        return -1;
    }
    int ret = fcmp(fd1, fd2);
    
    switch (ret) {
        case SIZEDIFER:
            fprintf(stderr, "%s: fildes %d and %d difer in size\n",
                            argv[0], fd1, fd2);
            return -1;
        case ERR_READ1:
            snprintf(errbuff, 256, "%s: read: fildes %d", argv[0], fd1);
            break;
        case ERR_READ2:
            snprintf(errbuff, 256, "%s: read: fildes %d", argv[0], fd2);
            break;
        case 0: break;
        default:
            fprintf(stderr, "%s: fildes %d and %d difer in byte %d\n",
                            argv[0], fd1, fd2, ret);
            return -1;
    }
    if (ret) perror(errbuff);
    return ret;
}

static int main_cmp(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s FILE1 FILE2\n", argv[0]);
        return -1;
    }
    char errbuff[256];
    int fd1 = open(argv[1], O_RDONLY);
    if (fd1 == -1) {
        snprintf(errbuff, 256, "%s: open: '%s'", argv[0], argv[1]);
        perror(errbuff);
        return -1;
    }
    int fd2 = open(argv[2], O_RDONLY);
    if (fd2 == -1) {
        snprintf(errbuff, 256, "%s: open: '%s'", argv[0], argv[2]);
        perror(errbuff);
        close(fd1);
        return -1;
    }
    int ret = fcmp(fd1, fd2);
    
    switch (ret) {
        case SIZEDIFER:
            fprintf(stderr, "%s: files '%s' and '%s' difer in size\n",
                            argv[0], argv[1], argv[2]);
            ret = 1;
            break;
        case ERR_READ1:
            snprintf(errbuff, 256, "%s: read: '%s'", argv[0], argv[1]);
            break;
        case ERR_READ2:
            snprintf(errbuff, 256, "%s: read: '%s'", argv[0], argv[2]);
            break;
        case 0: break;
        default:
            fprintf(stderr, "%s: files '%s' and '%s' difer in byte %d\n",
                            argv[0], argv[1], argv[2], ret);
            ret = 1;
            break;
    }
    if (ret < 0) perror(errbuff);
    close(fd1);
    close(fd2);
    return ret;
}


static int main_cat(int argc, char *argv[])
{
    int fd;
    int ret = 0;

    if (argc == 1) {
        int n = COPY(STDIN_FILENO, STDOUT_FILENO);
        if (n == -1) {
            char buff[256];
            snprintf(buff, 256, "%s: read: STDIN_FILENO", argv[0]);
            perror(buff);
            ret = 1;
        } else if (n == -2) {
            char buff[256];
            snprintf(buff, 256, "%s: write: STDOUT_FILENO", argv[0]);
            perror(buff);
            ret = 1;
        }
    } else {
        for (int i = 1; i < argc; i++)
        {
            fd=open(argv[i], O_RDONLY);
            if (fd == -1) {
                char buff[256];
                snprintf(buff, 256, "%s: open: %s", argv[0], argv[i]);
                perror(buff);
                ret++;
                continue;
            }
            COPY(fd, STDOUT_FILENO);
            close(fd);
        }
    }
    return ret;
}

static int main_tee(int argc, char *argv[])
{
    int fd[argc];
    int flags = O_CREAT | O_WRONLY;

    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        flags |= O_APPEND;
        argv++;
        argc--;
    } else {
        flags |= O_TRUNC;
    }

    int nfd = 0;
    fd[nfd++] = STDOUT_FILENO;
    for (int i = 1; i < argc; i++) {
        int fdo;
        fdo = open(argv[i], flags, S_IRUSR | S_IWUSR);
        if (fdo == -1) {
            char buff[256];
            snprintf(buff, 256, "%s: open: %s", argv[0], argv[2]);
            perror(buff);
            return -4;
        }
        fd[nfd++] = fdo;
    }

    char buf[BUFSIZ];
    ssize_t ret = 0;
    ssize_t rlen;
    while ((rlen = READ(STDIN_FILENO, buf, BUFSIZ)) > 0) {
        for (int i = 0; i < nfd; i++) {
            ssize_t off = 0;
            do {
                off += WRITE(fd[i], buf + off, rlen - off);
            } while (off < rlen);
        }
        ret += rlen;
    }

    for (int i = 1; i < nfd; i++) close(fd[i]);

    return 0;
}

static int copyat(int dirorigfd, char *orig, int dirdestfd, char *dest)
{
    int fdi, fdo;

    fdi = openat(dirorigfd, orig, O_RDONLY);
    if (fdi == -1) return -3;
    struct stat st;
    if (fstat(fdi, &st) == -1) {
        close(fdi);
        return -4;
    }
    fdo = openat(dirdestfd, dest, O_CREAT | O_WRONLY | O_TRUNC, st.st_mode);
    if (fdo == -1) {
        close(fdi);
        return -5;
    }
    int res = COPY(fdi, fdo); // 0: ok, -1: READ, -2: WRITE
    close(fdi);
    close(fdo);
    return (res < 0)? res: 0;
}

static int main_cp(int argc, char *argv[])
{
    #define BUFFSIZE (PATH_MAX*2)
    char buff[BUFFSIZE];
    if (argc < 3) {
        fprintf(stderr, "Usage: cp SOURCE DEST\n");
        fprintf(stderr, "\tCopy SOURCE to DEST, or copy SOURCE(s) to DIRECTORY\n");
        return -1;
    }
    int nsources = argc - 2;
    char *source = argv[1];
    char *dest = argv[argc-1];
    struct stat st;
    int dest_is_dir = ((fstatat(AT_FDCWD, dest, &st, 0) == 0) && (S_ISDIR(st.st_mode)));

    // if there is a source and a destination then 'try as is'
    if (nsources == 1 && !dest_is_dir) {
        int res = copyat(AT_FDCWD, source, AT_FDCWD, dest);
        switch (res) {
            error_out: perror(buff);
            case 0:    return res;
            case -1:   snprintf(buff, BUFFSIZE, "%s: read: %s", argv[0], source);
                       goto error_out;
            case -2:   snprintf(buff, BUFFSIZE, "%s: write: %s", argv[0], source);
                       goto error_out;
            case -3:   snprintf(buff, BUFFSIZE, "%s: open: %s", argv[0], source);
                       goto error_out;
            case -4:   snprintf(buff, BUFFSIZE, "%s: fstat: %s", argv[0], source);
                       goto error_out;
            // res == -5, dest may be directory
            default:   break;
        }
    }
    // is dest directory?
    if (!dest_is_dir) {
        snprintf(buff, BUFFSIZE, "target '%s' is not a directory\n", dest);
        perror(buff);
        return -2;
    }

    int err = 0;
    char dir_buf[PATH_MAX];
    char *directory = realpath(dest, dir_buf);
    for (int i = 1; i < argc-1; i++) {
        strncpy(buff, argv[i], BUFFSIZE-1);
        source = basename(buff);
        char newname[PATH_MAX];
        snprintf(newname, PATH_MAX, "%s/%s", directory, source);
        if (copyat(AT_FDCWD, argv[i], AT_FDCWD, newname) < 0) {
            if (fstatat(AT_FDCWD, argv[i], &st, AT_SYMLINK_NOFOLLOW) == -1) {
                snprintf(buff, BUFFSIZE, "%s: cannot stat (source) '%s'", argv[0], argv[i]);
                perror(buff);
                err++;
            } else if (fstatat(AT_FDCWD, dest, &st, AT_SYMLINK_NOFOLLOW) == -1) {
                snprintf(buff, BUFFSIZE, "%s: cannot stat (dest) '%s'", argv[0], dest);
                perror(buff);
                err++;		
            } else {    // access problem? permision denied?
                snprintf(buff, BUFFSIZE, "%s: cannot copy '%s' to '%s'", argv[0], argv[i], dest);
                perror(buff);
                err++;
            }
        }
    }
    return err; 
}

// CRC32 from https://gist.github.com/timepp/1f678e200d9e0f2a043a9ec6b3690635
//
// usage: the following code generates crc for 2 pieces of data
// uint32_t table[256];
// crc32_generate_table(table);
// uint32_t crc = crc32_update(table, 0, data_piece1, len1);
// crc = crc32_update(table, crc, data_piece2, len2);
// output(crc);

static uint32_t crc32_table[256];
static void crc32_generate_table(uint32_t* table)
{
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (size_t j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            }
            else {
                c >>= 1;
            }
        }
        table[i] = c;
    }
}

static uint32_t crc32_update(uint32_t* table, uint32_t initial, const void* buf, size_t len)
{
    uint32_t c = initial ^ 0xFFFFFFFF;
    const uint8_t* u = (const uint8_t*)(buf);
    for (size_t i = 0; i < len; ++i) {
        c = crc32_table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFF;
}

static uint32_t crc32_compute(const char* filename, int *err)
{
    errno =0;
    *err = 1;
    static int do_table = 1;
    if (do_table){
        crc32_generate_table(crc32_table);
        do_table = 0;
    }
    uint32_t crc = 0;
    int fh = open(filename, O_RDONLY);
    if (fh>=0) {
        uint8_t buff[256];
        ssize_t r = 1;
        while ( 0 < (r = read(fh, buff, 256))) {
            crc = crc32_update(crc32_table, crc, buff, r);
        }
        close(fh);
        if (!errno) {
            *err = 0;
        }
    }
    return crc;
}

static int main_crc32(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Compute the CRC32 hash of a file.\nUsage:\n");
        printf("       %s <filename>\n", argv[0]);
        return -1;
    }
    int err = 1;
    uint32_t crc = crc32_compute(argv[1], &err);
    if (!err) printf("%08x\n", crc);
}


static int main_dd(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s if=<input file> of=<output file> [count=<num item>] [bs=<tam item>]\n",argv[0]);
        return -1;
    }
    char *comm = *argv++;
    char *ifname = NULL, *ofname = NULL;
    unsigned long bs = 512;
    unsigned long count = 0;
    ssize_t nbytes = -1; //the entire file, until EOF (rlen == 0)
    while (*argv) {
        if ((argv[0][0]=='i')&&(argv[0][1]=='f')&&(argv[0][2]=='=')) {
            ifname = &argv[0][3];
        } else if ((argv[0][0]=='o')&&(argv[0][1]=='f')&&(argv[0][2]=='=')) {
            ofname = &argv[0][3];
        } else if ((argv[0][0]=='b')&&(argv[0][1]=='s')&&(argv[0][2]=='=')) {
            bs = atol(&argv[0][3]);
        } else if (strncmp(argv[0],"count=",6)==0) {
            count = atol(&argv[0][6]);
        } else {
            fprintf(stderr,"Invalid argument: '%s'\n",argv[0]);
        }
        argv++;
    }
    if (!ifname || !*ifname) {
        fprintf(stderr, "Missing input file\n");
        return -1;
    }
    if (!ofname || !*ofname) {
        fprintf(stderr, "Missing output file\n");
        return -1;
    }
    if (bs == 0) {
        fprintf(stderr, "Invalid value for bs\n");
        return -1;
    }
    if (count) {
        nbytes = bs * count;
    }

    int ret = 0;
    char errbuff[256];
    int fdi = open(ifname, O_RDONLY);
    if (fdi == -1) {
        snprintf(errbuff, 256, "%s: open: %s", comm, ifname);
        ret = -1;
    }
    int fdo = open(ofname, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fdo == -1) {
        snprintf(errbuff, 256, "%s: open: %s", comm, ofname);
        ret = -1;
    }
    ssize_t rlen = 0, acc = 0;
    while (ret != -1 && (nbytes == -1 || nbytes > acc)) {
        char buf[bs];
        if ((rlen = read(fdi, buf, bs)) <= 0) break;
        ssize_t off = 0;
        do {
            if ((ret = write(fdo, buf + off, rlen - off)) < 0) break;
            off += ret;
        } while (off < rlen);
        acc += off;
    }
    if (ret == -1) {
        snprintf(errbuff, 256, "%s: write", comm);
    }
    if (rlen == -1) {
        snprintf(errbuff, 256, "%s: read", comm);
        ret = -1;
    }
    if (ret != -1) fprintf(stdout, "Transferred %ld\n", acc);
    else perror(errbuff);

    if (fdi != -1) close(fdi);
    if (fdo != -1) close(fdo);
    return ret;
}

static int main_rename(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <oldpath> <newpath>\n", argv[0]);
        return 1;
    }
    int ret = rename(argv[1], argv[2]);
    if (ret == -1) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: rename %s %s", argv[0], argv[1], argv[2]);
        perror(errbuff);
        return 2;
    }
    return 0;
}

static int main_renameat(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <olddirfd> <oldpath> <newdirfd> <newpath>\n", argv[0]);
        return 1;
    }
    errno = 0;
    long olddirfd = strtol(argv[1], NULL, 10);
    if (errno) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: olddirfd: %s", argv[0], argv[1]);
        perror(errbuff);
        return 2;
    }
    long newdirfd = strtol(argv[3], NULL, 10);
    if (errno) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: newdirfd: %s", argv[0], argv[3]);
        perror(errbuff);
        return 2;
    }
    int ret = renameat(olddirfd, argv[2], newdirfd, argv[4]);
    if (ret == -1) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: renameat: %ld %s %ld %s", argv[0],
                         olddirfd, argv[2], newdirfd, argv[4]);
        perror(errbuff);
        return 2;
    }
    return 0;
}

static void usage_ln(char *name)
{
    fprintf(stderr, "Usage: %s [-s] <oldfile> <newfile>\n",name);
    return;
}

static int main_ln(int argc, char *argv[])
{
    int status = 1;
    int (*link_func)(const char *, const char*);
    const char *func_name;
    char *comm_name = *argv++;
    argc--;
    if (!argc) {
        usage_ln(comm_name);
        return 1;
    }
    if (strcmp(*argv,"-s") == 0) {
        argv++;
        argc--;
        link_func = symlink;
        func_name = "symlink";
    } else {
        link_func = link;
        func_name = "link";
    }
    if (argc < 2) {
        usage_ln(comm_name);
        return -1;
    }
    status = link_func(argv[0], argv[1]);    
    if (status < 0){
        char buff[256];
        snprintf(buff,256,"%s: %s: %s",comm_name,func_name,argv[0]);
        perror(buff);
    }
    return status;
}

static int main_echo(int argc, char *argv[])
{
    int endl = 1;
    argc--;
    argv++;
    if (*argv && strcmp("-n",*argv)==0) {
        endl = 0;
        argc--;
        argv++;
    }
    if (*argv) printf("%s",*argv++);
    while (*argv) printf(" %s",*argv++);
    if (endl) printf("\n");
    return 0;
}

static int main_pwd(int argc, char *argv[])
{
    (void)argv;
    if (argc > 1) {
        printf("%s: too many arguments\n", argv[0]);
        return -1;
    }
    char *w;
    w = getcwd(cwd, PATH_MAX-1);
    if (!w) {
        perror("getwd");
        printf("Function getwd() FAILED!\n");
        return -1;
    }
    printf("%s\n", w);
    return 0;
}




// Recursive mkdir (mkdir of all parents)
static int rmkdir(char *dir, mode_t mode)
{
    if (strnlen(dir, PATH_MAX)>= PATH_MAX-1){
        return -1;
    }

    char *dn, buff[PATH_MAX];
    strcpy(buff, dir); 
    dn = dirname(buff);

    if (!dn || !*dn) {
        return 0;
    }

    struct stat s;
    if (!stat(dn, &s) && S_ISDIR(s.st_mode)) {
        // Its parent exists
        mkdir(dir, mode);
    } else {
        // Its parent does not exist, create it recursively
        rmkdir(dn, mode);
        mkdir(dir, mode);
    }

    // If finally it is created return no error
    if (!stat(dir, &s) && S_ISDIR(s.st_mode)) {
        return 0;
    } else {
        return -1;
    }
}

// int largest_memory_chunck(int high, int low, int steps)
//      Returns the size of the largest memory chunck available.
//      The search start in 2^high (HIGHER_BIT set to 48 in macro) and goes
//      iteratively down making a number of 'steps' refinement
static unsigned long largest_memory_chunck(int high, int low, int steps)
{
    unsigned long base = 0;
    long incr;
    void *ptr;
    int refine = 0;
    for (incr = (1UL << high); incr >= (1UL << low); incr >>= 1) {
        //printf("Trying: %ld (%ld + %ld)\n", base + incr, base, incr);
        ptr = malloc(base + incr);
        if (ptr) {
        //printf("\tGot: %ld (at: %p)\n", base + incr, ptr);
            free(ptr);
            ptr = NULL;
            base += incr;
            if (refine++ >= steps) break;
        }
    }
    return base;
}

#define HIGHER_BIT 48
#define LOWER_BIT  10
#define MAX_REFINEMENT 50
static int main_meminfo(int argc, char *argv[])
{
    void *p = sbrk(0);
    unsigned long space = ((unsigned long)&p-(unsigned long)p);
    printf("STACK: %p\nHEAP:  %p\nSpace between both: %ld (%#lx)\n", &p, p, space, space);
    unsigned long size = 0;
    void *ptr = NULL;
    int high = HIGHER_BIT;
    int low = LOWER_BIT;
    int steps = MAX_REFINEMENT;
    switch (argc) {
        case 4: steps = atoi(argv[3]);
        case 3: low = atoi(argv[2]);
        case 2: high = atoi(argv[1]);
        default:
    }
    printf("Exploring dinamic memory with high=%d, low=%d and refinement=%d\n", high, low, steps);
    size = largest_memory_chunck(high, low, steps);    
    printf("Max. memory chuck available: %ld\n", size);
    free(ptr);
    return 0;
}

static int main_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s directory\n", argv[0]);
        fprintf(stderr, "       %s -p directory\n", argv[0]);
        return -1;
    }
    char *d;
    long m;
    if (!strcmp(argv[1], "-p")) {
        if (argc < 3) {
            fprintf(stderr, "Missing directory name\n");
            return -1;
        }
        d = argv[2];
        m = rmkdir(d, 0777);
    } else {
        d = argv[1];
        m = mkdir(d, 0777);
    }
    if (m == -1) {
        char buff[256];
        snprintf(buff, 256, "%s: mkdir: creating directory '%s' FAILED!", argv[0], d);
        perror(buff);
        return -1;
    }
    fprintf(stderr, "Directory '%s' just created\n", d);// TODO: comment this line
    return 0;
}

static int main_rmdir(int argc, char *argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: %s directory\n", argv[0]);
        return -1;
    }
    char *d = argv[1];
    long m = rmdir(d);
    if (m == -1) {
        char buff[256];
        snprintf(buff, 256, "%s: rmdir: erasing directory '%s' FAILED!", argv[0], d);
        perror(buff);
        return -1;
    }
    fprintf(stderr, "Directory '%s' just removed\n", d);// TODO: comment this line
    return 0;
}

// Recursive rm (like rm -rf)
// To be safer, only path whose realpath contains needle are deleted
static int rrm_needle(char *path, char *needle)
{
    if (!path || !*path) return 0; // Null or empty string: do nothing

    if (needle) {
        // Do nothing if the path does not have the needle
        // (this is checked only in the first recursive invocation)
        char fullpath[PATH_MAX];
        char* rl = realpath(path, fullpath); fullpath[PATH_MAX-1]='\0';
        if (!rl || !strstr(fullpath, needle)) return 0;
    }

    char d[PATH_MAX];
    strcpy(d, path);

    // If path is file or link, it can be deleted directly
    int ret = unlink(d);
    if (ret >= 0) {
        return 0;
    } 

    // Check if it is a directory
    DIR *od = opendir(d);
    if (od){
        closedir(od);
    } else {
        // It should be a directory, but it is not possible to open it
        return -1;
    }

    // It must be a readable directory at this point

    // Scan directory
    struct dirent **files;
    int nfiles = scandir(d, &files, NULL, alphasort);
    if (nfiles == -1) {
        perror("scandir");
        return -1;
    }

    long status = 0;

    // Save current directory
    char currwd[PATH_MAX];
    char *w = getcwd(currwd, PATH_MAX);
    if (!w) { return -1; }

    // Remove children recursively
    if (chdir(d) == 0) {
        for (long k=0; k<nfiles; k++){
            struct dirent *dd = files[k];
            // Ignore . and .. , to avoid infinite recursion
            if (strcmp(dd->d_name, ".") && strcmp(dd->d_name, "..")) {
                status |= rrm_needle(dd->d_name, NULL);
            }
            free(dd);
        }
        if (chdir(w) < 0) return -1;
    }

    // Remove the dir itself
    ret = rmdir(d);
    if (ret < 0) {
        perror("rmdir");
        fprintf(stderr, "Removing dir '%s' FAILED!\n", d);
    }
    status |= ret;

    free(files);
    return 0;
}


static int rrm(char *path)
{
    return rrm_needle(path, NULL);
}

static int main_rm(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Delete (unlink) files\n");
        fprintf(stderr, "Usage: %s file_to_delete1 file_to_delete2 ...\n", argv[0]);
        fprintf(stderr, "       %s -r directory  # recursive deletion \n", argv[0]);
        return -1;
    }
    int recursive = !strcmp("-r", argv[1]);
    int status = 0;
    for (int i = 1; i < argc; i++){
        char *name;
        if (recursive) {
            //Recursive deletion, first file starts after -r
            #ifdef __ivm64__
            if (i+1 < argc)
                status |= rrm(argv[i+1]);
            #else
            fprintf(stderr, "Recursive deletion only allowed for ivm64 filesystem\n");
            return -1;
            #endif
        } else {
            // Sequential deletion of regular/link files
            name = argv[i];
            int ret = unlink(name);
            if (ret < 0){
                char buff[256];
                snprintf(buff, 256, "%s: unlink: %s", argv[0], name);
                perror(buff);
                status |= ret;
            }
        }
    }
    return status;
}


/* recursive du (like du -s) */
#define HUMANSIZE(x) ((double)(((x)>1e12)?((x)/1.0e12):((x)>1e9)?((x)/1.0e9):((x)>1.0e6)?((x)/1.0e6):((x)>1e3)?((x)/1.0e3):(x)))
#define HUMANPREFIX(x)  (((x)>1e12)?"T":((x)>1e9)?"G":((x)>1e6)?"M":((x)>1e3)?"K":"")

static long rdu(char *path)
{
    if (!path || !*path) return 0; // Null or empty string: do nothing

    char d[PATH_MAX];
    //strcpy(d, path);
    softpath(path, d);

    // If path is file or link, the size can be taken directly
    struct stat s;
    int serr = lstat(d, &s);  // p->d_name is the base name (not the full name)
    if (!serr && !S_ISDIR(s.st_mode)) {
        return s.st_size;
    }

    // It must be a directory at this point

    // Scan directory
    struct dirent **files;
    int nfiles = scandir(d, &files, NULL, alphasort);
    if (nfiles == -1) {
        fprintf(stderr, "error scanning '%s'\n", d);
        perror("scandir");
        return -1;
    }

    // Save current directory
    char currwd[PATH_MAX];
    char *w = getcwd(currwd, PATH_MAX);
    if (!w) { perror("getcwd"); return -1; }

    // Count size of children recursively
    long size = 0;
    if (chdir(d) == 0) {
        for (long k=0; k<nfiles; k++){
            struct dirent *dd = files[k];
            // Ignore . and .. , to avoid infinite recursion
            if (strcmp(dd->d_name, ".") && strcmp(dd->d_name, "..")) {
                long dsize = rdu(dd->d_name);
                size += dsize;
            }
            free(dd);
        }
        if (chdir(w) < 0) return -1;
        printf("%ld\t(%.2f%sB) \t%s\n", size, HUMANSIZE(size), HUMANPREFIX(size), d);
    }
    free(files);
    return size;
}

static int main_du(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Total disk usage in bytes of a directory\n");
        fprintf(stderr, "Usage: %s <dir_name> \n", argv[0]);
        return -1;
    }
    long size = rdu(argv[1]);
    printf("%ld\t(%.2f%sB) \ttotal\n", size, HUMANSIZE(size), HUMANPREFIX(size));
    return 0;
}


static int main_mv(int argc, char *argv[])
{
    #define BUFFSIZE (PATH_MAX*2)
    char buff[BUFFSIZE];
    if (argc < 3) {
        fprintf(stderr, "Usage: mv SOURCE DEST\n");
        fprintf(stderr, "\tRename SOURCE to DEST, or move SOURCE(s) to DIRECTORY\n");
        return -1;
    }
    int nsources = argc - 2;
    char *source = argv[1];
    char *dest = argv[argc-1];
    struct stat st;
    int dest_is_dir = ((fstatat(AT_FDCWD, dest, &st, 0) == 0) && (S_ISDIR(st.st_mode)));
    // printf("1st source: %s\tdest: %s\tn.sources=%d\tdest_is_dir=%d\n",source,dest,nsources,dest_is_dir);
    // if there is a source and the destination is not a directory then rename
    if (nsources == 1 && !dest_is_dir) {
        return rename(source, dest);
    }
    // is dest directory?
    if (!dest_is_dir) {
        snprintf(buff, BUFFSIZE, "target '%s' is not a directory\n", dest);
        perror(buff);
        return -2;
    }

    int err = 0;
    char dir_buf[PATH_MAX];
    char *directory = realpath(dest, dir_buf);
    for (int i = 1; i < argc-1; i++) {
        strncpy(buff, argv[i], BUFFSIZE-1);
        source = basename(buff);
        char newname[PATH_MAX];
        snprintf(newname, PATH_MAX, "%s/%s", directory, source);
        if (rename(argv[i], newname) == -1) {
            if (fstatat(AT_FDCWD, argv[i], &st, AT_SYMLINK_NOFOLLOW) == -1) {
                snprintf(buff, BUFFSIZE, "%s: cannot stat (source) '%s'", argv[0], argv[i]);
                perror(buff);
                err++;
            } else if (fstatat(AT_FDCWD, dest, &st, AT_SYMLINK_NOFOLLOW) == -1) {
                snprintf(buff, BUFFSIZE, "%s: cannot stat (dest) '%s'", argv[0], dest);
                perror(buff);
                err++;		
            } else {    // access problem? permision denied?
                snprintf(buff, BUFFSIZE, "%s: cannot move '%s' to '%s'", argv[0], argv[i], dest);
                perror(buff);
                err++;
            }
        }
    }
    return err; 
}




static int usage_realpath(char *comm)
{
    fprintf(stderr, "Usage: %s [-e][-m] FILE\n", comm);
    fprintf(stderr, "\t all but the last component of the real path must exist\n");
    fprintf(stderr, "\t -e all components of the path must exist\n");
    fprintf(stderr, "\t -m no path components need exist or be a directory\n");
    return -1;
}
static int main_realpath(int argc, char *argv[])
{
    if (argc == 1) return usage_realpath(argv[0]);
    char *output, *input, buff[PATH_MAX];
    if (strcmp(argv[1],"-e") == 0) { //all components of the path must exist
        input = argv[2];
        if (!input) return usage_realpath(argv[0]);
        output = realpath(input, buff);
    } else if (strcmp(argv[1],"-m") == 0) { // no path components need exist or be a directory
        input = argv[2];
        if (!input) return usage_realpath(argv[0]);
        output = softpath(input, buff); // esto no es una 'syscall'
    } else { // all but the last component must exist
        input = argv[1];
        output = checkpath(input, buff); // esto no es una 'syscall'
    }
    if (!output) {
        char errbuff[PATH_MAX];
        snprintf(errbuff, PATH_MAX, "realpath: '%s'", input);
        perror(errbuff);
        return -1;
    }
    printf("Realpath of '%s' -> '%s'\n", input, output);
    return 0;
}

// Touch a regular file
static int main_touch(int argc, char *argv[])
{
    int status = 0;
    for (int i = 1; i < argc; i++){
        int fd = open(argv[i], O_CREAT|O_WRONLY, 0666);
        if (fd < 0){
            perror("open");
            printf("Touching file '%s' FAILED!\n", argv[i]);
            status++;
        }
        close(fd);
    }
    return status;
}


#define WC_INDEX_C 0
#define WC_INDEX_W 1
#define WC_INDEX_L 2
#define WC_SHOW_C (0x1 << WC_INDEX_C)
#define WC_SHOW_W (0x1 << WC_INDEX_W)
#define WC_SHOW_L (0x1 << WC_INDEX_L)
static int count_wc(int fileno, unsigned long *count)
{
    char buff[BUFSIZ];
    int stw = 0;
    ssize_t len;
    do {
        len = read(fileno, buff, BUFSIZ);
        if (len == -1) return -1;
        count[WC_INDEX_C] += len;
        for (int i = 0; i < len; i++) {
            if (buff[i] == '\n') count[WC_INDEX_L] += 1;
            if (buff[i] != ' ' && buff[i] != '\t' && buff[i] != '\n') {
               if (!stw) count[WC_INDEX_W] += 1;
               stw = 1;
            } else {
               stw = 0;
            } 
        }
    } while(len);
    return 0;
}

static void print_wc(int show, unsigned long *count, char *name)
{
    if (!show || (show & WC_SHOW_L)) printf("%7ld ", count[WC_INDEX_L]);
    if (!show || (show & WC_SHOW_W)) printf("%7ld ", count[WC_INDEX_W]);
    if (!show || (show & WC_SHOW_C)) printf("%7ld ", count[WC_INDEX_C]);
    puts(name?:"");
}

static int main_wc(int argc, char *argv[])
{
    unsigned long count[3];
    int show = 0;
    int res = 0;
    char *cmd = *argv++;
    argc--;
    while (argc) {
        if (strcmp(*argv, "-c") == 0)       show |= WC_SHOW_C;
        else if (strcmp(*argv, "-w") == 0)  show |= WC_SHOW_W;
        else if (strcmp(*argv, "-l") == 0)  show |= WC_SHOW_L;
        else break;
        argc--;
        argv++;
    }
    count[WC_INDEX_C] = 0;
    count[WC_INDEX_W] = 0;
    count[WC_INDEX_L] = 0;
    if (!argc) {
        res = count_wc(STDIN_FILENO, count);
        print_wc(show, count, NULL);
    } else {
        while (*argv) {
            char *name = *argv++;
            int fd = open(name, O_RDONLY);
            if (fd == -1) {
                char buff[256];
                snprintf(buff, 256, "%s: open: %s", cmd, name);
                perror(buff);
                res++;
                continue;
            }
            unsigned long c[3];
            c[WC_INDEX_C] = 0;
            c[WC_INDEX_W] = 0;
            c[WC_INDEX_L] = 0;
            res += count_wc(fd, c);
            print_wc(show, c, name);
            close(fd);
            count[WC_INDEX_C] += c[WC_INDEX_C];
            count[WC_INDEX_W] += c[WC_INDEX_W];
            count[WC_INDEX_L] += c[WC_INDEX_L];
        }
        if (argc > 1) print_wc(show, count, "total");
    }
    return res;
}

// Open a file
static void open_usage_common()
{
    fprintf(stderr, "\tflags in hexa\n\tmode in octal (rwxrwxrwx)\n");
    fprintf(stderr, "\t\tflags -> %#1x: O_RDONLY, %#1x: O_WRONLY, %#1x: O_RDWR\n", O_RDONLY,O_WRONLY,O_RDWR);
    fprintf(stderr, "\t\tflags -> %#10x: O_APPEND | flags\n", O_APPEND);
    fprintf(stderr, "\t\tflags -> %#10x: O_TRUNC | flags\n", O_TRUNC);
    fprintf(stderr, "\t\tflags -> %#10x: O_CREAT | flags\n", O_CREAT);
    fprintf(stderr, "\t\tflags -> %#10x: O_EXCL | flags\n", O_EXCL);
    fprintf(stderr, "\t\tflags -> %#10x: O_NOFOLLOW | flags\n", O_NOFOLLOW);
    fprintf(stderr, "\t\tflags -> %#10x: O_DIRECTORY | flags\n", O_DIRECTORY);
    fprintf(stderr, "\t\tflags -> %#10x: O_TMPFILE | flags\n", O_TMPFILE);
    fprintf(stderr, "\t\tflags -> %#10x: O_PATH | flags\n", O_PATH);
}

static int main_open(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s pathname flags\n", argv[0]);
        fprintf(stderr, "Usage: %s pathname flags mode\n", argv[0]);
        open_usage_common();
        return 1;
    }
    char *pathname = argv[1];
    int flags = (int)strtol(argv[2], NULL, 16);
    mode_t mode = 0;
    int fid;
    if ((flags & O_CREAT) || ((flags & O_TMPFILE)==O_TMPFILE)) {
        if (argc < 4) {
            fprintf(stderr, "open: missing mode with flag O_CREAT or O_TMPFILE(%#x)\n", flags);
            open_usage_common();
            return -1;
        }
        mode = (int)strtol(argv[3], NULL, 8);
        fid = open(pathname, flags, mode);
    } else {
        fid = open(pathname, flags);
    }
    if (-1 != fid){
        printf("File '%s' opened fid=%d with flags=%#x [mode=%#o]\n", pathname, fid, flags, mode);
    } else {
        perror("open");
        printf("Failed openenig file '%s' with flags %#x [mode=%#o]\n", pathname, flags, mode);
        return -1;
    }
    return 0;
}

static int main_openat(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s dirfd pathname flags\n", argv[0]);
        fprintf(stderr, "Usage: %s dirfd pathname flags mode\n", argv[0]);
        fprintf(stderr, "\t\tdirfd -> %d: AT_FDCWD | flags\n", AT_FDCWD);
        open_usage_common();
        return 1;
    }
    int dirfd = atoi(argv[1]);
    char *pathname = argv[2];
    int flags = (int)strtol(argv[3], NULL, 16);
    mode_t mode = 0;
    int fid;
    if ((flags & O_CREAT) || ((flags & O_TMPFILE)==O_TMPFILE)) {
        if (argc < 5) {
            fprintf(stderr, "open: missing mode with flag O_CREAT or O_TMPFILE(%#x)\n", flags);
            open_usage_common();
            return -1;
        }
        mode = (int)strtol(argv[4], NULL, 8);
        fid = openat(dirfd, pathname, flags, mode);
    } else {
        fid = openat(dirfd, pathname, flags);
    }
    if (-1 != fid){
        printf("File '%s' opened fid=%d with flags=%#x [mode=%#o]\n", pathname, fid, flags, mode);
    } else {
        perror("openat");
        printf("Failed openenig file '%s' in flags %#x [mode=%#o]\n", pathname, flags, mode);
        return -1;
    }
    return 0;
}

// Close an open file
static int main_close(int argc, char *argv[])
{
    if (argc < 2) {
         fprintf(stderr, "Usage: %s <fileno>\n", argv[0]);
         return 1;
    }
    int fid = atoi(argv[1]);
    int c = close(fid);
    if (-1 != c){
        printf("Closing file number fid=%d OK\n", fid);
    } else {
        printf("File number fid=%d cannot be closed\n", fid);
        return -1;
    }
    return 0;
}

static int main_glob(int argc, char *argv[])
{
// int glob(const char *pattern, int flags,
//                 int (*errfunc) (const char *epath, int eerrno),
//                 glob_t *pglob);
//
// void globfree(glob_t *pglob);
//
// DESCRIPTION
//        The  glob()  function  searches  for all the pathnames matching
//        pattern according to the rules used by the shell (see glob(7)).  No
//        tilde expansion or parameter substitution is done; if you want these,
//        use wordexp(3).
//
//        The globfree() function frees the dynamically allocated storage from
//        an earlier call to glob().
//
//        The results of a glob() call are stored in the structure pointed to
//        by pglob.  This structure is of type glob_t (declared in <glob.h>)
//        and includes the  following  elements  defined  by  POSIX.2 (more may
//        be present as an extension):
//
//            typedef struct {
//                size_t   gl_pathc;    /* Count of paths matched so far  */
//                char   **gl_pathv;    /* List of matched pathnames.  */
//                size_t   gl_offs;     /* Slots to reserve in gl_pathv.  */
//            } glob_t;
//
    if (argc <= 1) {
        fprintf(stderr, "List directories or files using glob wildcards\n");
        fprintf(stderr, "Usage: %s <glob expression>\n", argv[0]);
        return -1;
    }
    char *globexpr = argv[1];
    glob_t globbuf;

    glob(globexpr, GLOB_TILDE|GLOB_BRACE, NULL, &globbuf);

    for (long k=0; k<globbuf.gl_pathc && globbuf.gl_pathv[k]; k++){
        printf("%s ", globbuf.gl_pathv[k]);
    }
    #ifdef __ELF__
    printf("(%ld %ld)\n", globbuf.gl_pathc, globbuf.gl_offs);
    #else
    printf("(%d %d)\n", globbuf.gl_pathc, globbuf.gl_offs);
    #endif

    globfree(&globbuf);

    return 0;
}


int main_lsreel(int argc, char *argv[])
{
    int rdframefd = open("/dev/framein",  O_RDONLY);
    if (rdframefd == -1) {
        perror("open: /dev/framein");
        return 1;
    }
    long index = 0;
    long k = 4;
    for (;;) {
        //~ short int dim[2];
        //~ int err = read(rdframefd, dim, 4);
        short int dim[2 * k];
        int err = read(rdframefd, dim, 4 * k);
        if (err == -1) {
            perror("read: frame dimensions");
            return 2;
        }
        int i;
        for (i = 0; i < k; i++) {
            if (!dim[2 * i] || !dim[2 * i + 1]) break;
            printf("[%ld] %d x %d\n", index++, dim[2 * i], dim[2 * i + 1]);
        }
        if (i < k) break;
    }
    close(rdframefd);
    return 0;
}


#define WIDTHNAME 10
#define WIDTHFULLNAME 20
static void print_dirent(int dfd, struct dirent *p)
{
    if (p){
        char *name = p->d_name;

        struct stat s;
        int err = fstatat(dfd, name, &s, AT_SYMLINK_NOFOLLOW);
        if (err == -1){
            char errbuff[2048];
            snprintf(errbuff, 2048, "fstatat: '%s'", name);
            perror(errbuff);
            return;
        }
        mode_t mode = s.st_mode;
        long size = s.st_size;        

        char type[11];
        int isreg = S_ISREG(mode);
        int isdir = S_ISDIR(mode);
        int islnk = S_ISLNK(mode);
        int ischr = S_ISCHR(mode);
        int isblk = S_ISBLK(mode);
        type[0] = islnk?'l':isdir?'d':ischr?'c':isblk?'b':isreg?'-':'?';        
        type[1]=(mode & S_IRUSR)?'r':'-';
        type[2]=(mode & S_IWUSR)?'w':'-';
        type[3]=(mode & S_IXUSR)?'x':'-';
        type[4]=(mode & S_IRGRP)?'r':'-';
        //~ type[5]=(mode & S_IWGRP)?'w':'-';
        //~ type[6]=(mode & S_IXGRP)?'x':'-';
        //~ type[7]=(mode & S_IROTH)?'r':'-';
        //~ type[8]=(mode & S_IWOTH)?'w':'-';
        //~ type[9]=(mode & S_IXOTH)?'x':'-';
        //~ type[10]='\0';
        type[4]='\0';

        printf("%#lx\t% 8ld\t%s\t %-*s", p->d_ino, size, type, WIDTHNAME, name);
        if (islnk) {
            char buff[PATH_MAX];
            int len = readlinkat(dfd, name, buff, PATH_MAX-1);
            if (len >= 0) {
                    buff[len] = '\0';
                    printf(" -> %s", buff);
            }
        }
        printf("\n");
    }
}

static int main_seekdir(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s DIR loc\n", argv[0]);
        return 1;
    }
    // Open and list a directory
    DIR *dir = opendir(argv[1]);
    if (!dir) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: opendir: %s", argv[0], argv[1]);
        perror(errbuff);
        return 2;
    }
    errno = 0;
    long pos = strtol(argv[2], NULL, 10);
    if (errno) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: strtol: %s", argv[0], argv[2]);
        perror(errbuff);
        return 3;        
    }

    /* Scan directory */
    struct dirent *pdirent;
    int dfd = dirfd(dir);

    // scanning and printing the directory
    printf("scanning and printing the directory: %s\n", argv[1]);
    #if defined(__ivm64__)
    printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
    #else
    printf("inode \t\t dirent \t\t %-*s\n",WIDTHNAME,"name");
    printf("----- \t\t ------ \t\t %-*s\n",WIDTHNAME,"----");
    #endif    
    long i = 0;
    long loc[100];
    loc[0] = telldir(dir);
    while ((pdirent = readdir(dir)) != NULL) {
        i++;
        print_dirent(dfd, pdirent);            
        loc[i] = telldir(dir);
    }
    #if defined(__ivm64__)
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",20,"----",40,"--------");
    #else
    printf("----- \t\t ------ \t\t %-*s\n",20,"----");
    #endif
    
    // seekdir to 'loc' and list the rest of the directory
    printf("seekdir to %ld and list the rest of the directory\n", pos);
    #if defined(__ivm64__)
    printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
    #else
    printf("inode \t\t dirent \t\t %-*s\n",WIDTHNAME,"name");
    printf("----- \t\t ------ \t\t %-*s\n",WIDTHNAME,"----");
    #endif
    seekdir(dir, loc[pos]);
    while ((pdirent = readdir(dir)) != NULL) {
        print_dirent(dfd, pdirent);
    }
    #if defined(__ivm64__)
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",20,"----",40,"--------");
    #else
    printf("----- \t\t ------ \t\t %-*s\n",20,"----");
    #endif
    
    // using seekdir to list the directory from last to 'loc'
    printf("using seekdir to list the directory from last to %ld\n", pos);
    #if defined(__ivm64__)
    printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
    #else
    printf("inode \t\t dirent \t\t %-*s\n",WIDTHNAME,"name");
    printf("----- \t\t ------ \t\t %-*s\n",WIDTHNAME,"----");
    #endif
    for (long j = i-1; j >= pos; j--) {
        seekdir(dir, loc[j]);
        pdirent = readdir(dir);
        print_dirent(dfd, pdirent);
    }
    #if defined(__ivm64__)
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",20,"----",40,"--------");
    #else
    printf("----- \t\t ------ \t\t %-*s\n",20,"----");
    #endif


    if (closedir(dir) == -1) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: closedir: %s (loc=%s)", argv[0], argv[1], argv[2]);
        perror(errbuff);
        return 4;
    }

    return 0;
}

static int main_ls(int argc, char *argv[])
{
    char *cmdname = *argv++;
    char *dentryname;
    int isTTY = isatty(STDOUT_FILENO);
    if (!*argv || !**argv) dentryname = (char *)"."; // Null or empty string
    else dentryname = *argv++;

    while (dentryname) {
        DIR *dir = opendir(dentryname);
        if (!dir) { // file
            DIR *dot = opendir(".");
            if (!dot) {
                char errbuff[256];
                snprintf(errbuff, 256, "%s: opendir: .", cmdname);
                perror(errbuff);
                return 1;
            }
            int dfd = dirfd(dot);
            struct dirent *pdirent;
            int found = 0;
            while ((pdirent = readdir(dot)) != NULL) {
                if (strcmp(pdirent->d_name, dentryname) == 0) {
                    print_dirent(dfd, pdirent);
                    found = 1;
                    break;
                }
            }
            if (!found) fprintf(stderr, "%s: cannot access '%s': No such file or directory\n", cmdname, dentryname);
            closedir(dot);
            dentryname = *argv++;
            continue;
        }

        // Scan directory
        struct dirent *pdirent;
        #if defined(__ivm64__)
        printf("%s:\n", dentryname);
        if (isTTY) printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
        if (isTTY) printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
        #else
        if (isTTY) printf("inode \t\t dirent \t\t %-*s\n",WIDTHNAME,"name");
        if (isTTY) printf("----- \t\t ------ \t\t %-*s\n",WIDTHNAME,"----");
        #endif
        int dfd = dirfd(dir);
        while ((pdirent = readdir(dir)) != NULL) {
            print_dirent(dfd, pdirent);
        }
        #if defined(__ivm64__)
        if (isTTY) printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
        #else
        if (isTTY) printf("----- \t\t ------ \t\t %-*s\n",20,"----");
        #endif    

        if (closedir(dir) == -1) {
            char errbuff[256];
            snprintf(errbuff, 256, "%s: closedir: %s", cmdname, dentryname);
            perror(errbuff);
            return 4;
        }
        dentryname = *argv++;
    }

    return 0;
}


/* ls using scandir, sort alphabetically */
static int main_dir(int argc, char *argv[])
{
    // Open and list a directory
    int fd;
    const char *d = argv[1];
    if (!d || !*d) d = "."; // Null or empty string

    // Find canonical name
    char buff[PATH_MAX];
    char *canon_path = realpath(d, buff);
    if (!canon_path) {
        printf("%s: '%s': No such file or directory\n", argv[0], d);
        return -1;
    }
    if ((fd = open(d, O_RDONLY | O_DIRECTORY)) == -1)
    {
        char buff[256];
        snprintf(buff,256,"%s: open: %s",argv[0],d);
        perror(buff);
        return -1;
    }

    /* Scan directory */
    struct dirent **files;
    int nfiles = scandir(d, &files, NULL, alphasort);
    if (nfiles == -1) {
        perror("scandir");
        return -1;
    }

    /* Traverse scan */
    int isTTY = isatty(STDOUT_FILENO);
    if (isTTY) printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
    if (isTTY) printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
    for (long k=0; k<nfiles; k++){
        struct dirent *d = files[k];
        print_dirent(fd, d);
        free(d);
    }
    if (isTTY) printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");

    free(files);
    close(fd);
    return 0;
}


#ifdef FD_SYMLINKS
static int main_lsof(int argc, char *argv[])
{
    #define DEV_FD "/dev/fd"
    DIR *dir = opendir(DEV_FD);
    if (!dir) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: opendir: %s", argv[0], DEV_FD);
        perror(errbuff);
        return 1;
    }

    /* Scan directory */
    struct dirent *pdirent;
    int dfd = dirfd(dir);
    printf("inode \t\t bytes  \ttype  \t %-*s\t %-*s\n",WIDTHNAME,"name",WIDTHFULLNAME,"link");
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");
    while ((pdirent = readdir(dir)) != NULL){
        print_dirent(dfd, pdirent);
    }
    printf("----- \t\t -------\t----- \t %-*s\t %-*s\n",WIDTHNAME,"----",WIDTHFULLNAME,"--------");

    if (closedir(dir) == -1) {
        char errbuff[256];
        snprintf(errbuff, 256, "%s: closedir: %s", argv[0], DEV_FD);
        perror(errbuff);
        return 4;
    }

    return 0;
}
#else
#define RLIMIT_NOFILE 64*1024
static int main_lsof(int argc, char *argv[])
{
    for (long i=0; i < RLIMIT_NOFILE; i++){
        int fd = dup(i);
        if (fd >= 0){
            struct stat s;
            if (fstat(fd, &s) == 0) { // no error with fstat
                printf("fid=%ld", i);
                long pos = lseek(fd, 0, SEEK_CUR);
                printf(" inode=%ld size=%ld pos=%ld %s",
                       s.st_ino, s.st_size, pos,
                       S_ISREG(s.st_mode)?"REG":S_ISDIR(s.st_mode)?"DIR":S_ISLNK(s.st_mode)?"LNK":S_ISCHR(s.st_mode)?"CHR":S_ISBLK(s.st_mode)?"BLK":"");
                puts("");
            }
            close(fd);
        } else if (errno != EBADF) {
            return -1;
        }
    }
    return 0;
}
#endif

static int main_ftruncate(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s fildes length \n", argv[0]);
        return 1;
    }
    int fd = atoi(argv[1]);
    char errbuff[256];
    errno = 0;
    off_t offset = strtol(argv[2], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: offset: '%s'", argv[0], argv[2]);
        perror(errbuff);
        return -1;
    }
    int res = ftruncate(fd, offset);
    printf("ftruncate(%d, %ld) = %d\n", fd, offset, res);
    if (res == -1) {
        snprintf(errbuff, 256, "%s: ftruncate: '%s'", argv[0], argv[1]);
        perror(errbuff);
    }
    return res;
}

static int main_truncate(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s FILE length\n", argv[0]);
        return 1;
    }
    char errbuff[256];
    errno = 0;
    off_t offset = strtol(argv[2], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: offset: '%s'", argv[0], argv[2]);
        perror(errbuff);
        return -1;
    }
    
    int res = truncate(argv[1], offset);
    printf("truncate('%s', %ld) = %d\n", argv[1], offset, res);
    if (res == -1) {
        snprintf(errbuff, 256, "%s: truncate: '%s'", argv[0], argv[1]);
        perror(errbuff);
    }
    return res;
}


static int main_fchmod(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s mode fildes \n", argv[0]);
        fprintf(stderr, "\tmode in octal, e.g.: 754, 644\n");
        return 1;
    }
    int fd = atoi(argv[2]);
    char errbuff[256];
    errno = 0;
    mode_t mode = strtol(argv[1], NULL, 8);
    if (errno) {
        snprintf(errbuff, 256, "%s: mode: '%s'", argv[0], argv[1]);
        perror(errbuff);
        return -1;
    }
    int res = fchmod(fd, mode);
    printf("fchmod(%d, %#o) = %d\n", fd, mode, res);
    if (res == -1) {
        snprintf(errbuff, 256, "%s: fchmod: '%s'", argv[0], argv[2]);
        perror(errbuff);
    }
    return res;
}

static int main_fchmodat(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s dirfd pathname mode flags\n", argv[0]);
        fprintf(stderr, "\tmode in octal, e.g.: 754, 644\n");
        fprintf(stderr, "\tUse %d:AT_FDCWD; to refer to current working directory\n",
                AT_FDCWD);
        fprintf(stderr, "\tflags in hexa -> %#x:AT_SYMLINK_NOFOLLOW\n", AT_SYMLINK_NOFOLLOW);
        return -1;
    }
    char errbuff[256];
    errno = 0;
    int dirfd = strtol(argv[1], NULL, 10);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: dirfd: %s", argv[0], argv[1]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    char *pathname = argv[2];
    mode_t mode = strtol(argv[3], NULL, 8);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: mode: '%s'", argv[0], argv[3]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    int flags = strtol(argv[4], NULL, 16);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: flags: '%s'", argv[0], argv[4]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    // let's do it
    int res = fchmodat(dirfd, pathname, mode, flags);
    printf("fchmodat(%d, '%s', %#o, %#x) = %d\n", dirfd, pathname, mode, flags, res);
    if (res == -1) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: chmod: '%s'", argv[0], argv[2]);
        errno = errsv;
        perror(errbuff);
    }
    return res;
}

static int main_chmod(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s mode FILE\n", argv[0]);
        fprintf(stderr, "\tmode in octal, e.g.: 754, 644\n");
        return 1;
    }
    char errbuff[256];
    errno = 0;
    mode_t mode = strtol(argv[1], NULL, 8);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: mode: '%s'", argv[0], argv[1]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    
    int res = chmod(argv[2], mode);
    printf("chmod('%s', %#o) = %d\n", argv[2], mode, res);
    if (res == -1) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: chmod: '%s'", argv[0], argv[2]);
        errno = errsv;
        perror(errbuff);
    }
    return res;
}

static int main_stat(int argc, char *argv[])
{
    struct stat st;
    int (*stat_func)(const char *, struct stat *);
    int res = 0;
    if (argc == 1) {
        fprintf(stderr, "Usage: %s [-L] FILE\n",argv[0]);
        fprintf(stderr, "\t -L follow lins\n");
        return -1;
    }
    char *progname = *argv++;
    const char *funcname;
    if (strcmp(argv[0],"-L") == 0) {
        stat_func = stat;
        funcname = "stat";
        argv++;
    } else {
        stat_func = lstat;
        funcname = "lstat";
    }
    while (*argv) {
        char *name = *argv++;
        if (stat_func(name, &st) == 0) {
            printf("File: %s\nSize: %ld\tBlocks: %ld\tBlock size: %ld\t%s\nDevice: %ldd\tInode: %#lx\tLinks: %ld\nAccess: %#o\tUid: %d\tGid: %d\tRdev: %ld\n",
                    name, st.st_size, st.st_blocks, st.st_blksize,
                    S_ISBLK(st.st_mode)? "block special file":
                    S_ISCHR(st.st_mode)? "character special file":
                    S_ISLNK(st.st_mode)? "symbolic link":
                    S_ISDIR(st.st_mode)? "directory":
                    S_ISREG(st.st_mode)? "regular file": "unknown",
                    st.st_dev, st.st_ino, st.st_nlink, st.st_mode & 0777,
                    st.st_uid,st.st_gid, st.st_rdev);
        } else {
            int errsv = errno;
            char errbuff[256];
            snprintf(errbuff, 256, "%s: %s: '%s'", progname, funcname, name);
            errno = errsv;
            perror(errbuff);
            res = -1;
        }
    }
    return res;
}

static int main_bn(int argc, char *argv[])
{
    printf("%s\n", basename(argv[1]));
    return 0;
}

static int main_dn(int argc, char *argv[])
{
    printf("%s\n", dirname(argv[1]));
    return 0;
}

static int main_readlink(int argc, char *argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: %s [-f][-e] FILE\n",argv[0]);
        return -1;
    }
    char canon_name[PATH_MAX];
    char *output=canon_name;
    int res;
    if (strcmp("-f", argv[1]) == 0) {
        output = checkpath(argv[2], canon_name);
        res = (output == NULL);
    } else if (strcmp("-e",argv[1]) == 0) {
        output = realpath(argv[2], canon_name);
        res = (output == NULL);
    } else {
        res = readlink(argv[1], canon_name, PATH_MAX-1);
        canon_name[res] = '\0';
        if (res < 0) output = NULL;
    }
    if (output) printf("%s\n", output);
    return res;
}

int main_argv(int argc, char *argv[])
{
    while (*argv) printf("%s\n",*argv++);
    COPY(STDIN_FILENO, STDOUT_FILENO);
    return 0;
}
// A regular file exists
static int exist(char *name)
{
    int fid = open(name, O_RDONLY);
    if (fid < 0) return 0;
    close(fid);
    return 1;
}

static void print_byte(unsigned char c, size_t size)
{
    if (isprint(c) || isspace(c)) {
        printf("%c", c);
    } else {
        printf("\\x%02x", c);
    }
}
// A command like cat but simpler to show regular files
static int main_type(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Dump a regular file showing hexcodes of non-printable chars\n");
        printf("Usage: type regular_file_name\n");
        return 1;
    } else {
        argv[2] = NULL;
        FILE *fh = fopen(argv[1], "r");
        if (!fh) {
            printf("Error reading regular file '%s'\n", argv[1]);
            return 2;
        } else {
            long n = 0;
            unsigned char c;
            while (fscanf(fh, "%c", &c) && !feof(fh)){
                print_byte(c,1);
                n++;
            }
            if (!n) {
                fprintf(stderr, "Regular file '%s' is empty\n", argv[1]);
                fclose(fh);
                return 3;
            }
            fclose(fh);
            //puts("");
            return 0;
        }
    }
}

static int main_dup(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: dup fd\n");
        return 1;
    }
    int fd = atoi(argv[1]);
    int res = dup(fd);
    printf("dup(%d) = %d\n", fd, res);
    if (res == -1) perror("dup");
    return 0;
}

static int main_dup2(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: dup2 oldfd newfd\n");
        return 1;
    }
    int oldfd = atoi(argv[1]);
    int newfd = atoi(argv[2]);
    int res = dup2(oldfd, newfd);
    printf("dup2(%d, %d) = %d\n", oldfd, newfd, res);
    if (res == -1) perror("dup2");
    return 0;
}

static int main_linkat(int argc, char *argv[])
{
    if (argc < 6) {
        fprintf(stderr, "Usage: %s olddirfd oldpath newdirfd newpath flags\n", argv[0]);
        fprintf(stderr, "\tUse %d:AT_FDCWD; to refer to current working directory\n",
                AT_FDCWD);
        fprintf(stderr, "\tflags in hexa -> %#x:AT_SYMLINK_NOFOLLOW; %#x:AT_EMPTY_PATH\n",
                AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH);

        return -1;
    }
    char errbuff[256];
    errno = 0;
    int olddirfd = strtol(argv[1], NULL, 10);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: olddirfd: %s", argv[0], argv[1]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    char *oldpath = argv[2];
    int newdirfd = strtol(argv[3], NULL, 10);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: newdirfd: %s", argv[0], argv[3]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    char *newpath = argv[4];
    int flags = strtol(argv[5], NULL, 16);
    if (errno) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: newdirfd: %s", argv[0], argv[3]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    flags &= (AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH);
    
    int ret = linkat(olddirfd, oldpath, newdirfd, newpath, flags);
    
    printf("linkat(%d, '%s', %d, '%s', %#x) = %d\n", olddirfd, oldpath,
            newdirfd, newpath, flags, ret);
    if (ret == -1) {
        int errsv = errno;
        snprintf(errbuff, 256, "%s: linkat", argv[0]);
        errno = errsv;
        perror(errbuff);
        return -1;
    }
    return 0;
}

static int main_lseek(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: lseek fildes offset whence\n");
        fprintf(stderr, "\twhence: 0: SEEK_SET;\t\t1: SEEK_CUR;\t\t2: SEEK_END\n");
        return 1;
    }
    int fd = atoi(argv[1]);
    long offset = atol(argv[2]);
    int whence = atoi(argv[3]);
    int ret;
    switch (whence) {
    case 2: ret = lseek(fd, offset, SEEK_END);
            printf("lseek(%d, %ld, SEEK_END) = %d\n", fd, offset, ret);
            break;
    case 1: ret = lseek(fd, offset, SEEK_CUR);
            printf("lseek(%d, %ld, SEEK_CUR) = %d\n", fd, offset, ret);
            break;
    case 0:
    default:ret = lseek(fd, offset, SEEK_SET);
            printf("lseek(%d, %ld, SEEK_SET) = %d\n", fd, offset, ret);
            break;
    }
    if (ret == -1) perror("lseek");
    return ret;
}


static int main_umask(int argc, char *argv[])
{
    int mask = umask(0);    //query umask an set to 0
    umask(mask);            //restore umask
    int symbolic = 0;
    int change = 0;
    argv++;
    argc--;
    if (argc) {
        if (strcmp(*argv,"-S") == 0) {
            symbolic = 1;
            argc--;
            argv++;
        } else {
            if (change == 1) {
                fprintf(stderr,"umask: too many arguments\n");
                return 1;
            }
            change = 1;
            errno = 0;
            mask = strtol(*argv, NULL, 8);
            if (errno) {
                fprintf(stderr,"umask: bad symbolic mode operator: %s\n", *argv);
                return 1;
            }
        }
    }
    if (change) {
        umask(mask);
    }else if (symbolic) {
        char buf[20];
        int i = 0;
        mode_t nmask = ~mask;
        buf[i++] = 'u';   buf[i++] = '=';
        if (nmask & 0400) buf[i++] = 'r';
        if (nmask & 0200) buf[i++] = 'w';
        if (nmask & 0100) buf[i++] = 'x';
        buf[i++] = ',';
        buf[i++] = 'g';   buf[i++] = '=';
        if (nmask & 0040) buf[i++] = 'r';
        if (nmask & 0020) buf[i++] = 'w';
        if (nmask & 0010) buf[i++] = 'x';
        buf[i++] = ',';
        buf[i++] = 'o';   buf[i++] = '=';
        if (nmask & 0004) buf[i++] = 'r';
        if (nmask & 0002) buf[i++] = 'w';
        if (nmask & 0001) buf[i++] = 'x';
        buf[i++] = '\0';
        puts(buf);
    } else {
        printf("%#o\n", mask);
    }

    return 0;
}


// snprintf -> return negative if error
#define SNPRINTF1(item, size, format, val)\
    ({\
        ssize_t ret = snprintf(item, size, format, val);\
        if (ret < 0) return -3;\
        ret;\
    })

static int read_writehexa(int fdi, int fdo, off_t start, ssize_t nbytes)
{
    #define LINE_SIZE 16
    unsigned char buf1[LINE_SIZE];
    unsigned char buf2[LINE_SIZE];
    unsigned char *p1 = buf1;
    unsigned char *p2 = buf2;
    int markdiff = 0;
    *p1 = '+';
    *p2 = '-';
    unsigned long acc = 0;

    while (nbytes) {
        ssize_t rlen;
        long off = 0;
        do {
            rlen = READ(fdi, p1 + off, MIN(LINE_SIZE, nbytes) - off);
            if (rlen == 0) break;
            off += rlen;
        } while (off < LINE_SIZE);
        acc += off;
        nbytes -= off;

        if (memcmp(p1, p2, off) == 0 && off == LINE_SIZE) {
            if (!markdiff) WRITE(fdo, "...\n", 4);
            markdiff = 1;
            continue;
        }
        markdiff = 0;
        #define ITEM_SIZE 16
        char item[ITEM_SIZE];
        ssize_t wlen;
        wlen = SNPRINTF1(item, ITEM_SIZE, "%#010lx ", start + acc - off);
        if (wlen > 0) WRITE(fdo, item, wlen);

        for (int j = 0; j < off; j++) {
            if (p1[j]) {
                wlen = SNPRINTF1(item, ITEM_SIZE, "%#04x ", p1[j]);
                if (wlen > 0) WRITE(fdo, item, wlen);
            } else {
                WRITE(fdo, "0x00 ", 5);
            }
        }
        for (int k = off; k < LINE_SIZE; k++) WRITE(fdo, " .   ", 5);
        for (int k = 0; k < off; k++) {
            wlen = SNPRINTF1(item, ITEM_SIZE, "%c", (isprint((int)p1[k]))?p1[k]:'.');
            if (wlen > 0) WRITE(fdo, item, wlen);
        }
        WRITE(fdo, "\n", 1);

        if (rlen == 0) break;   // no more to process
        SWAP(p1, p2);
    }

    return 0;
}


static int main_read(int argc, char *argv[])
{
     if (argc < 3) {
        fprintf(stderr, "Usage: read fildes <nbytes> [0:stdout; 1:stderr]\n");
        return 1;
    }
    errno = 0;
    char errbuff[256];    
    int fdi = strtol(argv[1], NULL, 10);
    int fdo = STDOUT_FILENO;
    if (errno) {
        snprintf(errbuff, 256, "%s: fildes '%s'", argv[0], argv[1]);
        perror(errbuff);
        return -1;
    }
    errno = 0;
    ssize_t nbytes = strtol(argv[2], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: nbytes '%s'", argv[0], argv[2]);
        perror(errbuff);
        return -1;
    }
    if (nbytes == 0) return 0;

    if (argc == 4 && isatty(STDOUT_FILENO) && atoi(argv[3])) {
        fdo = STDERR_FILENO;
    }
    
    //~ off_t start = lseek(fdi, 0, SEEK_CUR);
    //~ if (start == -1) {
        //~ snprintf(errbuff, 256, "%s: lseek fd=%d", argv[0], fdi);
        //~ perror(errbuff);
        //~ return -1;
    //~ }
    //~ int ret = read_writehexa(fdi, fdo, start, nbytes);
    int ret = read_writehexa(fdi, fdo, 0, nbytes);
    if (ret == -1) {
        snprintf(errbuff, 256, "%s: read fd=%d", argv[0], fdi);
    } else if (ret == -2) {
        snprintf(errbuff, 256, "%s: write fd=%d", argv[0], fdo);
    } else if (ret == -3) {
        snprintf(errbuff, 256, "%s: snprintf", argv[0]);
    }

    if (ret) perror(errbuff);
    return ret;
}


static int main_hexdump(int argc, char *argv[])
{
     if (argc < 3) {
        fprintf(stderr, "Usage: %s file <nbytes> [start] [0:stderr; 1:stdout]\n", argv[0]);
        return 1;
    }
    errno = 0;
    char errbuff[256];
    int fdi = open(argv[1], O_RDONLY);
    if (fdi == -1) {
        snprintf(errbuff, 256, "%s: open '%s'", argv[0], argv[1]);
        perror(errbuff);
        return -1;       
    }
    int fdo = STDOUT_FILENO;
    errno = 0;
    ssize_t nbytes = strtol(argv[2], NULL, 10);
    if (errno) {
        snprintf(errbuff, 256, "%s: nbytes '%s'", argv[0], argv[2]);
        perror(errbuff);
        return -1;
    }
    if (nbytes == 0) return 0;
    ssize_t start = 0;
    if (argc > 3) {
        start = strtol(argv[3], NULL, 10);
        if (errno) {
            snprintf(errbuff, 256, "%s: start '%s'", argv[0], argv[3]);
            perror(errbuff);
            return -1;
        }        
        off_t seekstart = lseek(fdi, start, SEEK_SET);
        if (seekstart != start) {
            snprintf(errbuff, 256, "%s: lseek: %ld", argv[0], seekstart);
            perror(errbuff);
            return -1;
        }
    }
    if (argc > 4) {
        fdo = atoi(argv[4]);
        if (fdo != STDOUT_FILENO) fdo = STDERR_FILENO;
    }

    int ret = read_writehexa(fdi, fdo, start, nbytes);
    if (ret == -1) {
        snprintf(errbuff, 256, "%s: read fd=%d", argv[0], fdi);
    } else if (ret == -2) {
        snprintf(errbuff, 256, "%s: write fd=%d", argv[0], fdo);
    } else if (ret == -3) {
        snprintf(errbuff, 256, "%s: snprintf", argv[0]);
    }
    close(fdi);

    if (ret) perror(errbuff);
    return ret;
}


// Write n char to a file, ovewriting it
static int populate(int fd, ssize_t count)
{
    const char *A="abcdefghijklmnopqrstuvwxyz~";
    int Alen = strlen(A);
    ssize_t left = count;
    while (left) {
        ssize_t len = write(fd, A, MIN(left, Alen));
        if (len == -1) return -1;
        left -= len;
    }
    return 0;
}

static int main_write(int argc, char *argv[])
{
     if (argc < 3) {
        fprintf(stderr, "Usage: write fildes <nbytes>\n");
        return 1;
    }
    int fd = atoi(argv[1]);
    ssize_t count = atol(argv[2]);
    if (populate(fd, count)) {
        char buff[256];
        snprintf(buff, 256, "%s: write", argv[0]);
        perror(buff);
        return -1;
    }
    return 0;
}

static int writechars(int c, char **args)
{
    if (c < 3) {
        fprintf(stderr, "Usage: writechars <nbytes> filename\n");
        return 1;
    }

    long N = atol(args[1]);
    char *name = args[2];
    int fd = open(name, O_CREAT|O_WRONLY, 0777);
    if (fd){
        populate(fd, N);
        close(fd);
        return 0;
    } else {
        perror("open");
        return 2;
    }
}


static void usage_ioctl(char *name)
{
    printf("Usage: %s fd request [lflag]\n", name);
    printf("  Call ioctl(fd, request, tty), with tty->c_lflag=lflag\n");
    printf("  fd (dec), file no.:\n\t STDIN=0, STDOUT=1, STDERR=2 by default (use lsof to check open fd)\n");
    printf("  request (hex), one of:\n\t TCGETS = %#x  TCSETS = %#x  TCSETSW = %#x  TCSETSF = %#x\n", TCGETS, TCSETS, TCSETSW, TCSETSF);
    printf("  lflag (hex), OR-ed of:\n\t ECHO = %#x  ICANON = %#x\n", ECHO, ICANON);
}

static int main_ioctl(int argc, char *argv[])
{
    if (argc < 3) {
        usage_ioctl(argv[0]);
        return -1;
    }

    int fd = atoi(argv[1]);
    unsigned long request = strtol(argv[2], NULL, 16);

    struct termios t;
    if (request == TCSETS || request == TCSETSW || request == TCSETSF) {
        if (argc < 4) {
            fprintf(stderr, "requests TCSETS, TCSETSW and TCSETSF requires lflag argument\n");
            usage_ioctl(argv[0]);
            return -1;
        }
        t.c_lflag = strtol(argv[3], NULL, 16);
    }

    if (ioctl(fd, request, &t) == -1) {
        perror("ioctl");
        return -1;
    }

    if (request == TCGETS) {
        printf("lflags=%#x echo=%d icanon=%d\n", t.c_lflag, t.c_lflag & ECHO, t.c_lflag & ICANON);
    }    

    return 0;
}


static int main_stty(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s -a        # show current attributyes\n",argv[0]);
        printf("       %s [-]echo   # set/unset(-) echo\n",argv[0]);
        printf("       %s [-]icanon # set/unset(-) icanon mode\n",argv[0]);
        return -1;
    }

    struct termios t;
    if (tcgetattr(fileno(stdin), &t)) {    // Read current termios
        perror("tcgetattr");
        return -1;
    }
    argv++;
    if (!strcmp(*argv, "-a")){
        fprintf(stdout, "%secho %sicanon\n", (t.c_lflag & ECHO)?"":"-", (t.c_lflag & ICANON)?"":"-");
        return 0;
    }

    do {
        if (!strcmp(*argv, "echo")){
            t.c_lflag |= ECHO;
        } else if (!strcmp(*argv, "-echo")){
            t.c_lflag &= ~ECHO;
        } else if (!strcmp(*argv, "icanon")){
            t.c_lflag |= ICANON;
        } else if (!strcmp(*argv, "-icanon")){
            t.c_lflag &= ~ICANON;
//t.c_cc[VTIME] = 0;
//t.c_cc[VMIN] = 1;
        } else {
            fprintf(stderr,"unknown option: %s\n", *argv);
            return -1;
        }
    } while (*++argv);
    return tcsetattr(fileno(stdin), TCSANOW, &t);
}


// Tree from https://github.com/kddnewton/tree
typedef struct {
    size_t dirs;
    size_t lnks;
    size_t regs;
    size_t chrs;
    size_t unks;
} counter_t;

typedef struct entry {
    char *name;
    int type;
    char *target;
    struct entry *next;
} entry_t;

static int walk(const char* directory, const char* prefix, counter_t *counter) {
    entry_t *head = NULL, *current, *iter;
    size_t size = 0, index;

    struct dirent *file_dirent;
    DIR *dir_handle;

    char *full_path, *segment, *pointer, *next_prefix;

    dir_handle = opendir(directory);
    if (!dir_handle) {
        fprintf(stderr, "Cannot open directory '%s'\n", directory);
        return -1;
    }

    counter->dirs++;

    while ((file_dirent = readdir(dir_handle)) != NULL) {
        if ((file_dirent->d_name[0] == '.' && (file_dirent->d_name[1] == '\0' ||    // '.'
            (file_dirent->d_name[1] == '.' && file_dirent->d_name[2] == '\0')))) {  // '..'
            continue;
        }
        

        char *name = file_dirent->d_name;
        current = (entry_t*)malloc(sizeof(entry_t));
        current->name = strcpy((char *)malloc(strlen(name) + 1), name);
        current->type = file_dirent->d_type;

        if (current->type == DT_LNK) {
            char buf[PATH_MAX];
            int dirfd = open(directory, O_DIRECTORY|O_RDONLY);
            if (dirfd != -1) {
                ssize_t len = readlinkat(dirfd, name, buf, PATH_MAX - 1);
                close(dirfd);
                if (len != -1) {
                    buf[len]='\0';
                    current->target = strcpy((char*)malloc(len+1), buf); //strdup
                } else {
                    current->type = DT_UNKNOWN;
                }
            }
        }

        current->next = NULL;

        if (head == NULL) {
          head = current;
        } else if (strcmp(current->name, head->name) < 0) {
          current->next = head;
          head = current;
        } else {
          for (iter = head; iter->next && strcmp(current->name, iter->next->name) > 0; iter = iter->next) {/*nothing*/}
          current->next = iter->next;
          iter->next = current;
        }

        size++;
    }

    closedir(dir_handle);
    if (!head) {
        return 0;
    }

    for (index = 0; index < size; index++) {
        #define TREE_US_ASCII_
        #ifdef TREE_US_ASCII
        if (index == size - 1) {
            pointer = (char *)"`-- ";
            segment = (char *)"    ";
        } else {
            pointer = (char *)"+-- ";
            segment = (char *)"|   ";
        }
        #else
        if (index == size - 1) {
            pointer = "└── ";
           segment = "    ";
        } else {
            pointer = "├── ";
            segment = "│  ";
        }
        #endif

        printf("%s%s%s", prefix, pointer, head->name);
        switch (head->type) {
            case DT_DIR:
                printf("[d]\n");
                break;
            case DT_REG:
                printf("[r]\n");
                counter->regs++;
                break;
            case DT_CHR:
                printf("[c]\n");
                counter->chrs++;
                break;
            case DT_BLK:
                printf("[b]\n");
                counter->chrs++;
                break;
            case DT_LNK:
                printf("[l] -> %s\n", head->target);
                free(head->target);
                head->target = NULL;
                counter->lnks++;
                break;
            case DT_UNKNOWN:
            default:
                printf("[u]\n");
                counter->unks++;
                break;
        }

        if (head->type == DT_DIR) {
            full_path = (char *)malloc(strlen(directory) + strlen(head->name) + 2);
            sprintf(full_path, "%s/%s", directory, head->name);

            next_prefix = (char *)malloc(strlen(prefix) + strlen(segment) + 1);
            sprintf(next_prefix, "%s%s", prefix, segment);

            walk(full_path, next_prefix, counter);
            free(full_path);
            free(next_prefix);
        }

        current = head;
        head = head->next;

        free(current->name);
        free(current);
    }

    return 0;
}

static int main_tree(int argc, char *argv[]) {
  char* directory = argc > 1 ? argv[1] : (char *)".";
  printf("%s\n", directory);

  counter_t counter = {0, 0, 0, 0, 0};
  walk(directory, "", &counter);

  printf("\n%lu directories, %lu files, %lu links, %lu char devices, %lu unkown\n",
    counter.dirs ? counter.dirs - 1 : 0, counter.regs, counter.lnks, counter.chrs, counter.unks);
  return 0;
}


// ROAE SHELL COMMANDS
extern int IDA_siard2sql(const char*, const char*, const char*);
static void help_siard(int argc, char *argv[]) {
    printf("Usage: %s tosql <siard file>   sqlitefile.sql\n",argv[0]);
    printf("       %s tosql <siard folder> sqlitefile.sql\n",argv[0]);
    printf("       %s tosql <siard file>   sqlitefile.sql [schema regex filter]\n",argv[0]);
    printf("       %s tosql <siard folder> sqlitefile.sql [schema regex filter]\n",argv[0]);
    printf("       %s schemas <siard file or folder> \n",argv[0]);
    printf("       %s schemas <siard file or folder> [schema regex filter]\n",argv[0]);
}
int main_siard(int argc, char *argv[]) {
    char *siardfile=NULL, *sqlfile=NULL;

    if (argc < 2) {
        help_siard(argc, argv);
        return -1;
    }

    const char *schema_filter = ""; 
    if (!strcmp(argv[1], "tosql")){
        if (argc < 4) { help_siard(argc,argv); return -1;}
        siardfile = argv[2];
        sqlfile = argv[3];
        if (argv[4]){
            schema_filter = argv[4];
        }
        // SIARD -> SQL
        IDA_siard2sql(siardfile, sqlfile, schema_filter);
    }
    else if (!strcmp(argv[1], "schemas")) {
        if (argc < 3) { help_siard(argc,argv); return -1;}
        siardfile = argv[2];
        if (argv[3]){
            schema_filter = argv[3];
        }
        IDA_siard2sql(siardfile, NULL, schema_filter);
    }
    else {
        help_siard(argc, argv);
        return -1;
    }

    return 0;
}

extern int IDA_unzip(const char *zipfile, const char *onefile);
int main_unzip(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file.zip\n", argv[0]);
        return -1;
    }
    return IDA_unzip(argv[1], NULL);
}

// Init sqlite shell
extern void IDA_SQLITE_shell_init();
// Run an internal sqlite command, that is, those that start with "."
extern int IDA_SQLITE_do_meta_command(char *cmd);
// Run an SQL command
extern int IDA_SQLITE_shell_exec(char *cmd);
// Run an internal command or SQL command depending on whether it starts with "."
extern int IDA_SQLITE_run(char *cmd);
// Run an sequence of internal or SQL commands separated by "\n" (without blanks) 
extern int IDA_SQLITE_run_sequence(char *cmd);

#define SQLBUFFSIZE 4096*2
static void sqlite_shell_init(){
    static long sqlite_shell_initialized = 0;
    if (!sqlite_shell_initialized){
        //TODO: put this in constructor or similar
        IDA_SQLITE_shell_init();

        // Some particular configuration
        // Use a buffer, as constant strings cannot
        // be modified by do_meta_command() in order to parse
        // the command
        IDA_SQLITE_run(".header on");
        IDA_SQLITE_run(".mode table");
        IDA_SQLITE_run("PRAGMA encoding = 'UTF-8'");
        //
        sqlite_shell_initialized = 1;
    }
}

static void help_sqlite(int argc, char *argv[]) {
    printf("Usage:\n");
    printf("       %s \"<sqlite shell command>\" \n",argv[0]);
    printf("Available shortcuts:\n");
    printf("       %s -- clear\n",argv[0]);
    printf("              # equivalent to \".open :memory:\"\n");
    printf("       %s -- load <sql_file>\n",argv[0]);
    printf("              # equivalent to \".read <sql_file>\"\n");
    printf("       %s -- loadsiard <siard_file> [schema_filter_regex]\n",argv[0]);
    printf("              # equivalent to unzip + convert siard->sql + clear + read sql\n");
    printf("       %s -- tables\n",argv[0]);
    printf("              # equivalent to \"ANALYZE main; select * from sqlite_stat1;\"\n");
    printf("              # this shows non-empty tables; a table with multiple indexed may appear once per index\"\n");
    printf("       %s -- table_info <table_name>\n",argv[0]);
    printf("              # equivalent to \"SELECT * FROM pragma_table_info('<table_name>');\"\n");
    printf("       %s -- bytes \n",argv[0]);
    printf("              # print the size of current database\n");
    printf("              # equivalent to \"SELECT P.page_count*S.page_size FROM pragma_page_count() AS P, pragma_page_size() AS S;\"\n");
}
static int main_sqlite(int argc, char *argv[]) {
    if (argc < 2) {
        help_sqlite(argc, argv);
        return -1;
    }

    static char buff[SQLBUFFSIZE];
    if (!strcmp(argv[1], "--")){
        // Useful sqlite shortcuts
        if (argc < 3) {
            help_sqlite(argc, argv);
            return -1;
        }
        else if (!strcmp(argv[2], "clear")){
            strcpy(buff, ".open :memory:");
            IDA_SQLITE_do_meta_command(buff);
        }
        else if (!strcmp(argv[2], "load")){
            if (argc < 3) {
                help_sqlite(argc, argv);
                return -1;
            }
            snprintf(buff, SQLBUFFSIZE, ".read \"%s\"", argv[3]);
            buff[SQLBUFFSIZE-1]='\0';
            IDA_SQLITE_do_meta_command(buff);
        }
        else if (!strcmp(argv[2], "loadsiard")){
            if (argc < 3) {
                help_sqlite(argc, argv);
                return -1;
            }

            // Get realpath for siard file and, current dir 
            char realsiard[PATH_MAX], currwd[PATH_MAX];
            char *rl = realpath(argv[3], realsiard);
            if (!rl) {
                fprintf(stderr, "File '%s' not found\n", argv[3]);
                return -1;
            }
            char *wd = getcwd(currwd, PATH_MAX);
            if (!wd) return -1;
            
            // Create tmp dir, chdir to it and unzip siard
            #define TMPDIR_SIARD2SQL "_roaesh_ld_siard_tmp_" 
            char *tmpdir = "/tmp/" TMPDIR_SIARD2SQL;
            char *sqlfile = "_out_siard2sql_tmp_.sql";
            rmkdir(tmpdir, 0777);
            if (!chdir(tmpdir)) {
                int trydir = 0;
                //int uz = IDA_unzip(realsiard, NULL);
                //if (uz) {
                //    chdir(wd);
                //    fprintf(stderr, "Cannot unzip file '%s'; trying as a directory ...\n", realsiard);
                //    trydir = 1;
                //    //return -1;
                //}

                // Convert siard -> sql
                fprintf(stderr, "\n");
                fprintf(stderr, "Converting to SQL ...\n");
                char *filter = ""; // To be get as parameter
                if (argv[4]) filter = argv[4];
                unlink(sqlfile);
                int sqlerr = 1;
                //if (!trydir) {
                //    sqlerr = IDA_siard2sql(tmpdir, sqlfile, filter);
                //} else {
                    // Perhaps is a dir with an already unzipped siard
                    sqlerr = IDA_siard2sql(realsiard, sqlfile, filter);
                //}
                if (sqlerr) {
                    int d_ = chdir(wd);
                    fprintf(stderr, "Error converting to SQL\n");
                    return -1;
                }
            }
            else {
                fprintf(stderr, "Unable to change to temporary directory '%s'\n", tmpdir);
                return -1;
            }

            // Reset current sqlite state and load converted sql
            fprintf(stderr, "\n");
            fprintf(stderr, "Cleaning sqlite3 engine and loading SQL ...\n");
            snprintf(buff, SQLBUFFSIZE, ".open :memory:");
            IDA_SQLITE_do_meta_command(buff);
            snprintf(buff, SQLBUFFSIZE, ".read \"%s\"", sqlfile);
            IDA_SQLITE_do_meta_command(buff);

            int d_ = chdir(wd); // Restore dir

            // delete temporary dir safely and recursively
            rrm_needle(tmpdir, TMPDIR_SIARD2SQL);

            fprintf(stderr, "done\n");
        }
        else if (!strcmp(argv[2], "tables")){
            strcpy(buff, "ANALYZE main; select * from sqlite_stat1 order by cast(stat as integer);");
            IDA_SQLITE_shell_exec(buff);
        }
        else if (!strcmp(argv[2], "table_info")){
            if (argc < 4) {
                help_sqlite(argc, argv);
                return -1;
            }
            snprintf(buff, SQLBUFFSIZE, "SELECT * FROM pragma_table_info('%s');", argv[3]);
            buff[SQLBUFFSIZE-1]='\0';
            IDA_SQLITE_shell_exec(buff);
        }
        else if (!strcmp(argv[2], "bytes")){
            strcpy(buff, "SELECT P.page_count*S.page_size FROM pragma_page_count() AS P, pragma_page_size() AS S;");
            IDA_SQLITE_shell_exec(buff);
        }
    }
    else {
        IDA_SQLITE_run(argv[1]);
    }
    return 0;
}

extern long  IDA_ROAE_load(char *filename);
extern void  IDA_ROAE_clear();
extern void  IDA_ROAE_print_commands();
extern void  IDA_ROAE_print_command(long nc);
extern void  IDA_ROAE_search(char *re);
extern long  IDA_ROAE_count();
extern char* IDA_ROAE_get_command_title(long nc);
extern long  IDA_ROAE_get_command_nargs(long nc);
extern char* IDA_ROAE_get_command_arg_name(long nc, long na);
extern char* IDA_ROAE_get_command_arg_comment(long nc, long na);
extern char* IDA_ROAE_eval_command(long nc, char *buff, long buffsize, char *values[]);
extern char**IDA_ROAE_command_bind_list(long nc, char *values[], ...);
extern char* IDA_ROAE_command_bind_list_to_sqlite(char *bind_list[]);

#define ROAEBUFFSIZE (1024*16)
#define FREEARGS(args)  do{long i=0; if (args){ while(args[i]){free(args[i]);i++;}; free(args);}}while(0)

static void help_roae(int argc, char *argv[])
{
    printf("Usage: %s load filename \n",argv[0]);
    printf("       %s clear \n",argv[0]);
    printf("       %s list\n",argv[0]);
    printf("       %s show <command_number> \n",argv[0]);
    printf("       %s search <regexp>\n",argv[0]);
    printf("       %s run-replace <command_number> param0 param1 ...\n",argv[0]);
    printf("              Replace parameters in body, then execute  \n");
    printf("              Use sqlite types for parameters, e.g.: 123, 'string', X'f09f8dba'\n");
    printf("              Note that the replacement is literal, therefore strings need quotes\n");
    printf("       %s run-bind <command_number> param0 param1 ...\n",argv[0]);
    printf("              Prepare SQL statement, bind parameters, then execute  \n");
    printf("              Note that quotes are not required for strings on using binding\n");
    printf("       %s menu\n", argv[0]);
    printf("              Choose interactively a roae rule from a list,\n");
    printf("              then select the execution method (replace/bind, see above), and enter parameters\n");
    printf("              SQL statement is prepared, parameters replaced or bound, and executed\n");
    printf("              Do not forget to load first the ROAE file and its associated DB\n");
    printf("              Example:\n");
    printf("                      sqlite -- loadsiard example.siard\n");
    printf("                      roae load example.roae\n");
}

static void roae_menu()
{
    long ncommands = IDA_ROAE_count();
    // Print a menu with all roae commands 
    if (ncommands <= 0) {
        fprintf(stderr, "No ROAE commands available\nA ROAE file and its associated DB must be loaded first\n");
        fprintf(stderr, "Example:\n\t sqlite -- loadsiard example.siard\n\t roae load example.roae\n");
        return;
    }

    long nc, scnf;
    char menubuff[ROAEBUFFSIZE];
    while (1) { 
        printf("\nAvailable ROAE cases:\n");
        for (long i=0; i<ncommands; i++){
            char *c = IDA_ROAE_get_command_title(i);
            printf(" [%ld] %s\n", i, c);
            free(c);
        };
        printf(" [Q] QUIT\n"); // #last entry to quit the selection loop

        // Select a ROAE command
        nc=-1; scnf=0;
        printf("Select ROAE command number: ");
        char *roae = fgets(menubuff, ROAEBUFFSIZE-1, stdin); menubuff[ROAEBUFFSIZE-1]='\0';
        if (roae){
            scnf = sscanf(roae, "%ld", &nc);
            if ('\n' == roae[0]){
                 continue; // Typed enter, do nothing, restart the menu
            }
            if ('Q' == roae[0] || 'q' == roae[0]){
                printf(" QUIT selected ... quitting selection menu ... you can restart it with 'roae menu'\n\n");
                break;
            }
        } else { // fgets EOF
            fprintf(stderr, "\nBroken stdin\n");
            clearerr(stdin);
            break; // Quit on ^D 
        } 
        if (scnf>0 && nc>=0 && nc<ncommands){
            printf("  selected ROAE command no. %ld\n", nc);
            char *c = IDA_ROAE_get_command_title(nc);
            printf("  title=%s\n", c);
            free(c);

            printf("Select evaluation method (Replace/Bind)[R]: ");
            char *meth = fgets(menubuff, ROAEBUFFSIZE-1, stdin);
            if (!meth || *meth == 'B' || *meth == 'b') meth = "B";
            else meth = "R";  // Replace evaluation method by default

            // Read the required arguments from stdin
            long npar = IDA_ROAE_get_command_nargs(nc);
            char **arglist = NULL;
            arglist = (char**)malloc(sizeof(char*) * (npar+1));
            if (arglist) {
                if (npar > 0) {
                    printf("ROAE rule #%ld requires %ld parameters:\n", nc, npar);
                    for (long k=0; k<npar; k++){
                        char* arg_name = IDA_ROAE_get_command_arg_name(nc, k);
                        char* arg_comment = IDA_ROAE_get_command_arg_comment(nc, k);
                        printf("  - Enter parameter #%ld '%s' (%s): ", k+1, arg_name, arg_comment);
                        free(arg_name);
                        free(arg_comment);
                        char *arg = fgets(menubuff, ROAEBUFFSIZE-1, stdin);
                        menubuff[ROAEBUFFSIZE-1] = '\0';
                        if ('\n' == menubuff[strlen(menubuff)-1]) menubuff[strlen(menubuff)-1] = '\0'; // Remove last newline
                        if (arg) {
                            arglist[k] = strdup(arg);
                        } else {
                            arglist[k] = NULL;
                        }
                    }
                } else {
                    printf("This rule does not requires any parameter\n");
                }
                arglist[npar] = NULL;
            } else {
                printf("Error allocating memory for parameters\n");
                return;
            }

            char *ec = NULL;
            printf("-----------\n");
            if (*meth == 'B') {
                // Method of evaluation: run-bind
                // Create the list of sqlite commands with parameters to bind
                char **bind_list = IDA_ROAE_command_bind_list(nc, arglist);
                // Create a string with the sqlite command sequence to bind parameters
                // and execute it
                char *bl = IDA_ROAE_command_bind_list_to_sqlite(bind_list);
                if (bl) {
                    printf("Binding parameters:\n-----------\n%s\n----------\n", bl);
                    IDA_SQLITE_run_sequence(bl);
                    free(bl);
                } 
                ec = IDA_ROAE_eval_command(nc, NULL, 0, NULL);
                if (bind_list) FREEARGS(bind_list);
            } else {
                // Method of evaluation: run-replace
                ec = IDA_ROAE_eval_command(nc, NULL, 0, arglist);
            }  

            if (ec) {
                printf("Evaluated command:\n-----------\n%s\n----------\n", ec);
                IDA_SQLITE_shell_exec(ec);
            } else {
                fprintf(stderr, "Error evaluating command #%ld\n", nc);
            }
            if (ec) free(ec);
            if (arglist) FREEARGS(arglist);
            
        } else {
            fprintf(stderr, "\nROAE command number is not a valid integer ([0, %ld])\n", ncommands-1);
            continue; // Restart menu if not valid number
        }
    }
}

static int main_roae(int argc, char *argv[]) {
    static long ncommands = 0;
    if (argc < 2) {
        help_roae(argc, argv);
        return -1;
    }
    if (!strcmp(argv[1], "load")){
        if (argc < 3) { help_roae(argc,argv); return -1;}
        ncommands = IDA_ROAE_load(argv[2]);
        printf("Read %ld commands from ROAE file '%s'\n", ncommands, argv[2]);
    }
    else if (!strcmp(argv[1], "clear")){
        ncommands = 0;
        IDA_ROAE_clear();
    }
    else if (!strcmp(argv[1], "list")){
        IDA_ROAE_print_commands();
    }
    else if (!strcmp(argv[1], "show")){
        if (argc < 3) { help_roae(argc,argv); return -1;}
        int nc = atoi(argv[2]);
        IDA_ROAE_print_command(nc);
    }
    else if (!strcmp(argv[1], "search")){
        if (argc < 3) { help_roae(argc,argv); return -1;}
        IDA_ROAE_search(argv[2]);
    }
    else if (!strcmp(argv[1], "run-replace")) {
        if (argc < 3) { help_roae(argc,argv); return -1;}
        long nc = atol(argv[2]);
        char *ec = NULL;
        // Evaluate (replace) parameters in argv
        ec = IDA_ROAE_eval_command(nc, NULL, 0, &argv[3]);
        if (ec) {
            fprintf(stdout, "Command #%ld evaluated: '%s'\n", nc, ec);
            IDA_SQLITE_shell_exec(ec);
            free(ec);
        } else {
            fprintf(stderr, "Error evaluating command #%ld\n", nc);
            return -1;
        }
    }
    else if (!strcmp(argv[1], "run-bind")) {
        if (argc < 3) { help_roae(argc,argv); return -1;}
        long nc = atol(argv[2]);
        char buff[ROAEBUFFSIZE], *ec = NULL;

        // 1. Bind parameters
        long i = 0;
        strcpy(buff, ".parameter clear");
        IDA_SQLITE_do_meta_command(buff);
        // List of parameters to bind
        char **bind_list = IDA_ROAE_command_bind_list(nc, &argv[3]);
        // Sqlite sequence to bind parameters
        char *bl = IDA_ROAE_command_bind_list_to_sqlite(bind_list);
        fprintf(stderr, "bind list:\n--\n%s\n--\n", bl);
        if (bl) {
            IDA_SQLITE_run_sequence(bl);
            free(bl);
        } 
        if (bind_list) FREEARGS(bind_list);
        
        // 2. Prepare the sql statement, use NULL as argv
        ec = IDA_ROAE_eval_command(nc, buff, sizeof(buff), NULL);

        // 3. Execute
        if (ec) {
            fprintf(stdout, "Command #%ld evaluated: '%s'\n", nc, ec);
            IDA_SQLITE_shell_exec(ec);
        } else {
            fprintf(stderr, "Error evaluating command #%ld\n", nc);
            return -1;
        }
    }
    else if (!strcmp(argv[1], "menu")) {
        roae_menu();
    }
    else {
        help_roae(argc, argv);
        return -1;
    }
    return 0;
}
// END ROAE SHELL COMMANDS



static int main_spawn(int argc, char *argv[])
{
    int ret = 0;
    #ifdef __ivm64__
        ret = ivm_spawn(argc, argv);
    #else
        pid_t pid = fork();
        if (pid == 0) {
            execv(argv[0], argv);
            perror("exec");
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            int wstatus;
            wait(&wstatus);
            ret = WEXITSTATUS(wstatus);
        } else{
            perror("fork");
            ret = -1;
        }
    #endif
    //fprintf(stderr, "spawn returned %d\n", ret);
    return ret;
}

static int main_help(int argc, char *argv[])
{
    printf( "Immortal Database Access (iDA) EUROSTARS project\n"
            "ROAE shell, %s: "
            "A shell to interface with the Read-Only Access Engine (ROAE)\n", ROAESHELL_VERSION);

    printf( "IDA commands:\n   roae\n   siard\n   sqlite\n   unzip\n"
            "Available redirections:\n"
            "   ' > file', ' 2> file', ' >> file', ' < file', ' << HEREDOC', ' <<<\'string\'' \n"
            "FS commands: argv basename cat cd chmod cmp cp crc32 dd du dir dirname echo env exit export find glob grep help hexdump\n"
            "             ln ls lsof meminfo mkdir mv pwd quit(=^D) readlink realpath rm rmdir seekdir set source stat tee touch tree\n"
            "             truncate type umask unset wc writechars\n"
            "Functions: close dup dup2 fchmod fchmodat fcmp ftruncate linkat lseek open openat read rename renameat write\n"
            "Pseudopipes: 'cmd1 | cmd2 | cmd3 ... '\n"
          );
    return 0;
}
