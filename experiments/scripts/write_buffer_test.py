#!/usr/bin/env python3
import subprocess, os, re, csv

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BUILD = os.path.join(REPO, "build")
ENGINE = os.path.join(REPO, "cadss-engine")
CONFIG_DIR = os.path.join(REPO, "experiments/configs")
TRACE_DIR = "/afs/cs.cmu.edu/academic/class/15346-f23/public/traces/cache"
TRACES = ["astar.trace", "bzip2.trace", "long.trace", "mcf.trace", "perlbench.trace"]

# (s, E, b, policy, k) -- the three per-trace winners from the main sweep
FINALISTS = [
    (3, 8, 9, "LRU", 0),
    (2, 8, 10, "LRU", 0),
    (4, 8, 8, "LRU", 0),
]

def count_accesses(trace_path):
    n = 0
    with open(trace_path) as f:
        for line in f:
            if line.startswith("L") or line.startswith("S"):
                n += 1
    return n

def make_config(path, s, E, b, policy, k, w):
    extra = f"-R {k}" if policy == "RRIP" else ""
    with open(path, "w") as f:
        f.write(f"__processor\n__cache -s {s} -E {E} -b {b} {extra} -w {w}\n"
                 f"__branch\n__coherence\n__interconnect\n__memory\n")

def run_one(config_path, trace_path):
    out = subprocess.run([ENGINE, "-s", config_path, "-c", "teamCache",
                           "-t", trace_path], cwd=BUILD,
                          capture_output=True, text=True, timeout=600)
    m = re.search(r"Ticks - (\d+)", out.stdout)
    return int(m.group(1)) if m else None

def main():
    access_counts = {t: count_accesses(os.path.join(TRACE_DIR, t)) for t in TRACES}

    out_csv = os.path.join(REPO, "experiments/derived/write_buffer_comparison.csv")
    with open(out_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["s", "E", "b", "policy", "k", "w"] + TRACES + ["avg_AAT"])
        for (s, E, b, policy, k) in FINALISTS:
            for w in [0, 1]:
                cfg_path = os.path.join(
                    CONFIG_DIR, f"wbtest_s{s}_E{E}_b{b}_{policy}_k{k}_w{w}.config")
                make_config(cfg_path, s, E, b, policy, k, w)
                aats = []
                for t in TRACES:
                    ticks = run_one(cfg_path, os.path.join(TRACE_DIR, t))
                    aats.append(ticks / access_counts[t])
                avg = sum(aats) / len(aats)
                writer.writerow([s, E, b, policy, k, w, *aats, avg])
                print(f"s={s} E={E} b={b} {policy} k={k} w={w} avg_AAT={avg:.4f}")
    print(f"\nWrote results to {out_csv}")

if __name__ == "__main__":
    main()