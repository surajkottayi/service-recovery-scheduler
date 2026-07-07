# Security policy

## Reporting a vulnerability

Please do **not** open a public GitHub issue for security-sensitive reports.
Instead, email the maintainers at **security@example.invalid** with:

- A description of the issue and its impact.
- Steps to reproduce (a proof-of-concept is welcome but not required).
- The commit / tag you tested against.

You will receive an acknowledgement within 3 business days and a first
assessment within 10 business days. Fixes are coordinated privately;
disclosure is agreed with the reporter before any public advisory.

## Supported versions

Only the latest tagged release on the `main` branch receives security fixes.

## Trust boundaries

`app_srs` runs as a D-Bus service. The following inputs are treated as
untrusted and must be validated by the daemon:

- Method arguments over D-Bus (`registerService`, `unregisterService`,
  `reportServiceState`) — including caller-provided service names, PIDs,
  and recovery-action vectors.
- Bus name / unique-name strings from D-Bus signals (`NameOwnerChanged`).
- Any config file consumed at startup (`recoveryConfig.Json`, etc.).

Any parsing regressions or missing bounds checks along these paths are
in-scope for this policy.

## Out of scope

- Vulnerabilities in the third-party components tracked in `NOTICE`
  (CommonAPI, libdbus, GoogleTest) — please report those upstream.
- Denial-of-service by a caller with legitimate access to the same
  session/system D-Bus bus.
