#!/usr/bin/env python3
"""
Automated Experiment Runner for Reliable Data Transfer over UDP
Executes systematic sweeps for:
1. Packet Loss Sensitivity (SW, GBN, SR)
2. Delay Sensitivity (SW, GBN, SR)
3. Window Size Sensitivity (GBN, SR)
4. Other Fault Modes (Duplication, Corruption, Reordering, Jitter)
5. Adaptive RTO under variable latency (Adaptive Sender)
"""

import os
import sys
import time
import subprocess
import hashlib
import csv
import re
from pathlib import Path

# Paths
REPO_ROOT = Path(__file__).resolve().parent.parent
RESULTS_DIR = REPO_ROOT / "results"
RAW_DIR = RESULTS_DIR / "raw"
LOGS_DIR = RAW_DIR / "run_logs"
PROCESSED_DIR = RESULTS_DIR / "processed"
CSV_PATH = RAW_DIR / "experiment_results.csv"

PAYLOAD_FILE = REPO_ROOT / "experiments" / "test_payload.bin"
PAYLOAD_SIZE = 20480  # 20 KB (20 packets of 1024 bytes)

# Ensure output directories exist
LOGS_DIR.mkdir(parents=True, exist_ok=True)
PROCESSED_DIR.mkdir(parents=True, exist_ok=True)

def safe_unlink(p):
    if not p.exists():
        return
    for _ in range(25):
        try:
            p.unlink()
            return
        except OSError:
            time.sleep(0.1)

def generate_payload():
    """Generate deterministic payload file and return its SHA-256."""
    if not PAYLOAD_FILE.exists() or PAYLOAD_FILE.stat().st_size != PAYLOAD_SIZE:
        print(f"Generating test payload of {PAYLOAD_SIZE} bytes...")
        PAYLOAD_FILE.parent.mkdir(parents=True, exist_ok=True)
        # Deterministic byte pattern
        data = bytearray(
            (i * 37 + (i >> 8) * 17 + 0x5A) & 0xFF for i in range(PAYLOAD_SIZE)
        )
        PAYLOAD_FILE.write_bytes(data)

    hasher = hashlib.sha256()
    hasher.update(PAYLOAD_FILE.read_bytes())
    return hasher.hexdigest()

EXPECTED_HASH = generate_payload()
print(f"Test payload ready. SHA-256: {EXPECTED_HASH}")

CSV_HEADERS = [
    "run_id",
    "experiment_type",
    "protocol",
    "window_size",
    "loss_rate",
    "delay_ms",
    "jitter_ms",
    "corruption_rate",
    "duplication_rate",
    "reorder_rate",
    "seed",
    "file_size_bytes",
    "duration_sec",
    "goodput_bytes_sec",
    "goodput_kib_sec",
    "retransmissions",
    "timeouts",
    "hash_match",
    "status",
    "error_message"
]

def parse_sender_output(output_text):
    """Extract metrics from sender stdout."""
    metrics = {
        "bytes": PAYLOAD_SIZE,
        "retransmissions": 0,
        "timeouts": 0,
        "duration": 0.0,
        "goodput_bytes_sec": 0.0,
        "goodput_kib_sec": 0.0,
    }

    bytes_match = re.search(r"Total bytes transferred:\s+(\d+)", output_text)
    if bytes_match:
        metrics["bytes"] = int(bytes_match.group(1))

    retx_match = re.search(r"Retransmissions:\s+(\d+)", output_text)
    if retx_match:
        metrics["retransmissions"] = int(retx_match.group(1))

    to_match = re.search(r"Timeouts:\s+(\d+)", output_text)
    if to_match:
        metrics["timeouts"] = int(to_match.group(1))

    dur_match = re.search(r"Duration:\s+([\d\.]+)\s+s", output_text)
    if dur_match:
        metrics["duration"] = float(dur_match.group(1))

    gp_match = re.search(r"Goodput:\s+([\d\.]+)\s+bytes/s\s+\(([\d\.]+)\s+KiB/s\)", output_text)
    if gp_match:
        metrics["goodput_bytes_sec"] = float(gp_match.group(1))
        metrics["goodput_kib_sec"] = float(gp_match.group(2))
    elif metrics["duration"] > 0:
        metrics["goodput_bytes_sec"] = metrics["bytes"] / metrics["duration"]
        metrics["goodput_kib_sec"] = metrics["goodput_bytes_sec"] / 1024.0

    return metrics

def run_single_transfer(run_id, exp_type, protocol, window_size, loss, delay, jitter,
                        corrupt, duplicate, reorder, seed, port_base):
    rec_port = port_base
    emu_port = port_base - 2000
    out_file = REPO_ROOT / f"received_{run_id}.bin"

    safe_unlink(out_file)

    # Determine receiver executable
    if protocol == "Selective-Repeat":
        rec_cmd = [str(REPO_ROOT / "receiver_sr.exe"), str(out_file), str(rec_port), str(window_size)]
    else:
        rec_cmd = [str(REPO_ROOT / "receiver.exe"), str(out_file), str(rec_port)]

    # Determine emulator arguments
    emu_cmd = [
        str(REPO_ROOT / "channel_emulator.exe"),
        "--port", str(emu_port),
        "--receiver-port", str(rec_port),
        "--loss", str(loss),
        "--delay", str(delay),
        "--jitter", str(jitter),
        "--corrupt", str(corrupt),
        "--duplicate", str(duplicate),
        "--reorder", str(reorder),
        "--seed", str(seed)
    ]

    # Determine sender executable
    if protocol == "Stop-and-Wait":
        send_cmd = [str(REPO_ROOT / "stop_wait.exe"), "127.0.0.1", str(emu_port), str(PAYLOAD_FILE)]
    elif protocol == "Go-Back-N":
        send_cmd = [str(REPO_ROOT / "go_back_n.exe"), "127.0.0.1", str(emu_port), str(PAYLOAD_FILE), str(window_size)]
    elif protocol == "Selective-Repeat":
        send_cmd = [str(REPO_ROOT / "selective_repeat.exe"), "127.0.0.1", str(emu_port), str(PAYLOAD_FILE), str(window_size)]
    elif protocol == "Adaptive-RTO":
        send_cmd = [str(REPO_ROOT / "sender.exe"), "127.0.0.1", str(emu_port), str(PAYLOAD_FILE)]
    else:
        raise ValueError(f"Unknown protocol: {protocol}")

    rec_proc = None
    emu_proc = None
    log_content = []
    error_msg = ""
    status = "SUCCESS"
    hash_match = False
    parsed = {}

    try:
        # 1. Start Receiver
        rec_proc = subprocess.Popen(rec_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.15)

        # 2. Start Emulator
        emu_proc = subprocess.Popen(emu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.15)

        # 3. Run Sender synchronously
        send_res = subprocess.run(send_cmd, capture_output=True, text=True, timeout=12)
        log_content.append("=== SENDER OUTPUT ===\n" + send_res.stdout + send_res.stderr)

        if send_res.returncode != 0:
            status = "FAIL"
            error_msg = f"Sender exited with code {send_res.returncode}"

        # 4. Wait for receiver to finish
        try:
            rec_out, _ = rec_proc.communicate(timeout=3)
            log_content.append("=== RECEIVER OUTPUT ===\n" + rec_out)
        except subprocess.TimeoutExpired:
            rec_proc.terminate()
            rec_out, _ = rec_proc.communicate()
            log_content.append("=== RECEIVER OUTPUT (TERMINATED) ===\n" + rec_out)

        # 5. Stop emulator
        if emu_proc.poll() is None:
            emu_proc.terminate()
            try:
                emu_out, _ = emu_proc.communicate(timeout=2)
                log_content.append("=== EMULATOR OUTPUT ===\n" + emu_out)
            except Exception:
                emu_proc.kill()

        parsed = parse_sender_output(send_res.stdout)

        # Verify output file
        if out_file.exists():
            actual_hash = hashlib.sha256(out_file.read_bytes()).hexdigest()
            hash_match = (actual_hash == EXPECTED_HASH)
            if not hash_match:
                status = "FAIL"
                error_msg = f"Hash mismatch: got {actual_hash[:8]}, expected {EXPECTED_HASH[:8]}"
            safe_unlink(out_file)
        else:
            status = "FAIL"
            error_msg = "Output file not created"

    except subprocess.TimeoutExpired:
        status = "TIMEOUT"
        error_msg = "Execution timed out (12s limit)"
        if rec_proc and rec_proc.poll() is None:
            rec_proc.kill()
        if emu_proc and emu_proc.poll() is None:
            emu_proc.kill()
    except Exception as e:
        status = "ERROR"
        error_msg = str(e)
    finally:
        if rec_proc and rec_proc.poll() is None:
            rec_proc.kill()
        if emu_proc and emu_proc.poll() is None:
            emu_proc.kill()
        safe_unlink(out_file)

    # Save log file
    log_file = LOGS_DIR / f"{run_id}.log"
    log_file.write_text("\n".join(log_content), encoding="utf-8")

    row = {
        "run_id": run_id,
        "experiment_type": exp_type,
        "protocol": protocol,
        "window_size": window_size,
        "loss_rate": loss,
        "delay_ms": delay,
        "jitter_ms": jitter,
        "corruption_rate": corrupt,
        "duplication_rate": duplicate,
        "reorder_rate": reorder,
        "seed": seed,
        "file_size_bytes": PAYLOAD_SIZE,
        "duration_sec": parsed.get("duration", 0.0),
        "goodput_bytes_sec": parsed.get("goodput_bytes_sec", 0.0),
        "goodput_kib_sec": parsed.get("goodput_kib_sec", 0.0),
        "retransmissions": parsed.get("retransmissions", 0),
        "timeouts": parsed.get("timeouts", 0),
        "hash_match": hash_match,
        "status": status,
        "error_message": error_msg
    }

    return row

def main():
    print("=== STARTING COMPREHENSIVE EXPERIMENT SUITE ===")
    results = []
    run_idx = 1
    base_port = 9200

    # Write CSV header
    with open(CSV_PATH, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_HEADERS)
        writer.writeheader()

    def record_run(exp_type, protocol, window_size, loss, delay, jitter,
                   corrupt, duplicate, reorder, seed):
        nonlocal run_idx, base_port
        run_id = f"run_{run_idx:04d}_{protocol}"
        port = base_port + (run_idx % 40) * 2

        print(f"[{run_idx:03d}] {exp_type:18s} | {protocol:16s} | L:{loss:.2f} D:{delay}ms J:{jitter}ms C:{corrupt:.2f} Dup:{duplicate:.2f} R:{reorder:.2f} W:{window_size} S:{seed} ... ", end="", flush=True)

        row = run_single_transfer(run_id, exp_type, protocol, window_size, loss, delay, jitter,
                                  corrupt, duplicate, reorder, seed, port)

        results.append(row)
        with open(CSV_PATH, "a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_HEADERS)
            writer.writerow(row)

        print(f"{row['status']} | dur: {row['duration_sec']:.3f}s | retx: {row['retransmissions']} | gp: {row['goodput_kib_sec']:.1f} KiB/s")
        run_idx += 1
        time.sleep(0.05)

    # -------------------------------------------------------------
    # 1. Packet Loss Sensitivity Sweep
    # -------------------------------------------------------------
    print("\n--- EXPERIMENT 1: Packet Loss Sensitivity Sweep ---")
    loss_rates = [0.00, 0.01, 0.03, 0.05, 0.10]
    protocols_exp1 = [
        ("Stop-and-Wait", 1),
        ("Go-Back-N", 4),
        ("Selective-Repeat", 4)
    ]
    seeds_exp1 = [101, 102, 103]

    for loss in loss_rates:
        for proto, win in protocols_exp1:
            for seed in seeds_exp1:
                record_run(
                    exp_type="loss_sensitivity",
                    protocol=proto,
                    window_size=win,
                    loss=loss,
                    delay=5,
                    jitter=0,
                    corrupt=0.0,
                    duplicate=0.0,
                    reorder=0.0,
                    seed=seed
                )

    # -------------------------------------------------------------
    # 2. Delay Sensitivity Sweep
    # -------------------------------------------------------------
    print("\n--- EXPERIMENT 2: Delay Sensitivity Sweep ---")
    delays = [0, 20, 50, 100]
    seeds_exp2 = [201, 202, 203]

    for d in delays:
        for proto, win in protocols_exp1:
            for seed in seeds_exp2:
                record_run(
                    exp_type="delay_sensitivity",
                    protocol=proto,
                    window_size=win,
                    loss=0.0,
                    delay=d,
                    jitter=0,
                    corrupt=0.0,
                    duplicate=0.0,
                    reorder=0.0,
                    seed=seed
                )

    # -------------------------------------------------------------
    # 3. Window Size Sensitivity Sweep
    # -------------------------------------------------------------
    print("\n--- EXPERIMENT 3: Window Size Sensitivity Sweep ---")
    windows = [1, 4, 8, 16]
    protocols_exp3 = ["Go-Back-N", "Selective-Repeat"]
    seeds_exp3 = [301, 302, 303]

    for win in windows:
        for proto in protocols_exp3:
            for seed in seeds_exp3:
                record_run(
                    exp_type="window_sensitivity",
                    protocol=proto,
                    window_size=win,
                    loss=0.02,
                    delay=10,
                    jitter=0,
                    corrupt=0.0,
                    duplicate=0.0,
                    reorder=0.0,
                    seed=seed
                )

    # -------------------------------------------------------------
    # 4. Other Channel Faults (Duplication, Corruption, Reordering, Jitter)
    # -------------------------------------------------------------
    print("\n--- EXPERIMENT 4: Other Fault Modes ---")
    fault_scenarios = [
        ("baseline",    0.0, 0,  0,  0.0,  0.0, 0.0),
        ("duplication", 0.0, 5,  0,  0.0,  0.10, 0.0),
        ("corruption",  0.0, 5,  0,  0.03, 0.0, 0.0),
        ("reordering",  0.0, 5,  0,  0.0,  0.0, 0.15),
        ("jitter",      0.0, 20, 10, 0.0,  0.0, 0.0)
    ]
    seeds_exp4 = [401, 402, 403]

    for scenario_name, loss, delay, jitter, corrupt, dup, reorder in fault_scenarios:
        for proto, win in protocols_exp1:
            for seed in seeds_exp4:
                record_run(
                    exp_type=f"fault_{scenario_name}",
                    protocol=proto,
                    window_size=win,
                    loss=loss,
                    delay=delay,
                    jitter=jitter,
                    corrupt=corrupt,
                    duplicate=dup,
                    reorder=reorder,
                    seed=seed
                )

    # -------------------------------------------------------------
    # 5. Adaptive RTO Evaluation
    # -------------------------------------------------------------
    print("\n--- EXPERIMENT 5: Adaptive RTO Evaluation ---")
    rto_delays = [(10, 2), (25, 5), (50, 15), (100, 30)]
    seeds_exp5 = [501, 502, 503]

    for d, j in rto_delays:
        for seed in seeds_exp5:
            record_run(
                exp_type="adaptive_rto",
                protocol="Adaptive-RTO",
                window_size=1,
                loss=0.0,
                delay=d,
                jitter=j,
                corrupt=0.0,
                duplicate=0.0,
                reorder=0.0,
                seed=seed
            )

    print(f"\nAll {len(results)} experiments completed successfully!")
    print(f"Results saved to: {CSV_PATH}")

    # Process summary data
    print("\n--- Processing Aggregated Statistics ---")
    subprocess.run([sys.executable, str(REPO_ROOT / "analysis" / "process_data.py")], check=True)

    # Generate plots
    print("\n--- Generating Publication Plots ---")
    subprocess.run([sys.executable, str(REPO_ROOT / "analysis" / "generate_plots.py")], check=True)

if __name__ == "__main__":
    main()
