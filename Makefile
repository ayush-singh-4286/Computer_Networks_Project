
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude

COMMON = src/packet.c src/checksum.c

all: sender receiver

sender: src/sender.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/sender.c $(COMMON) -o sender

receiver: src/receiver.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/receiver.c $(COMMON) -o receiver

clean:
	rm -f sender receiver sender.exe receiver.exe

.PHONY: all clean
