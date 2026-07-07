cmake_minimum_required(VERSION 3.16)

project(lib_srs_tests LANGUAGES CXX)

if(NOT TARGET service_recovery_scheduler)
    message(FATAL_ERROR
        "gtest/ must be configured as a subdirectory of the top-level "
        "project (target 'service_recovery_scheduler' not found). "
        "Build from the repo root:  cmake -S . -B build && cmake --build build -j && ctest --test-dir build")
endif()

include(FetchContent)

set(SRS_GTEST_TAG "v1.14.0" CACHE STRING "GoogleTest git tag")
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        ${SRS_GTEST_TAG}
)
# Keep gtest's runtime lib layout consistent with the rest of the build.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(GLOB ${PROJECT_NAME}_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SRC})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib_srs/inc)

target_link_libraries(${PROJECT_NAME} PRIVATE
    service_recovery_scheduler
    GTest::gtest
    GTest::gtest_main)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE --coverage -O0 -g)
    target_link_options(${PROJECT_NAME} PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

# ------------------------------------------------------------------
# `coverage` target: runs the test binary, aggregates gcov data, and
# produces an HTML dashboard at ${CMAKE_BINARY_DIR}/coverage/index.html.
# Requires -DENABLE_COVERAGE=ON at configure time.
# Usage:  cmake --build build --target coverage
# ------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(_cov_out ${CMAKE_BINARY_DIR}/coverage)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_cov_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_cov_out}
        # Reset stale counters (.gcda) so a rerun measures only this run
        COMMAND find ${CMAKE_BINARY_DIR}/lib_srs ${CMAKE_BINARY_DIR}/gtest
                -name "*.gcda" -delete || true
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --gtest_output=xml:${_cov_out}/gtest.xml
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gen_report.py
                --build-dir ${CMAKE_BINARY_DIR}
                --source-dir ${CMAKE_SOURCE_DIR}/lib_srs
                --gtest-xml ${_cov_out}/gtest.xml
                --project-name service_recovery_scheduler
                --build-id "local"
                --out ${_cov_out}
        DEPENDS ${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests, collecting gcov data, and rendering HTML dashboard -> ${_cov_out}/index.html"
        VERBATIM)
endif()
