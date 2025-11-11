PROJECT(device-watch-enumerator VERSION 1 LANGUAGES CXX)

set_taret_name(TARGET ${PROJECT_NAME})

if (WIN32)
set (PLATFORM_SRCS
  usb-watch-win.cc
  usb-watch-win.h)
else()
set (PLAT_NAME linux)
endif()

add_library(${TARGET}
  usb-watch-base.cc
  usb-watch-base.h
  ${PLATFORM_SRCS})

select_msvc_runtime_library(${TARGET})
target_include_directories(${TARGET} PRIVATE ..)

target_link_libraries(${TARGET} PRIVATE
  picosha2
  ${DEVICE_WATCH_NS}::process
  ${DEVICE_WATCH_NS}::adbclient)

add_library(${DEVICE_WATCH_NS}::enumerator ALIAS ${TARGET})
