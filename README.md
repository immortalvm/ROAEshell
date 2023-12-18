# ROAE shell

ROAEshell, a shell to interface with the Read-Only Access Engine (ROAE).

Developed as a part of the Immortal Database Access (iDA) EUROSTARS project.

## Build
Type ```build.sh``` for help:

* ```./build.sh -c linux```: compile for linux

* ```./build.sh -c ivm```: (cross)compile for ivm64

If used flag ```-c```, a cleaning is made prior to building.

## Cross compiling for ivm64

When cross-compiling for the ivm64 architecture you need to
prepare the environment:

* Add to the environment variable PATH, these 3 elements:
  1. the path to the ivm64-gcc compiler and assembler (```ivm64-gcc```, ```ivm64-g++```, ```ivm64-as```) (generally ```<gcc-12.2.0 install dir>/bin```)
  1. the path to your favourite ivm emulator (e.g. ```ivm64-emu```)
  1. the path to the ivm in-ram filesystem generator (```ivm64-fsgen```, distributed together with the compiler as of release 3.2)
  <br/><br/>
* Optionally, you can define the environment IVM_EMU variable with your favourite
ivm emulator (e.g. export IVM_EMU="ivm64-emu" for the fast emulator, or export IVM_EMU="ivm run" to use the ivm implementation), and the IVM_AS variable with your favourite assembler (e.g. export IVM_AS=ivm64-as for the assembler integrated in the compiler or IVM_AS="ivm as" to use the ivm implementation.)

## Dependences

This project depends on several libraries that have been included in directories ```thirparty``` and ```ida```.

The ```thirparty``` directory contains some external projects that have adapted to be compiled for the ivm64 architecture:

  * [sqlite3](./thirdparty/sqlite3/README.md) (version adapted for IVM, which provide ```libsqlite3.a```)
  * [tinyxml2](./thirdparty/tinyxml2/readme.md) (which provides ```libtinyxml2.a```)
  * [zlib](./thirdparty/zlib/README) (which provides ```libz.a```, ```libminizip.a```)

The ```ida``` directory contains the sources of these libraries, that implement the core of the roaeshell functionality:

  * [siard2sql](./ida/siard2sql/README.md) (which provides  ```libsiar2sqlite.a```, a library to convert SIARD format to sqlite3 SQL)
  * [roaeparser](./ida/roaeparser/README.md) (which provides ```libroae.a```, a library to parse ROAE files)


## References 

* IVM C/C++ compiler and assembler (```ivm64-gcc, ivm64-g++, ivm64-as```): https://github.com/immortalvm/ivm-compiler
* Yet another (FAST) IVM emulator (```ivm64-emu```): https://github.com/immortalvm/yet-another-fast-ivm-emulator
* IVM in-ram filesystem (```ivmfs-gen.sh```): https://github.com/immortalvm/ivm-fs
* IVM F# implementation (ivm: assembler+emulator): https://github.com/immortalvm/ivm-implementations


