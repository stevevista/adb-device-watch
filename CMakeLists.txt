cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(adb-device-watch VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

option(DEVICE_WATCH_BUILD_EXE "Build executable binary." ${PROJECT_IS_TOP_LEVEL})
option(DEVICE_WATCH_BUILD_DOTNET "Build cs dotnet binary." ${PROJECT_IS_TOP_LEVEL})

include (cmake/msvc_runtime_selector.cmake)

#set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (MSVC)
  # VS2022 bug
  add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR=1)
endif(MSVC)

if(NOT TARGET asio)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE
  external/asio/asio/include)

if (MSVC)
  target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0602)
endif()

endif()

if(NOT TARGET picosha2)
add_subdirectory(external/picosha2 EXCLUDE_FROM_ALL)
endif()

set_taret_name(DEVICE_WATCH_NS device_watch)
#set (DEVICE_WATCH_MSVC_RUNTIME MD)
#set (DEVICE_WATCH_TARGET_PREFIX md)
add_subdirectory(src)

if (DEVICE_WATCH_BUILD_EXE)

set(NLOHMANN_JSON_TARGET_NAME "adb_nlohmann_json")

add_subdirectory(external/gflags EXCLUDE_FROM_ALL)
add_subdirectory(external/json EXCLUDE_FROM_ALL)

add_executable(${PROJECT_NAME} src/dev-watch.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
  gflags::gflags
  adb_nlohmann_json
  device_watch)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  VERSION=\"${PROJECT_VERSION}\")

add_executable(example1 src/example1.cc)
target_link_libraries(example1 PRIVATE
  gflags::gflags
  adb_nlohmann_json
  device_watch)

endif()

if (DEVICE_WATCH_BUILD_DOTNET)
add_subdirectory(adb-device-watch-sharp)
endif()
