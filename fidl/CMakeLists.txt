# fidl/CMakeLists.txt
# Produces the CommonAPI (core + dbus) stubs for RecoveryScheduler and
# exposes them as an OBJECT library named `srs_capi_stubs`.

commonapi_generate_stubs(TARGET srs_capi_stubs
    FIDL ${CMAKE_CURRENT_SOURCE_DIR}/RecoveryScheduler.fidl
    FDEPL ${CMAKE_CURRENT_SOURCE_DIR}/RecoveryScheduler-DBus.fdepl
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

# Make the runtime routing .ini available in the build tree next to binaries
# so tests/apps can just `export COMMONAPI_CONFIG=...`.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/commonapi4dbus.ini
    ${CMAKE_BINARY_DIR}/commonapi4dbus.ini
    COPYONLY)
