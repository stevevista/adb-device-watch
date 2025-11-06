
cmake_minimum_required(VERSION 3.16)

include(CSharpUtilities)

PROJECT(adb-device-watch-sharp VERSION 1.0 LANGUAGES CSharp)

#set(CMAKE_DOTNET_TARGET_FRAMEWORK net8.0)
#set(CMAKE_DOTNET_SDK "Microsoft.NET.Sdk")


add_library(${PROJECT_NAME} SHARED
    dev-watch.cs
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_PACKAGE_REFERENCES "System;System.Core"
    DOTNET_TARGET_FRAMEWORK net8.0
    DOTNET_SDK "Microsoft.NET.Sdk"
)

add_executable(test-${PROJECT_NAME}
    test.cs
)

SET_TARGET_PROPERTIES(test-${PROJECT_NAME} PROPERTIES
    VS_PACKAGE_REFERENCES "System;System.Core"
    DOTNET_TARGET_FRAMEWORK net8.0
    DOTNET_SDK "Microsoft.NET.Sdk"
)

target_link_libraries(test-${PROJECT_NAME} PRIVATE ${PROJECT_NAME})
