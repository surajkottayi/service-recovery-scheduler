# Contributing

Thanks for taking the time to contribute.

## Ground rules

- Discuss significant changes in an issue before opening a PR.
- Every PR must build cleanly and keep unit tests green.
- Follow the coding style enforced by `.clang-format` and `.clang-tidy`.
- One logical change per commit. Write a good subject line and body.

## Development workflow

```bash
# clone + first build
git clone <repo>
cd service-recovery-scheduler
./build_srs.sh 1

# run tests + coverage report
./build_srs.sh 2

# sanitizer run before shipping
./build_srs.sh 3
```

## Coding style

- C++17. `.clang-format` is authoritative; run `clang-format -i` on any
  file you touch.
- Warnings are errors in CI: `-Wall -Wextra -Wpedantic -Wshadow`
  `-Wconversion -Wsign-conversion -Wold-style-cast`.
- Prefer `[[nodiscard]]`, `enum class`, and `constexpr` where sensible.
- No raw `new`/`delete` in application code; use `std::make_unique` /
  `std::make_shared`.
- Any state touched from more than one thread must be guarded by a mutex
  or documented as thread-safe (atomic, immutable, thread-local, etc.).

## Tests

- Unit tests live under `gtest/src/`. Add a test for every bug you fix
  and every branch you add.
- Do not test the D-Bus surface with mocked live connections in unit
  tests; add integration tests under `dbus-run-session` instead.
- Coverage must not regress. `./build_srs.sh 2` produces the report at
  `build/coverage/index.html`.

## Commit sign-off

By contributing you agree to license your changes under the project's
MIT license (see `LICENSE`).

Sign your commits with `git commit -s` to indicate agreement.
