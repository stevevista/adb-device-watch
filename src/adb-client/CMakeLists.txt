PROJECT(dev-watch-adb-client VERSION 1 LANGUAGES CXX)

set_taret_name(TARGET ${PROJECT_NAME})

add_library(co-${TARGET}
  co-adb-client.cc
  co-adb-client.h)

select_msvc_runtime_library(co-${TARGET})
target_include_directories(co-${TARGET} PRIVATE ..)

target_link_libraries(co-${TARGET} PUBLIC
  asio)

target_link_libraries(co-${TARGET} PRIVATE
  ${DEVICE_WATCH_NS}::process)

add_library(${TARGET}
  adb-client.cc
  adb-client.h)

select_msvc_runtime_library(${TARGET})
target_include_directories(${TARGET} PRIVATE ..)

target_link_libraries(${TARGET} PRIVATE
  co-${TARGET})

add_library(${DEVICE_WATCH_NS}::adbclient_co ALIAS co-${TARGET})
add_library(${DEVICE_WATCH_NS}::adbclient ALIAS ${TARGET})
