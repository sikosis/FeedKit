# FeedKit

FeedKit is an RSS Kit for Haiku. This fork aims to preserve the original
project while repairing, updating, and improving it for modern Haiku systems.

The current target is Haiku R1/beta6 on x86_64 and the modern `x86` secondary
architecture on x86_gcc2 hybrid installations.

> **Current status:** compatibility audit and restoration planning are
> complete. The default core target now builds successfully on R1/beta6
> x86_64. Network and parser modernization is complete: the native focused
> suites pass all 215 checks, and the startup, failure-isolation, persistence,
> restart, and clean-shutdown integration paths pass on beta6. The staged
> package layout, install/remove lifecycle, and strict HaikuPorter x86_64
> package build also pass natively.

See [DESIGN.md](DESIGN.md) for the full compatibility audit, technical
findings, constraints, and acceptance criteria.

## Goal progress

![Audit: complete](https://img.shields.io/badge/1_Audit-complete-2ea44f)
![Design: complete](https://img.shields.io/badge/2_Design-complete-2ea44f)
![Native build: complete](https://img.shields.io/badge/3_Native_build-complete-2ea44f)
![Core safety: complete](https://img.shields.io/badge/4_Core_safety-complete-2ea44f)
![Network and parsers: complete](https://img.shields.io/badge/5_Network_%26_parsers-complete-2ea44f)
![Packaging: complete](https://img.shields.io/badge/6_Packaging-complete-2ea44f)
![Clients: in progress](https://img.shields.io/badge/7_Clients-in_progress-d29922)
![Release: planned](https://img.shields.io/badge/8_Release-planned-8c959f)

![Overall progress: 6 of 8 complete](https://img.shields.io/badge/Overall_progress-6_of_8_complete-0969da)

- Green: complete
- Amber: in progress
- Grey: planned

## Progress timeline

| Milestone | Status | Progress and outcome |
| --- | --- | --- |
| Original FeedKit baseline | Historical | Existing library, server, parsers, clients, preflet, and tests retained as the basis of this fork. |
| R1/beta6 compatibility audit | Complete — 25 October 2026 | Audited the active Jam build, legacy makefiles, core library, server, parsers, clients, preflet, and tests against the beta6 SDK and package ecosystem. |
| Restoration design | Complete — 25 October 2026 | Documented build blockers, correctness defects, dependency/API changes, compatibility constraints, staged goals, and release acceptance criteria in `DESIGN.md`. |
| Native beta6 baseline | Complete — 25 October 2026 | User-confirmed successful default build of `libfeedkit.so`, `feed_server`, and the RSS/Atom parser add-ons on R1/beta6 x86_64. Build repairs covered malformed makefiles, dependencies, compiler/linker flags, fixed-width types, protected server access, modern curl callbacks, and repository-local linking. |
| Core safety and compatibility | Complete — 26 October 2026 | The dependency-free suite passes all 113 checks natively on beta6. It covers stable UUIDs, serialization, download progress, listener safety, parent-owned model trees, deep copy and assignment, replacement-safe unflattening, independent refresh merges, download-request ownership, and complete server teardown. Native smoke and integration runs also pass. |
| Network and parser modernization | Complete — 26 October 2026 | Native beta6 smoke and integration tests pass, including RSS/Atom registration and refresh, ordered package-aware parser discovery, clean shutdown, missing downloads, malformed-feed isolation, and two-process persistence/restart with stable UUIDs. The 55-check FileRequest suite covers local files and offline loopback HTTP. The 47-check parser suite covers valid, malformed, incomplete, discovered, duplicate-path, ISO-8859-1, and UTF-8 cases. Together with 113 model checks, all 215 focused checks pass. |
| Package and lifecycle integration | Complete — 27 October 2026 | The staged layout, local and HaikuPorter x86_64 HPKG generation, activation, signature-based on-demand startup, packaged library/server/parser discovery, clean shutdown, removal, and user-data preservation pass natively. HaikuPorter produced runtime, debuginfo, and devel packages. Secondary-architecture coverage remains part of release validation. |
| Optional client ports | In progress — 27 October 2026 | The first independently buildable slice covers the `RegisterFeed` and `FeedSubscriber` command utilities. Both now build natively with repository-local headers and libraries, and use bounded server startup and operation waits instead of racing asynchronous FeedKit requests. An isolated runtime smoke check is ready for native validation. |
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

The default core build has been verified on Haiku R1/beta6 x86_64 with:

```sh
make clean
make
```

The unit, startup/parser-loading/shutdown smoke, and local RSS/Atom
registration-and-refresh integration suites are verified on R1/beta6 x86_64.
The package layout, x86_64 package lifecycle, removal behavior, and strict
HaikuPorter package build are also verified. The secondary `x86` architecture
remains for release validation.

### Runtime smoke test

On Haiku, the smoke target builds the core, starts `feed_server`, verifies that
the RSS and Atom add-ons load, confirms that the server remains alive, and
exercises the server's clean shutdown path:

```sh
make smoke
```

The integration check exercises local RSS and Atom fixtures through the real
downloader and parser add-ons, validates both initial registration and a forced
refresh, confirms that missing and malformed local feeds fail safely, then
persists and restores the valid feeds through a second server process. It uses
an isolated temporary store and never writes fixture subscriptions into the
user's feed store:

```sh
make integration
```

The focused model, FileRequest, and parser add-on suites can be built and run
with:

```sh
make unit
```

`make test` runs both focused suites followed by the smoke and integration
suites. The model executable remains dependency-free; the FileRequest
executable links against the same curl dependency as the server and uses an
in-process loopback HTTP fixture, so it does not require internet access. The
parser executable loads the built RSS and Atom add-ons directly and uses only
repository-owned in-memory and local fixtures.

Captured server output is written to `build/smoke-test.log` or
`build/integration-test.log` when a check fails.

### Optional client builds

Goal 7 clients are enabled in independently validated groups. Build the
currently supported command utilities and their core dependencies with:

```sh
make clients
```

This currently produces `build/Clients/RegisterFeed` and
`build/Clients/FeedSubscriber`. Other historical clients remain excluded from
this target until their dependencies, lifecycle, and smoke checks have been
modernized.

After the build passes, exercise both utilities against the local RSS fixture
and an isolated temporary feed store with:

```sh
make client-smoke
```

The check refuses to run while another `feed_server` is active. It never reads
or writes the user's normal FeedKit settings or feed store.

### Package layout check

The packaging work uses a staging tree and does not invoke the unsafe legacy
`make install` target. The default build also stays inside the repository;
tests resolve `libfeedkit.so` from `build` rather than creating a system
symlink. On Haiku, build and validate the current package contents with:

```sh
make package-check
```

The staged tree is written to `build/package-root` by default. It contains the
runtime library, server, RSS and Atom parser add-ons, the explicitly
maintained public-header set, and project documentation. Override
`PACKAGE_ROOT` to inspect another disposable staging directory.

After the layout check passes, create and inspect a local HPKG with:

```sh
make package
```

This writes `build/feedkit-0.1.0-2-<architecture>.hpkg`. It validates the
embedded package name, version, architecture, server, library, and parser
paths with Haiku's `package` tool. It does not install, publish, or otherwise
activate the package.

FeedKit uses explicit on-demand startup: constructing a listener has no
system-wide effect, while `FeedListener::StartListening()` asks `BRoster` to
launch the server by its preserved `application/x-vnd.beclan.feed_server`
signature when necessary. It does not install a permanent `launch_daemon`
service.

After manually installing the HPKG, verify that the runtime library, server,
and parser add-ons are resolved from the activated package with:

```sh
make installed-package-check
```

The check deliberately fails if an old non-packaged `libfeedkit.so` shadows
the packaged library. Development builds register the same application
signature, so the check first verifies the packaged server's embedded
signature and selects its packagefs entry as the MIME application hint. It
then starts the server through `StartListening()`, checks the server and both
parser image paths, and requests clean shutdowns of the server and listener.
It does not install or uninstall packages.

To verify package removal without modifying real feeds or preferences, create
a uniquely named marker inside FeedKit's documented user settings directory:

```sh
make package-removal-prepare
```

Then uninstall the package and validate that packagefs removed the packaged
files without deleting the marker:

```sh
pkgman uninstall -y feedkit
make package-removal-check
```

The successful check removes only its own marker and state file. It never
deletes the FeedKit settings directory, preferences, feeds, or enclosures.

### HaikuPorter recipe

The pinned recipe is stored at
`packaging/haikuports/feedkit-0.1.0.recipe`. HaikuPorter is separate development
tooling and is not installed by default. Follow the official setup by cloning
both repositories, copying the sample configuration, and setting `PACKAGER`
and `TREE_PATH` in that configuration:

```sh
cd /boot/home
git clone https://github.com/haikuports/haikuporter.git --depth=50
git clone https://github.com/haikuports/haikuports.git haikuports-tree --depth=50
cp haikuporter/haikuports-sample.conf \
    /boot/home/config/settings/haikuports.conf
lpe /boot/home/config/settings/haikuports.conf
```

Copy the recipe into the configured HaikuPorts checkout, then run strict
source and recipe checks. Calling the tool by its full path avoids requiring
a global symlink:

```sh
HAIKUPORTS_TREE=/boot/home/haikuports-tree
mkdir -p "$HAIKUPORTS_TREE/net-libs/feedkit"
cp packaging/haikuports/feedkit-0.1.0.recipe \
    "$HAIKUPORTS_TREE/net-libs/feedkit/feedkit-0.1.0.recipe"
/boot/home/haikuporter/haikuporter -S --no-source-packages \
    --get-dependencies feedkit
```

The recipe is pinned to the source commit that passed x86_64 build, test,
HPKG activation, lifecycle, and removal checks. Strict native HaikuPorter
validation produces `feedkit`, `feedkit_debuginfo`, and `feedkit_devel`
packages successfully. The devel package contains the public headers and
development library link. On an x86_gcc2 hybrid installation the recipe
intentionally builds only for the modern secondary `x86` architecture.

The current native build requires the PCRE development package. On Haiku,
install it with:

```sh
pkgman install libpcre_devel
```

The RSS and Atom parser add-ons also require the libxml2 development package:

```sh
pkgman install libxml2_devel
```

On an x86_gcc2 hybrid system building for the modern secondary architecture,
use `libpcre_x86_devel` and run the build under the `x86` architecture. Use
`pkgman search libpcre` to see the packages available for the current system.

The default `make` target builds the core restoration scope. Optional clients
and the Preflet can be attempted separately with `make optional` while their
ports remain incomplete.

