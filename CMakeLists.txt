cmake_minimum_required(VERSION 3.20)
project(LookUp LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure compiler flags
if(MSVC)
    add_compile_options(/W4 /WX) # Treat warnings as errors for high-quality code
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# Force Unicode APIs
add_compile_definitions(UNICODE _UNICODE)

# Add source files
add_executable(LookUp WIN32
    src/main.cpp
    src/D2DRenderer.h
    src/D2DRenderer.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/PopupWindow.h
    src/PopupWindow.cpp
    src/DictionaryClient.h
    src/DictionaryClient.cpp
)

# Include directories
target_include_directories(LookUp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/windows-clib/win-CLIB-CPU-x64/include
)

# Direct2D, DirectWrite, WinHTTP, and core Win32 libraries
target_link_libraries(LookUp PRIVATE
    d2d1
    dwrite
    winhttp
    shell32
    user32
    gdi32
    ole32
    dwmapi
    runtimeobject
    ${CMAKE_CURRENT_SOURCE_DIR}/windows-clib/win-CLIB-CPU-x64/lib/RapidOcrOnnx.lib
)

# Copy RapidOcrOnnx.dll to output directory post-build
add_custom_command(TARGET LookUp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/windows-clib/win-CLIB-CPU-x64/bin/RapidOcrOnnx.dll"
        "$<TARGET_FILE_DIR:LookUp>/RapidOcrOnnx.dll"
    COMMENT "Copying RapidOcrOnnx DLL to build output folder..."
)

# Copy Assets to output directory post-build
add_custom_command(TARGET LookUp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:LookUp>/assets"
    COMMENT "Copying project assets folder to build output folder..."
)
