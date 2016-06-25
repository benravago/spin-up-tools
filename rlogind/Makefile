
CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS = 
LIBS =

%.o: %.c
	$(CC) $(CFLAGS) $< -c

all: rlogind rlogin

rlogind: maind.o rlogind.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -lutil -o $@

rlogin: rlogin.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f *.o rlogind rlogin

