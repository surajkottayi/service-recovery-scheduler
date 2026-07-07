# -----------------------------------------------------------------------------
# CommonAPI.cmake
#
# Self-contained, in-tree CommonAPI setup (D-Bus backend).
#
# What this module does:
#   1. Downloads and builds capicxx-core-runtime + capicxx-dbus-runtime from
#      source into the build tree via FetchContent (no system install needed).
#   2. Downloads the pre-built Java generators (Eclipse standalone RCP zips)
#      into ${CMAKE_BINARY_DIR}/_deps/commonapi-tools.
#   3. Exposes commonapi_generate_stubs(...) to run the generators on a .fidl
#      and .fdepl pair and expose the produced C++ sources as CMake targets.
#
# Requirements on the host:
#   - g++/clang with C++17
#   - pkg-config + libdbus-1-dev
#   - Java 11+ runtime (for the generator only)
#   - Internet access on first configure (to download runtimes + generators)
# -----------------------------------------------------------------------------

include_guard(GLOBAL)
include(FetchContent)

# ---- Versions (override on the command line with -D...) ---------------------
set(CAPICXX_CORE_RUNTIME_TAG "3.2.4"    CACHE STRING "capicxx-core-runtime git tag")
set(CAPICXX_DBUS_RUNTIME_TAG "3.2.3-r1" CACHE STRING "capicxx-dbus-runtime git tag")

set(CAPICXX_CORE_TOOLS_VERSION "3.2.15" CACHE STRING "capicxx-core-tools release")
set(CAPICXX_DBUS_TOOLS_VERSION "3.2.15" CACHE STRING "capicxx-dbus-tools release")

set(CAPICXX_CORE_GENERATOR_URL
    "https://github.com/COVESA/capicxx-core-tools/releases/download/${CAPICXX_CORE_TOOLS_VERSION}/commonapi_core_generator.zip"
    CACHE STRING "URL of the pre-built commonapi core generator zip")
set(CAPICXX_DBUS_GENERATOR_URL
    "https://github.com/COVESA/capicxx-dbus-tools/releases/download/${CAPICXX_DBUS_TOOLS_VERSION}/commonapi_dbus_generator.zip"
    CACHE STRING "URL of the pre-built commonapi dbus generator zip")

# ---- Host dependencies ------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

find_program(JAVA_EXECUTABLE
    NAMES java
    DOC "Java runtime used to launch the CommonAPI generators")
if(NOT JAVA_EXECUTABLE)
    message(WARNING
        "java not found on PATH. Stub generation (commonapi_generate_stubs) "
        "will fail until Java 11+ is installed. Runtimes will still build.")
endif()

# ---- Runtimes ---------------------------------------------------------------
# We consume the runtimes in-tree only; skip their `install()` rules so that
# capicxx-dbus-runtime's install(EXPORT ...) doesn't drag the CommonAPI target
# (owned by capicxx-core-runtime) into its export set — CMake rejects that.
set(CMAKE_SKIP_INSTALL_RULES TRUE)

# capicxx-core-runtime is straightforward: FetchContent + add_subdirectory,
# which additionally generates CommonAPIConfig.cmake into its build tree so
# capicxx-dbus-runtime can find_package() it below.
FetchContent_Declare(capicxx_core_runtime
    GIT_REPOSITORY https://github.com/COVESA/capicxx-core-runtime.git
    GIT_TAG        ${CAPICXX_CORE_RUNTIME_TAG}
    GIT_SHALLOW    TRUE
    # Types.hpp uses std::string without including <string>, which trips
    # GCC 13 / libstdc++ 13. Inject the missing include.
    PATCH_COMMAND sed -i
        "/^#include <cstdint>/a #include <string>"
        <SOURCE_DIR>/include/CommonAPI/Types.hpp)

FetchContent_MakeAvailable(capicxx_core_runtime)

# Point find_package(CommonAPI ...) at the just-populated build tree.
set(CommonAPI_DIR ${capicxx_core_runtime_BINARY_DIR} CACHE PATH "" FORCE)

# capicxx-dbus-runtime does find_package(DBus1 1.4 CONFIG). Ubuntu's
# libdbus-1-dev only provides a pkg-config file, so we synthesize a minimal
# DBus1 CMake package that wraps PkgConfig::dbus-1 into a target named `dbus-1`
# (which is what dbus-runtime links against).
set(_dbus1_shim_dir ${CMAKE_BINARY_DIR}/cmake-shims/DBus1)
file(MAKE_DIRECTORY ${_dbus1_shim_dir})
file(WRITE ${_dbus1_shim_dir}/DBus1Config.cmake [[
if(NOT TARGET dbus-1)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_dbus1_shim REQUIRED IMPORTED_TARGET GLOBAL dbus-1)
    add_library(dbus-1 INTERFACE IMPORTED GLOBAL)
    target_link_libraries(dbus-1 INTERFACE PkgConfig::_dbus1_shim)
    set(DBus1_VERSION       ${_dbus1_shim_VERSION})
    set(DBus1_INCLUDE_DIRS  ${_dbus1_shim_INCLUDE_DIRS})
    set(DBus1_LIBRARIES     ${_dbus1_shim_LIBRARIES})
endif()
set(DBus1_FOUND TRUE)
]])
file(WRITE ${_dbus1_shim_dir}/DBus1ConfigVersion.cmake [[
# Fake version file: pkg-config validates the real libdbus-1 version at
# configure time. Report a modern-enough version to satisfy any minimum.
set(PACKAGE_VERSION "1.14.0")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
if("${PACKAGE_FIND_VERSION}" VERSION_LESS_EQUAL "1.14.0")
    set(PACKAGE_VERSION_EXACT FALSE)
endif()
]])
set(DBus1_DIR ${_dbus1_shim_dir} CACHE PATH "" FORCE)

FetchContent_Declare(capicxx_dbus_runtime
    GIT_REPOSITORY https://github.com/COVESA/capicxx-dbus-runtime.git
    GIT_TAG        ${CAPICXX_DBUS_RUNTIME_TAG}
    GIT_SHALLOW    TRUE
    # Upstream defines top-level custom targets `maintainer-clean` and `dist`
    # that also exist in capicxx-core-runtime -> CMP0002 collision when both
    # are add_subdirectory'd into this build. Also strip the install(EXPORT ...)
    # rules that try to bundle the CommonAPI target (owned by the other
    # subproject) into this project's export set. PATCH_COMMAND only runs at
    # populate time (once per fresh clone).
    PATCH_COMMAND sed -i
        -e "s|add_custom_target(maintainer-clean|add_custom_target(dbus_maintainer-clean|"
        -e "s|add_custom_target(dist|add_custom_target(dbus_dist|"
        -e "/^ *EXPORT CommonAPI-DBusTargets$/d"
        -e "/^install(EXPORT CommonAPI-DBusTargets/,+1d"
        -e "/^export(TARGETS CommonAPI-DBus$/,+1d"
        -e "/^export(PACKAGE CommonAPI-DBus)/d"
        <SOURCE_DIR>/CMakeLists.txt)

# Populate (download + patch) but do NOT add_subdirectory yet — we first need
# to build a patched libdbus using patches shipped inside this source tree.
FetchContent_GetProperties(capicxx_dbus_runtime)
if(NOT capicxx_dbus_runtime_POPULATED)
    FetchContent_Populate(capicxx_dbus_runtime)
endif()

# ---- Patched libdbus (required by capicxx-dbus-runtime) --------------------
# capicxx-dbus-runtime calls dbus_connection_send_with_reply_set_notify(), a
# BMW/COVESA addition that only exists in a patched libdbus. Their patch set
# is shipped inside src/dbus-patches/ and targets libdbus 1.12.x.
set(LIBDBUS_VERSION "1.12.20" CACHE STRING
    "Version of libdbus source to fetch and patch")
set(LIBDBUS_URL
    "https://dbus.freedesktop.org/releases/dbus/dbus-${LIBDBUS_VERSION}.tar.gz"
    CACHE STRING "Download URL of the libdbus source tarball")

set(_dbus_prefix ${CMAKE_BINARY_DIR}/dbus-patched)
set(_dbus_stamp  ${_dbus_prefix}/.built-${LIBDBUS_VERSION}-${CAPICXX_DBUS_RUNTIME_TAG})

if(NOT EXISTS ${_dbus_stamp})
    set(_dbus_workdir ${CMAKE_BINARY_DIR}/_deps/dbus-patched-build)
    set(_dbus_srcdir  ${_dbus_workdir}/dbus-${LIBDBUS_VERSION})
    set(_dbus_tar     ${_dbus_workdir}/dbus-${LIBDBUS_VERSION}.tar.gz)

    file(MAKE_DIRECTORY ${_dbus_workdir})
    file(REMOVE_RECURSE ${_dbus_srcdir})

    message(STATUS "Downloading libdbus ${LIBDBUS_VERSION}...")
    file(DOWNLOAD ${LIBDBUS_URL} ${_dbus_tar}
         SHOW_PROGRESS TLS_VERIFY ON
         STATUS _dl_status)
    list(GET _dl_status 0 _dl_rc)
    if(NOT _dl_rc EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "Failed to download ${LIBDBUS_URL}: ${_dl_msg}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${_dbus_tar}
        WORKING_DIRECTORY ${_dbus_workdir}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Failed to extract ${_dbus_tar}")
    endif()

    file(GLOB _dbus_patches
        ${capicxx_dbus_runtime_SOURCE_DIR}/src/dbus-patches/*.patch)
    list(SORT _dbus_patches)
    find_program(_patch patch REQUIRED)
    foreach(_p IN LISTS _dbus_patches)
        # A couple of patches in the set overlap with each other on the exact
        # hunk regions, so `patch` sees the second one as "already applied".
        # Probe with a dry-run reverse: if it applies backwards cleanly, the
        # forward change is effectively in place already — skip and move on.
        execute_process(
            COMMAND ${_patch} -p1 --dry-run -R -i ${_p}
            WORKING_DIRECTORY ${_dbus_srcdir}
            RESULT_VARIABLE _reverses_ok
            OUTPUT_QUIET ERROR_QUIET)
        if(_reverses_ok EQUAL 0)
            message(STATUS "  libdbus patch already applied: ${_p}")
            continue()
        endif()

        message(STATUS "  patching libdbus: ${_p}")
        execute_process(
            COMMAND ${_patch} -p1 -i ${_p}
            WORKING_DIRECTORY ${_dbus_srcdir}
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${_p}")
        endif()
    endforeach()

    include(ProcessorCount)
    ProcessorCount(_nproc)
    if(_nproc EQUAL 0)
        set(_nproc 1)
    endif()

    message(STATUS "Configuring patched libdbus (this happens once)...")
    execute_process(
        COMMAND ./configure
            --prefix=${_dbus_prefix}
            --disable-doxygen-docs
            --disable-xml-docs
            --disable-tests
            --disable-selinux
            --disable-libaudit
            --disable-systemd
            --without-x
            --disable-static
            --with-systemdsystemunitdir=${_dbus_prefix}/lib/systemd/system
        WORKING_DIRECTORY ${_dbus_srcdir}
        RESULT_VARIABLE _rc
        OUTPUT_FILE ${_dbus_workdir}/configure.log
        ERROR_FILE  ${_dbus_workdir}/configure.log)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "libdbus ./configure failed. See ${_dbus_workdir}/configure.log")
    endif()

    message(STATUS "Building patched libdbus (this happens once)...")
    execute_process(
        COMMAND make -j${_nproc} install
        WORKING_DIRECTORY ${_dbus_srcdir}
        RESULT_VARIABLE _rc
        OUTPUT_FILE ${_dbus_workdir}/build.log
        ERROR_FILE  ${_dbus_workdir}/build.log)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "libdbus build failed. See ${_dbus_workdir}/build.log")
    endif()

    file(TOUCH ${_dbus_stamp})
    message(STATUS "Patched libdbus installed to ${_dbus_prefix}")
endif()

# Prepend the patched libdbus to pkg-config's search path so the DBus1 shim
# picks it up in preference to the system libdbus-1.
set(ENV{PKG_CONFIG_PATH} "${_dbus_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# Tell CMake the patched-dbus lib dir is a known implicit link directory. Every
# consumer of libdbus-1 otherwise triggers a "Cannot generate a safe runtime
# search path" warning because our RPATH entry ${_dbus_prefix}/lib appears to
# "hide" the system /usr/lib/.../libdbus-1.so.3 -- which is intentional here.
if(NOT "${_dbus_prefix}/lib" IN_LIST CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES)
    list(APPEND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${_dbus_prefix}/lib")
endif()

# Now that dbus-1 (patched) is discoverable via pkg-config, wire the fetched
# capicxx-dbus-runtime into the build.
add_subdirectory(${capicxx_dbus_runtime_SOURCE_DIR} ${capicxx_dbus_runtime_BINARY_DIR})

# Upstream targets are named CommonAPI and CommonAPI-DBus. Expose aliases so
# consumers can link against a stable name from this project.
if(TARGET CommonAPI AND NOT TARGET CommonAPI::Core)
    add_library(CommonAPI::Core ALIAS CommonAPI)
endif()
if(TARGET CommonAPI-DBus AND NOT TARGET CommonAPI::DBus)
    add_library(CommonAPI::DBus ALIAS CommonAPI-DBus)
endif()

# Silence the flood of `std::unary_function`/`binary_function` C++17
# deprecation warnings coming from CommonAPI's own headers by re-exposing their
# INTERFACE_INCLUDE_DIRECTORIES as SYSTEM includes. GCC/Clang skip most
# warnings for headers found via -isystem. (CMake 3.25+ has a per-target SYSTEM
# property; the manual swap keeps us portable to 3.16.)
foreach(_capi_tgt CommonAPI CommonAPI-DBus)
    if(TARGET ${_capi_tgt})
        get_target_property(_capi_incs ${_capi_tgt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_capi_incs)
            set_target_properties(${_capi_tgt} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
            target_include_directories(${_capi_tgt} SYSTEM INTERFACE ${_capi_incs})
        endif()
        # And silence them while compiling the CommonAPI libraries themselves
        # (their own TUs consume the same headers via PRIVATE include dirs).
        # These are third-party sources — suppress all warnings for their
        # object files but keep our own targets strict.
        target_compile_options(${_capi_tgt} PRIVATE -w)
    endif()
endforeach()

# ---- Generators (downloaded on demand) --------------------------------------
set(_capi_tools_root ${CMAKE_BINARY_DIR}/_deps/commonapi-tools)
file(MAKE_DIRECTORY ${_capi_tools_root})

function(_capi_download_generator name url out_bin_var)
    set(dest ${_capi_tools_root}/${name})
    set(marker ${dest}/.fetched-${CAPICXX_CORE_TOOLS_VERSION}-${CAPICXX_DBUS_TOOLS_VERSION})
    if(NOT EXISTS ${marker})
        message(STATUS "Downloading CommonAPI generator '${name}' from ${url}")
        file(REMOVE_RECURSE ${dest})
        file(MAKE_DIRECTORY ${dest})
        set(zip ${dest}/generator.zip)
        file(DOWNLOAD ${url} ${zip}
             STATUS _dl_status
             SHOW_PROGRESS
             TLS_VERIFY ON)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            message(FATAL_ERROR "Failed to download ${url}: ${_dl_msg}")
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${zip}
            WORKING_DIRECTORY ${dest}
            RESULT_VARIABLE _unzip_rc)
        if(NOT _unzip_rc EQUAL 0)
            message(FATAL_ERROR "Failed to unpack ${zip}")
        endif()
        file(REMOVE ${zip})
        file(TOUCH ${marker})
    endif()

    # Locate the launcher binary shipped in the zip (linux/x86_64 variant).
    file(GLOB_RECURSE _bin
        ${dest}/commonapi-${name}-generator-linux-x86_64
        ${dest}/commonapi_${name}_generator-linux-x86_64
        ${dest}/commonapi-generator-linux-x86_64
        ${dest}/*/commonapi-${name}-generator-linux-x86_64
        ${dest}/*/commonapi_${name}_generator-linux-x86_64)
    list(LENGTH _bin _bin_count)
    if(_bin_count EQUAL 0)
        message(FATAL_ERROR
            "Could not find a Linux launcher for the '${name}' generator in ${dest}. "
            "Contents:\n${_bin}")
    endif()
    list(GET _bin 0 _bin_path)
    execute_process(COMMAND chmod +x ${_bin_path})
    set(${out_bin_var} ${_bin_path} PARENT_SCOPE)
endfunction()

_capi_download_generator(core ${CAPICXX_CORE_GENERATOR_URL} COMMONAPI_CORE_GENERATOR)
_capi_download_generator(dbus ${CAPICXX_DBUS_GENERATOR_URL} COMMONAPI_DBUS_GENERATOR)

# ---- Public: generate stubs and expose them as a library --------------------
#
# commonapi_generate_stubs(
#     TARGET       <name of INTERFACE library to create>
#     FIDL         <path/to/foo.fidl>
#     FDEPL        <path/to/foo-DBus.fdepl>
#     OUTPUT_DIR   <path>       # optional; defaults to CMAKE_CURRENT_BINARY_DIR/gen
# )
#
# The created target is an OBJECT library that:
#   - Compiles the generated *.cpp
#   - PUBLICly exposes the generated include directories
#   - PUBLICly links CommonAPI::Core and CommonAPI::DBus
function(commonapi_generate_stubs)
    set(oneValueArgs TARGET FIDL FDEPL OUTPUT_DIR)
    cmake_parse_arguments(CAG "" "${oneValueArgs}" "" ${ARGN})

    if(NOT CAG_TARGET OR NOT CAG_FIDL OR NOT CAG_FDEPL)
        message(FATAL_ERROR "commonapi_generate_stubs: TARGET, FIDL and FDEPL are required")
    endif()
    if(NOT CAG_OUTPUT_DIR)
        set(CAG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
    endif()

    get_filename_component(CAG_FIDL  ${CAG_FIDL}  ABSOLUTE)
    get_filename_component(CAG_FDEPL ${CAG_FDEPL} ABSOLUTE)

    set(core_out  ${CAG_OUTPUT_DIR}/core)
    set(dbus_out  ${CAG_OUTPUT_DIR}/dbus)
    file(MAKE_DIRECTORY ${core_out} ${dbus_out})

    set(stamp ${CAG_OUTPUT_DIR}/${CAG_TARGET}.stamp)

    add_custom_command(
        OUTPUT  ${stamp}
        COMMAND ${COMMONAPI_CORE_GENERATOR} -sk -d ${core_out} ${CAG_FIDL}
        COMMAND ${COMMONAPI_DBUS_GENERATOR}     -d ${dbus_out} ${CAG_FDEPL}
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${CAG_FIDL} ${CAG_FDEPL}
        COMMENT "Generating CommonAPI (core+dbus) stubs for ${CAG_TARGET}"
        VERBATIM)

    add_custom_target(${CAG_TARGET}_generate DEPENDS ${stamp})

    # Collect generated sources at build-time via GLOB CONFIGURE_DEPENDS so
    # newly generated files are picked up on rebuild.
    file(GLOB_RECURSE _gen_srcs CONFIGURE_DEPENDS
        ${core_out}/*.cpp
        ${dbus_out}/*.cpp)

    # If configure runs before the first generation, _gen_srcs may be empty.
    # In that case emit a placeholder source so the library target is valid;
    # the real sources will be picked up on subsequent configures.
    if(NOT _gen_srcs)
        set(_placeholder ${CAG_OUTPUT_DIR}/_placeholder.cpp)
        if(NOT EXISTS ${_placeholder})
            file(WRITE ${_placeholder} "// placeholder — regenerate & re-run cmake\n")
        endif()
        set(_gen_srcs ${_placeholder})
    endif()

    add_library(${CAG_TARGET} OBJECT ${_gen_srcs})
    set_target_properties(${CAG_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_dependencies(${CAG_TARGET} ${CAG_TARGET}_generate)

    target_include_directories(${CAG_TARGET} PUBLIC
        ${core_out}
        ${dbus_out})

    target_link_libraries(${CAG_TARGET} PUBLIC
        CommonAPI::Core
        CommonAPI::DBus
        PkgConfig::DBUS)

    target_compile_features(${CAG_TARGET} PUBLIC cxx_std_17)
endfunction()
