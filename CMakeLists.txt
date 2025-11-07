cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(adb-device-watch VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE
  external/asio/asio/include)

if (MSVC)
  target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0602)
endif()

set(NLOHMANN_JSON_TARGET_NAME "adb_nlohmann_json")

add_subdirectory(external/gflags EXCLUDE_FROM_ALL)
add_subdirectory(external/json EXCLUDE_FROM_ALL)
add_subdirectory(external/picosha2 EXCLUDE_FROM_ALL)

add_subdirectory(src/process)
add_subdirectory(src/adb-client)
add_subdirectory(src/device-enumerator)

add_executable(${PROJECT_NAME} src/dev-watch.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
  process-lib
  gflags::gflags
  device-enumerator
  adb_nlohmann_json
  adb-client)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  VERSION=\"${PROJECT_VERSION}\")

add_subdirectory(adb-device-watch-sharp)

