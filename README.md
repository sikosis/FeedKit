# FeedKit

FeedKit is an RSS Kit for Haiku. This fork aims to preserve the original
project while repairing, updating, and improving it for modern Haiku systems.

The current target is Haiku R1/beta6 on x86_64 and the modern `x86` secondary
architecture on x86_gcc2 hybrid installations.

> **Current status:** compatibility audit and restoration planning are
> complete, and the first native R1/beta6 build iteration is in progress.
> FeedKit is not yet confirmed to build or run successfully on R1/beta6.

See [DESIGN.md](DESIGN.md) for the full compatibility audit, technical
findings, constraints, and acceptance criteria.

## Goal progress

![Audit: complete](https://img.shields.io/badge/1_Audit-complete-2ea44f)
![Design: complete](https://img.shields.io/badge/2_Design-complete-2ea44f)
![Native build: in progress](https://img.shields.io/badge/3_Native_build-in_progress-d29922)
![Core safety: planned](https://img.shields.io/badge/4_Core_safety-planned-8c959f)
![Network and parsers: planned](https://img.shields.io/badge/5_Network_%26_parsers-planned-8c959f)
![Packaging: planned](https://img.shields.io/badge/6_Packaging-planned-8c959f)
![Clients: planned](https://img.shields.io/badge/7_Clients-planned-8c959f)
![Release: planned](https://img.shields.io/badge/8_Release-planned-8c959f)

![Overall progress: 2 of 8 complete](https://img.shields.io/badge/Overall_progress-2_of_8_complete-0969da)

- Green: complete
- Amber: in progress
- Grey: planned

## Progress timeline

| Milestone | Status | Progress and outcome |
| --- | --- | --- |
| Original FeedKit baseline | Historical | Existing library, server, parsers, clients, preflet, and tests retained as the basis of this fork. |
| R1/beta6 compatibility audit | Complete — 25 October 2026 | Audited the active Jam build, legacy makefiles, core library, server, parsers, clients, preflet, and tests against the beta6 SDK and package ecosystem. |
| Restoration design | Complete — 25 October 2026 | Documented build blockers, correctness defects, dependency/API changes, compatibility constraints, staged goals, and release acceptance criteria in `DESIGN.md`. |
| Native beta6 baseline | In progress — 25 October 2026 | The first native `make` attempt exposed malformed fallback targets in `libfeedkit/makefile`. Parsing, modern dependency-directory probes, and x86_64 architecture selection have been repaired; the next native build will identify the next compiler or linker blocker. |
| Core safety and compatibility | Planned | Fix undefined behaviour and ownership defects while preserving UUIDs, serialized data, public APIs, application signatures, and stored user data. |
| Network and parser modernization | Planned | Update curl, OpenSSL, temporary-file handling, parser discovery, feed fixtures, and server integration tests. |
| Package and lifecycle integration | Planned | Add package-aware paths and an HPKG workflow, then verify installation, discovery, startup, persistence, and removal. |
| Optional client ports | Planned | Port clients independently after the core is stable; replace obsolete NetPositive and InfoPopper integrations where necessary. |
| Beta6 release validation | Planned | Pass builds, tests, integration checks, and HPKG installation on x86_64 and the modern secondary `x86` architecture. |

## Goals

### Immediate

- Establish and document a clean Haiku R1/beta6 build environment.
- Repair one supported build path and add a repeatable repository-owned
  validation command.
- Build `libfeedkit`, `feed_server`, RSS and Atom parser add-ons, and tests.
- Add a HaikuPorter recipe with package-aware dependencies and installation
  paths.
- Confirm the existing behaviour and persisted formats before making safety
  fixes.

### Core restoration

- Preserve stable UUID values and `BFlattenable` serialization formats.
- Repair memory safety, ownership, initialization, and pointer-lifetime
  defects.
- Modernize OpenSSL and libcurl usage without changing persisted identities.
- Load parser add-ons correctly from packaged and non-packaged locations.
- Verify feed registration, refresh, persistence, restart, and clean shutdown.

### Release

- Produce an installable HPKG without hard-coded developer or legacy `/boot`
  paths.
- Support R1/beta6 x86_64 and the modern `x86` secondary architecture.
- Preserve existing settings and user data across installation and removal.
- Port optional clients only when each can compile, launch, and pass its own
  smoke test.

## Compatibility principles

Until a separately documented migration is approved, restoration work should
preserve:

- persisted UUID values;
- serialized message field names and type codes;
- the server application signature;
- stored feed and settings locations;
- public FeedKit method signatures; and
- parser add-on symbols and ownership expectations.

## Building and testing

Exact beta6 build, test, and packaging commands will be added after the first
native build baseline is established. The existing build files contain known
obsolete paths and defects, so they should not yet be treated as a supported
installation workflow.

