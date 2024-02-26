IDABUILDDIRPREFIX:=$(if $(IDABUILDDIRPREFIX),$(IDABUILDDIRPREFIX),.)
ifeq ($(HOST),ivm64)
    CC=ivm64-gcc
    CXX=ivm64-g++
    IVM_AS:=$(or $(IVM_AS), ivm64-as --noopt)
    IVM_EMU:=$(or $(IVM_EMU), ivm64-emu)
    BUILDDIR:=$(IDABUILDDIRPREFIX)/run-$(HOST)
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
    BUILDDIR=$(IDABUILDDIRPREFIX)/run-linux
    MAKEMINIZIP=Makefile
    IVMLDFLAGS=
    XMLCONFOPT=
    IVM_FSGEN=true
    IVMFS=
endif
#RSYNC=rsync
RSYNC=true

CDEFFLAGS=
CXXDEFFLAGS=
CFLAGS := $(if $(CFLAGS), $(CFLAGS), $(CDEFFLAGS))
CXXFLAGS := $(if $(CXXFLAGS), $(CXXFLAGS), $(CXXDEFFLAGS))

LIBDIR=$(BUILDDIR)/lib

INCDIR=$(BUILDDIR)/include
INC=-I. -I $(INCDIR)

# External IDA project dirs
IDAEXTDIR=..
IDAEXTROAEDIR=$(IDAEXTDIR)/roaeparser
IDAEXTSIARD2SQLDIR=$(IDAEXTDIR)/siard2sql
IDAEXTSQLITEDIR=$(IDAEXTDIR)/sqlite-ivm

# Third party dirs included in this project
THIRDPARTYDIR=$(realpath ./thirdparty)
THIRDPARTYSQLITE3DIR=$(THIRDPARTYDIR)/sqlite3
THIRDPARTYLIBZDIR=$(THIRDPARTYDIR)/zlib
THIRDPARTYTINYXML2DIR=$(THIRDPARTYDIR)/tinyxml2

# IDA libraries included in this project
IDAROAEPARSERDIR=./ida/roaeparser
IDASIARD2SQLDIR=./ida/siard2sql

# All this directory is included in the IVM filesystem
DATADIR=db

# IDA project static libraries
ALIBS=$(LIBDIR)/libroae.a $(LIBDIR)/libsiard2sql.a  $(LIBDIR)/libminizip.a $(LIBDIR)/libz.a $(LIBDIR)/libtinyxml2.a $(LIBDIR)/libsqlite3.a

# Sources required by the shell (used by internal commands  ...)
REQSRC= $(THIRDPARTYDIR)/utils/libfind.c $(THIRDPARTYDIR)/utils/libgrep.c $(THIRDPARTYDIR)/utils/regexp.c

.PHONY: roaeshell binaries clean

roaeshell: $(ALIBS) $(BUILDDIR)/ivmfs.c libspawn.c $(REQSRC) shell.c
	$(CC) $(CFLAGS) -o $(BUILDDIR)/$@  libspawn.c $(BUILDDIR)/ivmfs.c $(REQSRC) shell.c $(INC) -L $(LIBDIR) -lsiard2sql -lroae -lsqlite3 -lstdc++ -lminizip -lz -ltinyxml2 -lm
	cp -r "$(DATADIR)" $(BUILDDIR)/
	mkdir -p  $(BUILDDIR)/bin/ ; mv -f bin/* $(BUILDDIR)/bin/ ; rmdir -v bin
	@echo; echo; test -f "$(BUILDDIR)/$@"  && echo "Run as: (cd $(BUILDDIR); ./$@)"

$(LIBDIR)/libtinyxml2.a:
	-$(RSYNC) -Pa --delete $(IDAEXTSIARD2SQLDIR)/thirdparty/tinyxml2/ $(THIRDPARTYTINYXML2DIR)/
	@mkdir -p $(BUILDDIR) || exit -1
	@mkdir -p $(LIBDIR)   || exit -1
	$(CXX) $(CXXFLAGS) -I$(THIRDPARTYTINYXML2DIR) -c $(THIRDPARTYTINYXML2DIR)/tinyxml2.cpp -o $(BUILDDIR)/tinyxml2.o
	ar r $@ $(BUILDDIR)/tinyxml2.o

$(LIBDIR)/libz.a:
	-$(RSYNC) -Pa --delete $(IDAEXTSIARD2SQLDIR)/thirdparty/zlib/ $(THIRDPARTYLIBZDIR)/
	@mkdir -p $(LIBDIR) || exit -1
	+cd $(THIRDPARTYLIBZDIR) && rm -rf build; mkdir -p build; cd build; CFLAGS="$(CFLAGS)" CC=$(CC) ../configure --static; CFLAGS="$(CFLAGS)" CC=$(CC) make
	@cp -v `find $(THIRDPARTYLIBZDIR)/build -name libz.a` "$@"

$(LIBDIR)/libminizip.a: $(LIBDIR)/libz.a
	@mkdir -p $(LIBDIR) || exit -1
	cd $(THIRDPARTYLIBZDIR)/contrib/minizip; make clean; CFLAGS="$(CFLAGS) -Dmain=_IDA_miniunz_main_" CC=$(CC) make libminizip.a
	@cp -v `find $(THIRDPARTYLIBZDIR)/contrib/minizip -name libminizip.a` "$@"

$(LIBDIR)/libsqlite3.a:
	-$(RSYNC) -Pa --delete $(IDAEXTSQLITEDIR)/*.[ch] $(IDAEXTSQLITEDIR)/Makefile* $(IDAEXTSQLITEDIR)/build.sh $(IDAEXTSQLITEDIR)/README* $(THIRDPARTYSQLITE3DIR)/
	@mkdir -p $(LIBDIR) || exit -1
	+cd $(THIRDPARTYSQLITE3DIR) && make sqlite_lib
	@cp -v `find $(THIRDPARTYSQLITE3DIR) -name libsqlite3.a` "$@"

$(LIBDIR)/libroae.a:
	-$(RSYNC) -Pa --delete $(IDAEXTROAEDIR)/*.[ch]*  $(IDAEXTROAEDIR)/README*  $(IDAEXTROAEDIR)/build.sh  $(IDAEXTROAEDIR)/Makefile $(IDAROAEPARSERDIR)/
	@mkdir -p $(BUILDDIR) || exit -1
	@mkdir -p $(LIBDIR)   || exit -1
	+cd $(IDAROAEPARSERDIR) && ./build.sh -c && HOST=$(HOST) make libroae
	@cp -v `find $(IDAROAEPARSERDIR) -name libroae.a` "$@"

$(LIBDIR)/libsiard2sql.a:
	-$(RSYNC) -Pa --delete $(IDAEXTSIARD2SQLDIR)/*.[ch]*  $(IDAEXTSIARD2SQLDIR)/README*  $(IDAEXTSIARD2SQLDIR)/build.sh  $(IDAEXTSIARD2SQLDIR)/Makefile $(IDASIARD2SQLDIR)/
	@mkdir -p $(BUILDDIR) || exit -1
	+export IDABUILDDIRPREFIX=. ; cd $(IDASIARD2SQLDIR) && make cleanbuild && HOST=$(HOST) THIRDPARTYDIR=$(THIRDPARTYDIR) make libsiard2sql
	@mkdir -p $(LIBDIR)   || exit -1
	@cp -v `find $(IDASIARD2SQLDIR) -name libsiard2sql.a` "$@"

#$(LIBDIR)/%.a: $(IDAEXTSIARD2SQLDIR)/$(LIBDIR)/%.a
#	@mkdir -p $(LIBDIR) || exit -1
#	@cp -v "$^" "$@"

#The IVM filesystem
$(BUILDDIR)/ivmfs.c: binaries
	@mkdir -p $(BUILDDIR) || exit -1
	$(IVM_FSGEN) `find $(DATADIR)` `find bin` > $(BUILDDIR)/ivmfs.c

# Spwaneable binaries
binaries: roaeshell.b find.b grep.b

roaeshell.b: $(ALIBS) $(BUILDDIR)/ivmfs-empty.c libspawn.c $(REQSRC) shell.c
	@mkdir -p bin || exit -1
	$(CC) $(CFLAGS) -o bin/$@.ivm  libspawn.c $(BUILDDIR)/ivmfs-empty.c $(REQSRC) shell.c $(INC) -L $(LIBDIR) -lsiard2sql -lroae -lsqlite3 -lstdc++ -lminizip -lz -ltinyxml2 -lm
	if test "$(CC)" = "ivm64-gcc" ; then \
       $(IVM_AS) bin/$@.ivm --bin bin/$@ --sym /dev/null; rm -f bin/$@.ivm; chmod +rx "bin/$@"; \
    else \
       mv bin/$@.ivm  bin/$@; \
    fi

%.b: %.c $(BUILDDIR)/ivmfs-empty.c
	@mkdir -p bin || exit -1
	$(CC) $(CFLAGS) -I $(THIRDPARTYDIR)/utils/ -o "bin/$@.ivm" libspawn.c $(BUILDDIR)/ivmfs-empty.c "$<"
	if test "$(CC)" = "ivm64-gcc" ; then \
       $(IVM_AS) "bin/$@.ivm" --bin "bin/$@" --sym /dev/null; rm -f "bin/$@.ivm"; chmod +rx "bin/$@";  \
    else \
       mv "bin/$@.ivm" "bin/$@";  \
    fi

#An empty filessystem for spawneable binaries
$(BUILDDIR)/ivmfs-empty.c:
	@mkdir -p $(BUILDDIR) || exit -1
	$(IVM_FSGEN) > $@

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(IDASIARD2SQLDIR)/run-linux $(IDASIARD2SQLDIR)/run-ivm64
	cd $(IDAROAEPARSERDIR) && ./build.sh -c
	cd $(THIRDPARTYSQLITE3DIR) && ./build.sh -c
	rm -rf $(THIRDPARTYLIBZDIR)/build $(THIRDPARTYLIBZDIR)/lib*.a $(THIRDPARTYLIBZDIR)/contrib/minizip/lib*.a
	cd  $(THIRDPARTYLIBZDIR)/contrib/minizip/ && make clean
	@#find . \( -name "*.b" -o -name "*.sym" -o -name "lib*.a" -o -name "*.b" -o -name "*.o" \) -exec rm -f {} \;
	@#rm -rf content header
