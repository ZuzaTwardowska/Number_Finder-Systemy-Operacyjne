CC=gcc
CFLAGS=-std=gnu99 -Wall -ggdb
LDLIBS=-lpthread -lm
numf:
	${CC} ${CFLAGS} numf.c ${LDLIBS} -o numf
clean:
	rm numf