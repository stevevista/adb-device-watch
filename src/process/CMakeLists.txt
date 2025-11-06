PROJECT(process-lib VERSION 1 LANGUAGES CXX)

add_library(${PROJECT_NAME}
  process.cc
  process-output.cc
  process-output.h
  process.h)

target_include_directories(${PROJECT_NAME} INTERFACE ..)
