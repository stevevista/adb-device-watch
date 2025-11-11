PROJECT(dev-watch-process VERSION 1 LANGUAGES CXX)

set_taret_name(TARGET ${PROJECT_NAME})

add_library(${TARGET}
  process.cc
  process-output.cc
  process-output.h
  process.h)

select_msvc_runtime_library(${TARGET})

add_library(${DEVICE_WATCH_NS}::process ALIAS ${TARGET})
