INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS= -D _GNU_SOURCE -Wall
CFLAGS= -g -O3 $(INCLUDE_DIRS) $(CDEFS)
LIBS= -lcurl
CPPLIBS=

HFILES=
CFILES=
CPPFILES= curl_transfer.c

SRCS= ${HFILES} ${CFILES}
CPPOBJS= ${CPPFILES:.c=.o}

all:	curl_transfer

clean:
	-rm -f *.o *.d
	-rm -f curl_transfer

distclean:
	-rm -f *.o *.d

curl_transfer: curl_transfer.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(CPPLIBS) $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<
