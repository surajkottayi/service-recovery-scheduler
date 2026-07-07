# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Signal-based recovery action executor (`RESTART` → SIGTERM, `STOP`/`DISABLE` → SIGKILL) with `DISABLE` also collapsing the action ring so subsequent failures do not restart the service.
- Graceful shutdown on `SIGINT`/`SIGTERM`; `SIGPIPE` ignored to survive dead clients.
- CMake `install()` rules for the daemon, library, headers, config, and systemd unit; CPack recipe for TGZ + DEB.
- `systemd/app_srs.service` unit with hardening (`NoNewPrivileges`, `Protect*`, capability bounding set = `CAP_KILL`).
- `ENABLE_SANITIZERS` CMake option (ASan + UBSan).
- `./build_srs.sh 3` (sanitizers) and `./build_srs.sh 4` (packages).
- LICENSE (MIT), NOTICE, SECURITY.md, CONTRIBUTING.md, AUTHORS, `.editorconfig`, `.clang-tidy`.
- GitHub Actions CI: build + tests + coverage on every push/PR.

### Changed
- `CRecoveryScheduler::onRegisterService`: single critical section covers the
  check-then-insert (previously TOCTOU).
- Recovery execution moved outside the lock to avoid blocking queries during
  signal delivery / (future) systemd RPCs.

### Known limitations
- Recovery uses raw signals; there is no `systemd RestartUnit` integration
  yet (would require the client to declare its systemd unit name at register
  time — deferred pending FIDL extension).
- `recoveryConfig.Json` is present but not loaded at runtime — schema and
  loader to be added in a follow-up.
- D-Bus surface of `CRecoverySchedulerStubImpl` (peer-loss / signal-message
  paths) is not covered by unit tests; integration tests under
  `dbus-run-session` are planned.

## [1.0.0]
- Initial internal drop.
