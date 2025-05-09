CC = gcc
CFLAGS = -O2 -Wall

all: sender switch receiver

sender: sender.c protocol.h
	$(CC) $(CFLAGS) -o sender sender.c

switch: switch.c protocol.h
	$(CC) $(CFLAGS) -o switch switch.c

receiver: receiver.c protocol.h
	$(CC) $(CFLAGS) -o receiver receiver.c

clean:
	rm -f sender switch receiver
