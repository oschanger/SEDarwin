include ../../../Makeconfig

POLICYCOREUTILS=../../policycoreutils
VPATH = $(POLICYCOREUTILS)/newrole

PROG= newrole
MAN1= $(POLICYCOREUTILS)/newrole/newrole.1

OBJS= newrole.o

CFLAGS+= -DPACKAGE="\"policycoreutils\"" -DVERSION="\"1.30\""
CFLAGS+= -DUSE_PAM
CFLAGS+= -I$(EXPORT_HDRS)/bsd -idirafter $(EXPORT_HDRS)/osfmk
CFLAGS+= -I$(SOURCE_ROOT)/sedarwin/libselinux/include
LDADD+= -L$(SOURCE_ROOT)/sedarwin/libselinux/src 
LDADD+= -L$(SOURCE_ROOT)/sedarwin/libsepol/src
LDADD+= -L$(SOURCE_ROOT)/sedarwin/libsecompat
LDADD+= -lselinux -lsepol -lsecompat -lsecompat $(LIBMAC) -lpam


all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDADD)

install: $(PROG) $(MAN1)
	install -m 555 $(PROG) $(DESTDIR)/usr/bin
	install -m 444 $(MAN1) $(DESTDIR)/usr/share/man/man1

clean:
	rm -f $(OBJS) $(PROG)
