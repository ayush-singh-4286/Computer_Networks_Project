CC ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -Iemulator

ifeq ($(OS),Windows_NT)
    LDLIBS = -lws2_32
    RM_CMD = cmd /c "del /f /q sender receiver receiver_sr stop_wait go_back_n selective_repeat channel_emulator *.exe *.o src\*.o emulator\*.o 2>nul || exit 0"
else
    LDLIBS =
    RM_CMD = rm -f $(TARGETS) *.exe *.o src/*.o emulator/*.o
endif

COMMON = src/packet.c src/checksum.c
TARGETS = sender receiver receiver_sr stop_wait go_back_n selective_repeat channel_emulator

all: $(TARGETS)

sender: src/sender.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/sender.c $(COMMON) $(LDLIBS) -o $@

receiver: src/receiver.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/receiver.c $(COMMON) $(LDLIBS) -o $@

receiver_sr: src/receiver_sr.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/receiver_sr.c $(COMMON) $(LDLIBS) -o $@

stop_wait: src/stop_wait.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/stop_wait.c $(COMMON) $(LDLIBS) -o $@

go_back_n: src/go_back_n.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/go_back_n.c $(COMMON) $(LDLIBS) -o $@

selective_repeat: src/selective_repeat.c $(COMMON) include/packet.h include/net_compat.h
	$(CC) $(CFLAGS) src/selective_repeat.c $(COMMON) $(LDLIBS) -o $@

channel_emulator: emulator/channel_emulator.c emulator/channel_fault.c emulator/channel_emulator.h include/net_compat.h
	$(CC) $(CFLAGS) emulator/channel_emulator.c emulator/channel_fault.c $(LDLIBS) -o $@

clean:
	-$(RM_CMD)

.PHONY: all clean
