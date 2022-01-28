CC=gcc
CFLAGS=-Wall -g -DDEBUG

all: diskinfo disklist diskget diskput

diskinfo: diskinfo.c
	$(CC) $(CFLAGS) diskinfo.c -o diskinfo

disklist: disklist.c
	$(CC) $(CFLAGS) disklist.c -o disklist

diskget: diskget.c
	$(CC) $(CFLAGS) diskget.c -o diskget

diskput: diskput.c
	$(CC) $(CFLAGS) diskput.c -o diskput

.PHONY: clean

clean:
	rm -f diskinfo disklist diskget diskput