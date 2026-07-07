cmake_minimum_required(VERSION 3.16)
project(app_srs
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "A runnable entry point which uses the srs library to schedule service recovery tasks")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

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

# Unit tests — opt-in via -DBUILD_TESTING=ON (default ON for local dev).
option(BUILD_TESTING "Build lib_srs unit tests" ON)
# Coverage instrumentation — opt-in via -DENABLE_COVERAGE=ON. Only meaningful with gcc/clang.
option(ENABLE_COVERAGE "Instrument lib_srs and lib_srs_tests for gcov coverage" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(gtest)
endif()