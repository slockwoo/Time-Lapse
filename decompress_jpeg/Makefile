INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS= -D _GNU_SOURCE -Wall
CFLAGS= -g -O3 $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lrt -lz -lpthread -lcurl -ljpeg -lturbojpeg
CPPLIBS=

HFILES=
CFILES=
CPPFILES= decompress_jpeg.c

SRCS= ${HFILES} ${CFILES}
CPPOBJS= ${CPPFILES:.c=.o}

all:	decompress_jpeg

clean:
	-rm -f *.o *.d
	-rm -f decompress_jpeg

distclean:
	-rm -f *.o *.d

decompress_jpeg: decompress_jpeg.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(CPPLIBS) $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<
