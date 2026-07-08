# syntax=docker/dockerfile:1.6
# ---------------------------------------------------------------------------
# service-recovery-scheduler container image.
#
# Multi-stage:
#   builder  -- compiles everything from source (uses scripts/bootstrap.sh
#               to pull the same host prereqs the developer flow expects),
#               then stages a DESTDIR install tree.
#   runtime  -- debian:12-slim + only the runtime libraries the daemon
#               needs. Ships the patched libdbus (built by CommonAPI.cmake)
#               under /opt/dbus-patched/lib so the COVESA-specific symbol
#               dbus_connection_send_with_reply_set_notify() is resolvable
#               at load time.
#
# Build:   docker build -t service-recovery-scheduler:local .
# Run:     docker run --rm -it service-recovery-scheduler:local
#          (add `-e SRS_CONSOLE=1` to attach the interactive query loop)
# ---------------------------------------------------------------------------

ARG DEBIAN_TAG=12-slim

# ---------------- builder ---------------------------------------------------
FROM debian:${DEBIAN_TAG} AS builder

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

# Copy just what bootstrap.sh needs first so this layer caches across
# source-only changes.
COPY scripts/bootstrap.sh scripts/bootstrap.sh
RUN chmod +x scripts/bootstrap.sh && scripts/bootstrap.sh

# patchelf lets us bake an RPATH into our own binaries pointing at the
# patched libdbus in /opt, without polluting the global library search
# path (which would break distro binaries like dbus-run-session that were
# linked against a newer libdbus with symversions our 1.12 build lacks).
RUN apt-get update \
    && apt-get install -y --no-install-recommends patchelf \
    && rm -rf /var/lib/apt/lists/*

# Now bring in the rest of the tree and build a release, no-test image.
COPY . .

RUN cmake -S . -B build -Wno-dev \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DENABLE_COVERAGE=OFF \
        -DENABLE_SANITIZERS=OFF \
    && cmake --build build -j "$(nproc)"

# Stage the install tree under /stage. CMAKE_INSTALL_PREFIX is forced to
# /usr by the top-level CMakeLists when the caller doesn't override it, so
# this produces /stage/usr/bin/app_srs, /stage/usr/lib/*.so*, and
# /stage/etc/app_srs/commonapi4dbus.ini.
RUN DESTDIR=/stage cmake --install build

# Ship the patched libdbus built in-tree so the CommonAPI D-Bus runtime can
# resolve dbus_connection_send_with_reply_set_notify at runtime. Kept in
# /opt to avoid colliding with the distro's /usr/lib libdbus-1.so.3.
RUN mkdir -p /stage/opt/dbus-patched/lib \
    && cp -a build/dbus-patched/lib/libdbus-1.so* /stage/opt/dbus-patched/lib/

# Bake /opt/dbus-patched/lib into RPATH of *our* binaries only, so the
# dynamic loader finds the patched libdbus for app_srs / appA-C /
# libCommonAPI-DBus.so, while the distro dbus-run-session keeps using
# /usr/lib/x86_64-linux-gnu/libdbus-1.so.3 (its own newer libdbus).
RUN set -eux; \
    for bin in /stage/usr/bin/app_srs /stage/usr/bin/appA /stage/usr/bin/appB /stage/usr/bin/appC; do \
        [ -f "$bin" ] && patchelf --force-rpath --set-rpath '$ORIGIN/../lib:/opt/dbus-patched/lib' "$bin"; \
    done; \
    for lib in /stage/usr/lib/libCommonAPI-DBus.so* /stage/usr/lib/libservice_recovery_scheduler.so*; do \
        if [ -f "$lib" ] && [ ! -L "$lib" ]; then \
            patchelf --force-rpath --set-rpath '$ORIGIN:/opt/dbus-patched/lib' "$lib"; \
        fi; \
    done

# ---------------- runtime ---------------------------------------------------
FROM debian:${DEBIAN_TAG} AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# dbus provides dbus-daemon + dbus-run-session (needed for the per-container
# session bus). libdbus-1-3 is a hard dependency of dbus-daemon itself; the
# patched libdbus we ship is loaded ahead of it for our own binaries via
# ld.so.conf.d below.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        dbus \
        libdbus-1-3 \
        libexpat1 \
        libsystemd0 \
        libstdc++6 \
        libgcc-s1 \
        ca-certificates \
        procps \
        tini \
    && rm -rf /var/lib/apt/lists/*

# Bring in everything staged by the builder: /usr/bin/app_srs,
# /usr/lib/libservice_recovery_scheduler*, /usr/lib/libCommonAPI*.so*,
# /etc/app_srs/commonapi4dbus.ini, /opt/dbus-patched/lib/*.
COPY --from=builder /stage/ /

# NOTE: we deliberately do NOT add /opt/dbus-patched/lib to
# /etc/ld.so.conf.d/. Doing so would make the patched (1.12) libdbus win
# ahead of /usr/lib/x86_64-linux-gnu/libdbus-1.so.3 (1.14) for every
# process in the container, and the distro dbus-run-session/dbus-daemon
# binaries fail with `version LIBDBUS_PRIVATE_1.14.x not found` because
# our older libdbus does not export those symversions. Instead, RPATH is
# baked into app_srs/appA/appB/appC/libCommonAPI-DBus.so during the
# builder stage (patchelf) so only our own binaries pick up the patched
# libdbus.

# Non-root user. app_srs doesn't need any elevated capability now that it
# talks to a per-container session bus.
RUN groupadd --system --gid 1000 srs \
    && useradd  --system --uid 1000 --gid srs --home /var/lib/srs \
                --shell /usr/sbin/nologin srs \
    && install -d -o srs -g srs /var/lib/srs

COPY docker/entrypoint.sh /usr/local/bin/srs-entrypoint
RUN chmod +x /usr/local/bin/srs-entrypoint

USER srs
WORKDIR /var/lib/srs

# CommonAPI runtime lookup: the config was installed via cpack rules into
# /etc/app_srs. The runtime consults both COMMONAPI_CONFIG and the *_DEFAULT
# variant, so set both for good measure.
ENV COMMONAPI_CONFIG=/etc/app_srs/commonapi4dbus.ini \
    COMMONAPI_DBUS_DEFAULT_CONFIG=/etc/app_srs/commonapi4dbus.ini \
    SRS_CONSOLE=0

# tini as PID 1 handles signal forwarding + zombie reaping for the
# dbus-daemon child that the entrypoint launches.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/srs-entrypoint"]
