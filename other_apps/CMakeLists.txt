cmake_minimum_required(VERSION 3.16)

project(OtherApps VERSION 1.0.0 LANGUAGES CXX DESCRIPTION "A collection of runnable entry points which use the srs library to schedule service recovery tasks"  )

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(appA ${CMAKE_CURRENT_SOURCE_DIR}/appA.cpp)
add_executable(appB ${CMAKE_CURRENT_SOURCE_DIR}/appB.cpp)
add_executable(appC ${CMAKE_CURRENT_SOURCE_DIR}/appC.cpp)
target_include_directories(appA PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib_srs/inc)
target_include_directories(appB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib_srs/inc)
target_include_directories(appC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib_srs/inc)

target_link_libraries(appA PRIVATE service_recovery_scheduler)
target_link_libraries(appB PRIVATE service_recovery_scheduler)
target_link_libraries(appC PRIVATE service_recovery_scheduler)
target_link_directories(appA PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_directories(appB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_directories(appC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
