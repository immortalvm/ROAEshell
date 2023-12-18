/*
    Immortal Virtual Machine (IVM) and
    Immortal Database Access (iDA) EUROSTARS projects

    A library to spawn ivm64 executable binaries from ivm64 binaries

    It must be warned that because there is no memory protection, the swpawned
    program can allocate dynamic memory beyond its limits, damaging the head of the
    spawner. The stack is the same both for the spawner and the spawned. Nesting
    spawn is allowed, that is, a spawned program can in turn spawn another program
    but having available only a fraction of the memory space of the spawner.

    This code needs to be compiled together with the IVMFS filesystem:
        ivm64-gcc ivmfs.c spawn.c main.c

    Authors:
        Sergio Romero, Eladio Gutierrez, Oscar Plata
        University of Malaga, Spain

    September, 2023
*/

#ifdef __ivm64__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>

#define VERBOSE 0

// Nested (spawned) program will have available a fraction
// (1/IVM_SPAWN_MEM_FRAC) of the memory allocated for the parent (spawner)
// IVM_SPAWN_MEM_FRAC must be greater than 1
#define IVM_SPAWN_MEM_FRAC 4
#define IVM_SPAWN_MEM_PAD 8*1024
//#define IVM_SPAWN_MEM_PAD 8*1024*1024

// From sbrk.c
#define IVM64_STACK_SPARE (1024*64)

extern int printk(const char*, ...);
int ivm64_valid_bin(char *filename, int quick, int verbose);
static int ivm64_valid_bin_fd(int fd, int quick, int verbose);

// This is the limit of the free memory space
//  If spawned -> given by IVM_SPAWN_MEMORY_LIMIT env.var. (allocated memory)
//  If original -> the top of the stack (less a protection gap)
static unsigned long memory_limit;
static int iamnotspawned;


__attribute__((constructor))
static void __IVM_SPAWN_start__(void)
{
    void *heap = sbrk(0);
    iamnotspawned = 0;
    char *var_mlimit = getenv("IVM_SPAWN_MEMORY_LIMIT");
    if (!var_mlimit || ((memory_limit = strtoul(var_mlimit, NULL, 16)) == 0)) {
        // If IVM_SPAWN_MEMORY_LIMIT not set, the limit is the used stack
        memory_limit = (unsigned long)&heap - IVM64_STACK_SPARE;
        // This MUST happen for the first spawner, as it is the only mechanism
        // to avoid overlapping memory in case of nested spawning.
        iamnotspawned = 1;
    }

    if (heap != (void *)(-1) && !iamnotspawned) {
        extern unsigned long __IVM64_max_heap_allocated__;
        __IVM64_max_heap_allocated__ = (memory_limit > (unsigned long)heap)?(memory_limit - (unsigned long)heap):0;
    }
    unsetenv("IVM_SPAWN_MEMORY_LIMIT");

    //printk("[%s] available = %ld\n", __func__, available); //Debug
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
        ptr = malloc(base + incr);
        if (ptr) {
            free(ptr);
            ptr = NULL;
            base += incr;
            if (refine++ >= steps) break;
        }
    }
    return base;
}

#define HIGHER_BIT 48
#define LOWER_BIT  16
#define MAX_REFINEMENT 5
static int ivm_load_bin(char *filename, char **dest, unsigned long *size, unsigned long *offset)
{
    int retval = 0;
    long allocated = 0;

    if (*size == 0 && *dest != NULL) {
        fprintf(stderr, "ivm_load_bin: size is zero but destination is not null\n");
	    return -1;
    }
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "ivm_load_bin: Command not found: '%s'\n", filename);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    long filesize = st.st_size;
    // Check if filename refers to a file with execution permission
    if (!(st.st_mode & S_IXUSR)) {
        fprintf(stderr, "ivm_spawn: permission denied: '%s'\n", filename);
        close(fd);
        return -1;
    }
    // Check if filename refers to a valid ivm64 binary file
    if (!ivm64_valid_bin_fd(fd, 0, 0)){
        fprintf(stderr, "ivm_spawn: ivm64 binary format error: '%s'\n", filename);
        close(fd);
        return -1;
    }

    if (*size == 0) {
        // ask for the largest memory chuck available
        unsigned long spawnsize = largest_memory_chunck(HIGHER_BIT, LOWER_BIT, MAX_REFINEMENT);
        // The first program (not spawned) holds the filesystem, so the memory
        //      assigned to the spawned program/machine should be smaller (a
        //      fraction) in order to leave enought memory for the filesystem.
        //printf("Max. allocatable space=%20ld\n", spawnsize);
        if (iamnotspawned) spawnsize = spawnsize / IVM_SPAWN_MEM_FRAC;
        //printf("Currently allocated   =%20ld\n", spawnsize);
        if (spawnsize < filesize) {
            fprintf(stderr, "ivm_load_bin: Not enough memory to load binary (spawnsize: %ld) (binarysize: %ld)\n", spawnsize, filesize);
            close(fd);
            return -1;
        }
        void *mem = calloc(spawnsize, 1);
        if (!mem) { // this should NOT occurr!!!
            perror("ivm_load_bin: calloc");
            close(fd);
            return -1;
        }
        *size = spawnsize;
        *dest = (char *)mem;
        allocated = 1; // from here, goto exit_on_err to free the mem in case of error
    } else if (filesize > *size) {
        fprintf(stderr, "ivm_load_bin: Given size (%ld) is not enough for file size (%ld)\n", *size, filesize);
        retval = -8;
        goto exit_on_err;
    }
    char *where = *dest + *offset;
    lseek(fd, 0, SEEK_SET);    // ensure start from the beggining
    long fs = read(fd, where, filesize); // file size
    if (fs == -1) {
        perror("read");
        retval = -9;
        goto exit_on_err;
    }
    #if (VERBOSE>0)
    fprintf(stdout, "ivm_load_bin: Read %ld bytes from '%s'\n", fs, filename);
    #endif    

    close(fd);
    *offset += fs;
    return 0;

exit_on_err:
    if (allocated) {
        free(*dest);
        *dest = NULL;
        *size = 0;
    }
    close(fd);
    return retval;
}

// arguments copied from argv[0]...argv[k]==NULL
// environ vars copied from env[0]...env[j]==NULL
static unsigned int ivm_copy_str_array(char *array[], char *dest, unsigned long *offset)
{
    char *ini = dest + *offset;
    char *p = ini;
    for (; *array; array++, p++) {
        p = stpcpy(p, *array);
    }
    long len = p - ini;
    *offset += len;
    return len;
}


int ivm_spawn(int argc, char *argv[])
{
    jmp_buf spawn_jb;
    volatile int spawn_val;

    // Prepare the memory for the child
    #if (VERBOSE>0)
    fprintf(stdout, "\tPreparing memory\n");
    #endif
    unsigned long offset = 0;
    unsigned long mem_size = 0;
    char *mem = NULL;

    // Chech if filename exist then save the file size
    char *filename = *argv;
    // Read que binary file into the spawn program memory
    if (ivm_load_bin(filename, &mem, &mem_size, &offset) != 0) {
        return -1;
    }

//      In adition to the argument file, this run-time considers also a zone
//      with the same format placed after the argument file area for the environment
//      variables: the first word of the environment area is its size.
//      +---------------------------------------+-------+--------+----- -+----------+
//      |  N=no. of bytes of arguments (1 word) | byte0 | byte 1 | ....  | byte N-1 |
//      +---------------------------------------+-------+--------+----- -+----------+
//      |  M=no. of bytes of environ.  (1 word) | byte0 | byte 1 | ....  | byte M-1 |
//      +---------------------------------------+-------+--------+----- -+----------+
//      The memory must be zero initialized before the load of the program and argument
//      file (at least the next word ater the argument file, in case no enviroment
//      was loaded, this position must be zero (no bytes in enviroment area))

    // Copy args
    // (A zero-separated list of arguments "arg0\0arg1\0arg2\0...")
    void* arg_len = mem + offset;
    offset += 8;
    unsigned long alen = ivm_copy_str_array(argv, mem, &offset);
    *(uint64_t*)arg_len = alen; // argument size in bytes

    // Copy environment -> IVM_SPAWN_RETURN_JB, IVM_SPAWN_RETURN_VAL & IVM_SPAWN_MEMORY_LIMIT
    // (A zero-separated list of environ vars "key0=val0\0key1=val1\0key2=val2\0...")
    char buff[48];
    snprintf(buff, 48, "%#lx", (unsigned long)&spawn_jb);
    setenv("IVM_SPAWN_RETURN_JB", buff, 1);
    snprintf(buff, 48, "%#lx", (unsigned long)&spawn_val);
    setenv("IVM_SPAWN_RETURN_VAL", buff, 1);
    snprintf(buff, 48, "%p", mem + mem_size -1);
    setenv("IVM_SPAWN_MEMORY_LIMIT", buff, 1);
    void* env_len = mem + offset;
    offset += 8;
    unsigned long elen = ivm_copy_str_array(environ, mem, &offset);
    *(uint64_t*)env_len = elen; // environ size in bytes
    unsetenv("IVM_SPAWN_RETURN_JB");
    unsetenv("IVM_SPAWN_RETURN_VAL");
    unsetenv("IVM_SPAWN_MEMORY_LIMIT");

    // Execution
    #if (VERBOSE>0)
    fprintf(stdout, "\tLet's go execute!\n");
    #endif

    typedef void(*spawn_func_t)();
    spawn_func_t spawn_func = (spawn_func_t)mem;
    if (__builtin_setjmp(spawn_jb) == 0) {
        spawn_func();
    }
    int ret = spawn_val;

    #if (VERBOSE>0)
    fprintf(stdout, "\tProgram %s returned: %ld\n", argv[command_idx], ret);
    #endif

    // Free child's memory
    #if (VERBOSE>0)
    fprintf(stdout, "\tFreeing Memory\n");
    #endif
    free(mem);

    return ret;
}

enum OPCODES {
    OPCODE_EXIT         = 0x00,
    OPCODE_NOP          = 0x01,
    OPCODE_JUMP         = 0x02,
    OPCODE_JZFWD        = 0x03,
    OPCODE_JZBACK       = 0x04,
    OPCODE_SETSP        = 0x05,
    OPCODE_GETPC        = 0x06,
    OPCODE_GETSP        = 0x07,
    OPCODE_PUSH0        = 0x08,
    OPCODE_PUSH1        = 0x09,
    OPCODE_PUSH2        = 0x0a,
    OPCODE_PUSH4        = 0x0b,
    OPCODE_PUSH8        = 0x0c,
//0x0d-0x0f
    OPCODE_LOAD1        = 0x10,
    OPCODE_LOAD2        = 0x11,
    OPCODE_LOAD4        = 0x12,
    OPCODE_LOAD8        = 0x13,
    OPCODE_STORE1       = 0x14,
    OPCODE_STORE2       = 0x15,
    OPCODE_STORE4       = 0x16,
    OPCODE_STORE8       = 0x17,
//0x18-0x1f
    OPCODE_ADD          = 0x20,
    OPCODE_MULT         = 0x21,
    OPCODE_DIV          = 0x22,
    OPCODE_REM          = 0x23,
    OPCODE_LT           = 0x24,
//0x25-0x27
    OPCODE_AND          = 0x28,
    OPCODE_OR           = 0x29,
    OPCODE_NOT          = 0x2a,
    OPCODE_XOR          = 0x2b,
    OPCODE_POW          = 0x2c,
//0x2d-0x2f
    OPCODE_CHECK        = 0x30,
//0x31-0xf7
    OPCODE_READCHAR     = 0xf8,
    OPCODE_PUTBYTE      = 0xf9,
    OPCODE_PUTCHAR      = 0xfa,
    OPCODE_ADDSAMPLE    = 0xfb,
    OPCODE_SETPIXEL     = 0xfc,
    OPCODE_NEWFRAME     = 0xfd,
    OPCODE_READPIXEL    = 0xfe,
    OPCODE_READFRAME    = 0xff,
};

static char *OPCODE_STR[256] =
{
    [0x00]="exit"         ,
    [0x01]="nop"          ,
    [0x02]="jump"         ,
    [0x03]="jzfwd"        ,
    [0x04]="jzback"       ,
    [0x05]="setsp"        ,
    [0x06]="getpc"        ,
    [0x07]="getsp"        ,
    [0x08]="push0"        ,
    [0x09]="push1"        ,
    [0x0a]="push2"        ,
    [0x0b]="push4"        ,
    [0x0c]="push8"        ,
//0x0d-0x0f
    [0x10]="load1"        ,
    [0x11]="load2"        ,
    [0x12]="load4"        ,
    [0x13]="load8"        ,
    [0x14]="store1"       ,
    [0x15]="store2"       ,
    [0x16]="store4"       ,
    [0x17]="store8"       ,
//0x18-0x1f
    [0x20]="add"          ,
    [0x21]="mult"         ,
    [0x22]="div"          ,
    [0x23]="rem"          ,
    [0x24]="lt"           ,
//0x25-0x27
    [0x28]="and"          ,
    [0x29]="or"           ,
    [0x2a]="not"          ,
    [0x2b]="xor"          ,
    [0x2c]="pow"          ,
//0x2d-0x2f
    [0x30]="check"        ,
//0x31-0xf7
    [0xf8]="readchar"     ,
    [0xf9]="putbyte"      ,
    [0xfa]="putchar"      ,
    [0xfb]="addsample"    ,
    [0xfc]="setpixel"     ,
    [0xfd]="newframe"     ,
    [0xfe]="readpixel"    ,
    [0xff]="readframe"
};

#define MAX_INSN  24
#define MIN_INSN  6
#define MIN_PUSH  2
#define MIN_GETPC 2
#define MIN_ADD   2

// Due to the lack of a magic number or signature at the beginning of the ivm64
// binaries, this function determines if a potential ivm64 binary has the right
// format. For that purpose, checking at most MAX_INSN instructions, find the
// first basic block of instructions, and consider the binary to be a valid
// ivm64 program if this basic block:
//   - have at least MIN_INSN instructions 
//   - contain at least MIN_PUSH push instructions
//   - contain at least MIN_GETPC getpc instructions
//   - contain at least MIN_ADD add instructions
//   - contain only valid instruction operation codes
// If quick is set, the function quits immediatly the valid condition is meet,
// otherwise continues until MAX_INSN instructions searching for invalid
// opcodes
static int ivm64_valid_bin_fd(int fd, int quick, int verbose);

int ivm64_valid_bin(char *filename, int quick, int verbose)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        if (verbose) fprintf(stderr, "Command not found: '%s'\n", filename);
        return -1;
    }
    int retval = ivm64_valid_bin_fd(fd, quick, verbose);
    close(fd);
    return retval;
}

static int ivm64_valid_bin_fd(int fd, int quick, int verbose)
{
    int valid = 0;
    long count = 0;
    long count_getpc = 0, count_push = 0, count_add = 0;

    lseek (fd, 0, SEEK_SET);    // ensure start from the beggining
    for (long i=0; i < MAX_INSN; i++){
        unsigned char c;
        long r = read(fd, &c, 1);

        if (r <= 0) break;

        if ((c >= 0x0d && c <= 0x0f) ||
            (c >= 0x18 && c <= 0x1f) ||
            (c >= 0x25 && c <= 0x27) ||
            (c >= 0x2d && c <= 0x2f) ||
            (c >= 0x31 && c <= 0xf7)) {
            valid = 0;
            if (verbose) fprintf(stderr, "No valid opcode: %#02x\n", (unsigned int)c);
            break;
        } else {
            if (verbose) fprintf(stderr, "%#02x -> %s\n", (unsigned int)c, OPCODE_STR[c]);
            count++;
        }

        if (c==OPCODE_JUMP || c==OPCODE_JZFWD || c==OPCODE_JZBACK || c==OPCODE_EXIT || c==OPCODE_CHECK) {
            // basic block ends
            break;
        }
        else if (c==OPCODE_PUSH1) {
            lseek(fd, 1, SEEK_CUR);
        }
        else if (c==OPCODE_PUSH2) {
            lseek(fd, 2, SEEK_CUR);
        }
        else if (c==OPCODE_PUSH4) {
            lseek(fd, 4, SEEK_CUR);
        }
        else if (c==OPCODE_PUSH8) {
            lseek(fd, 8, SEEK_CUR);
        }

        if (!valid) {
            if ((c==OPCODE_PUSH1) || (c==OPCODE_PUSH2) || (c==OPCODE_PUSH4) || (c==OPCODE_PUSH8)){
                count_push++; 
            }
            if (c==OPCODE_GETPC){
                count_getpc++; 
            }
            if (c==OPCODE_ADD){
                count_add++; 
            }

            valid = (count >= MIN_INSN)
                && (count_push >= MIN_PUSH)
                && (count_getpc >= MIN_GETPC)
                && (count_add >= MIN_ADD);

            if (verbose && valid)
                fprintf(stderr, "-> VALID (@%ld insns)\n", count);
        }

        if (quick && valid) break;
    }

    return valid;
}

#else
// Not an ivm64 architecture
#include <stdio.h>
int ivm_spawn(int argc, char *argv[])
{
    fprintf(stderr, "ivm_spawn only available for the IVM64 architecture\n");
    return 0;
}
#endif /* __ivm64__ */
