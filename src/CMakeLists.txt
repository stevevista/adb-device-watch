cmake_minimum_required(VERSION 3.21)

project(device_watch VERSION 1.0)

set_taret_name(TARGET ${PROJECT_NAME})

add_subdirectory(process)
add_subdirectory(adb-client)
add_subdirectory(device-enumerator)

add_library(${TARGET} INTERFACE)

target_include_directories(${TARGET} INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${TARGET} INTERFACE
  ${DEVICE_WATCH_NS}::process
  ${DEVICE_WATCH_NS}::enumerator
  ${DEVICE_WATCH_NS}::adbclient)
