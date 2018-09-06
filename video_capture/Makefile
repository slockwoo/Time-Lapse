INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS= -D _GNU_SOURCE -Wall
CFLAGS= -g -O0 $(INCLUDE_DIRS) $(CDEFS)
LIBS=
CPPLIBS=

HFILES=
CFILES=
CPPFILES= video_capture.c

SRCS= ${HFILES} ${CFILES}
CPPOBJS= ${CPPFILES:.c=.o}

all:	video_capture

clean:
	-rm -f *.o *.d
	-rm -f v4l2_to_memory

distclean:
	-rm -f *.o *.d

video_capture: video_capture.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(CPPLIBS) $(LIBS)
	mkdir -p ./frames/

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<
