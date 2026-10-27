#!/usr/bin/env python3
"""
Generate publication-quality PNG plots from experimental CSV data.
Plots generated:
1. results/plots/goodput_vs_packet_loss.png
2. results/plots/retransmissions_vs_packet_loss.png
3. results/plots/transfer_time_vs_packet_loss.png
4. results/plots/goodput_vs_delay.png
5. results/plots/protocol_comparison.png
6. results/plots/window_size_vs_goodput.png
"""

import csv
from pathlib import Path
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")  # Non-interactive backend
import matplotlib.pyplot as plt
import numpy as np

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_CSV = REPO_ROOT / "results" / "raw" / "experiment_results.csv"
SUMMARY_CSV = REPO_ROOT / "results" / "processed" / "summary.csv"
PLOTS_DIR = REPO_ROOT / "results" / "plots"

PLOTS_DIR.mkdir(parents=True, exist_ok=True)

# Styling configuration
plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
plt.rcParams.update({
    "font.size": 11,
    "axes.labelsize": 12,
    "axes.titlesize": 13,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "legend.fontsize": 11,
    "figure.titlesize": 14,
    "grid.alpha": 0.5,
    "grid.linestyle": "--"
})

PROTO_COLORS = {
    "Stop-and-Wait": "#1f77b4",     # Blue
    "Go-Back-N": "#d62728",         # Red
    "Selective-Repeat": "#2ca02c",  # Green
    "Adaptive-RTO": "#9467bd"       # Purple
}

PROTO_MARKERS = {
    "Stop-and-Wait": "o",
    "Go-Back-N": "s",
    "Selective-Repeat": "^",
    "Adaptive-RTO": "D"
}

def load_summary():
    rows = []
    if not SUMMARY_CSV.exists():
        print(f"Error: {SUMMARY_CSV} does not exist.")
        return []
    with open(SUMMARY_CSV, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)
    return rows

def plot_goodput_vs_loss(data):
    exp_rows = [r for r in data if r["experiment_type"] == "loss_sensitivity"]
    if not exp_rows:
        return

    plt.figure(figsize=(8, 5.5), dpi=300)
    protocols = ["Stop-and-Wait", "Go-Back-N", "Selective-Repeat"]

    for proto in protocols:
        p_data = sorted([r for r in exp_rows if r["protocol"] == proto], key=lambda x: float(x["loss_rate"]))
        if not p_data:
            continue
        losses = [float(r["loss_rate"]) * 100 for r in p_data]
        goodputs = [float(r["goodput_mean_kib_sec"]) for r in p_data]
        errors = [float(r["goodput_std_kib_sec"]) for r in p_data]

        plt.errorbar(
            losses, goodputs, yerr=errors,
            label=f"{proto} (W={p_data[0]['window_size']})",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linewidth=2, markersize=7, capsize=4, capthick=1.5
        )

    plt.title("Goodput vs. Packet Loss Probability")
    plt.xlabel("Packet Loss Rate (%)")
    plt.ylabel("Goodput (KiB/s)")
    plt.xticks([0, 1, 3, 5, 10])
    plt.legend(frameon=True, facecolor="white", framealpha=0.9)
    plt.tight_layout()
    out_path = PLOTS_DIR / "goodput_vs_packet_loss.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def plot_retransmissions_vs_loss(data):
    exp_rows = [r for r in data if r["experiment_type"] == "loss_sensitivity"]
    if not exp_rows:
        return

    plt.figure(figsize=(8, 5.5), dpi=300)
    protocols = ["Stop-and-Wait", "Go-Back-N", "Selective-Repeat"]

    for proto in protocols:
        p_data = sorted([r for r in exp_rows if r["protocol"] == proto], key=lambda x: float(x["loss_rate"]))
        if not p_data:
            continue
        losses = [float(r["loss_rate"]) * 100 for r in p_data]
        retx = [float(r["retransmissions_mean"]) for r in p_data]

        plt.plot(
            losses, retx,
            label=f"{proto}",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linewidth=2, markersize=7
        )

    plt.title("Retransmissions vs. Packet Loss Probability")
    plt.xlabel("Packet Loss Rate (%)")
    plt.ylabel("Mean Retransmitted Packets")
    plt.xticks([0, 1, 3, 5, 10])
    plt.legend(frameon=True, facecolor="white", framealpha=0.9)
    plt.tight_layout()
    out_path = PLOTS_DIR / "retransmissions_vs_packet_loss.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def plot_duration_vs_loss(data):
    exp_rows = [r for r in data if r["experiment_type"] == "loss_sensitivity"]
    if not exp_rows:
        return

    plt.figure(figsize=(8, 5.5), dpi=300)
    protocols = ["Stop-and-Wait", "Go-Back-N", "Selective-Repeat"]

    for proto in protocols:
        p_data = sorted([r for r in exp_rows if r["protocol"] == proto], key=lambda x: float(x["loss_rate"]))
        if not p_data:
            continue
        losses = [float(r["loss_rate"]) * 100 for r in p_data]
        durations = [float(r["duration_mean_sec"]) for r in p_data]
        errors = [float(r["duration_std_sec"]) for r in p_data]

        plt.errorbar(
            losses, durations, yerr=errors,
            label=f"{proto}",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linewidth=2, markersize=7, capsize=4, capthick=1.5
        )

    plt.title("Transfer Duration vs. Packet Loss Probability")
    plt.xlabel("Packet Loss Rate (%)")
    plt.ylabel("Transfer Duration (seconds)")
    plt.xticks([0, 1, 3, 5, 10])
    plt.legend(frameon=True, facecolor="white", framealpha=0.9)
    plt.tight_layout()
    out_path = PLOTS_DIR / "transfer_time_vs_packet_loss.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def plot_goodput_vs_delay(data):
    exp_rows = [r for r in data if r["experiment_type"] == "delay_sensitivity"]
    if not exp_rows:
        return

    plt.figure(figsize=(8, 5.5), dpi=300)
    protocols = ["Stop-and-Wait", "Go-Back-N", "Selective-Repeat"]

    for proto in protocols:
        p_data = sorted([r for r in exp_rows if r["protocol"] == proto], key=lambda x: int(x["delay_ms"]))
        if not p_data:
            continue
        delays = [int(r["delay_ms"]) for r in p_data]
        goodputs = [float(r["goodput_mean_kib_sec"]) for r in p_data]
        errors = [float(r["goodput_std_kib_sec"]) for r in p_data]

        plt.errorbar(
            delays, goodputs, yerr=errors,
            label=f"{proto} (W={p_data[0]['window_size']})",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linewidth=2, markersize=7, capsize=4, capthick=1.5
        )

    plt.title("Goodput vs. Configured One-Way Delay")
    plt.xlabel("One-Way Delay (ms)")
    plt.ylabel("Goodput (KiB/s)")
    plt.xticks([0, 20, 50, 100])
    plt.legend(frameon=True, facecolor="white", framealpha=0.9)
    plt.tight_layout()
    out_path = PLOTS_DIR / "goodput_vs_delay.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def plot_window_size_vs_goodput(data):
    exp_rows = [r for r in data if r["experiment_type"] == "window_sensitivity"]
    if not exp_rows:
        return

    fig, ax1 = plt.subplots(figsize=(8, 5.5), dpi=300)
    ax2 = ax1.twinx()

    protocols = ["Go-Back-N", "Selective-Repeat"]
    for proto in protocols:
        p_data = sorted([r for r in exp_rows if r["protocol"] == proto], key=lambda x: int(x["window_size"]))
        if not p_data:
            continue
        windows = [int(r["window_size"]) for r in p_data]
        goodputs = [float(r["goodput_mean_kib_sec"]) for r in p_data]
        durations = [float(r["duration_mean_sec"]) for r in p_data]

        ax1.plot(
            windows, goodputs,
            label=f"{proto} Goodput",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linewidth=2.2, markersize=8
        )

        ax2.plot(
            windows, durations,
            label=f"{proto} Duration",
            color=PROTO_COLORS.get(proto, "black"),
            marker=PROTO_MARKERS.get(proto, "o"),
            linestyle="--", linewidth=1.8, markersize=6
        )

    ax1.set_xlabel("Window Size (Packets)")
    ax1.set_ylabel("Goodput (KiB/s)", color="#1f77b4")
    ax2.set_ylabel("Transfer Duration (s)", color="#555555")
    ax1.set_xticks([1, 4, 8, 16])
    ax1.set_title("Effect of Window Size on Goodput & Duration (Loss=2%, Delay=10ms)")

    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="center right", frameon=True, facecolor="white", framealpha=0.9)

    plt.tight_layout()
    out_path = PLOTS_DIR / "window_size_vs_goodput.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def plot_protocol_comparison(data):
    """Multi-bar comparative chart under baseline, delay, and loss."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5.5), dpi=300)

    # Scenarios to compare:
    # 1: Baseline (delay=0, loss=0)
    # 2: Loss 5% (delay=5, loss=0.05)
    # 3: Delay 50ms (delay=50, loss=0)

    scenarios = ["Baseline (0% Loss, 0ms)", "Loss (5% Loss, 5ms)", "Delay (0% Loss, 50ms)"]
    protocols = ["Stop-and-Wait", "Go-Back-N", "Selective-Repeat"]

    bar_width = 0.25
    x_indices = np.arange(len(scenarios))

    for idx, proto in enumerate(protocols):
        # find baseline
        b_row = [r for r in data if r["experiment_type"] == "delay_sensitivity" and r["protocol"] == proto and int(r["delay_ms"]) == 0]
        # find loss 5%
        l_row = [r for r in data if r["experiment_type"] == "loss_sensitivity" and r["protocol"] == proto and float(r["loss_rate"]) == 0.05]
        # find delay 50ms
        d_row = [r for r in data if r["experiment_type"] == "delay_sensitivity" and r["protocol"] == proto and int(r["delay_ms"]) == 50]

        gp_vals = [
            float(b_row[0]["goodput_mean_kib_sec"]) if b_row else 0.0,
            float(l_row[0]["goodput_mean_kib_sec"]) if l_row else 0.0,
            float(d_row[0]["goodput_mean_kib_sec"]) if d_row else 0.0,
        ]

        dur_vals = [
            float(b_row[0]["duration_mean_sec"]) if b_row else 0.0,
            float(l_row[0]["duration_mean_sec"]) if l_row else 0.0,
            float(d_row[0]["duration_mean_sec"]) if d_row else 0.0,
        ]

        pos = x_indices + (idx - 1) * bar_width
        ax1.bar(pos, gp_vals, width=bar_width, label=proto, color=PROTO_COLORS.get(proto, "black"), alpha=0.85)
        ax2.bar(pos, dur_vals, width=bar_width, label=proto, color=PROTO_COLORS.get(proto, "black"), alpha=0.85)

    ax1.set_title("Goodput Comparison Across Conditions")
    ax1.set_ylabel("Goodput (KiB/s)")
    ax1.set_xticks(x_indices)
    ax1.set_xticklabels(scenarios, rotation=15)
    ax1.legend(frameon=True, facecolor="white", framealpha=0.9)

    ax2.set_title("Transfer Duration Comparison")
    ax2.set_ylabel("Duration (seconds)")
    ax2.set_xticks(x_indices)
    ax2.set_xticklabels(scenarios, rotation=15)
    ax2.legend(frameon=True, facecolor="white", framealpha=0.9)

    plt.suptitle("ARQ Protocol Comprehensive Benchmark Comparison", fontsize=14, y=0.98)
    plt.tight_layout()
    out_path = PLOTS_DIR / "protocol_comparison.png"
    plt.savefig(out_path)
    plt.close()
    print(f"Saved: {out_path}")

def main():
    data = load_summary()
    if not data:
        print("No data found to plot.")
        return 1

    print("Generating figures...")
    plot_goodput_vs_loss(data)
    plot_retransmissions_vs_loss(data)
    plot_duration_vs_loss(data)
    plot_goodput_vs_delay(data)
    plot_window_size_vs_goodput(data)
    plot_protocol_comparison(data)
    print("All plots generated successfully!")
    return 0

if __name__ == "__main__":
    main()
