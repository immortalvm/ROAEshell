ifeq ($(HOST),ivm64)
    CC=ivm64-gcc
    CXX=ivm64-g++
    IVM_AS:=$(or $(IVM_AS), ivm64-as --noopt)
    IVM_EMU:=$(or $(IVM_EMU), ivm64-emu)
    BUILDDIR=./run-$(HOST)
    MAKEMINIZIP=Makefile-$(HOST)
    IVMLDFLAGS=-Xlinker -mbin
    XMLCONFOPT=--without-pic
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

CDEFFLAGS=-O2
CXXDEFFLAGS=-O2
CFLAGS := $(if $(CFLAGS), $(CFLAGS), $(CDEFFLAGS))
CXXFLAGS := $(if $(CXXFLAGS), $(CXXFLAGS), $(CXXDEFFLAGS))

LIBDIR=$(BUILDDIR)/lib

INCDIR=$(BUILDDIR)/include
INC_ROAE=-I. -I $(INCDIR)

# All this directory is included in the IVM filesystem
DATADIR=data

.PHONY: clean idaroae libroae

idaroae: libroae $(BUILDDIR)/ivmfs.c main.c roae.h
	cp -ar roae.h $(INCDIR)/
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@  $(BUILDDIR)/ivmfs.c  main.c $(INC_ROAE) -L $(LIBDIR) -lroae -lm -lstdc++
	cp -ar $(DATADIR) $(BUILDDIR)/
	@echo; echo; echo "Run as: (cd $(BUILDDIR); ./$@ $(ROAEEXAMPLE))"

libroae: $(LIBDIR)/libroae.a

$(LIBDIR)/libroae.a: $(BUILDDIR)/libroae.o
	ar r  $@ $<

$(BUILDDIR)/libroae.o: libroae.cpp
	mkdir -p $(LIBDIR) || exit -1
	mkdir -p $(BUILDDIR) || exit -1
	mkdir -p $(INCDIR) || exit -1
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/ivmfs.c:
	mkdir -p $(BUILDDIR) || exit -1
	$(IVM_FSGEN) `find $(DATADIR)` > $(BUILDDIR)/ivmfs.c

clean:
	rm -rf $(BUILDDIR)
	@#find . \( -name "*.b" -o -name "*.sym" -o -name "lib*.a" -o -name "*.b" -o -name "*.o" \) -exec rm -f {} \;
	@#rm -rf content header
