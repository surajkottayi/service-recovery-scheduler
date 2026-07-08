cmake_minimum_required(VERSION 3.16)
project(app_srs
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "A runnable entry point which uses the srs library to schedule service recovery tasks")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Options declared here so they are visible to every subdirectory.
option(BUILD_TESTING     "Build lib_srs unit tests"                              ON)
option(ENABLE_COVERAGE   "Instrument lib_srs and lib_srs_tests for gcov coverage" OFF)
option(ENABLE_SANITIZERS "Build with -fsanitize=address,undefined"               OFF)

# Pulls in capicxx-core-runtime, capicxx-dbus-runtime and the generators,
# and exposes commonapi_generate_stubs(). Must be included before any
# subdirectory that generates or links CommonAPI stubs.
include(CommonAPI)

add_subdirectory(fidl)          # produces the `srs_capi_stubs` object library
add_subdirectory(lib_srs)
add_subdirectory(other_apps_dummy)

add_executable(${PROJECT_NAME} main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib_srs/inc)
target_link_libraries(${PROJECT_NAME} PRIVATE service_recovery_scheduler)

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O1 -g -fno-omit-frame-pointer
        -fsanitize=address -fsanitize=undefined -fno-sanitize=vptr)
    target_link_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address -fsanitize=undefined -fno-sanitize=vptr)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(gtest)
endif()

# -------------------- install & package --------------------
# CMAKE_SKIP_INSTALL_RULES is reset to FALSE at the end of cmake/CommonAPI.cmake
# (after the FetchContent'd runtimes are integrated), so our install() rules
# below take effect while the third-party trees stay export-clean.

# We ship as a system daemon; when the caller doesn't override the prefix,
# install under /usr (not the CMake default /usr/local) so cpack -G DEB
# lands binaries in /usr/bin, libraries in /usr/lib, etc.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

include(GNUInstallDirs)

# System configuration lives outside the ${CMAKE_INSTALL_PREFIX} tree so that
# cpack -G DEB emits /etc/... (not /usr/etc/...). Only override when the caller
# hasn't already fixed a value.
if(NOT DEFINED CMAKE_INSTALL_SYSCONFDIR OR "${CMAKE_INSTALL_SYSCONFDIR}" STREQUAL "etc")
    set(CMAKE_INSTALL_SYSCONFDIR /etc CACHE PATH "Read-only single-machine data (/etc)" FORCE)
    set(CMAKE_INSTALL_FULL_SYSCONFDIR /etc)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# CommonAPI D-Bus runtime config file used by app_srs at startup.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fidl/commonapi4dbus.ini
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME})

# systemd unit (optional -- only when systemd is present at package time)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/systemd/${PROJECT_NAME}.service)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/${PROJECT_NAME}.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
endif()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    OPTIONAL)

# Packaging (`cpack -G DEB` / `cpack -G RPM` / `cpack -G TGZ`)
# CPACK_SET_DESTDIR ensures absolute DESTINATION paths (e.g. /etc/app_srs) are
# staged under DESTDIR at package time instead of writing to the host FS.
set(CPACK_SET_DESTDIR                   ON)
set(CPACK_PACKAGING_INSTALL_PREFIX      "/usr")
set(CPACK_PACKAGE_NAME                  "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION               "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY   "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR                "service-recovery-scheduler contributors")
set(CPACK_PACKAGE_CONTACT               "maintainers@example.invalid")
set(CPACK_RESOURCE_FILE_LICENSE         "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_DEPENDS        "libdbus-1-3, libsystemd0")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS      ON)
set(CPACK_RPM_PACKAGE_LICENSE           "MIT")
include(CPack)