cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0074 NEW)
project(service_recovery_scheduler VERSION 1.0.0 LANGUAGES CXX DESCRIPTION "A library to schedule service recovery tasks")

file(GLOB_RECURSE ${PROJECT_NAME}_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${${PROJECT_NAME}_SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>)
find_package(PkgConfig)
pkg_check_modules(SYSTEMD libsystemd)

# Link the generated CommonAPI (core+dbus) stubs plus their transitive
# runtime dependencies (CommonAPI::Core, CommonAPI::DBus, dbus-1).
# srs_capi_stubs is defined in fidl/CMakeLists.txt via commonapi_generate_stubs().
target_link_libraries(${PROJECT_NAME} PUBLIC
    pthread
    ${SYSTEMD_LIBRARIES}
    srs_capi_stubs)
# use C++17
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION   ${PROJECT_VERSION}          # libsrs.so.1.0.0
    SOVERSION ${PROJECT_VERSION_MAJOR}    # libsrs.so.1  <- soname
    CXX_EXTENSIONS        OFF             # -std=c++17, not -std=gnu++17
    # CXX_VISIBILITY_PRESET hidden          # hide all symbols by default
    VISIBILITY_INLINES_HIDDEN ON)         # hide inlined functions too

# Warnings are PRIVATE: never push them onto consumers of this library
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4             # high warning level
        /permissive-    # strict standard conformance
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall                  # common warnings
        -Wextra                # extra heuristic warnings
        -Wpedantic             # strict ISO C++ conformance
        -Wshadow               # variable shadowing (e.g. ctor param vs member)
        -Wnon-virtual-dtor     # base class missing virtual dtor
        -Wold-style-cast       # enforce C++ casts (static_cast etc.)
        -Wcast-align           # pointer cast increases alignment requirement
        -Woverloaded-virtual   # hides a virtual function from base class
        -Wconversion           # implicit narrowing conversions
        -Wsign-conversion      # signed/unsigned mismatch
        -Wnull-dereference     # possible null pointer dereference
        -Wdouble-promotion     # float silently promoted to double
        -Wformat=2             # printf/scanf format string vulnerabilities
    )
endif()

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--no-undefined   # fail at link time if a symbol is unresolved
    -Wl,--as-needed      # drop unused DT_NEEDED entries
    -Wl,-z,relro         # make GOT read-only after relocation
    -Wl,-z,now           # resolve all symbols at load time (BIND_NOW)
    -Wl,-z,noexecstack)  # non-executable stack

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g --coverage -fno-inline)
    target_link_options(${PROJECT_NAME} PRIVATE --coverage)
endif()

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Address + Undefined-behavior sanitizers. Thread sanitizer is mutually
    # exclusive with ASan, so keep it as a separate opt-in if needed later.
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O1 -g -fno-omit-frame-pointer
        -fsanitize=address -fsanitize=undefined)
    target_link_options(${PROJECT_NAME} PRIVATE
        -fsanitize=address -fsanitize=undefined)
endif()

# -------------------- install() rules --------------------
# Third-party FetchContent trees keep their install rules skipped; ours are
# re-enabled at the end of cmake/CommonAPI.cmake, so no override is needed
# here.

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    EXPORT  ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY inc/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY config/
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME}
    OPTIONAL)
