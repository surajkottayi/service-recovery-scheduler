cmake_minimum_required(VERSION 3.16)

project(OtherApps VERSION 1.0.0 LANGUAGES CXX
    DESCRIPTION "Client apps that talk to the srs service over CommonAPI D-Bus")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Each client only needs the generated CommonAPI proxy + runtimes, not the
# full recovery-scheduler library.
foreach(app appA appB appC)
    add_executable(${app} ${CMAKE_CURRENT_SOURCE_DIR}/${app}.cpp)
    target_link_libraries(${app} PRIVATE srs_capi_stubs)
    target_include_directories(${app} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib_srs/inc)
endforeach()

# Install the dummy clients so the Docker image can launch them alongside
# app_srs to demonstrate end-to-end CommonAPI/D-Bus communication.
include(GNUInstallDirs)
install(TARGETS appA appB appC
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
