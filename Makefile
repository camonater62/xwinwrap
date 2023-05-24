CC = gcc
CFLAGS= -O2 -Wall
INCLUDE = -L /usr/lib/x86_64-linux-gnu
LIBS = -lX11 -lXext -lXrender

all:
	${CC} xwinwrap.c ${CFLAGS} ${INCLUDE} ${LIBS} -o xwinwrap

install: all
	install xwinwrap '/usr/local/bin'

uninstall:
	rm -f '/usr/local/bin/xwinwrap'

clean:
	rm -f xwinwrap
