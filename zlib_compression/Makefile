INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS= -D _GNU_SOURCE -Wall
CFLAGS= -g -O0 $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lz
CPPLIBS=

HFILES=
CFILES=
CPPFILES= zlib_compression.c

SRCS= ${HFILES} ${CFILES}
CPPOBJS= ${CPPFILES:.c=.o}

all:	zlib_compression

clean:
	-rm -f *.o *.d
	-rm -f zlib_compression

distclean:
	-rm -f *.o *.d

zlib_compression: zlib_compression.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(CPPLIBS) $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<
