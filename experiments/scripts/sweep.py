#!/usr/bin/env python3
import subprocess, itertools, math, csv, os, re, sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BUILD = os.path.join(REPO, "build")
ENGINE = os.path.join(REPO, "cadss-engine")
CONFIG_DIR = os.path.join(REPO, "experiments/configs")
TRACE_DIR = "/afs/cs.cmu.edu/academic/class/15346-f23/public/traces/cache"
TRACES = ["astar.trace", "bzip2.trace", "long.trace", "mcf.trace", "perlbench.trace"]
BUDGET_BITS = 54 * 1024 * 8

def storage_bits(s, E, b, policy, k):
    S, B, L = 2**s, 2**b, (2**s) * E
    tag_bits_per_line = 64 - s - b
    data_bits = L * B * 8
    tag_bits = L * tag_bits_per_line
    valid_dirty = L * 2
    if policy == "LRU":
        repl_bits = L * math.ceil(math.log2(E)) if E > 1 else 0
    else:  # RRIP
        repl_bits = L * k
    return data_bits + tag_bits + valid_dirty + repl_bits

def gen_candidates():
    cands = []
    for b in range(4, 11):
        for s in range(0, 12):
            for E in [1, 2, 4, 8]:
                if s + b > 63:
                    continue
                for policy, k in [("LRU", 0)] + [("RRIP", k) for k in (2, 3, 4)]:
                    bits = storage_bits(s, E, b, policy, k)
                    if bits <= BUDGET_BITS:
                        cands.append((s, E, b, policy, k))
    return cands

def make_config(path, s, E, b, policy, k, w=0):
    extra = f"-R {k}" if policy == "RRIP" else ""
    with open(path, "w") as f:
        f.write(f"__processor\n__cache -s {s} -E {E} -b {b} {extra} -w {w}\n"
                 f"__branch\n__coherence\n__interconnect\n__memory\n")

def count_accesses(trace_path):
    n = 0
    with open(trace_path) as f:
        for line in f:
            if line.startswith("L") or line.startswith("S"):
                n += 1
    return n

def run_one(config_path, trace_path):
    out = subprocess.run([ENGINE, "-s", config_path, "-c", "teamCache",
                           "-t", trace_path], cwd=BUILD,
                          capture_output=True, text=True, timeout=600)
    m = re.search(r"Ticks - (\d+)", out.stdout)
    if not m:
        return None
    return int(m.group(1))

def main():
    print("Counting accesses per trace...")
    access_counts = {t: count_accesses(os.path.join(TRACE_DIR, t)) for t in TRACES}
    print(access_counts)

    candidates = gen_candidates()
    print(f"{len(candidates)} candidates pass the 54KB filter")

    out_csv = os.path.join(REPO, "experiments/derived/candidates.csv")
    with open(out_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["s", "E", "b", "policy", "k"] + TRACES + ["avg_AAT"])

        for i, (s, E, b, policy, k) in enumerate(candidates):
            cfg_path = os.path.join(CONFIG_DIR, f"cand_{i}.config")
            make_config(cfg_path, s, E, b, policy, k)
            aats = []
            for t in TRACES:
                ticks = run_one(cfg_path, os.path.join(TRACE_DIR, t))
                if ticks is None:
                    aats = None
                    break
                aats.append(ticks / access_counts[t])
            if aats is None:
                print(f"[{i+1}/{len(candidates)}] FAILED s={s} E={E} b={b} {policy} k={k}")
                continue
            avg_aat = sum(aats) / len(aats)
            writer.writerow([s, E, b, policy, k, *aats, avg_aat])
            f.flush()  # <-- write to disk immediately, don't wait for buffer
            print(f"[{i+1}/{len(candidates)}] s={s} E={E} b={b} {policy} k={k} "
                  f"avg_AAT={avg_aat:.4f}")

    print("Done. Results in", out_csv)

if __name__ == "__main__":
    main()