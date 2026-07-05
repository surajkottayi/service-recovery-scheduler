cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0074 NEW)
project(service_recovery_scheduler VERSION 1.0.0 LANGUAGES CXX DESCRIPTION "A library to schedule service recovery tasks")

file(GLOB_RECURSE ${PROJECT_NAME}_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${${PROJECT_NAME}_SRC})
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>)

# use C++14
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_14)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION   ${PROJECT_VERSION}          # libsrs.so.1.0.0
    SOVERSION ${PROJECT_VERSION_MAJOR}    # libsrs.so.1  <- soname
    CXX_EXTENSIONS        OFF             # -std=c++14, not -std=gnu++14
    CXX_VISIBILITY_PRESET hidden          # hide all symbols by default
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
