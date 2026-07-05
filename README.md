# service-recovery-scheduler
A small C++ library that manages recovery actions for a set of monitored services,
exposed to other processes as a **CommonAPI D-Bus** service.


#Assumptions
- Modern app => modern c++ (> = c++11 is preferred), choosing c++14
- Prefered Environment => Linux
- Apps are connected to the scheduler over dbus


#decisions
-Common api shall be downloaded and compiled for this project


## Layout

```
CMakeLists.txt          Top-level build; includes cmake/CommonAPI.cmake.
cmake/CommonAPI.cmake   Self-contained CommonAPI setup (see below).
fidl/                   Franca IDL + D-Bus deployment + runtime .ini.
lib_srs/                Core scheduler library + CommonAPI service skeleton.
other_apps/             appA is a working D-Bus client; appB/C are placeholders.
main.cpp                Runs the scheduler and registers it on the session bus.
```

## CommonAPI setup (self-contained)

Everything lives in-tree — no system install of CommonAPI is needed.
On the first `cmake` configure, `cmake/CommonAPI.cmake` will:

1. **FetchContent** the runtimes from source into `build/_deps/`:
   - `capicxx-core-runtime` (tag `CAPICXX_CORE_RUNTIME_TAG`, default `3.2.4`)
   - `capicxx-dbus-runtime` (tag `CAPICXX_DBUS_RUNTIME_TAG`, default `3.2.3-r1`)
2. **Download** the pre-built Eclipse RCP generators into
   `build/_deps/commonapi-tools/`:
   - `commonapi_core_generator` (`CAPICXX_CORE_TOOLS_VERSION`, default `3.2.15`)
   - `commonapi_dbus_generator` (`CAPICXX_DBUS_TOOLS_VERSION`, default `3.2.15`)
3. Expose a helper `commonapi_generate_stubs(TARGET ... FIDL ... FDEPL ...)` that
   runs both generators against `fidl/RecoveryScheduler.fidl` and
   `fidl/RecoveryScheduler-DBus.fdepl` and turns the generated C++ into
   an OBJECT library named `srs_capi_stubs`.

Override any version on the command line:

```bash
cmake -S . -B build \
  -DCAPICXX_CORE_RUNTIME_TAG=3.2.4 \
  -DCAPICXX_DBUS_RUNTIME_TAG=3.2.3-r1 \
  -DCAPICXX_CORE_TOOLS_VERSION=3.2.15 \
  -DCAPICXX_DBUS_TOOLS_VERSION=3.2.15
```

### Host prerequisites

- C++17 compiler (`g++`/`clang++`), `cmake >= 3.16`, `pkg-config`
- `libdbus-1-dev` (Ubuntu/Debian) or equivalent
- A Java 11+ runtime on `$PATH` (needed only to launch the generators)
- Internet access on the first configure

On a fresh Ubuntu 24.04 host:

```bash
scripts/bootstrap.sh            # installs the packages listed below via apt
# or run scripts/bootstrap.sh --check to see what is missing without installing
```

Under the hood this installs:

```bash
sudo apt install build-essential cmake pkg-config libdbus-1-dev \
                 libsystemd-dev default-jre-headless unzip git
```

## Build & run

```bash
# Configure + build (fetches runtimes & generators on first run)
cmake -S . -B build
cmake --build build -j

# Tell the CommonAPI runtime how to route RecoveryScheduler over D-Bus
export COMMONAPI_CONFIG=$PWD/fidl/commonapi4dbus.ini
export COMMONAPI_DBUS_DEFAULT_CONFIG=$PWD/fidl/commonapi4dbus.ini

# Terminal 1: run the scheduler (server)
./build/app_srs

# Terminal 2: run a client
./build/other_apps/appA
```

## What talks to what

- `lib_srs::CRecoveryScheduler` — the core in-process scheduler (unchanged
  behaviour).
- `lib_srs::RecoverySchedulerStubImpl` — thin D-Bus skeleton that inherits
  from the generated `RecoverySchedulerStubDefault` and delegates
  `registerService` / `unregisterService` to `CRecoveryScheduler`, and fires
  the `serviceStateChanged` broadcast.
- `other_apps/appA.cpp` — reference client. Builds a
  `RecoverySchedulerProxy`, subscribes to `serviceStateChanged`, and
  calls `registerService("AppA", ...)` remotely.

## Regenerating stubs

Stub generation is a normal CMake dependency of `srs_capi_stubs`: touching
either `.fidl` or `.fdepl` file triggers regeneration on the next build. To
force it manually:

```bash
cmake --build build --target srs_capi_stubs_generate
```

## SOME/IP note

The legacy `fidl/RecoveryScheduler.fdepl` (SOME/IP) is kept for
reference but is not wired into the build. Switch to SOME/IP by adding the
matching runtime (`capicxx-someip-runtime` + `vsomeip3`) in
`cmake/CommonAPI.cmake` and pointing `commonapi_generate_stubs` at the
SOME/IP `.fdepl`.

