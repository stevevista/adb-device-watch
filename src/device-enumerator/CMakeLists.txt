PROJECT(device-enumerator VERSION 1 LANGUAGES CXX)

if (WIN32)
set (PLATFORM_SRCS
  usb-watch-win.cc
  usb-watch-win.h)
else()
set (PLAT_NAME linux)
endif()

add_library(${PROJECT_NAME}
  usb-watch-base.cc
  usb-watch-base.h
  ${PLATFORM_SRCS})

target_include_directories(${PROJECT_NAME} INTERFACE ..)

target_link_libraries(${PROJECT_NAME} PRIVATE
  adb-client
  process-lib
  picosha2)
