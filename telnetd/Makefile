
CC = gcc
CFLAGS = -O2 -Wall -fno-strict-aliasing
LDFLAGS = 
LIBS =

%.o: %.c
	$(CC) $(CFLAGS) $< -c

all: telnet telnetd

telnet: main.o telnet.o toys.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

telnetd: maind.o telnetd.o toys.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -lutil -o $@

clean:
	rm -f *.o telnet telnetd

