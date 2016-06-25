CC = gcc
CFLAGS = -Wall

all: logind login

logind: logind.c
	$(CC) $(CFLAGS) -o $@ $< 

login: login.c
	$(CC) $(CFLAGS) -lcrypt -o $@ $< 

clean:
	rm -f logind login *.o
