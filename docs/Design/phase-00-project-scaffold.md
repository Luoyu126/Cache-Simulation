# Phase 00: Project Scaffold

## Status

- Design: Confirmed
- Implementation: Implemented
- Acceptance: Partial

## Scope and Non-Goals

This phase creates and registers an isolated cache component for the team. It
does not implement cache behavior.

## Confirmed Decisions and Rationale

- The implementation directory and component name are `teamCache`.
- The original `cache/` starter remains unchanged so it can continue to serve
  as a framework reference.
- The teammate's 15-213 repository remains a separate source of reusable ideas
  and is not part of the CADSS build.
- Gradescope component selection uses `__cache__:teamCache`.
- Phase harnesses live outside the submitted cache component directory, at
  `tests/teamCache/`, so that `teamCache/` contains only component sources.

## Implementation Mapping

- `teamCache/cache.c`: copied cache starter entry points
- `teamCache/CMakeLists.txt`: `teamCache` shared-library target
- `CMakeLists.txt`: `add_subdirectory(teamCache)` registration
- `submission`: selected cache component
- `tests/teamCache/`: phase harnesses and their reference traces/configs

## Harness Location and Submission Robustness

`submission` names only the cache directory, so an autograder is free to build
that directory itself instead of using `teamCache/CMakeLists.txt`. While the
eight phase harnesses lived in `teamCache/tests/`, that directory contained
eight `main` definitions, which breaks any build that compiles every source
under the named cache directory. Locally this stayed invisible, because
`teamCache/CMakeLists.txt` lists its nine component sources explicitly and
never compiled the harnesses.

Confirmed layout decision: harnesses and their data files moved to
`tests/teamCache/`. The recorded strict-compilation commands keep `-IteamCache`,
so harness `#include "lifecycle.h"` style includes still resolve and no harness
source needed editing.

Observed after the move:

- Passing: all eight harnesses compile under
  `-std=c11 -Wall -Wextra -Wpedantic -Werror` and pass.
- Passing: `grep -rn 'int main' teamCache/` reports no matches.
- Passing: compiling every `.c` under `teamCache/` into one shared library
  succeeds, which is the condition that previously failed.
- Passing: `cmake --build ... --target teamCache` still links
  `libteamCache.so`.

Open: whether this was the actual cause of the Gradescope
`autograder failed to execute correctly` result is unconfirmed, because the
autograder's build procedure is not published. A separate and still
unexplained submission symptom is a 600-second autograder timeout, which was
not reproduced on any checked-in trace.

## Acceptance Behaviors and Observed Results

- Passing: the isolated `teamCache` CMake target compiled successfully and
  produced `libteamCache.so` in an out-of-tree test build.
- Passing: `submission` selects `teamCache`.
- Passing: the original `cache/` and `refCache/` were not modified.
- Not yet run in the supported course environment: a clean full
  `cmake .; make` followed by engine loading with `-c teamCache`.

## Deviations and Follow-Ups

- The current local environment uses a CMake version that requires
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for this repository's declared minimum.
- The current local environment lacks `zlib.h`, so a full build fails in the
  unrelated task-graph library. Final clean-build acceptance must be checked on
  the recommended course machines.
