include ../../../Makeconfig

CFLAGS+= -I$(EXPORT_HDRS)/bsd -idirafter $(EXPORT_HDRS)/osfmk -I../.. -I../include -I../include/selinux -I../../libsepol/include

# Installation directories.
PREFIX = $(DESTDIR)/usr
LIBDIR = $(PREFIX)/lib
SHLIBDIR = $(DESTDIR)/lib

LIBVERSION = 1

SRCS=   booleans.c canonicalize_context.c \
                compute_av.c \
                compute_relabel.c compute_user.c context.c \
                disable.c enabled.c fgetfilecon.c freecon.c freeconary.c \
                fsetfilecon.c get_context_list.c get_default_type.c getcon.c \
                getenforce.c getexeccon.c getfilecon.c get_file_contexts.c \
                getpeercon.c getpidcon.c init.c is_customizable_type.c \
                lgetfilecon.c load_policy.c lsetfilecon.c matchmediacon.c \
                matchpathcon.c policyvers.c query_user_context.c \
                sedarwin_config.c setcon.c setenforce.c \
                setfilecon.c seusers.c trans.c load_migscs.c

# Files left out, waiting for supporting functionality in the sebsd kernel
# module
#               check_context.c compute_create.c \
#               compute_member.c getfscreatecon.c \
#               getprevcon.c setfscreatecon.c \
#               checkAccess.c rpm.c setexeccon.c 
#

LIBA=libselinux.a 
#TARGET=libselinux.so
#LIBSO=$(TARGET).$(LIBVERSION)
OBJS= $(patsubst %.c,%.o,$(SRCS))
LOBJS= $(patsubst %.c,%.lo,$(SRCS))

all: $(LIBA)

$(LIBA):  $(OBJS)
	libtool -static -c -o $@ $^
#	$(AR) rcs $@ $^
#	ranlib $@

$(LIBSO): $(LOBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ -Wl,-soname,$(LIBSO) 
	ln -sf $@ $(TARGET) 

%.o:  %.c policy.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.lo:  %.c policy.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

install: all
	test -d $(LIBDIR) || install -m 755 -d $(LIBDIR)
	install -c -m 644 $(LIBA) $(LIBDIR)
#	ranlib $(LIBDIR)/$(LIBA)
#	test -d $(SHLIBDIR) || install -m 755 -d $(SHLIBDIR)
#	install -m 755 $(LIBSO) $(SHLIBDIR)
#	cd $(LIBDIR) && ln -sf ../../`basename $(SHLIBDIR)`/$(LIBSO) $(TARGET)

clean: 
	rm -f $(OBJS) $(LOBJS) $(LIBA) $(LIBSO) $(TARGET) 

