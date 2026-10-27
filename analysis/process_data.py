#!/usr/bin/env python3
"""
Process raw experiment CSV data to generate:
- results/processed/summary.csv (Aggregated per-configuration statistics)
- results/processed/protocol_summary.csv (High-level protocol performance summary)
"""

import csv
import math
from pathlib import Path
from collections import defaultdict

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_CSV = REPO_ROOT / "results" / "raw" / "experiment_results.csv"
PROCESSED_DIR = REPO_ROOT / "results" / "processed"
SUMMARY_CSV = PROCESSED_DIR / "summary.csv"
PROTOCOL_SUMMARY_CSV = PROCESSED_DIR / "protocol_summary.csv"

def mean(vals):
    return sum(vals) / len(vals) if vals else 0.0

def std_dev(vals):
    if len(vals) < 2:
        return 0.0
    m = mean(vals)
    variance = sum((x - m) ** 2 for x in vals) / (len(vals) - 1)
    return math.sqrt(variance)

def main():
    if not RAW_CSV.exists():
        print(f"Error: {RAW_CSV} not found.")
        return 1

    rows = []
    with open(RAW_CSV, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)

    print(f"Loaded {len(rows)} raw experiment runs.")

    # Group by configuration key
    # (exp_type, protocol, window_size, loss_rate, delay_ms, jitter_ms, corrupt_rate, dup_rate, reorder_rate)
    groups = defaultdict(list)
    for r in rows:
        key = (
            r["experiment_type"],
            r["protocol"],
            int(r["window_size"]),
            float(r["loss_rate"]),
            int(r["delay_ms"]),
            int(r["jitter_ms"]),
            float(r["corruption_rate"]),
            float(r["duplication_rate"]),
            float(r["reorder_rate"]),
        )
        groups[key].append(r)

    summary_rows = []
    for key, group in groups.items():
        exp_type, proto, win, loss, delay, jitter, corrupt, dup, reorder = key

        durations = [float(r["duration_sec"]) for r in group]
        goodputs_kib = [float(r["goodput_kib_sec"]) for r in group]
        retrans = [int(r["retransmissions"]) for r in group]
        timeouts = [int(r["timeouts"]) for r in group]
        success_count = sum(1 for r in group if r["status"] == "SUCCESS")
        hash_match_count = sum(1 for r in group if r["hash_match"].lower() in ("true", "1"))

        summary_rows.append({
            "experiment_type": exp_type,
            "protocol": proto,
            "window_size": win,
            "loss_rate": loss,
            "delay_ms": delay,
            "jitter_ms": jitter,
            "corruption_rate": corrupt,
            "duplication_rate": dup,
            "reorder_rate": reorder,
            "runs": len(group),
            "successful_runs": success_count,
            "hash_matches": hash_match_count,
            "duration_mean_sec": round(mean(durations), 4),
            "duration_std_sec": round(std_dev(durations), 4),
            "duration_min_sec": round(min(durations), 4),
            "duration_max_sec": round(max(durations), 4),
            "goodput_mean_kib_sec": round(mean(goodputs_kib), 2),
            "goodput_std_kib_sec": round(std_dev(goodputs_kib), 2),
            "retransmissions_mean": round(mean(retrans), 2),
            "retransmissions_max": max(retrans),
            "timeouts_mean": round(mean(timeouts), 2)
        })

    # Sort summary rows
    summary_rows.sort(key=lambda x: (x["experiment_type"], x["protocol"], x["loss_rate"], x["delay_ms"], x["window_size"]))

    summary_fields = list(summary_rows[0].keys()) if summary_rows else []
    with open(SUMMARY_CSV, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=summary_fields)
        writer.writeheader()
        writer.writerows(summary_rows)

    print(f"Wrote {len(summary_rows)} aggregated configurations to {SUMMARY_CSV}")

    # Generate protocol_summary.csv across all common benchmark conditions
    proto_groups = defaultdict(list)
    for r in rows:
        proto_groups[r["protocol"]].append(r)

    proto_summary = []
    for proto, p_rows in proto_groups.items():
        durations = [float(r["duration_sec"]) for r in p_rows]
        goodputs_kib = [float(r["goodput_kib_sec"]) for r in p_rows]
        retrans = [int(r["retransmissions"]) for r in p_rows]
        timeouts = [int(r["timeouts"]) for r in p_rows]
        success_count = sum(1 for r in p_rows if r["status"] == "SUCCESS")
        hash_match_count = sum(1 for r in p_rows if r["hash_match"].lower() in ("true", "1"))

        proto_summary.append({
            "protocol": proto,
            "total_runs": len(p_rows),
            "successful_runs": success_count,
            "hash_match_count": hash_match_count,
            "reliability_pct": round(100.0 * hash_match_count / len(p_rows), 2),
            "avg_duration_sec": round(mean(durations), 4),
            "avg_goodput_kib_sec": round(mean(goodputs_kib), 2),
            "avg_retransmissions": round(mean(retrans), 2),
            "total_retransmissions": sum(retrans),
            "total_timeouts": sum(timeouts)
        })

    proto_fields = list(proto_summary[0].keys()) if proto_summary else []
    with open(PROTOCOL_SUMMARY_CSV, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=proto_fields)
        writer.writeheader()
        writer.writerows(proto_summary)

    print(f"Wrote protocol summary to {PROTOCOL_SUMMARY_CSV}")
    return 0

if __name__ == "__main__":
    main()
