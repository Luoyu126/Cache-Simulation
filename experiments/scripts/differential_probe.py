#!/usr/bin/env python3
"""Find and reduce short teamCache/refCache behavioral differences."""

import argparse
import random
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
TICK_RE = re.compile(r"Ticks - (\d+)")


@dataclass(frozen=True)
class Result:
    returncode: int
    ticks: int | None
    accesses: tuple[str, ...]
    stderr: str


def run(engine: Path, build: Path, config: Path, trace: Path, component: str) -> Result:
    completed = subprocess.run(
        [str(engine), "-s", str(config), "-c", component, "-t", str(trace), "-v"],
        cwd=build,
        text=True,
        capture_output=True,
        timeout=15,
        check=False,
    )
    stdout = completed.stdout.replace("\x00", "")
    match = TICK_RE.search(stdout)
    accesses = tuple(
        line.strip() for line in stdout.splitlines() if "Address:" in line
    )
    return Result(
        completed.returncode,
        int(match.group(1)) if match else None,
        accesses,
        completed.stderr.strip(),
    )


def differs(team: Result, reference: Result) -> bool:
    return (
        team.returncode != reference.returncode
        or team.ticks != reference.ticks
        or team.accesses != reference.accesses
    )


def write_trace(path: Path, operations: list[str]) -> None:
    path.write_text("\n".join(operations) + "\n", encoding="ascii")


def compare(
    engine: Path,
    build: Path,
    config: Path,
    trace: Path,
    operations: list[str],
) -> tuple[Result, Result]:
    write_trace(trace, operations)
    team = run(engine, build, config, trace, "teamCache")
    reference = run(engine, build, config, trace, str(REPO / "refCache"))
    return team, reference


def minimize(
    engine: Path,
    build: Path,
    config: Path,
    trace: Path,
    operations: list[str],
) -> tuple[list[str], Result, Result]:
    current = operations[:]
    changed = True
    while changed and len(current) > 1:
        changed = False
        for index in range(len(current)):
            candidate = current[:index] + current[index + 1 :]
            team, reference = compare(engine, build, config, trace, candidate)
            if differs(team, reference):
                current = candidate
                changed = True
                break
    team, reference = compare(engine, build, config, trace, current)
    return current, team, reference


def generated_cases(rng: random.Random, count: int) -> list[list[str]]:
    cases = [
        ["L 0,1", "L f,2"],
        ["L 0,1", "L 10,1", "L f,2"],
        ["L 0,1", "L 10,1", "L 20,1", "L f,2"],
        ["S 0,1", "L 10,1", "L f,2"],
        ["L f,2", "L 10,1"],
        ["L f,2", "L 0,1", "L 10,1"],
    ]
    addresses = [0x0, 0x1, 0xE, 0xF, 0x10, 0x11, 0x1E, 0x1F,
                 0x20, 0x2F, 0x30, 0x3F, 0x40, 0x4F]
    sizes = [1, 2, 4, 8]
    for _ in range(count):
        operations = []
        for _ in range(rng.randint(2, 9)):
            op = rng.choice(("L", "S"))
            address = rng.choice(addresses)
            size = rng.choice(sizes)
            operations.append(f"{op} {address:x},{size}")
        cases.append(operations)
    return cases


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--random-cases", type=int, default=300)
    parser.add_argument("--max-findings", type=int, default=12)
    parser.add_argument("--findings-per-config", type=int, default=2)
    args = parser.parse_args()

    engine = REPO / "cadss-engine"
    policies = ("lru", "rrip")
    geometries = ((0, 1), (0, 2), (1, 2), (2, 4), (1, 8))
    rng = random.Random(346)
    findings = 0

    with tempfile.TemporaryDirectory(prefix="cadss-diff-") as tmp_name:
        tmp = Path(tmp_name)
        config = tmp / "probe.config"
        trace = tmp / "probe.trace"

        for policy in policies:
            for set_bits, ways in geometries:
                if policy == "rrip" and ways == 1:
                    continue
                rrip_bits = min(3, ways.bit_length() - 1)
                policy_args = "" if policy == "lru" else f"-R {rrip_bits}"
                config_findings = 0
                config.write_text(
                    "__processor\n"
                    f"__cache -s {set_bits} -E {ways} -b 4 -i 0 -u 0 -w 0 "
                    f"{policy_args}\n"
                    "__branch\n__coherence\n__interconnect\n__memory\n",
                    encoding="ascii",
                )
                seen: set[tuple[str, ...]] = set()
                for operations in generated_cases(rng, args.random_cases):
                    team, reference = compare(
                        engine, args.build, config, trace, operations
                    )
                    if not differs(team, reference):
                        continue
                    reduced, team, reference = minimize(
                        engine, args.build, config, trace, operations
                    )
                    key = tuple(reduced)
                    if key in seen:
                        continue
                    seen.add(key)
                    findings += 1
                    config_findings += 1
                    print(
                        f"[{policy} s={set_bits} E={ways}] "
                        f"team={team.ticks} ref={reference.ticks}"
                    )
                    print("  trace: " + "; ".join(reduced))
                    print("  team: " + " | ".join(team.accesses))
                    print("  ref:  " + " | ".join(reference.accesses))
                    if team.returncode != 0 or reference.returncode != 0:
                        print(f"  exit: team={team.returncode} ref={reference.returncode}")
                        print(f"  team stderr: {team.stderr}")
                        print(f"  ref stderr:  {reference.stderr}")
                    if findings >= args.max_findings:
                        return 1
                    if config_findings >= args.findings_per_config:
                        break

    if findings == 0:
        print("No differences found in the generated cases.")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
