# FeedKit Haiku R1/beta6 Compatibility Design

## Status

This document records the compatibility audit performed against Haiku
R1/beta6 and the staged plan for restoring FeedKit. The original audit
baseline did not build or run natively. As of 27 October 2026, the core
library, server, RSS and Atom parser add-ons build on beta6 x86_64, and the
113-check model, 55-check FileRequest, and 47-check parser suites pass. Phase 3
network/parser integration also passes registration, refresh, failure
isolation, persistence, restart, and clean-shutdown checks. Phase 4 packaging
work has started: the staged layout, content check, and initial x86_64 HPKG
generation pass natively. Explicit on-demand startup and installed-image
validation are implemented but still require native verification. HaikuPorter,
optional clients, and release validation remain and are tracked in
`README.md`.

The audit used:

- the repository at commit `456f8d748a34c726dc5641cc8e961a83c28ca295`;
- the official Haiku `r1beta6` source branch and SDK headers;
- the beta6 HaikuPorts package manifests; and
- static inspection of FeedKit's active Jam build, legacy makefiles, library,
  server, parsers, clients, preflet, and tests.

The original audit host was macOS and did not have a Haiku cross-toolchain.
Subsequent build and runtime findings have been confirmed on a native beta6
x86_64 installation as recorded in `README.md`.

## Compatibility verdict

FeedKit is not yet restored as a complete, packaged R1/beta6 product, but its
core is now buildable and testable without redesigning the fundamental
architecture. The principal Haiku APIs used by FeedKit still exist, including
`BApplication`, `BMessage`, `BMessenger`, `BFlattenable`, Storage Kit queries,
filesystem attributes, node monitoring, and add-ons.

The immediate blockers are:

1. a broken and packagefs-incompatible build system;
2. omission of the RSS and Atom parsers from the active build;
3. memory corruption and ownership defects in core code;
4. obsolete OpenSSL and libcurl interfaces; and
5. client integrations that depend on NetPositive, InfoPopper, or missing
   private headers.

The recommended first deliverable is a beta6-native core containing
`libfeedkit`, `feed_server`, the RSS and Atom parsers, and automated tests.
Optional clients should be ported only after that baseline works reliably.

## Supported platform target

The initial support matrix should be:

- Haiku R1/beta6 x86_64; and
- the modern `x86` secondary architecture on an x86_gcc2 hybrid beta6 system.

Building the restored project as native GCC 2 code is not recommended. Public
FeedKit headers expose C++ standard-library containers, and the existing build
tries to select `libstdc++` by probing obsolete filesystem paths. All FeedKit
components and parser add-ons must use a matching compiler architecture and
C++ ABI.

Beta6's x86_64 package repository includes GCC 13, curl 8.21, OpenSSL 3.5,
PCRE 8.45, and libxml2. These versions form the dependency baseline for the
port.

## Audit findings

### P0: build and packaging blockers

#### The Jam rules do not parse cleanly

`build/jam/CheckRules` contains malformed conditions at lines 69 and 116:

```jam
if ($(PCRE_INCLUDE_DIR) {
...
if ($(OPENSSL_INCLUDE_DIR) {
```

These must be corrected before Jam can reliably evaluate dependency checks.

#### Configuration uses directory constants removed from current Haiku

`configure` invokes `finddir` with these obsolete names:

- `B_COMMON_DIRECTORY`
- `B_COMMON_BIN_DIRECTORY`
- `B_COMMON_LIB_DIRECTORY`
- `B_COMMON_SERVERS_DIRECTORY`
- `B_COMMON_ADDONS_DIRECTORY`
- `B_COMMON_DEVELOP_DIRECTORY`

They are absent from beta6's `FindDirectory.h`. Modern Haiku provides
`B_SYSTEM_*`, `B_SYSTEM_NONPACKAGED_*`, `B_USER_*`, and
`B_USER_NONPACKAGED_*` locations. A Haiku package must use the directories
provided by HaikuPorter rather than hard-coded `/boot` paths.

The dependency checks also assume pre-packagefs layouts such as
`/boot/develop/lib/x86`. On x86_gcc2 hybrid systems this can select the wrong
C++ runtime and ABI.

#### Dependency detection contains copy-and-paste defects

`CheckPCRE` looks for `ssl.h` inside the PCRE include directory. `CheckOpenSSL`
tests for `libssl`, while FeedKit's UUID code links against `libcrypto`.
`CheckCURL` tests only for a header and does not verify that the library can be
linked.

Dependency presence should be declared in a HaikuPorter recipe and confirmed
by an actual compile/link check where a standalone source build needs one.

#### The active build excludes required components

The root `Jamfile` builds only:

- `Common`;
- `libfeedkit`; and
- `Server`.

The Parsers, Clients, Preflet, and Tests subdirectories are commented out.
`feed_server` dynamically loads parser add-ons, so a build without at least the
RSS and Atom parsers cannot perform its primary function.

#### Library installation rules reference unrelated files

`libfeedkit/Jamfile` attempts to install these nonexistent IMKit headers:

- `Constants.h`
- `Contact.h`
- `Helpers.h`
- `Manager.h`
- `Protocol.h`

It also references undefined `COMMON_INC_DIRECTORY` and
`IMKIT_COMMON_INC_DIRECTORY` variables. The actual public FeedKit headers need
to be enumerated and installed under a FeedKit-specific include directory.

#### Server link flags are malformed or ineffective

`Server/Jamfile` applies `LINKFLAGS on += ...` without naming a target and
checks `PLATFORM` even though the rest of the build checks `OS`. The server
does not directly use the socket API, so obsolete `socket` and `bind` library
handling should be removed unless an actual dependency is demonstrated.

#### The legacy makefiles are unsafe as the maintained build path

The recursive makefiles hard-code old BeOS and Haiku paths, explicitly link
`stdc++.r4`, install into former common directories, and contain destructive
installation steps such as removing an entire add-on directory. They should
not be used for beta6 installation.

They should remain in the repository until feature parity is achieved, but be
marked unsupported and then retired in a focused cleanup change.

### P0: core correctness defects

#### UUID generation writes past a stack buffer

Five classes allocate `char buffer[2]` and then call:

```cpp
sprintf(buffer, "%02x", hash[i]);
```

The call writes two hexadecimal characters plus a null terminator, requiring
at least three bytes. The defect exists in:

- `Feed`;
- `Channel`;
- `Item`;
- `Content`; and
- `Enclosure`.

This code should be consolidated into one helper. Its output must remain
byte-for-byte compatible with existing persisted UUIDs. Known-value tests must
be written before changing the implementation.

#### Enclosure progress updates are discarded

`Enclosure::SetProgress()` deletes the current object, clears the pointer, and
never stores or copies the supplied progress object. The existing
`ProgressDeletion` test does not assert useful behaviour and leaks its second
allocation.

Ownership semantics must be defined explicitly. The preferred implementation
is value-like copying so that an `Enclosure` owns its private
`DownloadProgress` instance.

#### Array memory is released with scalar delete

`RegEx` allocates `ovector` with `new[]` but destroys it using scalar `delete`.
This must become `delete[]` or, once compiler compatibility is established, a
container with automatic ownership.

#### FeedListener exposes invalid pointers and unsafe removal

`FeedListener::ChannelIconPath()` returns a `const char*` owned by a local
`BMessage`; the pointer is invalid after the function returns.

`FeedListener::RemoveHandler()` erases the result of `std::find()` without
checking for `end()`, which is undefined behaviour when the handler was not
registered.

The icon-path API is public. Changing its return type would be an API change,
so the first fix should preserve the signature using listener-owned storage,
with a safer overload considered for a future version.

#### Server ownership and initialization need an audit

Several server pointers are created only from `ReadyToRun()` but are deleted
or used elsewhere. Constructors should initialize every pointer, and cleanup
should be complete and safe after partial initialization. This includes curl
global state, message runners, query loopers, the volume roster, download
daemon, and parser manager.

### P1: beta6 API and dependency compatibility

#### Haiku is selected as the BeOS R5 code path

Beta6 intentionally defines `B_BEOS_VERSION` as `B_BEOS_VERSION_5` for source
compatibility. Conditions such as:

```cpp
#if B_BEOS_VERSION > B_BEOS_VERSION_5
```

therefore do not identify Haiku. In `Common/IMKitUtilities.cpp` this selects
manual reading of legacy bitmap icon attributes and misses modern HVIF icons.

Haiku-specific code should use `__HAIKU__` and the public
`BNodeInfo::GetTrackerIcon()` API. Similar version checks in UI colour setup
should be simplified to the current Haiku API while preserving any genuinely
needed BeOS fallback.

Beta6 still supplies `<be/...>` through a compatibility symlink, but Haiku's
own package definition describes that symlink as temporary. Includes should
be migrated to standard public headers such as `<Application.h>` and
`<MenuItem.h>`.

#### Direct SHA-1 calls are deprecated by OpenSSL 3

FeedKit uses the one-shot `SHA1()` interface solely to derive stable object
identifiers. Under beta6's OpenSSL 3 this interface is deprecated.

Use OpenSSL EVP or an equivalent maintained interface while preserving the
SHA-1 algorithm and exact serialized UUID values. Changing existing UUIDs
would disconnect persisted feeds, channels, items, contents, and enclosures.

#### The curl progress callback is deprecated

`FileRequest` uses `CURLOPT_PROGRESSFUNCTION`, `double` byte counts, and
`CURLOPT_PROGRESSDATA`. It should use:

- `CURLOPT_XFERINFOFUNCTION`;
- `CURLOPT_XFERINFODATA`; and
- `curl_off_t` counters.

The write callback must preserve a negative `BFile::Write()` result rather
than converting it to a very large unsigned value. All `curl_easy_setopt()`
and file initialization results should be checked.

#### Temporary-file creation is unsafe

`FileRequest` calls `tmpnam(NULL)`. Replace it with `mkstemp()` in the Haiku
system temporary directory and ensure that error paths close and remove the
temporary file appropriately.

#### Parser discovery is not package-aware

`ParserManager` searches only `B_USER_ADDONS_DIRECTORY/feed_kit/Parsers` unless
an explicit path is supplied. A packaged server must discover parser add-ons
in the system package location and should also allow user non-packaged
overrides.

The search order and duplicate-name behaviour must be documented and tested.
A suitable order is:

1. user non-packaged add-ons;
2. user packaged/config add-ons where applicable; and
3. system packaged add-ons.

### P2: optional client compatibility

#### Anchorman depends on a nonfunctional NetPositive replicant

Anchorman archives an `NPBaseView` replicant with the NetPositive application
signature and sends `B_NETPOSITIVE_OPEN_URL` to it. Beta6 retains the
NetPositive constants only for compile-time compatibility and explicitly
states that WebPositive does not implement these commands.

For the initial client port, display feed content in a safe native text view
and open external links through the user's preferred browser. HaikuWebKit can
be evaluated later as an optional feature, not as a requirement for restoring
the core.

#### InfoPopper is an unavailable external dependency

FeedInfoPopper and FeedTracer include `libim/InfoPopper.h`, which is not part
of the beta6 SDK or this repository. Replace this integration with Haiku's
notification API or omit these sample clients from the first package.

#### Some sample clients reference missing headers

`Clients/EmptyListener` includes `ChannelInfo.h` and
`ServerObjectFactory.h`, which are not present. The bundled column-list code
includes unavailable `archive_defs.h`. Each client needs an independent
compile audit rather than being enabled wholesale.

## Implementation plan

### Phase 1: establish a beta6 build baseline

1. Create and document a clean beta6 x86_64 build environment.
2. Select one maintained source-build path. The preferred low-risk route is
   to repair the existing Jam build while removing its obsolete install and
   package rules.
3. Add a HaikuPorter recipe for package-aware dependency resolution and
   installation paths.
4. Declare build dependencies for Haiku development headers, Jam/compiler,
   curl, OpenSSL/libcrypto, PCRE, libxml2, and the test framework.
5. Build `libcommon`, `libfeedkit`, `feed_server`, RSS, Atom, and tests.
6. Install the actual public FeedKit headers, library, server, parsers,
   resources, and documentation into recipe-provided directories.
7. Add a `README.md` containing exact dependency, build, test, and package
   commands.
8. Keep clients and the preflet disabled until the core acceptance criteria
   pass.

Acceptance criteria:

- the documented source build exits successfully on beta6 x86_64;
- no hard-coded `/boot/home/config/lib`, `/boot/develop`, or common-directory
  paths are required;
- RSS and Atom parser add-ons are present in the output;
- tests are built and executed by the documented validation command; and
- the installable package contains no missing or unrelated IMKit files.

### Phase 2: remove core undefined behaviour

1. Add UUID fixtures for representative Feed, Channel, Item, Content, and
   Enclosure objects.
2. Introduce a shared SHA-1-to-hex helper using a maintained crypto API and a
   correctly sized output buffer.
3. Correct `RegEx` array destruction.
4. Define and repair `Enclosure::SetProgress()` ownership.
5. Fix `FeedListener` pointer lifetime, handler removal, and null checking in
   reconstructed object graphs.
6. Initialize all server members and make partial-startup cleanup safe.
7. Remove debugging output from normal library code or place it behind a
   consistent debug facility.

Acceptance criteria:

- UUID fixtures retain their pre-fix values;
- flatten/unflatten round trips pass for all public model classes;
- progress state survives copying and serialization;
- removing an unknown handler is harmless; and
- focused tests run without known leaks or invalid memory accesses.

### Phase 3: modernize network and parser operation

1. Replace `tmpnam()` with secure temporary-file creation.
2. Migrate curl progress reporting to the xfer-info API and 64-bit counters.
3. Validate curl setup, HTTP status handling, redirects, cancellation, resume
   behaviour, TLS verification, and file-write failures.
4. Make parser discovery package-aware.
5. Update RSS and Atom builds for beta6's libxml2 package.
6. Add valid, malformed, missing-field, and encoding-focused feed fixtures.
7. Exercise server startup, registration, refresh, persistence, and clean
   shutdown against a local HTTP endpoint.

Acceptance criteria:

- a local RSS feed and Atom feed can be registered and refreshed;
- malformed feeds fail without crashing or corrupting stored data;
- redirect, cancellation, and failed-write paths return meaningful errors;
- parser add-ons load from an installed package; and
- restarting the server preserves feeds and stable UUIDs.

### Phase 4: packagefs and lifecycle integration

1. Package the server and parser add-ons in standard Haiku locations.
2. Verify application MIME signature registration and discovery by
   `BMessenger`/`BRoster`.
3. Decide whether the server should be launched on demand, through
   `launch_daemon`, or by an explicit client action; document the selected
   lifecycle.
4. Preserve the existing application signature and `BeClan/FeedKit` settings
   path for the first compatible release.
5. Test BFS query/index behaviour on writable BFS volumes and graceful
   handling of read-only or non-query volumes.
6. Produce an HPKG through the repository-owned validation workflow.

Acceptance criteria:

- installation and removal do not modify files outside package-managed or
  documented user-data locations;
- a clean beta6 system can discover and start the server;
- existing settings and stored feeds remain readable; and
- uninstalling the package does not delete user data.

The selected lifecycle is explicit on-demand startup. Creating a
`FeedListener` remains passive; calling `StartListening()` launches the server
through `BRoster` using the preserved application signature if it is not
already running. This matches client intent without keeping an unused daemon
resident or adding a new boot-time service.

### Phase 5: port clients independently

Suggested order:

1. command-line or single-purpose utilities;
2. FeedKit preferences;
3. Deskbar integration;
4. notification client using Haiku notifications; and
5. Anchorman.

Anchorman should initially replace the NetPositive replicant with a native
content view and external-browser link handling. The embedded legacy
column-list implementation should be evaluated against Haiku's current
column-list library before investing in extensive repairs.

Each client should be optional in the build until it compiles, launches, and
passes its own smoke test. Missing historical dependencies should not block
the core package.

### Phase 6: release validation

The final beta6 validation matrix is:

| Environment | Build | Unit tests | Integration | HPKG install |
| --- | --- | --- | --- | --- |
| R1/beta6 x86_64 | Required | Required | Required | Required |
| R1/beta6 x86_gcc2h, modern `x86` secondary architecture | Required | Required | Required | Required |

The repository-owned validation entry point should perform the build, tests,
package generation, and package-content checks. Publishing or tagging remains
a separate, explicitly authorized action.

Before release, review the final diff and artifacts for:

- accidental generated files;
- hard-coded developer paths;
- debug output;
- missing runtime or development dependencies;
- incorrect architecture declarations;
- public API or serialization changes;
- lost resource attributes or MIME signatures; and
- user-data deletion during package removal.

## Compatibility constraints

The restoration should preserve these behaviours unless a separately
documented migration is approved:

- persisted UUID values;
- `BFlattenable` type codes and serialized message field names;
- the server application signature;
- stored feed and settings locations;
- public FeedKit method signatures; and
- parser add-on symbol names and object ownership expectations.

Breaking public APIs should be deferred until after a working beta6-compatible
release. Where a safer API is needed, add it alongside the legacy entry point,
document ownership and lifetime, and deprecate the old API before removal.

## Upstream references

- [Haiku R1/beta6 release notes](https://github.com/haiku/website/blob/master/content/get-haiku/r1beta6/release-notes.md)
- [Haiku R1/beta6 `FindDirectory.h`](https://github.com/haiku/haiku/blob/r1beta6/headers/os/storage/FindDirectory.h)
- [Haiku R1/beta6 development package definition](https://github.com/haiku/haiku/blob/r1beta6/build/jam/packages/HaikuDevel)
- [Haiku R1/beta6 x86_64 package manifest](https://github.com/haiku/haiku/blob/r1beta6/build/jam/repositories/HaikuPorts/x86_64)
- [Haiku R1/beta6 NetPositive compatibility header](https://github.com/haiku/haiku/blob/r1beta6/headers/os/be_apps/NetPositive/NetPositive.h)
- [HaikuPorter recipe documentation](https://github.com/haikuports/haikuports/wiki/HaikuPorter-BuildRecipes)

