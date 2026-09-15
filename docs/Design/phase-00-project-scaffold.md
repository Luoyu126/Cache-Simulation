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

## Implementation Mapping

- `teamCache/cache.c`: copied cache starter entry points
- `teamCache/CMakeLists.txt`: `teamCache` shared-library target
- `CMakeLists.txt`: `add_subdirectory(teamCache)` registration
- `submission`: selected cache component

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
