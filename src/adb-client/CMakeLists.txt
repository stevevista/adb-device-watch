PROJECT(adb-client VERSION 1 LANGUAGES CXX)

add_library(co-${PROJECT_NAME}
  co-adb-client.cc
  co-adb-client.h)

target_include_directories(co-${PROJECT_NAME} INTERFACE .)

target_link_libraries(co-${PROJECT_NAME} PUBLIC
  asio)

target_link_libraries(co-${PROJECT_NAME} PRIVATE
  process-lib)

add_library(${PROJECT_NAME}
  adb-client.cc
  adb-client.h)

target_include_directories(${PROJECT_NAME} INTERFACE .)

target_link_libraries(${PROJECT_NAME} PRIVATE
  co-${PROJECT_NAME})
