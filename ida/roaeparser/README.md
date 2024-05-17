# ROAE parser

A library to parse simple ROAE files.

## Build
Type ```build.sh``` for help:

* ```./build.sh -c linux```: compile for linux

* ```./build.sh -c ivm```: (cross)compile for ivm64

If used flag ```-c```, a cleaning is made prior to building.

After building, the library libroae.a is placed in ```run-*/lib/libroa.a```

## Cross compiling for ivm64

When cross-compiling for the ivm64 architecture you need to
prepare the environment:

* Add to the environment variable PATH, these 3 things:
  1. the path to the ivm64-gcc compiler and assembler (```ivm64-gcc```, ```ivm64-g++```, ```ivm64-as```) (generally ```<gcc-12.2.0 install dir>/bin```)
  1. the path to your favourite ivm emulator (e.g. ```ivm64-emu```)
  1. the path to the ivm in-ram filesystem generator script (```ivm64-fsgen```, distributed together with the compiler since version 3.2)
  <br/><br/>
* Optionally, you can define the environment IVM_EMU variable with your favourite
ivm emulator (e.g. export IVM_EMU="ivm64-emu" for the fast emulator, or export IVM_EMU="ivm run" to use the ivm implementation), and the IVM_AS variable with your favourite assembler (e.g. export IVM_AS=ivm64-as for the assembler integrated in the compiler or IVM_AS="ivm as" to use the ivm implementation.)

## Using the library
This is the public C API provided by the roaeparser library:

```c
// Load a ROAE file and return the number of commands found
long IDA_ROAE_load(char *filename);
    
// Delete the current roae command list
void IDA_ROAE_clear();
    
// Print the list of commands
void IDA_ROAE_print_commands();
    
// Get the number of commands
long IDA_ROAE_count();
    
// Print the nc-th command
void IDA_ROAE_print_command(long nc);

// Get the title of the nc-th command; in case of error, return NULL
// Note that the returned string must be deallocated
char* IDA_ROAE_get_command_title(long nc);

// Return true if the title of the nc-th command match the regexp string r
int IDA_ROAE_command_title_match(long nc, char* r);

// Get the number of arguments of the nc-th command; in case of error, return -1
long IDA_ROAE_get_command_nargs(long nc);

// Get na-th argument's name of the nc-th command; in case of error, return NULL
// Note that the returned string must be deallocated
char* IDA_ROAE_get_command_arg_name(long nc, long na);

// Get na-th argument's comments of the nc-th command; in case of error, return NULL
// Note that the returned string must be deallocated
char* IDA_ROAE_get_command_arg_comment(long nc, long na);

// Print the lis of commands whose title
// match the regexp s
void IDA_ROAE_search(char *re);
    
// Eval the nc-th command with a list of nparams parameters 
// The list of parameter values is in the argv format (last element must be NULL).
// If values==NULL, the SQL prepared statement is returned instead.
// If buff==NULL, a dynamic array of chars is allocated with the size of
// the evaluated command in this case, the programmer must free it after its use
// Return NULL if error.
char* IDA_ROAE_eval_command(long nc, char *buff, long buffsize, char *values[]);

// Given a list of values in argv format for parameters, return a list of
// strings (in argv format) with the values to be replaced in a prepared
// sql statement, in the correct order and repeated if necessary
// Return NULL if something is wrong
// This function allocates the returned structure that needs to be freed
char** IDA_ROAE_command_bind_list(long nc, char *values[]);

// Given a list of values to be bind (in argv format) generate the
// sequence of sqlite commands to bind those values
// This function allocates the returned string that needs to be freed
char* IDA_ROAE_command_bind_list_to_sqlite(char *bind_list[]);
 ```
    
## References 

* IVM C/C++ compiler and assembler (```ivm64-gcc, ivm64-g++, ivm64-as```): https://github.com/immortalvm/ivm-compiler
* Yet another (FAST) IVM emulator (```ivm64-emu```): https://github.com/immortalvm/yet-another-fast-ivm-emulator
* IVM in-ram filesystem (```ivm64-fsgen```): https://github.com/immortalvm/ivm-fs
* IVM F# implementation (ivm: assembler+emulator): https://github.com/immortalvm/ivm-implementations


