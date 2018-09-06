INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS= -D _GNU_SOURCE -Wall
CFLAGS= -g -O3 $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lturbojpeg
CPPLIBS=

HFILES=
CFILES=
CPPFILES= decode_jpeg.c

SRCS= ${HFILES} ${CFILES}
CPPOBJS= ${CPPFILES:.c=.o}

all:	decode_jpeg

clean:
	-rm -f *.o *.d
	-rm -f decode_jpeg

distclean:
	-rm -f *.o *.d

decode_jpeg: decode_jpeg.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(CPPLIBS) $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<
