
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude

COMMON = src/packet.c src/checksum.c

all: sender receiver receiver_sr stop_wait go_back_n selective_repeat channel_emulator
sender: src/sender.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/sender.c $(COMMON) -o sender

receiver: src/receiver.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/receiver.c $(COMMON) -o receiver

receiver_sr: src/receiver_sr.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/receiver_sr.c $(COMMON) -o receiver_sr

stop_wait: src/stop_wait.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/stop_wait.c $(COMMON) -o stop_wait

go_back_n: src/go_back_n.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/go_back_n.c $(COMMON) -o go_back_n

selective_repeat: src/selective_repeat.c $(COMMON) include/packet.h
	$(CC) $(CFLAGS) src/selective_repeat.c $(COMMON) -o selective_repeat

channel_emulator: emulator/channel_emulator.c emulator/channel_fault.c emulator/channel_emulator.h
	$(CC) $(CFLAGS) -Iemulator emulator/channel_emulator.c emulator/channel_fault.c -o channel_emulator

clean:
	rm -f sender receiver receiver_sr stop_wait go_back_n selective_repeat channel_emulator sender.exe receiver.exe

.PHONY: all clean
