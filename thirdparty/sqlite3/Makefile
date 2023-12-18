ifeq ($(HOST),ivm64)
    CC=ivm64-gcc
    CXX=ivm64-g++
    IVM_AS:=$(or $(IVM_AS), ivm64-as --noopt)
    IVM_EMU:=$(or $(IVM_EMU), ivm64-emu)
    BUILDDIR=./run-$(HOST)
    MAKEMINIZIP=Makefile-$(HOST)
    IVMLDFLAGS=-Xlinker -mbin
    XMLCONFOPT=--without-pic
    #IVM_FSGEN=ivmfs-gen.sh
    IVM_FSGEN:=$(if $(IVM_FSGEN),$(IVM_FSGEN),ivm64-fsgen)
    IVMFS=$(BUILDDIR)/ivmfs.c
else
    HOST=
    CC=gcc
    CXX=g++
    IVM_AS=
    IVM_EMU=
    BUILDDIR=./run-linux
    MAKEMINIZIP=Makefile
    IVMLDFLAGS=
    XMLCONFOPT=
    IVM_FSGEN=true
    IVMFS=
endif

CFLAGS := $(if $(CFLAGS), $(CFLAGS), -DSQLITE_MUTEX_NOOP -O2 -static)
CXXFLAGS := $(if $(CXXFLAGS), $(CXXFLAGS), -O2 -static)

# Some debugging options
#CFLAGS += -DSQLITE_DEBUG
# debugging ivm filesystem
#CFLAGS += -DIVMFS_DEBUG
# debugging sqlite ivm version
#CFLAGS += -DIVM_SQLITE_DEBUG
#CFLAGS += -DSQLITE_FORCE_OS_TRACE

LIBDIR=$(BUILDDIR)/lib

INCDIR=$(BUILDDIR)/include
INC_SQLITE=-I. -I $(INCDIR)

#IVMFS_FILES=Database.db stdin
IVMFS_FILES=db/*

.PHONY: clean sqlite

sqlite: sqlite_app sqlite_lib 

sqlite_app: $(BUILDDIR)/sqlite3

sqlite_lib: $(LIBDIR)/libsqlite3.a

# Standard sqlite3 shell
$(BUILDDIR)/sqlite3: $(BUILDDIR)/ivm-fs.o $(BUILDDIR)/sqlite3.o $(BUILDDIR)/shell.o 
	@mkdir -p $(BUILDDIR)
	$(CC) $(LDFLAGS) $^ -o $@ -I.
	@mv ivm-fs.c $(BUILDDIR)
	@echo "Make:"
	@echo "  Generated standard sqlite shell '$@'"
	@echo "  Generated libsqlite3.a for IDA: $(LIBDIR)/libsqlite3.a"
	@echo

# IDA/IVM sqlite shell library
$(LIBDIR)/libsqlite3.a: $(BUILDDIR)/ida_sqlite3.o $(BUILDDIR)/sqlite3.o
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(LIBDIR)
	ar r $@ $^ ; ar t $@

$(BUILDDIR)/sqlite3-shell-nomain.o: shell.c sqlite3.h sqlite3ext.h
	@mkdir -p $(BUILDDIR)
	$(CC) -Dmain=__no_main__ -I. $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: %.c sqlite3.h sqlite3ext.h
	@mkdir -p $(BUILDDIR)
	$(CC) -I. $(CFLAGS) -c -o $@ $<

ivm-fs.c: $(IVMFS_FILES)
	@mkdir -p $(BUILDDIR)
	$(IVM_FSGEN) $(IVMFS_FILES) > ivm-fs.c

clean:
	rm -f *.o  a.out a.b a.sym a.out.b a.out.sym libsqlite3.a ./sqlite3 ivm-fs.c
	rm -rf $(BUILDDIR)

	@#find . \( -name "*.b" -o -name "*.sym" -o -name "lib*.a" -o -name "*.b" -o -name "*.o" \) -exec rm -f {} \;
	@#rm -rf content header
