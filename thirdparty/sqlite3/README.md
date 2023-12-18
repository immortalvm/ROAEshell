# SQLITE3 IDA API 

Immortal Database Access (iDA) EUROSTARS project:
A mini-API to launch sqlite3 shell commands. 

This project is based on SQLITE3 version 3.43 (_sqlite-amalgamation-3430000_ package). 

## Build
Type ```build.sh``` for help:

* ```./build.sh -c linux```: compile for linux

* ```./build.sh -c ivm```: (cross)compile for ivm64

If used flag ```-c```, a cleaning is made prior to building.

## Cross compiling for ivm64

When cross-compiling for the ivm64 architecture you need to
prepare the environment:

* Add to the environment variable PATH, these 3 things:
  1. the path to the ivm64-gcc compiler and assembler (```ivm64-gcc```, ```ivm64-g++```, ```ivm64-as```) (generally ```<gcc-12.2.0 install dir>/bin```)
  1. the path to your favourite ivm emulator (e.g. ```ivm64-emu```)
  1. the path to the ivm in-ram filesystem generator (```ivm64-fsgen```, distributed together with the compiler as of version 3.2)
  <br/><br/>
* Optionally, you can define the environment IVM_EMU variable with your favourite
ivm emulator (e.g. export IVM_EMU="ivm64-emu" for the fast emulator, or export IVM_EMU="ivm run" to use the ivm implementation), and the IVM_AS variable with your favourite assembler (e.g. export IVM_AS=ivm64-as for the assembler integrated in the compiler or IVM_AS="ivm as" to use the ivm implementation.)

## Using the library
You can interact with the sqlite3 shell by launching SQL statements or sqlite shell commands
(starting with dot), after initializing sqlite structures:

```c
  // Init sqlite shell
  void IDA_SQLITE_shell_init();
  
  // Run an internal command or SQL command depending on whether it starts with "."
  int IDA_SQLITE_run(char *cmd);
```

## References 

* IVM C/C++ compiler and assembler (```ivm64-gcc, ivm64-g++, ivm64-as```): https://github.com/immortalvm/ivm-compiler
* Yet another (FAST) IVM emulator (```ivm64-emu```): https://github.com/immortalvm/yet-another-fast-ivm-emulator
* IVM in-ram filesystem (```ivmfs-gen.sh```): https://github.com/immortalvm/ivm-fs
* IVM F# implementation (ivm: assembler+emulator): https://github.com/immortalvm/ivm-implementations
* SQLITE3: https://www.sqlite.org/download.html


