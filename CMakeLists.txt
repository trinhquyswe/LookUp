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
)
