cmake_minimum_required(VERSION 3.16)
project(app_srs VERSION 1.0.0 LANGUAGES CXX DESCRIPTION "A runnable entry point which uses the srs library to schedule service recovery tasks")
add_subdirectory(lib_srs)

add_executable(${PROJECT_NAME} main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib_srs/inc)
target_link_libraries(${PROJECT_NAME} PRIVATE service_recovery_scheduler)