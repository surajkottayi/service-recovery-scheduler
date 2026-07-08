# service-recovery-scheduler
A small C++ library and daemon that manages recovery actions for a set of
monitored services, exposed to other processes as a **CommonAPI D-Bus**
service.

## Assumptions
- C++17 (compiled with `-std=c++17`, no GNU extensions).
- Linux host; systemd is expected as the process supervisor.
- Client apps talk to the scheduler over D-Bus.

## Design decisions
- CommonAPI (core + D-Bus) is fetched and built in-tree — no system install
  required (see `cmake/CommonAPI.cmake`).
- Clients report state to SRS whenever their state changes; SRS observes
  liveness via D-Bus `NameOwnerChanged`.
- Recovery actions are signal-based (`SIGTERM` for `RESTART`, `SIGKILL` for
  `STOP`/`DISABLE`); the process supervisor decides whether/how to restart.
  A systemd-unit-name path is planned for a future FIDL extension.

## Layout

```
CMakeLists.txt          Top-level build; includes cmake/CommonAPI.cmake.
cmake/CommonAPI.cmake   Self-contained CommonAPI setup (see below).
fidl/                   Franca IDL + D-Bus deployment + runtime .ini.
lib_srs/                Core scheduler library + CommonAPI service skeleton.
gtest/                  GoogleTest suite for lib_srs (built when BUILD_TESTING=ON).
other_apps_dummy/       appA is a working D-Bus client; appB/C are placeholders.
scripts/gen_report.py   Generates HTML coverage + test-result dashboard.
systemd/                systemd unit file installed by cpack.
build_srs.sh            Interactive build / UT / sanitize / package helper.
main.cpp                Runs the scheduler and registers it on the session bus.
```

## Build script (`./build_srs.sh`)

One entry point for all common workflows. Run from the repo root.

```bash
./build_srs.sh          # interactive menu
./build_srs.sh 1        # option 1: build only
./build_srs.sh 2        # option 2: build + run unit tests + HTML coverage report
./build_srs.sh 3        # option 3: build with ASan+UBSan and run tests
./build_srs.sh 4        # option 4: release build + cpack (TGZ / DEB)
./build_srs.sh 5        # option 5: Docker
./build_srs.sh -h       # help
```
##on Terminal 1
```bash

pwd = <path>/service-recovery-scheduler

cd build
eval "$(dbus-launch --sh-syntax)"
echo "$DBUS_SESSION_BUS_ADDRESS" > /tmp/srs-bus     # share with other shells
export COMMONAPI_CONFIG=$PWD/../fidl/commonapi4dbus.ini
export COMMONAPI_DBUS_DEFAULT_CONFIG=$PWD/fidl/commonapi4dbus.ini

./app_srs
or
./app_srs mode=console (to get service state)


##on Terminal 2,3,4..

```bash

cd build/other_apps_dummy

export DBUS_SESSION_BUS_ADDRESS="$(cat /tmp/srs-bus)"
export COMMONAPI_CONFIG=$PWD/../../fidl/commonapi4dbus.ini
export COMMONAPI_DBUS_DEFAULT_CONFIG=$COMMONAPI_CONFIG
./appA
-send signal Ctrl+c or Ctrl +\
./appB
-send signal Ctrl+c or Ctrl +\
./appC
-send signal Ctrl+c or Ctrl +\

## Alternatively (docker)
```bash
./build_srs.sh 5    


- **Option 1 — Build.** Configures with `-DENABLE_COVERAGE=OFF -DBUILD_TESTING=ON` and builds every target (`app_srs`, `appA/B/C`, `lib_srs_tests`).
- **Option 2 — UT.** Configures with `-DENABLE_COVERAGE=ON`, builds, runs `ctest --output-on-failure`, then produces `build/coverage/index.html` — a dashboard with overall coverage %, per-file drill-downs (green/red annotated source), and a table of every gtest case. Open the file in any browser.
- **Option 3 — Sanitize.** Configures with `-DENABLE_SANITIZERS=ON`, builds, and runs the full test suite under AddressSanitizer + UBSan (`halt_on_error=1`).
- **Option 4 — Package.** Release configure, build, then `cpack -G TGZ;DEB`. Artefacts land in `build/*.tar.gz` (and `build/*.deb` when `dpkg` is available).

Switching between options auto-cleans `build/` (compile flags change under FetchContent'd third-party trees, which is unsafe to reconfigure in-place). Repeat runs of the same option are incremental.

Requires only the host prerequisites listed below plus `gcov` (bundled with gcc). No `lcov`/`gcovr` needed — the report generator is pure-stdlib Python.


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

The fastest path is the wrapper script (see above): `./build_srs.sh 1`.
The equivalent manual invocation:

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
./build/other_apps_dummy/appA
```

## Tests & coverage

Run the tests + generate the HTML dashboard in one command:

```bash
./build_srs.sh 2
xdg-open build/coverage/index.html
```

Or manually:

```bash
cmake -S . -B build -DENABLE_COVERAGE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --build build --target coverage
```

## Deployment (systemd)

`cpack` (`./build_srs.sh 4`) produces a `.deb` and/or `.tar.gz` that installs:

| File                                             | Purpose                              |
| ------------------------------------------------ | ------------------------------------ |
| `/usr/bin/app_srs`                               | The daemon binary                     |
| `/usr/lib/libservice_recovery_scheduler.so.*`    | Runtime library                       |
| `/usr/include/service_recovery_scheduler/*.hpp`  | Public headers                        |
| `/etc/app_srs/commonapi4dbus.ini`                | CommonAPI D-Bus runtime config        |
| `/etc/service_recovery_scheduler/recoveryConfig.Json` | Per-service recovery config (stub) |
| `/usr/lib/systemd/system/app_srs.service`        | Hardened systemd unit                 |
| `/usr/share/doc/app_srs/LICENSE`                 | License text                          |

Enable the daemon:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now app_srs.service
journalctl -u app_srs -f
```

## Security

See [SECURITY.md](SECURITY.md) for the vulnerability-reporting policy.
Trust boundaries and hardened systemd sandboxing are documented there.

## License

MIT — see [LICENSE](LICENSE). Third-party components pulled in at build
time carry their own licenses; see [NOTICE](NOTICE).

