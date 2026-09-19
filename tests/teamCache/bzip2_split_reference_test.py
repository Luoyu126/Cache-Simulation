#!/usr/bin/env python3
"""Compare the split-eviction compatibility path with refCache end to end."""

import argparse
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]


def run(build: Path, config: Path, trace: Path, component: str) -> str:
    completed = subprocess.run(
        [
            str(REPO / "cadss-engine"),
            "-s",
            str(config),
            "-c",
            component,
            "-t",
            str(trace),
            "-v",
        ],
        cwd=build,
        text=True,
        capture_output=True,
        timeout=30,
        check=False,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"{component} failed for {trace.name}: {completed.stderr.strip()}"
        )
    return completed.stdout.replace("\x00", "")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()

    test_dir = REPO / "tests" / "teamCache"
    configs = (
        test_dir / "bzip2_split_lru.config",
        test_dir / "bzip2_split_rrip.config",
    )
    traces = (
        test_dir / "bzip2_split_full_set.trace",
        test_dir / "bzip2_split_dirty_full_set.trace",
        test_dir / "bzip2_split_full_set_followup.trace",
        test_dir / "bzip2_split_repeat.trace",
    )

    for config in configs:
        for trace in traces:
            team = run(args.build, config, trace, "teamCache")
            reference = run(args.build, config, trace, str(REPO / "refCache"))
            if team != reference:
                raise AssertionError(
                    f"output differs for {config.name} / {trace.name}\n"
                    f"teamCache:\n{team}\nrefCache:\n{reference}"
                )
            ticks = team.splitlines()[0]
            print(f"PASS {config.name} {trace.name}: {ticks}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
