# Installation directories.
PREFIX ?= ${DESTDIR}/usr
INCLUDEDIR ?= $(PREFIX)/include
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= ${PREFIX}/lib
MANDIR ?= $(PREFIX)/share/man

CFLAGS ?= -Werror -Wall -W
override CFLAGS += -I$(INCLUDEDIR)
LDLIBS = -lsepol -lselinux -L$(LIBDIR)

all: semodule_expand

semodule_expand:  semodule_expand.o 

install: all
	-mkdir -p $(BINDIR)
	install -m 755 semodule_expand $(BINDIR)
	test -d $(MANDIR)/man8 || install -m 755 -d $(MANDIR)/man8
	install -m 644 semodule_expand.8 $(MANDIR)/man8/

relabel:

clean:
	-rm -f semodule_expand *.o 
