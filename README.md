# Reliable Data Transfer over UDP

## Project Overview

This project implements reliable data transfer over UDP using Stop-and-Wait, Go-Back-N, and Selective Repeat ARQ techniques. It includes packet encoding, checksum validation, acknowledgements, retransmissions, and a channel emulator for testing network faults.

## Technologies

- C
- UDP sockets
- Linux / WSL
- GCC and Make

## Build

From the project root directory, run:

```bash
make clean
make
```

## Implementations

- **Stop-and-Wait:** Sends a packet and waits for its acknowledgement before continuing.
- **Go-Back-N:** Uses a sliding window and cumulative acknowledgements; retransmits outstanding packets when necessary.
- **Selective Repeat:** Tracks acknowledgements per packet and retransmits unacknowledged packets.
- **General file transfer:** Transfers a file using UDP with packet acknowledgements and a finalization exchange.
- **Channel emulator:** Forwards UDP traffic and supports configurable packet loss, duplication, corruption, delay, and reordering.

## Basic Test Commands

Run each program in a separate terminal from the project root.

### Stop-and-Wait

```bash
./receiver stopwait_output.txt
./channel_emulator
./stop_wait
```

### Go-Back-N

```bash
./receiver gbn_output.txt
./channel_emulator
./go_back_n
```

### Selective Repeat

```bash
./receiver_sr
./channel_emulator
./selective_repeat
```

### General File Transfer

```bash
./receiver received.txt
./sender 127.0.0.1 9000 input.txt
```

The general sender connects directly to the receiver on port 9000. When using the channel emulator, the sender must instead target port 7000.

## Adaptive Retransmission Timeout

The general file sender estimates the round-trip time (RTT) and updates its retransmission timeout (RTO). It uses smoothed RTT and RTT variation, applies timeout backoff, and avoids RTT samples from retransmitted packets in accordance with Karn's algorithm.

## Testing

Basic tests have verified successful Stop-and-Wait, Go-Back-N, and Selective Repeat transfers. A Go-Back-N packet-loss recovery test and a reordering-enabled transfer have also produced the expected output.

## Limitations and Future Work

- Extend adaptive RTO and Karn's algorithm to Go-Back-N and Selective Repeat.
- Run systematic tests for packet loss, duplication, corruption, reordering, and variable delay.
- Add quantitative experiments for goodput, retransmissions, and timeout sensitivity.
- Add cryptographic file-integrity verification if required; the current FNV-1a checksum is not cryptographic.
