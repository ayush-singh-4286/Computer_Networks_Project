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

## Experiment Results and Graphs
The project includes automated experiments evaluating the reliability and performance of Stop-and-Wait, Go-Back-N, and Selective Repeat over UDP.

### Performance Graphs

#### 1. Goodput vs Packet Loss
[Goodput vs Packet Loss](results/plots/goodput_vs_packet_loss.png)

#### 2. Retransmissions vs Packet Loss
[Retransmissions vs Packet Loss](results/plots/retransmissions_vs_packet_loss.png)

#### 3. Transfer Time vs Packet Loss
[Transfer Time vs Packet Loss](results/plots/transfer_time_vs_packet_loss.png)

#### 4. Goodput vs Network Delay
[Goodput vs Network Delay](results/plots/goodput_vs_delay.png)

#### 5. Window Size vs Goodput
[Window Size vs Goodput](results/plots/window_size_vs_goodput.png)

#### 6. Protocol Comparison
[Protocol Comparison](results/plots/protocol_comparison.png)

### Running the Experiments

Install the required Python libraries:

```bash
python -m pip install matplotlib pandas numpy
Run the experiment suite:
python experiments/run_experiments.py
The scripts save raw experiment data to results/raw/, aggregated statistics to results/processed/, and generated graphs to results/plots/.


## 📊 Experiment Results and Performance Analysis
The project evaluates the performance and reliability of three ARQ protocols over UDP:

* **Stop-and-Wait**
* **Go-Back-N (GBN)**
* **Selective Repeat (SR)**
A configurable channel emulator introduces network impairments such as packet loss, delay, duplication, reordering, and corruption. Automated experiments measure protocol performance under different network conditions.

### 📈 Performance Graphs

#### 1. Goodput vs Packet Loss

Shows how effective data-transfer throughput changes as packet loss increases.

[Goodput vs Packet Loss](results/plots/goodput_vs_packet_loss.png)

#### 2. Retransmissions vs Packet Loss

Illustrates how packet loss affects the number of retransmissions.

[Retransmissions vs Packet Loss](results/plots/retransmissions_vs_packet_loss.png)

#### 3. Transfer Time vs Packet Loss

Compares file-transfer duration under different packet-loss conditions.

[Transfer Time vs Packet Loss](results/plots/transfer_time_vs_packet_loss.png)

#### 4. Goodput vs Network Delay

Examines the relationship between network delay and effective data-transfer throughput.
[Goodput vs Network Delay](results/plots/goodput_vs_delay.png)

#### 5. Window Size vs Goodput

Evaluates the relationship between sender window size and data-transfer performance.

[Window Size vs Goodput](results/plots/window_size_vs_goodput.png)

#### 6. Protocol Comparison

Provides a visual comparison of the tested ARQ protocols.

[Protocol Comparison](results/plots/protocol_comparison.png)

### 🧪 Running the Experiments

**1. Install Python dependencies**

```bash
python -m pip install matplotlib pandas numpy
```

**2. Run the experiment suite**

```bash
python experiments/run_experiments.py
```

The experiment pipeline runs the configured tests, processes the collected measurements, and generates performance graphs.

### 📁 Results Structure

| Directory            | Description                                  |
| -------------------- | -------------------------------------------- |
| `results/raw/`       | Raw experiment measurements                  |
| `results/processed/` | Aggregated statistics and protocol summaries |
| `results/plots/`     | Generated performance graphs                 |

### 📌 Note

The graphs represent the outcomes of the configured experiments. Individual runs may experience failures or timeouts under certain network conditions, so results should be interpreted alongside the recorded experiment data.
