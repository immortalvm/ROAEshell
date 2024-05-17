/*
    Immortal Database Access (iDA) EUROSTARS project

    ROAE shell
    A shell to interface with the Read-Only Access Engine (ROAE)

    Authors:
    Eladio Gutierrez, Sergio Romero, Oscar Plata
    University of Malaga, Spain

    Aug, 2023
*/

#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <ctype.h>
#include <glob.h>
#include <stdint.h>

#define ROAESHELL_VERSION "v0.1.9 (2024050200)"

#include <termios.h>
#include <sys/ioctl.h>

#ifdef __ivm64__
extern int ivm_spawn(int argc, char *argv[]);
#else
#include <spawn.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// See newlib/libc/include/sys/_default_fcntl.h
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH          16
#endif

// Linux specific flag
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

#ifdef __ivm64__
#define getline __getline
#define getdelim __getdelim
#endif

#define MAX_LINE 4*4096 /* chars per line, per command, should be enough. */

#undef BUFSIZ
#define BUFSIZ PATH_MAX

// MAX/MIN only one evaluation
#define MIN(a,b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); (_a < _b)?_a:_b;})
#define MAX(a,b) ({__typeof__(a) _a=(a); __typeof__(b) _b=(b); (_a > _b)?_a:_b;})

// Some prototypes
static int get_prompt();
static void set_prompt(int);

// Some external functions
extern char *get_current_dir_name(void);

// Some global variables related with each running command
static int stdin_0 = -1, stdout_0 = -1, stderr_0 = -1;
static int prompt = 2;

// -----------------------------------------------------------------------
// Parse redirections operators '<' '>' once args structure has been built.
// Include this file and call the function immediately after get_command():
//
//     #include "parse_redir.h"
//     ...
//     while(...){
//          // Shell main loop
//          ...
//          get_command(...);
//          char *file_in, *file_out;
//          parse_redirections(args, &argc, &file_in, &file_out_append, &file_out, &file_err);
//          ...
//     }
//
// For a valid redirection, a blank space is required before and after
// redirection operators '<' or '>'.
// --------------------------------------------------------------
static void parse_redirections(char **args,  int *argc, char **file_in, char **file_out, char **file_out_append, char **file_err, char **file_in_heredoc){
    *file_in = NULL;
    *file_out = NULL;
    *file_out_append = NULL;
    *file_err = NULL;
    *file_in_heredoc = NULL;
    char **args_start = args;
    while (*args) {
        int is_in = !strcmp(*args, "<");
        int is_out = !strcmp(*args, ">");
        int is_out_append = !strcmp(*args, ">>");
        int is_err = !strcmp(*args, "2>");
        int is_in_heredoc = !strcmp(*args, "<<");
        if (is_in || is_out || is_err || is_out_append || is_in_heredoc) {
            args++;
            if (*args){
                (*argc) -= 2;
                if (is_in)  {*file_in = *args; *file_in_heredoc = NULL;}
                if (is_out) {*file_out = *args; *file_out_append = NULL;}
                if (is_out_append) {*file_out = NULL; *file_out_append = *args;}
                if (is_err) *file_err = *args;
                if (is_in_heredoc) {*file_in = NULL; *file_in_heredoc = *args;}
                char **aux = args + 1;
                while (*aux) {
                   *(aux-2) = *aux;
                   aux++;
                }
                *(aux-2) = NULL;
                args--;
            } else {
                /* Syntax error */
                fprintf(stderr, "syntax error in redirection\n");
                args_start[0] = NULL; // Do nothing
            }
        } else {
            args++;
        }
    }
}

static int is_same_inode(int fd1, int fd2)
{
    struct stat s1, s2;
    int e1, e2;
    e1 = fstat(fd1, &s1);
    e2 = fstat(fd2, &s2);
    if (!e1 && !e2){
        if (s1.st_ino == s2.st_ino) {
            return 1;
        }
    }
    return 0;
}

static int is_same_file(const char *f1, const char *f2)
{
    struct stat s1, s2;
    int e1, e2;
    e1 = stat(f1, &s1);
    e2 = stat(f2, &s2);
    if (!e1 && !e2){
        if (s1.st_ino == s2.st_ino) {
            return 1;
        }
    }
    return 0;
}


// -----------------------------------------------------------------------
//  get_command() reads in the next command line, separating it into distinct tokens
//  using whitespace as delimiters.
//  Separators ';' and '&' allows having several subcommands in the same line; a
//  subcommand is returned in each invocation of get_command()
//  Reference: Operating System Concepts by A. Silberschatz et al.
// -----------------------------------------------------------------------
int get_command(char inputBuffer_i[], int size, char *args[], char *separator)
{
	int length, /* # of characters in the command line */
		i,      /* loop index for accessing inputBuffer array */
		start,  /* index where beginning of next command parameter is */
		ct;     /* index of where to place the next parameter into args[] */

	ct = 0;
    *separator = 0;

	/* read what the user enters on the command line */
	//length = read(STDIN_FILENO, inputBuffer, size);

    char *inputBuffer = inputBuffer_i;

    // In case several subcommands in the same line, this pointer points to the
    // next subcommand to be processed
    static char* inputBuffer_next = NULL;

    if (inputBuffer_next && *inputBuffer_next) {
        // There is left subcommands to be processed of the last line that was
        // read
        inputBuffer = inputBuffer_next;
        length = strlen(inputBuffer)+1;
    } else {
        // No pending subcommands: read a new line entered by the user on the
        // command line
        if (0 && isatty(STDIN_FILENO)) {
	        length = read(STDIN_FILENO, inputBuffer, size);
        } else {
            // Emulate line discipline, reading char by char, in case stdin was redirected from a file
            inputBuffer[0]='\0';
            length = 0;
            char c = 0;
            long l = 0;
            do{
                c=0;
                l=read(STDIN_FILENO, &c, 1);
                if (l>0) {
                    inputBuffer[length] = c;
                    inputBuffer[length+1] = '\0';
                    length++;
                }
            } while ((l>0) && (length<MAX_LINE-2) && (c!='\n'));
            if ((length>=MAX_LINE-2) && (c!='\n')){
                inputBuffer[length] = '\n';
                inputBuffer[length+1] = '\0';
                length++;
            }
        }
        inputBuffer_next = NULL;
    }

	start = -1;
	if (length == 0)
	{
		printf("\nBye\n");
		exit(0);            /* ^D was entered, end of user command stream */
	}
	if (length < 0){
		perror("error reading the command");
		exit(-1);           /* terminate with error code of -1 */
	}

    int instring = 0; // Arguments with spaces can be quoted by '\"'

	/* examine every character in the inputBuffer */
	for (i=0;i<length;i++)
	{
        char cc = inputBuffer[i];
		//switch (cc)
		//{
        //case '\"':
        if ('\"' == cc) {
            if (!instring) {
                instring = 1;
            } else {
				//if (start != -1) {
                    args[ct] = &inputBuffer[start];
                    ct++;
                //}
                inputBuffer[i] = '\0';
                start = -1;
                instring = 0;
            }
        }
		//case ' ':
		//case '\t' :
        else if (' ' == cc || '\t' == cc) {
            /* argument separators */
            if (!instring) {
                if(start != -1)
                {
                    args[ct] = &inputBuffer[start];    /* set up pointer */
                    ct++;
                }
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
                start = -1;
            } else {
                // Proceed with spaces as another common char
			    if (start == -1) start = i;  // start of new argument
            }
        }
		//case '\n':                 /* should be the final char examined */
        //case ';':
        //case '&':
        else if ('\n' == cc
                 || '\0' == cc
                 || '#'  == cc
                 || ((';' == cc || '&' == cc) && (!instring))) {
            /* should be the final char examined */
			if (start != -1)
			{
				args[ct] = &inputBuffer[start];
				ct++;
			}

            inputBuffer_next = NULL;
            if (';' == cc || '&' == cc || '\n' == cc) {
                inputBuffer_next = &inputBuffer[i+1];
                *separator = cc;
            }

			inputBuffer[i] = '\0';
            // no more arguments to this sub-command
			args[ct] = NULL;

            if ('#' == cc || '\0' == cc) {
                // Ignore from this point: it is a comment or the string ends (only
                // when stdin redirected)
                length = i;
                inputBuffer_next = NULL;
                break;
            }
        }
		//default :             /* some other character */
        else {
            /* some other character */
			//-- if (inputBuffer[i] == '&') // background indicator
			//-- {
            //--     *separator = cc;
			//-- 	if (start != -1)
			//-- 	{
			//-- 		args[ct] = &inputBuffer[start];
			//-- 		ct++;
			//-- 	}
			//-- 	inputBuffer[i] = '\0';
			//-- 	args[ct] = NULL; /* no more arguments to this command */
			//-- 	i=length; // make sure the for loop ends now

			//-- }
			//-- else
            if (start == -1) start = i;  // start of new argument
		}

        if (*separator) break; // A separator found, a subcommand ends here
	}  // end for
	args[ct] = NULL; /* just in case the input line was > MAXLINE */
    return ct;
}



static char buff[BUFSIZ];
static void replace_status(int argc, char *argv[], int status)
{
    snprintf(buff,BUFSIZ,"%d",status);
    for (int i=0; i < argc; i++) {
        if (strcmp("$?", argv[i]) == 0) {
            argv[i] = buff;
        }
    }
}

static void replace_env(int argc, char *argv[])
{
    for (int i=0; i < argc; i++) {
        if ('$' == argv[i][0] && '\0' != argv[i][1]) {
            char *e = getenv(&argv[i][1]);
            if (e)
                 argv[i] = e;
            else
                 argv[i] = "";
        }
    }
}

static void ignore_comments(int *argc, char *argv[])
{
    int nargs = *argc;
    for (int i=0; i < nargs; i++) {
        if ( argv[i][0] == '#') {
            argv[i] = NULL;
            *argc = i;
            break;
        }
    }
}

// Add a new argv[0], displacing the rest of the arguments
// (argv[0]->argv[1], argv[1]->argv[2] ...)
static int arg_add(int argc, char *argv[], char *arg0)
{
    long i = 1;
    char *p = argv[0], *n;
    while (argv[i] && i < MAX_LINE/2){
        n = argv[i];
        argv[i] = p;
        p = n;
        i++;
    }
    argv[0] = arg0;
    argv[i] = p;
    argv[i+1] = NULL;
    return argc+1;
}


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

    if ((!isatty(ifd) || !isatty(ofd)) && is_same_inode(ifd, ofd)){
        fprintf(stderr, "input file is output file\n");
        return 1;
    }

    while ((rlen = READ(ifd, buf, BUFSIZ)) > 0) {
        for (ssize_t off = 0; off < rlen; off += WRITE(ofd, buf+off, rlen-off));
        ret += rlen;
    }
    return ret;
}

#define COPY_SLOW(id,od) do{int c;while((c=getc(id))!=EOF)putc(c,od);}while(0)
static int main_cat(int argc, char *argv[])
{
    int fd;
    int ret = 0;

    // use strace to check the beaviour
    //setvbuf(stdout, NULL, _IONBF, 0);
    //setvbuf(stdin, NULL, _IONBF, 0);

    if (argc == 1) {
        int n = COPY(STDIN_FILENO, STDOUT_FILENO); // not affected by libc buffering mode
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
//        COPY_SLOW(stdin,stdout);
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
            COPY(fd, 1);
            close(fd);
        }
    }
    return ret;
}


static int echo(int argc, char *argv[])
{
    int endl = 1;
    argc--;
    argv++;
    if (*argv && strcmp("-n",*argv)==0) {
        endl = 0;
        argc--;
        argv++;
    }
    if (*argv) printf("%s", *argv++);
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
    char buff[PATH_MAX];
    w = getcwd(buff, PATH_MAX-1);
    // w = getwd(buff); // FAIL ???
    //w = get_current_dir_name(); // This calls malloc()
    if (!w) {
        perror("getwd");
        fprintf(stderr, "Function getwd() FAILED!\n");
        return -1;
    }
    printf("%s\n", w);
    //free(w); // Free w if got by get_current_dir_name
    return 0;
}

static int main_cd(int argc, char *argv[])
{
    char *d;
    if (argc==1) {
        d = getenv("HOME");
        if (!d) d = "/";
    } else d = argv[1];

    int c = chdir(d);
    if (c) {
        fprintf(stderr, "Changing to dir '%s' FAILED!\n", d);
        return c;
    }
    fprintf(stderr, "Successfuly changed to dir '%s'\n", d);
    return 0;
}

// As cd using an open file number corresponding
// to a directory open with opendir
static int main_fcd(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s dirfd\n",argv[0]);
        return -1;
    }
    int fd = atoi(argv[1]);
    int status = fchdir(fd);
    if (status) {perror("fchdir");}
    return status;
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

static int main_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s directory\n", argv[0]);
        printf("       %s -p directory\n", argv[0]);
        return -1;
    }
    char *d;
    long m;
    if (!strcmp(argv[1], "-p")) {
        if (argc < 3) {
            printf("Missing directory name\n");
            return -1;
        }
        d = argv[2];
        m = rmkdir(d, 0777);
    } else {
        d = argv[1];
        m = mkdir(d, 0777);
    }
    if (m == -1) {
        perror("mkdir");
        fprintf(stderr, "Creating directory '%s' FAILED!\n", d);
        return -1;
    }
    fprintf(stderr, "Directory '%s' just created\n", d);
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
        printf("List directories or files using glob wildcards\n");
        printf("Usage: %s <glob expression>\n", argv[0]);
        return -1;
    }
    char *globexpr = argv[1];
    glob_t globbuf;

    glob(globexpr, GLOB_TILDE|GLOB_BRACE, NULL, &globbuf);

    for (long k=0; k<globbuf.gl_pathc && globbuf.gl_pathv[k]; k++){
        printf("%s ", globbuf.gl_pathv[k]);
    }
    puts("");

    globfree(&globbuf);

    return 0;
}

static int main_setenv(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Assign/change an environment variable's value: varname=value\n");
        printf("Usage: %s varname value\n", argv[0]);
        return -1;
    }
    return setenv(argv[1], argv[2], 1);
}

static int main_unsetenv(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Delete the variable 'varname' from the environment\n");
        printf("Usage: %s varname\n", argv[0]);
        return -1;
    }
    return unsetenv(argv[1]);
}

static int main_getenv(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Get an environment variable\n");
        printf("Usage: %s varname\n", argv[0]);
        return -1;
    }

    char *v = getenv(argv[1]);
    if (v) printf("%s\n", v);
    return 0;
}

static int main_env(int argc, char *argv[])
{
    extern char **environ;
    int i=0;
    while (environ[i] && i<(1<<24)){
        printf("%s\n", environ[i]);
        i++;
    }
    return 0;
}


static int main_mkdirat(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Create a directory at a given directory open with opendir\n");
        printf("Usage: %s dirfd pathname\n", argv[0]);
        return -1;
    }
    int dirfd = atoi(argv[1]);
    char *pathname = argv[2];
    int m = mkdirat(dirfd, pathname, 0777);
    if (m){
        fprintf(stderr, "Making dir '%s' @ dirfd=%d FAILED!\n", pathname, dirfd);
        perror("openat");
        return -1;
    }
    fprintf(stderr, "Created dir '%s' @ dirfd=%d\n", pathname, dirfd);
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

// To test unlink
static int main_unlink(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Delete (unlink) files\n");
        printf("Usage: %s file_to_delete1 file_to_delete2 ...\n", argv[0]);
        printf("       %s -r directory  # recursive deletion, only for ivm64\n", argv[0]);
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
                perror("unlink");
                fprintf(stderr, "Removing file '%s' FAILED!\n", name);
                status |= ret;
            }
        }
    }
    return status;
}

// To test unlinkat
static int main_unlinkat(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Delete (unlink) a file at a given directory open with opendir\n");
        printf("Usage: %s dirfd filename\n", argv[0]);
        return -1;
    }
    int dirfd = atoi(argv[1]);
    char *pathname = argv[2];
    int m = unlinkat(dirfd, pathname, 0UL);
    if (m){
        fprintf(stderr, "Deleting file '%s' @ dirfd=%d FAILED!\n", pathname, dirfd);
        perror("openat");
        return -1;
    }
    fprintf(stderr, "File '%s' @ dirfd=%d\n", pathname, dirfd);
    return 0;
}

// To test link
static int main_symlink(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Create a soft-link file2 -> file1\n");
        printf("Usage: %s -s file1 file2\n", argv[0]);
        return -1;
    }
    if (strcmp(argv[1], "-s")) {
         fprintf(stderr, "Only soft links supported yet, use -s as 2nd. argument\n");
         return -1;
    }
    char *name1 = argv[2];
    char *name2 = argv[3];
    int ret = symlink(name1, name2);
    if (ret < 0){
        perror("symlink");
    }
    return ret;
}

// To test linkat
static int main_symlinkat(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Create a soft-link at open directory: file2@dirfd -> file1\n");
        printf("Usage: %s file1 dirfd file2\n", argv[0]);
        return -1;
    }
    char *name1 = argv[1];
    int dirfd = atoi(argv[2]);
    char *name2 = argv[3];
    int ret = symlinkat(name1, dirfd, name2);
    if (ret < 0){
        perror("symlinkat");
    }
    return ret;
}

// To test rename
static int main_rename(int argc, char *argv[])
{
    if (argc < 3) {
         printf("Usage: %s oldname newname\n", argv[0]);
         return 1;
    }
    int ret = rename(argv[1], argv[2]);
    if (ret == -1){
         perror("rename");
         fprintf(stderr, "Renaming '%s' -> '%s' FAILED!\n", argv[1], argv[2]);
    }
    return ret;
}


static int main_mv(int argc, char *argv[])
{
    #define BUFFSIZE (PATH_MAX*2)
    char buff[BUFFSIZE];
    if (argc < 3) {
        printf("Usage: mv SOURCE DEST\n");
        printf("\tRename SOURCE to DEST, or move SOURCE(s) to DIRECTORY\n");
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


static int copyat(int dirorigfd, char *orig, int dirdestfd, char *dest)
{
    int fdi, fdo;

    fdi = openat(dirorigfd, orig, O_RDONLY);

    int fdro = openat(dirdestfd, dest, O_RDONLY);
    if (fdro > 0 && is_same_inode(fdi, fdro)){
        // Check in read-only if they are the same file to not overwrite destination
        fprintf(stderr, "input file is output file\n");
        close(fdro);
        return -10;
    }
    close(fdro);

    if (fdi == -1) return -3;
    fdo = openat(dirdestfd, dest, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fdo == -1) {
        close(fdi);
        return -4;
    }
    int res = COPY(fdi,fdo); // 0: ok, -1: READ, -2: WRITE
    close(fdi);
    close(fdo);
    return (res < 0)? res: 0;
}

static int main_cp(int argc, char *argv[])
{
    #define BUFFSIZE (PATH_MAX*2)
    char buff[BUFFSIZE];
    if (argc < 3) {
        printf("Usage: cp SOURCE DEST\n");
        printf("\tCopy SOURCE to DEST, or copy SOURCE(s) to DIRECTORY\n");
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
            // res == -4, dest may be directory
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

static int main_dd(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s if=<input file> of=<output file> [count=<num item>] [bs=<tam item>]\n",argv[0]);
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

// To test renameat
static int main_renameat(int argc, char *argv[])
{
    if (argc < 4) {
         printf("Rename files at directories opened with opendir\n");
         printf("Usage: %s olddirfd oldpathname newdirfd newpathname\n", argv[0]);
         printf("       AT_FDCWD=%d\n", AT_FDCWD);
         return 1;
    }
    int olddirfd = atoi(argv[1]), newdirfd = atoi(argv[3]);
    char *oldpathname = argv[2], *newpathname = argv[4];
    int ret = renameat(olddirfd, oldpathname, newdirfd, newpathname);
    if (ret == -1){
        perror("renameat");
        fprintf(stderr, "Renaming '%s' @ dirfd=%d -> '%s' @ dirfd=%d FAILED!\n", oldpathname, olddirfd, newpathname, newdirfd);
    } else {
        fprintf(stderr, "OK Renaming '%s' @ dirfd=%d -> '%s' @ dirfd=%d\n", oldpathname, olddirfd, newpathname, newdirfd);
    }
    return ret;
}


static int main_rmdir(int argc, char *argv[]) {
    int status = 0;
    for (int i = 1; i < argc; i++){
        char *dirname = argv[i];
        int ret = rmdir(dirname);
        if (ret < 0){
            perror("rmdir");
            fprintf(stderr, "Removing directory '%s' FAILED!\n", dirname);
            status |= ret;
        }
    }
    return status;
}


// Touch a regular file
static int main_touch_open(int argc, char *argv[])
{
    int status = 0;
    for (int i = 1; i < argc; i++){
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd < 0){
            perror("open");
            printf("Touching file '%s' FAILED!\n", argv[i]);
            status++;
        } else {
            close(fd);
        }
    }
    return status;
}

// Touch a regular file (fopen version)
// static int main_touch_fopen(int argc, char *argv[])
// {
//     int status = 0;
//     for (int i = 1; i < argc; i++){
//         FILE *fd = fopen(argv[i], "a");
//         if (!fd){
//             perror("open");
//             printf("Touching file '%s' FAILED!\n", argv[i]);
//             status++;
//         } else {
//             fclose(fd);
//         }
//     }
//     return status;
// }

#define main_touch(a,b) main_touch_open(a,b)

// Open a file
static void print_open_flags(){
    printf("Flags:\t O_RDONLY=%#x O_WRONLY=%#x O_RDWR=%#x \n"
              "\t O_CREAT=%#x O_EXCL=%#x\n"
              "\t O_TRUNC=%#x O_APPEND=%#x\n"
              "\t O_DIRECTORY=%#x O_NOFOLLOW=%#x\n"
              "\t O_TMPFILE=%#x\n",
                O_RDONLY, O_WRONLY, O_RDWR,
                O_CREAT, O_EXCL,
                O_TRUNC, O_APPEND,
                O_DIRECTORY, O_NOFOLLOW,
                O_TMPFILE);
}
static int main_open(int argc, char *argv[])
{
    if (argc < 2) {
         printf("Usage:%s filename [or-ed hex flags: 0x...]\n", argv[0]);
         print_open_flags();
         return 1;
    }
    char *pathname = argv[1];
    int flags = 0;
    if (!argv[2]){
        flags = O_RDWR;
    } else {
        flags = strtol(argv[2], NULL, 16);
    }
    int fid = open(pathname, flags, 0777);
    if (-1 != fid){
        printf("File '%s' opened fid=%d (flags=%#x)\n", pathname, fid, flags);
        char ans[33];
        sprintf(ans, "%i", fid);
        setenv("ans", ans, 1);
    } else {
        perror("open");
        printf("Opening file or dir '%s' FAILED!\n", pathname);
        return -1;
    }
    return 0;
}

// Open a file in a directory open with opendir
static int main_openat(int argc, char *argv[])
{
    if (argc < 3) {
         printf("Open a file at a directory open with opendir\n");
         printf("Usage: %s dirfd filename\n", argv[0]);
         return 1;
    }
    int dirfd = atoi(argv[1]);
    char *pathname = argv[2];
    int fid = openat(dirfd, pathname, O_RDWR);
    if (-1 != fid){
        printf("File '%s' @ dirfd=%d opened fid=%d\n", pathname, dirfd, fid);
        char ans[33];
        sprintf(ans, "%i", fid);
        setenv("ans", ans, 1);
    } else {
        printf("Opening regular file '%s' @ dirfd=%d FAILED!\n", pathname, dirfd);
        perror("openat");
        return -1;
    }
    return 0;
}


// Close an open file
static int main_close(int argc, char *argv[])
{
    if (argc < 2) {
         printf("Usage: %s <fileno>\n", argv[0]);
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

// Open a directory
static int main_opendir(int argc, char *argv[])
{
    if (argc < 2) {
         printf("Usage: %s dirname\n", argv[0]);
         return 1;
    }
    char *pathname = argv[1];
    DIR *d = opendir(pathname);
    if (d){
        printf("Directory '%s' opened fid=%d\n", pathname, dirfd(d));
        char ans[33];
        sprintf(ans, "%i", dirfd(d));
        setenv("ans", ans, 1);
    } else {
        printf("Opening dir '%s' FAILED!\n", pathname);
        perror("opendir");
        return -1;
    }
    return 0;
}


// Close an open directory
static int main_closedir(int argc, char *argv[])
{
    if (argc < 2) {
         printf("Usage: %s <fileno>\n", argv[0]);
         return 1;
    }
    return main_close(argc, argv);
}

static void print_dirent(struct dirent *p, char *dirname) {
    if (p){
        char *type = "n/a", *arrow="";
        long size = -1;
        struct stat s;

        char fullname[PATH_MAX*2+1], realfullname[PATH_MAX*2];
        snprintf(fullname, PATH_MAX*2+1, "%s/%s", dirname, p->d_name); fullname[PATH_MAX-1] = '\0';
        char *rl = realpath(fullname, realfullname);  realfullname[PATH_MAX-1] = '\0';

        char linkname[PATH_MAX]; linkname[0]='\0';
        int serr = lstat(fullname, &s);  // p->d_name is the base name (not the full name)

        if (!serr) {
            type = S_ISREG(s.st_mode)?" ":S_ISDIR(s.st_mode)?"d":S_ISLNK(s.st_mode)?"l":"?";
            size = s.st_size;
            if (S_ISLNK(s.st_mode)){
                long r = readlink(fullname, linkname, PATH_MAX);
                linkname[r] = '\0';
                arrow = (r>0)?"->":"";
            }
        }
        char bn[PATH_MAX];
        strncpy(bn, p->d_name, PATH_MAX); bn[PATH_MAX-1] = '\0'; // basename may alter the buffer

        char nametoprint[PATH_MAX*2+3];
        sprintf(nametoprint, "%s %s %s", basename(bn), arrow, linkname);

        printf("% 9ld\t% 8ld\t %s \t %-15s %s\n", p->d_ino, size, type, nametoprint, realfullname);
    }
}

static int main_ls(int argc, char *argv[])
{
    // Open and list a directory
    char buff[PATH_MAX];
    char *path=NULL, *canon_path=NULL;
    DIR *dir;

    char *d = argv[1];
    if (!d || !*d) d = "."; // Null or empty string

    // Find canonical name
    path=d;
    canon_path = realpath(path, buff);
    //printf("realpath('%s') -> '%s'\n", path, canon_path);

    // Open the directory
    dir = opendir(d);
    if(!dir){
        printf("Failed opendir '%s'\n", d);
        if (canon_path) {
            d = canon_path;
            printf("Trying its canonicalized form '%s'\n", d);
            dir = opendir(d);
        }
    }
    if (dir) {
        printf("Dir '%s' is open ", d);
        printf(" ==> Listing '%s' (realpath='%s')\n", d, canon_path);

        /* Scan directory */
        struct dirent *pdirent;
        printf("  inode   \t bytes  \ttype  \t name            fullname\n");
        printf("--------- \t -------\t----- \t -----           -----\n");
        while ((pdirent = readdir(dir)) != NULL){
            print_dirent(pdirent, canon_path);
        }
        printf("--------- \t -------\t----- \t -----           -----\n");

        int c = closedir(dir);
        //printf("c=%d\n\n", c);
    } else {
        printf("Failed opendir '%s'\n", d);
    }
    return 0;
}

/* ls using scandir, sort alphabetically */
static int main_dir(int argc, char *argv[])
{
    char *d = argv[1];
    if (!d || !*d) d = "."; // Null or empty string

    // Find canonical name
    char buff[PATH_MAX];
    char *canon_path=NULL;
    canon_path = realpath(d, buff);

    /* Scan directory */
    struct dirent **files;
    int nfiles = scandir(d, &files, NULL, alphasort);
    if (nfiles == -1) {
        perror("scandir");
        return -1;
    }

    /* Traverse scan */
    printf("  inode   \t bytes  \ttype  \t name            fullname\n");
    printf("--------- \t -------\t----- \t -----           -----\n");
    for (long k=0; k<nfiles; k++){
        struct dirent *dd = files[k];
        print_dirent(dd, canon_path);
        free(dd);
    }
    printf("--------- \t -------\t----- \t -----           -----\n");

    free(files);
    return 0;
}

static int main_lseek(int argc, char *argv[])
{
    if (argc < 4){
        printf("Usage:\n\t%s fd offset <whence>\n",argv[0]);
        printf("Whence:\n\tSEEK_SET=%d, SEEK_CUR=%d, SEEK_END=%d\n", SEEK_SET, SEEK_CUR, SEEK_END);
        return -1;
    }
    int fd = atoi(argv[1]);
    long offset = atol(argv[2]);
    int whence = atoi(argv[3]);
    long newoffset = lseek(fd, offset, whence);
    if (newoffset != -1) {
        fprintf(stdout, "new offset=%ld\n", newoffset);
    } else {
        perror("lseek");
        return -1;
    }
}


static int main_seekdir(int argc, char *argv[])
{
    // To test rewindir() and seekdir(),
    // show the listing but after rewindir + seekdir(loc)

    if (argc != 3){
        printf("Seekdir: show a directory list starting at a given location\n");
        printf("Usage: %s dirname loc\n",argv[0]);
        return -1;
    }

    // Open and list a directory
    char buff[PATH_MAX];
    char *path=NULL, *canon_path=NULL;
    DIR *dir;

    char *d = argv[1];
    if (!d || !*d) d = "."; // Null or empty string

    long loc = atoi(argv[2]);

    // Find canonical name
    path=d;
    canon_path = realpath(path, buff);

    // Open the directory
    dir = opendir(d);
    if(!dir){
        printf("Failed opendir '%s'\n", d);
        if (canon_path) {
            d = canon_path;
            printf("Trying its canonicalized form '%s'\n", d);
            dir = opendir(d);
        }
    }
    if (dir) {
        printf("Dir '%s' is open ", d);
        printf(" ==> Listing '%s' (realpath='%s')\n", d, canon_path);

        /* Scan directory */
        struct dirent *pdirent;
        // Test rewind() / seekdir()
        printf("Listing after rewinddir() + seekdir(dir, %ld)\n", loc);
        printf("inode \t bytes  \ttype  \t name            fullname\n");
        printf("----- \t -------\t----- \t -----           -----\n");

        #define MAXLOC 1024*1024

        long offset[MAXLOC], i=0;
        offset[0] = telldir(dir);
        while ((pdirent = readdir(dir)) != NULL){
            offset[++i] = telldir(dir);
            if (i>=MAXLOC) break;
        }

        // Values under 0 -> set 0; over the number of dirs -> max. offset
        long eoffset = (loc > i )?offset[i]:(loc<0)?offset[0]:offset[loc];
        //- long eoffset = ((loc>=0)&&(loc<=i))?offset[loc]:loc; // Arbitrary value

        rewinddir(dir);
        seekdir(dir, eoffset);

        while ((pdirent = readdir(dir)) != NULL){
            print_dirent(pdirent, canon_path);
        }
        printf("----- \t -------\t----- \t -----           -----\n");

        //- // Print all the seen entries, reversely
        //- for (long k=i; k>=0; k--){
        //-     seekdir(dir, offset[k]);
        //-     print_dirent(readdir(dir), canon_path);
        //- }
        //- printf("----- \t -------\t----- \t -----           -----\n");

        closedir(dir);
    } else {
        printf("Failed opendir '%s'\n", d);
    }
    return 0;
}


// Functions stat() or lstat()
static int main_stat(int argc, char *argv[])
{
    struct stat st;
    int res = 0;
    if (argc == 1) {
        printf("%s: missing operand\n",argv[0]);
        printf("Usage: stat FILE1  FILE2 ...\n");
        printf("       lstat FILE1  FILE2 ...\n");
        printf("       fstat fd1  fd2 ...\n");
        return -1;
    }
    char *cmd = argv[0];
    argv++;
    while (*argv) {
        int s;
        if (!strcmp(cmd, "lstat")) {
            s = lstat(*argv++, &st);
        }
        else if (!strcmp(cmd ,"stat")) {
            s = stat(*argv++, &st);
        }
        else if (!strcmp(cmd, "fstat")) {
            s = fstat(atoi(*argv++), &st);
        }
        if (s == 0) {
            printf("%s: dev=%ld, ino=%ld, mode=%#o, nlink=%ld, uid=%d, gid=%d, rdev=%ld, size=%ld, blksize=%ld, blocks=%ld\n",
                    cmd, st.st_dev, st.st_ino, st.st_mode, st.st_nlink, st.st_uid, st.st_gid, st.st_rdev, st.st_size,
                    st.st_blksize, st.st_blocks);
        } else {
            perror(cmd);
            res = -1;
        }
    }
    return res;
}

static int bn(char *name)
{
    printf("%s\n", basename(name));
    return 0;
}

static int dn(char *name)
{
    printf("%s\n", dirname(name));
    return 0;
}


//-- // A regular file exists
//-- static int exist(char *name){
//--     int fid;
//--     fid = open(name, O_RDWR);
//--     if (fid < 0){
//--         //printf("'%s' NOT found in filesystem\n", name);
//--         return 0;
//--     } else {
//--         //printf("'%s' FOUND in filesystem\n", name);
//--         close(fid);
//--         return 1;
//--     }
//-- }

static int main_readlink(int argc, char *argv[])
{
    if (argc == 1) {
        printf("Usage: %s [-f][-e] FILE\n",argv[0]);
        return -1;
    }
    char canon_name[PATH_MAX];
    char *output=canon_name;
    int res = 0;
    if (strcmp("-f", argv[1]) == 0) {
        //output = checkpath(argv[2], canon_name);// checkpath no es una "syscall", insertar aqui el code
        //res = (output == NULL);
    } else if (strcmp("-e",argv[1]) == 0) {
        output = realpath(argv[2], canon_name);
        res = (output == NULL);
    } else {
        res = readlink(argv[1], canon_name, PATH_MAX);
        if (res < 0) {
             perror("readlink");
             output = NULL;
        }
    }
    if (output) printf("%s\n",output);
    return res;
}

static int main_readlinkat(int argc, char *argv[])
{
    if (argc == 1) {
        printf("Usage: %s dirfd FILE\n",argv[0]);
        return -1;
    }
    char canon_name[PATH_MAX];
    char *output=canon_name;
    int fd = atoi(argv[1]);
    int res = 0;
    res = readlinkat(fd, argv[2], canon_name, PATH_MAX);
    if (res < 0) output = NULL;
    if (output) printf("%s\n",output);
    return res;
}

static int main_dup2(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s oldfd newfd [-silent]\n",argv[0]);
        return -1;
    }
    int oldfd = atoi(argv[1]);
    int newfd = atoi(argv[2]);
    int res = dup2(oldfd, newfd);
    if (!(argv[3] && !strncmp(argv[3], "-s", 2))) {
        // If not -s, be a bit verbose
        fprintf(stderr, "dup2(%d, %d) = %d\n", oldfd, newfd, res);
    }
    return res;
}

static int main_dup(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s oldfd [-silent]\n",argv[0]);
        return -1;
    }
    int oldfd = atoi(argv[1]);
    int res = dup(oldfd);
    if (!(argv[2] && !strncmp(argv[2], "-s", 2))) {
        // If not -s, be a bit verbose
        fprintf(stderr, "dup(%d) = %d\n", oldfd, res);
    }
    return res;
}

static int usage_realpath(char *comm)
{
    //printf("Usage: %s [-e][-m] FILE\n", comm);
    printf("Usage: %s FILE\n", comm);
    return -1;
}
static int main_realpath(int argc, char *argv[])
{
    char *output, *input, buff[PATH_MAX];
    //~ if (argc == 1) return usage_realpath(argv[0]);
    //~ if (strcmp(argv[1],"-e")==0) { //all components of the path must exist
    //~     input = argv[2];
    //~     if (!input) return usage_realpath(argv[0]);
    //~     output = realpath(input, buff);
    //~ } else if (strcmp(argv[1],"-m")==0) { // no path components need exist or be a directory
    //~     input = argv[2];
    //~     if (!input) return usage_realpath(argv[0]);
    //~     output = softpath(input, buff); // esto no es una 'syscall'
    //~ } else { // all but the last component must exist
    //~     input = argv[1];
    //~     output = checkpath(input, buff); // esto no es una 'syscall'
    //~ }
    input = argv[1];
    if (!input) return usage_realpath(argv[0]);
    output = realpath(input, buff);
    if (!output) {
        perror("realpath");
        return -1;
    }
    //printf("Realpath of '%s' -> '%s'\n", input, output);
    printf("%s\n", output);
    return 0;
}


static void print_byte(unsigned char c, size_t size)
{
    if (isprint(c) || isspace(c)) {
        printf("%c", c);
    } else {
        printf("\\x%02x", c);
    }
}
// A command like cat but simpler, to show regular files
// It shows non printable chars as hex codes
static int main_type(int c, char *args[])
{
    if (!args[1]) {
        printf("Dump a regular file showing hexcodes of non-printable chars\n");
        printf("Usage: type regular_file_name\n");
        return 1;
    } else {
        args[2]=NULL;
        FILE *fh = fopen(args[1], "r");
        if (!fh) {
            printf("Error reading regular file '%s'\n", args[1]);
            return 2;
        } else {
            long n = 0;
            unsigned char c;
            while (fscanf(fh, "%c", &c) && !feof(fh)){
                print_byte(c,1);
                n++;
            }
            if (!n) {
                fprintf(stderr, "Regular file '%s' is empty\n", args[1]);
                fclose(fh);
                return 3;
            }
            fclose(fh);
            //puts("");
            return 0;
        }
    }
}

// Write n char to a file, ovewriting it
static int main_writef(int c, char **args){
    if (c < 3) {
        printf("Write chars to a file by its name (-n add a newline at the end)\n");
        printf("Usage: writef <nbytes> filename\n");
        printf("       writef string filename\n");
        printf("       writef string filename -n\n");
        return 1;
    } else {
        long N = atol(args[1]);
        char *name = args[2];
        char *A="abcdefghijklmnopqrstuvwxyz~";
        if (N == 0){
            A = args[1];
            N = strlen(args[1]);
        }
        int newline = args[3] && !strcmp(args[3], "-n");
        FILE* fp = fopen(name, "w");
        if (fp){
            long lA = strlen(A);
            for (int i=0; i<N; i++){
                fprintf(fp, "%c", A[i % lA]);
            }
            if (newline)
                fprintf(fp, "\n");
            fclose(fp);
            return 0;
        } else {
            perror("fopen");
            return 2;
        }
    }
}

// Write n char to an open file
static int main_write(int argc, char **args){
    if (argc < 3) {
        printf("Write chars to an open file by its file no.\n");
        printf("Usage: write fd <nbytes> \n");
        printf("       write fd string \n");
        return 1;
    } else {
        int fd= atoi(args[1]);
        long N = atol(args[2]);
        char *A="abcdefghijklmnopqrstuvwxyz~";
        // If it is not a number, consider it a string, but string "0" are considered writing 0 bytes
        if (N == 0 && strcmp(args[2], "0")){
            A = args[2];
            N = strlen(args[2]);
        }

        char *buff = (char*)malloc(sizeof(char)*N+1);
        buff[0] = '\0';
        long lA = strlen(A);
        for (int i=0; i<N; i++){
            char cb[2];
            sprintf(cb, "%c", A[i % lA]);
            strcat(buff, cb);
        }

        long lw = write(fd, buff, N);
        free(buff);
        if (lw!=N){
            perror("write");
        }
        return (N-lw);
    }
}

// Read n char from an open file
static int main_read(int argc, char **args){
    int ret;
    if (argc < 3) {
        printf("Read chars from an open file by its file no.\n");
        printf("Usage: read fd <nbytes> \n");
        return 1;
    } else {
        int fd = atoi(args[1]);
        long N = atol(args[2]);
        char *buff = (char*)malloc(sizeof(char)*N+1);
        long lr = read(fd, buff, N);
        if (lr == 0){
            fprintf(stderr, "EOF\n");
            ret = 1;
        } else if (lr < 0){
            perror("read");
            ret = -1;
        } else {
            buff[lr]='\0';
            printf("Read %ld bytes: '", lr);
            for (long k=0; k < lr ; k++){
                print_byte(buff[k], 1); 
            }
            printf("'\n");
            free(buff);
            ret = N-lr;
        }
        return ret;
    }
}



// Truncate a file
static int main_truncate(int c, char **args){
    if (c < 3) {
        printf("Usage: truncate <nbytes> filename\n");
        return 1;
    } else {
        long N = atol(args[1]);
        char *name = args[2];
        int t = truncate(name, N);
        if (t<0) {
            perror("truncate");
            return t;
        }
    }
    return 0;
}

// Truncate an open file
static int main_ftruncate(int c, char **args){
    if (c < 3) {
        printf("Truncate an open file\nUsage: truncate <fd> <nbytes>\n");
        return 1;
    } else {
        long fid = atol(args[1]);
        long N = atol(args[2]);
        int t = ftruncate(fid, N);
        if (t<0) {
            perror("ftruncate");
            return t;
        }
    }
    return 0;
}

static int main_countargs(int argc, char **args){
    printf("%d\n", argc);
    return 0;
}


/* recursive du (like du -s) */
static unsigned long rdu(char *path)
{
    if (!path || !*path) return 0; // Null or empty string: do nothing

    char d[PATH_MAX];
    strcpy(d, path);

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
    unsigned long size = 0;
    if (chdir(d) == 0) {
        for (long k=0; k<nfiles; k++){
            struct dirent *dd = files[k];
            // Ignore . and .. , to avoid infinite recursion
            if (strcmp(dd->d_name, ".") && strcmp(dd->d_name, "..")) {
                unsigned long dsize = rdu(dd->d_name);
                size += dsize;
            }
            free(dd);
        }
        if (chdir(w) < 0) return -1;
    }

    free(files);
    return size;
}

#define HUMANSIZE(x) ((double)(((x)>1e12)?((x)/1.0e12):((x)>1e9)?((x)/1.0e9):((x)>1.0e6)?((x)/1.0e6):((x)>1e3)?((x)/1.0e3):(x)))
#define HUMANPREFIX(x)  (((x)>1e12)?"T":((x)>1e9)?"G":((x)>1e6)?"M":((x)>1e3)?"K":"")

static int main_du(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Total disk usage in bytes of a directory\n");
        printf("Usage: %s <dir_name> \n",argv[0]);
        return -1;
    }
    unsigned long size = rdu(argv[1]);
    printf("%ld (%.2f%sB)\n", size, HUMANSIZE(size), HUMANPREFIX(size));
    return 0;
}

// Returns the size of the largest memory chunck available.
// The search start in 2^high (HIGHER_BIT set to 48 in macro) and goes
// iteratively down making a number of 'steps' refinement
static unsigned long largest_memory_chunck(int high, int low, int steps, int *exp2)
{
    unsigned long base = 0;
    long incr;
    void *ptr;
    int refine = 0;
    *exp2 = 0;
    for (incr = (1UL << high); incr >= (1UL << low); incr >>= 1, high--) {
        ptr = malloc(base + incr);
        if (ptr) {
            if (*exp2 == 0) *exp2 = high; 
            free(ptr);
            ptr = NULL;
            base += incr;
            if (refine++ >= steps) break;
        }
    }
    return base;
}

static int main_free(int argc, char *argv[])
{
    int exp2;
    unsigned long m = largest_memory_chunck(48, 1, 5, &exp2);
    printf("Free (max. malloc): %.2f%sB (2^%d)\n", HUMANSIZE(m), HUMANPREFIX(m), exp2);
    return 0; 
}


static int main_mkstemp(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s templateXXXXXX\n",argv[0]);
        return -1;
    }

    char *template = strdup(argv[1]);
    int fd = mkstemp(template);
    if (fd < 0) {
        perror("mkstemp");
    } else {
        fprintf(stdout, "temporay file '%s' opened as fd=%d\n", template, fd);
    }
    free(template);

    return (fd<0);
}

static int main_mkdtemp(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s templateXXXXXX\n",argv[0]);
        return -1;
    }

    char *template = strdup(argv[1]);
    char *t = mkdtemp(template);
    if (!t) {
        perror("mkstemp");
    } else {
        fprintf(stdout, "temporary directory '%s' created\n", t);
    }
    free(template);

    return !t;
}

// Change permissions
static int main_chmod(int argc, char *args[]){
    if (argc < 3) {
        printf("Usage: chmod <mode(octal)> filename\n");
        return 1;
    } else {
        unsigned int mode;
        sscanf(args[1], "%o", &mode);
        int t = chmod(args[2],mode);
        if (t<0) {
            perror("chmod");
            return t;
        }
    }
    return 0;
}

#define RLIMIT_NOFILE 64*1024
static int main_lsof(int argc, char *argv[])
{
    int newfd = open("/", O_RDONLY | O_DIRECTORY);
    if (newfd >= 0) close(newfd);
    else return -1;

    for (long i=0; i<RLIMIT_NOFILE; i++){
        int fd = dup2(i, newfd);
        if (fd >= 0){
            close(fd);
            struct stat s;
            int fs = fstat(i, &s);
            printf("fid=%ld", i);
            if (!fs) {
                long pos = lseek(i, 0, SEEK_CUR);
                printf(" inode=%ld size=%ld pos=%ld %s",
                       s.st_ino, s.st_size, pos,
                       S_ISREG(s.st_mode)?"isreg":S_ISDIR(s.st_mode)?"isdir":S_ISLNK(s.st_mode)?"islnk":S_ISCHR(s.st_mode)?"isdev":"");
            }
            puts("");
        }
    }
    return 0;
}

static int main_spawn(int argc, char *argv[])
{
    //#ifndef __ivm64__
	//fprintf(stderr, "ivm_spawn only available for ivm64 architecture\n");
	//return -1;
	//#endif
    if (argc < 2) {
        printf("Usage: %s <ivm64_binary> [arg1] [arg2] ...\n",argv[0]);
        return -1;
    }
    int ret = 0;
    #ifdef __ivm64__
        ret = ivm_spawn(argc-1, &argv[1]);
    #else
        pid_t pid = fork();
        if (pid == 0) {
            execv(argv[1], &argv[1]);
            perror("exec");
            exit(-1);
        } else if (pid>0) {
            int wstatus;
            wait(&wstatus);
            ret = WEXITSTATUS(wstatus);
        } else{
            fprintf(stderr, "fork failed!\n");
        }
    #endif
    //fprintf(stderr, "spawn returned %d\n", ret);
    return ret;
}

static int main_source(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <shell_script>\n",argv[0]);
        printf("       note: arguments are not supported for now\n");
        return -1;
    }

    // This source command does not accept input redirections like 'source script < file'
    if (stdin_0 >= 0) {
        fprintf(stderr, "The source command does not accept input redirection\n");
        return 1;
    }

    // Save current stdout, stderr of the shell, in case the source command has
    // output redirection, in this case we will redirect in the preamble in
    // such a way all the script get redirected too (to run things like 'source
    // script.sh > output.txt'
    int shell_stdout_fileno0 = dup(stdout_0);
    int shell_stderr_fileno0 = dup(stderr_0);
    int source_stdout_fileno0 = dup(STDOUT_FILENO);
    int source_stderr_fileno0 = dup(STDERR_FILENO);

    //printk("[%s] stdout_0=%d shell_stdout_fileno0=%d\n", __func__, stdout_0, shell_stdout_fileno0); // Debug

    // Save current stdin
    int stdin_fileno0 = dup(STDIN_FILENO);
    int tmpfd = -1;
    if (stdin_fileno0 >= 0) {
        // Make a tmp copy of the script, in order to add a preamble to prepare
        // things, and an epilogue to restore STDIN_FILENO and close temporary
        // files
        mkdir("/tmp", 0777);
        tmpfd = open("/tmp/", O_TMPFILE | O_RDWR, 0777);
        //tmpfd = open("/tmp/_source_script_tmp", O_CREAT | O_TRUNC | O_RDWR, 0777); // Debug
        if (tmpfd >= 0) {
            lseek(tmpfd, 0, SEEK_SET);
            int fd = open(argv[1], 0x0);
            if (fd >= 0) {
                char buff[MAX_LINE];
                // Script preamble
                sprintf(buff, "# preamble ===========\n"
                              "dup2 %d 1 -silent \n"
                              "dup2 %d 2 -s \n"
                              //"lsof; echo end preamble----- \n"
                              "#=====================\n",
                               source_stdout_fileno0, source_stderr_fileno0);
                int nw = write(tmpfd, buff, strlen(buff));
                // Copy the script to the temporary file
                COPY(fd, tmpfd);
                close(fd);
                // Append to the end a dup2 to restore the stdin to the copy of
                // the script
                lseek(tmpfd, 0, SEEK_END);
                // Script epilogue
                // All the (sub)commands of the epilogue must be in the same
                // line separated by semicolons, because once STDIN_FILENO is
                // restored (dup2), no more reads are possible from the copy of
                // the script
                sprintf(buff, "\n# epilogue ===========\n"
                              //"echo begin_epilogue -----; lsof \n"
                              "dup2 %d 0 -s ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "dup2 %d 1 -s ;"
                              "dup2 %d 2 -s ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "close %d  > /tmp/null 2> /tmp/null ;"
                              "rm /tmp/null ;"
                              "\n",
                              stdin_fileno0,
                              stdin_fileno0, tmpfd,
                              shell_stdout_fileno0, shell_stderr_fileno0,
                              shell_stdout_fileno0, shell_stderr_fileno0,
                              source_stdout_fileno0, source_stderr_fileno0
                              );
                nw = write(tmpfd, buff, strlen(buff));
                // Do tmpfd->stdin, and the script will get executed automagically
                // Notice as from this point, we are not in a tty, there is no
                // prompt while executing source
                lseek(tmpfd, 0, SEEK_SET); //rewind after writing
                // This redirection does the magic for executing the script
                int res = dup2(tmpfd, STDIN_FILENO);
                if (res < 0) {
                    // If fails, restore all stuff like in the epilogue
                    fprintf(stderr, "Running script '%s' failed\n", argv[1]);
                    goto error;
                }
            } else {
                // Only close tmpfd if open() fails; otherwise the closing of this file
                // is done by the epilogue
                fprintf(stderr, "Opening '%s' failed\n", argv[1]);
                goto error;
            }
        } else {
            fprintf(stderr, "Opening tmp file failed\n");
            goto error;
        }
    }
    return 0;

    error:
        // If fails, close all stuff like in the epilogue
        if (tmpfd >= 0) close(tmpfd);
        close(stdin_fileno0);
        close(shell_stdout_fileno0);
        close(shell_stderr_fileno0);
        close(source_stdout_fileno0);
        close(source_stderr_fileno0);
        return -1;
}

// Support for implementing heredoc redirection (" << TOKEN"): read from stdin
// until a line with only "TOKEN" is found, write it to a temporary file; if
// everything is ok return the descriptor to the open temporary file;
// otherwise return -1 
static int heredoc_open(char* token)
{
    mkdir("/tmp", 0777);
    int tmpfd = open("/tmp/", O_TMPFILE | O_RDWR, 0777);
    int hderror = 0;
    if (tmpfd >= 0) {
        char *lineptr = NULL;
        ssize_t n = 0, nr, nw;
        long endlen = strlen(token);
        while ( (nr = getline(&lineptr, &n, stdin)) > 0) {
            // Do not check final EOL 
            int end = !strncmp(token, lineptr, endlen)
                && ('\0' == lineptr[endlen] || '\n' == lineptr[endlen]);
            if (end) {
                free(lineptr);
                break;}
            else {
                nw = write(tmpfd, lineptr, nr);
                if (nw != nr) {hderror = 1; break;}
                free(lineptr);
                lineptr=NULL; n=0;   // Prepare next getline
            }
        }
        //fflush(stdin); // getline() may have read chars in advance letting them in the buffer
        lseek(tmpfd, 0, SEEK_SET);
        if (!hderror) return tmpfd;
        else {
            close(tmpfd);
            return -1;
        }
    }
    return -1;
}

static int main_ioctl(int argc, char *argv[])
{
    if (argc < 4) {
        printf("Usage: %s fd cmd lflag\n",argv[0]);
        printf("  Call ioctl(fd, cmd, tty), with tty->c_lflag=lflag\n");
        printf("  fd (dec), file no.:\n\t STDIN=0, STDOUT=1, STDERR=2 by default\n");
        printf("  cmd (hex), one of:\n\t TCGETS = %#x  TCSETS = %#x  TCSETSW = %#x  TCSETSF = %#x\n", TCGETS, TCSETS, TCSETSW, TCSETSF); 
        printf("  lflag (hex), OR-ed of:\n\t ECHO = %#x  ICANON = %#x\n", ECHO, ICANON); 
        return -1;
    }

    int fd = atoi(argv[1]);
    int cmd = strtol(argv[2], NULL, 16);
    int lflag = strtol(argv[3], NULL, 16);

    struct termios t;
    // Read current termios
    int res = ioctl(fd, TCGETS, &t);

    if (res == 0) { // ioctl returns 0 if OK (no errors) 
        res = -1;
        if (cmd == TCSETS || cmd == TCSETSW || cmd == TCSETSF) {
            // Set o unset flag echo
            if (lflag & ECHO) 
                t.c_lflag |= ECHO;
            else
                t.c_lflag &= ~ECHO;
            // Set o unset flag icanon 
            if (lflag & ICANON) 
                t.c_lflag |= ICANON;
            else
                t.c_lflag &= ~ICANON;
            // Set terminal attributes 
            res = ioctl(fd, cmd, &t);
        } 
        else if (cmd == TCGETS) {
            fprintf(stdout, "lflags=%#x echo=%d icanon=%d\n", t.c_lflag, t.c_lflag & ECHO, t.c_lflag & ICANON);
            res = 0; // if we are here previous TCGETS was ok
        }
    }

    return res;
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

static int walk(const char* directory, const char* prefix, counter_t *counter)
{
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
            segment = "│   ";
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
            printf(" [%02ld] %s\n", i, c);
            free(c);
        };
        printf(" [%02ld] QUIT\n", ncommands); // #ncommands is to quit the selection loop

        // Select a ROAE command
        nc=0; scnf=0;
        printf("Select ROAE command number: ");
        char *roae = fgets(menubuff, ROAEBUFFSIZE-1, stdin); menubuff[ROAEBUFFSIZE-1]='\0';
        if (roae) scnf = sscanf(roae, "%ld", &nc);
        if (scnf>0 && nc>=0 && nc<=ncommands){
            if (nc == ncommands) {
                // Quit from the selection loop
                printf(" QUIT selected ... quitting ...\n\n");
                break;
            }
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
            fprintf(stderr, "ROAE command number is not a valid integer (0 <= n < %ld)\n", ncommands);
            break;
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


#ifdef __ivm64__
// Some external functions: they must be overridden when compiling its source file first
// spawn.c:
 __attribute__((noinline)) int ivm_spawn(int argc, char *argv[]){asm volatile(""); return -1;}

// Some debugging functions; they must be overridden
// when compiling ivmfs.c first
 __attribute__((noinline)) void  debug_print_file_table(){asm volatile(""); return; }
 __attribute__((noinline)) void  debug_print_open_file_table(){asm volatile(""); return;}
 __attribute__((noinline)) int   debug_has_trail(char *a){asm volatile(""); return 0;}
 __attribute__((noinline)) char* debug_remove_trail2(char *a, char *b, char *c){asm volatile(""); return NULL;}
 __attribute__((noinline)) char* debug_realpath_nocheck(char *a, char *b){asm volatile(""); return NULL;}
 __attribute__((noinline)) char* debug_realparentpath(char *a, char *b){asm volatile(""); return NULL;}
 __attribute__((noinline)) long debug_get_spawnlevel(){asm volatile(""); return -1;};
 __attribute__((noinline)) void* debug_get_errno_p(){asm volatile(""); return NULL;};
#endif

static int main_help(int argc, char *argv[])
{
    printf("Immortal Database Access (iDA) EUROSTARS project\n"
           "ROAE shell, %s: "
           "A shell to interface with the Read-Only Access Engine (ROAE)\n", ROAESHELL_VERSION);

    printf("\n"
           "File system commands:\n"
           "   basename cat cd chmod close closedir cp crc32 dd dir dup dup2 dirname echo\n"
           "   exit(=quit)(=^D) fcd find free fstat ftruncate getenv glob help ls lseek lsof lstat\n"
           "   mkdir mkdirat mkstemp mkdtemp mv open openat opendir prompt pwd\n"
           "   read readlink readlinkat realpath rename renameat rm(=unlink) rmdir seekdir\n"
           "   setenv source spawn stat stty symlink(=ln) symlinkat touch tree truncate\n"
           "   type unlinkat unsetenv write writef\n"
           "Available redirections:\n"
           "   '> file', ' 2> file', ' >> file', ' < file', ' << HEREDOC'\n"
           "IDA commands:\n"
           "   roae siard sqlite unzip\n"
          );
    return 0;
}


static void set_prompt(int p) {
    prompt = p;
}

static int get_prompt() {
    return prompt;
}

// -----------------------------------------------------------------------
//                            MAIN
// -----------------------------------------------------------------------
int main(void)
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	char separator = 0;         /* equals ';' or '&' if executing a subcommand ended by ';' or '&', otherwise is 0*/
    int argc;
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */
	int status = 0;             /* status returned by command */

    char *file_in = NULL, *file_out = NULL, *file_out_append= NULL, *file_err = NULL;
    char *file_in_heredoc = NULL;

    // Initialize sqlite shell
    sqlite_shell_init();

    //setvbuf(stdin, NULL, _IOLBF, MAX_LINE);

    status = main_help(0, NULL);
    puts("");

    char currwd[PATH_MAX];

    // Termios tty configuration, use ICANON|ECHO if the tty
    // where running this program has not ICANON nor ECHO.
    struct termios tty;
    ioctl(STDIN_FILENO, TCGETS, &tty);
    tty.c_lflag |= ICANON | ECHO;     // enable emulation of icanon and echo
    //tty.c_lflag &= ~ICANON & ~ECHO; // disable emulation of icanon and echo
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    ioctl(STDIN_FILENO, TCSETS, &tty);

	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{   		
        // Restore standard input/output streams after redirection
        if (file_in && stdin_0 != -1) {
            fclose(stdin);
            dup2(stdin_0,  STDIN_FILENO);
            stdin = fdopen(STDIN_FILENO, "r");
            close(stdin_0);
            stdin_0 = -1;
            file_in = NULL;
            //if (isatty(STDIN_FILENO)) clearerr(stdin);
        }
        if ((file_out || file_out_append) && stdout_0 != -1) {
            fclose(stdout);
            dup2(stdout_0, STDOUT_FILENO);
            stdout = fdopen(STDOUT_FILENO, "a");
            close(stdout_0);
            stdout_0 = -1;
            file_out = NULL;
            file_out_append = NULL;
        }
        if (file_err && stderr_0 != -1) {
            fclose(stderr);
            dup2(stderr_0, STDERR_FILENO);
            stderr = fdopen(STDERR_FILENO, "a");
            close(stderr_0);
            stderr_0 = -1;
            file_err = NULL;
        }
        if (file_in_heredoc && stdin_0 != -1) {
            fclose(stdin);
            dup2(stdin_0,  STDIN_FILENO);
            stdin = fdopen(STDIN_FILENO, "r");
            close(stdin_0);
            stdin_0 = -1;
            file_in_heredoc = NULL;
        }

        // Only print prompt if we are in a tty and for commands ended by
        // newline in a sequence of (sub-)commands separated by ';' or '&'
        if (isatty(STDIN_FILENO) && (separator == '\n' || !separator)) {
            char *wd;
            switch (prompt) {
                case 0: // No prompt
                        break;
                case 1: // Fixed prompt
                        printf("PROMPT> ");
                        break;
                case 2: //Current work directory
                default:
                        wd = getcwd(currwd, PATH_MAX);
                        //getwd(currwd);  // deprecated
                        currwd[PATH_MAX-1]='\0';
                        if (chdir(currwd)){
                            printf("ivmfs:%s> ", "(unknown dir, perhaps moved)");
                        } else {
                            printf("ivmfs:%s> ", wd);
                        }
                        break;
            }
        }
        fflush(NULL);

        /* get next command */
		argc = get_command(inputBuffer, MAX_LINE, args, &separator);

        // Argument postprocessing
        // ignore_comments(&argc, args);
        replace_status(argc, args, status); // Parse $? symbol
        replace_env(argc, args);            // Parse environment variables (e.g. $ENV) 
        parse_redirections(args, &argc, &file_in, &file_out, &file_out_append, &file_err, &file_in_heredoc);

        // Avoid loops in redirections, e.g. "cat < a.txt >> a.txt"
        if ((file_in && file_out && is_same_file(file_in, file_out))
             || (file_in && file_err && is_same_file(file_in, file_err))
             || (file_in && file_out_append && is_same_file(file_in, file_out_append))
           ) {
            fprintf(stderr, "input file is output file\n");
            continue;
        }


        // Do redirections
        // TODO: check errors in redirection
        FILE *fh;
        if (file_in){
            fh = fopen(file_in, "r");
            if (fh) {
                fflush(stdin);
                stdin_0  = dup(STDIN_FILENO);
                dup2(fileno(fh), STDIN_FILENO);
                fclose(fh);
            }
            else{
                perror("Error in stdin redirection '<'");
                status = -1;
                continue;
            }
        }

        if (file_out){
            fh = fopen(file_out, "w");
            if (fh) {
                stdout_0 = dup(STDOUT_FILENO);
                dup2(fileno(fh), STDOUT_FILENO);
                fclose(fh);
            }
            else{
                perror("Error in stdout redirection '>'");
                status = -1;
                continue;
            }
        }

        if (file_out_append){
            fh = fopen(file_out_append, "a");
            if (fh) {
                stdout_0 = dup(STDOUT_FILENO);
                dup2(fileno(fh), STDOUT_FILENO);
                fclose(fh);
            }
            else{
                perror("Error in append redirection '>>'");
                status = -1;
                continue;
            }
        }

        if (file_err){
            fh = fopen(file_err, "w");
            if (fh) {
                stderr_0 = dup(STDERR_FILENO);
                dup2(fileno(fh), STDERR_FILENO);
                fclose(fh);
            }
            else{
                perror("Error in stderr redirection '2>'");
                status = -1;
                continue;
            }
        }

        if (file_in_heredoc){
            int hderror = 0;
            int tmpfd = heredoc_open(file_in_heredoc);
            if(tmpfd > 0) {
                fflush(stdin); // clear buffer before dup
                stdin_0  = dup(STDIN_FILENO);
                dup2(tmpfd, STDIN_FILENO);
                close(tmpfd);
            } else {
                fprintf(stderr, "Error in heredoc redirection '<<'\n");
                status = -1;
                continue;
            }
        }

        // Process command and arguments

		if(args[0]==NULL) continue;   // if empty command

        if (!strcmp("c", args[0])){
            status = main_countargs(argc, args);
            continue;
        }

        if (!strcmp("pwd", args[0])) {
            status = main_pwd(argc, args);
            continue;
        }

        if (!strcmp("cd", args[0])) {
            status = main_cd(argc, args);
            continue;
        }

        if (!strcmp("fcd", args[0])) {
            status = main_fcd(argc, args);
            continue;
        }

        if (!strcmp("ls", args[0])) {
            status = main_ls(argc, args);
            continue;
        }

        if (!strcmp("dir", args[0])) {
            status = main_dir(argc, args);
            continue;
        }

        if (!strcmp("seekdir", args[0]) || !strcmp("sd", args[0])) {
            status = main_seekdir(argc, args);
            continue;
        }

        if (!strcmp("mkdir", args[0])){
            status = main_mkdir(argc, args);
            continue;
        }

        if (!strcmp("mkdirat", args[0])){
            status = main_mkdirat(argc, args);
            continue;
        }

        if (!strcmp("glob", args[0])){
            status = main_glob(argc, args);
            continue;
        }

        if (!strcmp("setenv", args[0])){
            status = main_setenv(argc, args);
            continue;
        }

        if (!strcmp("unsetenv", args[0])){
            status = main_unsetenv(argc, args);
            continue;
        }

        if (!strcmp("getenv", args[0])){
            status = main_getenv(argc, args);
            continue;
        }

        if (!strcmp("env", args[0])){
            status = main_env(argc, args);
            continue;
        }

        if (!strcmp("realpath", args[0]) || !strcmp("rp", args[0])){
            status = main_realpath(argc, args);
            continue;
        }

        if (!strcmp("cat", args[0])){
            status = main_cat(argc, args);
            continue;
        }

        if (!strcmp("type", args[0])){
            status = main_type(argc, args);
            continue;
        }

        if (!strcmp("cp", args[0])){
            status = main_cp(argc, args);
            continue;
        }

        if (!strcmp("dd", args[0])){
            status = main_dd(argc, args);
            continue;
        }

        if (!strcmp("stat", args[0]) || !strcmp("lstat", args[0]) || !strcmp("fstat", args[0])){
            status = main_stat(argc, args);
            continue;
        }

        if (!strcmp("echo", args[0])){
            status = echo(argc, args);
            continue;
        }

        if (!strcmp("rm", args[0]) || !strcmp("unlink", args[0])){
            status = main_unlink(argc, args);
            continue;
        }

        if (!strcmp("unlinkat", args[0])){
            status = main_unlinkat(argc, args);
            continue;
        }

        if (!strcmp("symlink", args[0]) || !strcmp("ln", args[0])){
            status = main_symlink(argc, args);
            continue;
        }

        if (!strcmp("symlinkat", args[0])){
            status = main_symlinkat(argc, args);
            continue;
        }

        if (!strcmp("basename", args[0]) || !strcmp("bn", args[0])){
            status = bn(args[1]);
            continue;
        }

        if (!strcmp("dirname", args[0]) || !strcmp("dn", args[0])){
            status = dn(args[1]);
            continue;
        }

        if (!strcmp("readlink", args[0]) || !strcmp("rl", args[0])){
            status = main_readlink(argc, args);
            continue;
        }

        if (!strcmp("readlinkat", args[0])){
            status = main_readlinkat(argc, args);
            continue;
        }

        if (!strcmp("touch",args[0])){
            status = main_touch(argc, args);
            continue;
        }

        if (!strcmp("mv", args[0])){
            status = main_mv(argc, args);
            continue;
        }

        if (!strcmp("rename", args[0]) || !strcmp("rn", args[0])){
            status = main_rename(argc, args);
            continue;
        }

        if (!strcmp("renameat", args[0])){
            status = main_renameat(argc, args);
            continue;
        }

        if (!strcmp("read", args[0])){
            status = main_read(argc, args);
            continue;
        }

        if (!strcmp("write", args[0])){
            status = main_write(argc, args);
            continue;
        }

        if (!strcmp("writef", args[0])){
            status = main_writef(argc, args);
            continue;
        }

        if (!strcmp("truncate", args[0])){
            status = main_truncate(argc, args);
            continue;
        }

        if (!strcmp("ftruncate", args[0])){
            status = main_ftruncate(argc, args);
            continue;
        }

        if (!strcmp("rmdir", args[0])){
            status = main_rmdir(argc, args);
            continue;
        }

        if (!strcmp("open", args[0])){
            status = main_open(argc, args);
            continue;
        }

        if (!strcmp("openat", args[0])){
            status = main_openat(argc, args);
            continue;
        }

        if (!strcmp("close", args[0])){
            status = main_close(argc, args);
            continue;
        }

        if (!strcmp("lseek", args[0])) {
            status = main_lseek(argc, args);
            continue;
        }

        if (!strcmp("dup", args[0])){
            status = main_dup(argc, args);
            continue;
        }

        if (!strcmp("dup2", args[0])){
            status = main_dup2(argc, args);
            continue;
        }

        if (!strcmp("opendir", args[0])){
            status = main_opendir(argc, args);
            continue;
        }

        if (!strcmp("closedir", args[0])){
            status = main_closedir(argc, args);
            continue;
        }

        if (!strcmp("tree", args[0])){
            status = main_tree(argc, args);
            continue;
        }

        if (!strcmp("du", args[0])){
            status = main_du(argc, args);
            continue;
        }

        if (!strcmp("free", args[0])){
            status = main_free(argc, args);
            continue;
        }

        if (!strcmp("mkstemp", args[0])){
            status = main_mkstemp(argc, args);
            continue;
        }

        if (!strcmp("mkdtemp", args[0])){
            status = main_mkdtemp(argc, args);
            continue;
        }

        if (!strcmp("chmod", args[0])){
            status = main_chmod(argc, args);
            continue;
        }

        if (!strcmp("lsof", args[0])){
            status = main_lsof(argc, args);
            continue;
        }

        if (!strcmp("spawn", args[0])){
            status = main_spawn(argc, args);
            continue;
        }

        if (!strcmp("source", args[0])|| !strcmp(".", args[0])){
            status = main_source(argc, args);
            continue;
        }

        if (!strcmp("ioctl", args[0])){
            status = main_ioctl(argc, args);
            continue;
        }

        if (!strcmp("stty", args[0])){
            status = main_stty(argc, args);
            continue;
        }

        if (!strcmp("prompt", args[0])){
            if (argc == 1) {
                printf("Usage: %s <mode>\n\t0:no prompt; 1:fixed; 2:cwd\n", args[0]);
            } else {
                set_prompt(atoi(args[1]));
            }
            continue;
        }

        if (!strcmp("crc32", args[0])){
            status = main_crc32(argc, args);
            continue;
        }

        extern int main_find(int argc, char** args);
        if (!strcmp("find", args[0])){
            status = main_find(argc, args);
            continue;
        }

        extern int main_grep(int argc, char** args);
        if (!strcmp("grep", args[0])){
            status = main_grep(argc, args);
            continue;
        }

        if (!strcmp("sqlite", args[0])){
            status = main_sqlite(argc, args);
            continue;
        }

        if (!strcmp("roae", args[0])){
            status = main_roae(argc, args);
            continue;
        }

        if (!strcmp("siard", args[0])){
            status = main_siard(argc, args);
            continue;
        }

        if (!strcmp("unzip", args[0])){
            status = main_unzip(argc, args);
            continue;
        }

        if (!strcmp("help", args[0])){
            status = main_help(argc, args);
            continue;
        }

        if (!strcmp("exit", args[0]) || !strcmp("quit", args[0])){
            fprintf(stderr, "exit\n");
            int ret = 0;
            if (args[1]) ret = atoi(args[1]);
            exit(ret);
        }


        //---------------------------- Some debugging commands
        #ifdef __ivm64__
        if (!strcmp("t", args[0])){
            debug_print_file_table();
            puts("");
            continue;
        }

        if (!strcmp("ot", args[0])){
            debug_print_open_file_table();
            puts("");
            continue;
        }

        if (!strcmp("ht", args[0])){
            if (argc > 1) printf("%s\n", debug_has_trail(args[1])?"true":"fase");
            continue;
        }

        if (!strcmp("rt2", args[0])){
            char path_copy[PATH_MAX], trail[PATH_MAX];
            if (argc > 1){
                 debug_remove_trail2(args[1], path_copy, trail);
                 printf("%s\n%s\n", path_copy, trail);
            }
            continue;
        }

        if (!strcmp("rpnchk", args[0])){
            char buff[PATH_MAX], *rl;
            if (argc > 1) {
                rl = debug_realpath_nocheck(args[1], buff);
                fprintf(stdout, "%s\n", rl);
            }
            continue;
        }

        if (!strcmp("rpp", args[0])){
            char buff[PATH_MAX], *rl;
            if (argc > 1) {
                rl = debug_realparentpath(args[1], buff);
                fprintf(stdout, "%s\n", rl);
            }
            continue;
        }

        if (!strcmp("spwl", args[0])){
            // Print spawn level
            long curlevel;
            curlevel = debug_get_spawnlevel();
            printf("current spawn level=%ld\n", curlevel);
            continue;
        }

        if (!strcmp("errno", args[0])){
            // Print current errno address 
            printf("&errno=%p\n", debug_get_errno_p());
            continue;
        }

        #endif

        if (access(args[0], X_OK) == 0) {
            // Try to spawn if it is an existing file
            argc = arg_add(argc, args, "spawn");
            status = main_spawn(argc, args);
            continue;
        }

        fprintf(stderr, "Command '%s' not found\n", args[0]);
        status = -1;
	} // end while
}
