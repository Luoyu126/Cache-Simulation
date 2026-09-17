#!/usr/bin/env python3
import csv, os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
CSV_PATH = os.path.join(REPO, "experiments/derived/candidates.csv")
TRACES = ["astar.trace", "bzip2.trace", "long.trace", "mcf.trace", "perlbench.trace"]

def main():
    with open(CSV_PATH) as f:
        rows = list(csv.DictReader(f))

    print(f"Loaded {len(rows)} candidates\n")

    winners = {}
    for trace in TRACES:
        best = min(rows, key=lambda r: float(r[trace]))
        winners[trace] = best
        print(f"Best for {trace}: s={best['s']} E={best['E']} b={best['b']} "
              f"{best['policy']} k={best['k']}  AAT={float(best[trace]):.4f}")

    print("\nDo the per-trace winners agree?")
    configs = {(w['s'], w['E'], w['b'], w['policy'], w['k']) for w in winners.values()}
    if len(configs) == 1:
        print("YES — one config wins on every trace. That's your answer.")
    else:
        print(f"NO — {len(configs)} distinct configs win across the 5 traces.")
        print("Need to justify a single trade-off pick for the report.")

if __name__ == "__main__":
    main()