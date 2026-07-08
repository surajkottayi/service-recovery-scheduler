#!/usr/bin/env bash
# Container entrypoint for service-recovery-scheduler.
#
# The daemon talks over a D-Bus session bus (see fidl/commonapi4dbus.ini).
# In a container we can't rely on a host bus, so we spawn a private session
# bus per container and export DBUS_SESSION_BUS_ADDRESS before exec'ing the
# daemon.
#
# Env:
#   SRS_CONSOLE=1              -> run app_srs with the interactive console loop
#   SRS_ARGS=...               -> extra positional args forwarded to app_srs
#   SRS_LAUNCH_CLIENTS="a b"   -> whitespace-separated list of installed client
#                                 binaries (e.g. "appA appB appC") to run
#                                 alongside app_srs on the same session bus so
#                                 you can observe them communicating in one
#                                 container. Default: none.
#   SRS_CRASH_AFTER="N:client" -> after N seconds, SIGKILL the named client
#                                 binary (must be in SRS_LAUNCH_CLIENTS). Used
#                                 for demoing crash detection: the D-Bus
#                                 daemon then emits NameOwnerChanged with an
#                                 empty new_owner, app_srs marks the peer as
#                                 CRASHED and increments crashCount.
#                                 Example: SRS_CRASH_AFTER="6:appA"

set -euo pipefail

# Extra CLI knobs, e.g. `docker run ... -e SRS_ARGS="mode=console"`.
srs_args=()
if [[ -n "${SRS_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    srs_args=($SRS_ARGS)
fi
if [[ "${SRS_CONSOLE:-0}" == "1" ]]; then
    srs_args+=("mode=console")
fi

# Runs inside the private session bus created by dbus-run-session. Starts the
# daemon, then any requested client processes, and waits on the daemon PID so
# the container's exit status reflects the service itself. Every child
# inherits DBUS_SESSION_BUS_ADDRESS from the enclosing dbus-run-session, and
# COMMONAPI_CONFIG is preset in the image so all CommonAPI runtimes load the
# same [local:...] routing.
run_stack() {
    /usr/bin/app_srs "${srs_args[@]}" &
    local srs_pid=$!
    echo "[srs-entrypoint] app_srs pid=$srs_pid"

    # Give the daemon a moment to register its D-Bus name before the clients
    # start proxying. app_srs's isAvailable() polling handles slower races.
    sleep 1

    local client_pids=()
    for client in ${SRS_LAUNCH_CLIENTS:-}; do
        if ! command -v "$client" >/dev/null 2>&1; then
            echo "[srs-entrypoint] warning: '$client' not installed in image, skipping" >&2
            continue
        fi
        echo "[srs-entrypoint] launching $client"
        "$client" &
        client_pids+=($!)
    done

    # Optional fault injection: SRS_CRASH_AFTER="6:appA" -> after 6s, SIGKILL
    # every process named 'appA'. SIGKILL (not SIGTERM) simulates a real crash:
    # the D-Bus daemon detects socket closure and emits NameOwnerChanged, which
    # is what CRecoverySchedulerStubImpl subscribes to for CRASHED accounting.
    if [[ -n "${SRS_CRASH_AFTER:-}" ]]; then
        local delay="${SRS_CRASH_AFTER%%:*}"
        local target="${SRS_CRASH_AFTER#*:}"
        if [[ -z "$delay" || -z "$target" || "$delay" == "$SRS_CRASH_AFTER" ]]; then
            echo "[srs-entrypoint] SRS_CRASH_AFTER=\"$SRS_CRASH_AFTER\" is not in the expected 'N:client' form, ignoring" >&2
        else
            (
                sleep "$delay"
                echo "[srs-entrypoint] ---- fault injection: SIGKILL '$target' (t+${delay}s) ----"
                pkill -KILL -x "$target" \
                    && echo "[srs-entrypoint] ---- killed; watch app_srs for CRASHED / NameOwnerChanged ----" \
                    || echo "[srs-entrypoint] no process named '$target' found; is it in SRS_LAUNCH_CLIENTS?" >&2
            ) &
        fi
    fi

    trap 'kill -TERM "$srs_pid" ${client_pids[@]:-} 2>/dev/null || true' TERM INT
    wait "$srs_pid"
    local rc=$?
    kill -TERM "${client_pids[@]:-}" 2>/dev/null || true
    exit "$rc"
}

# Export the helper so the subshell dbus-run-session spawns can invoke it.
export -f run_stack
export SRS_LAUNCH_CLIENTS SRS_ARGS SRS_CONSOLE SRS_CRASH_AFTER

# dbus-run-session starts a fresh dbus-daemon --session, exports
# DBUS_SESSION_BUS_ADDRESS, execs the given command, and reaps the daemon
# when the child exits. It's the minimal, systemd-free way to get a bus.
exec dbus-run-session -- bash -c 'run_stack "$@"' _ "${srs_args[@]}"
