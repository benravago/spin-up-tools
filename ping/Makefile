
CC = gcc
CFLAGS = -O2 -Wall -fno-strict-aliasing
LDFLAGS = 
LIBS =

%.o: %.c
	$(CC) $(CFLAGS) -c $<

ping: ping.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f *.o ping

