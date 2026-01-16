cmake_minimum_required(VERSION 3.16)
project(v3_test_client VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

# Windows 设置
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601
)

# 源文件
set(SOURCES
    main.cpp
)

# 资源文件
set(RESOURCES
    app.rc
)

# 可执行文件
add_executable(v3_test WIN32
    ${SOURCES}
    ${RESOURCES}
)

# 链接库
target_link_libraries(v3_test PRIVATE
    user32
    gdi32
    shell32
    advapi32
    comctl32
)

# MSVC 设置
if(MSVC)
    target_compile_options(v3_test PRIVATE /utf-8 /W4)
    set_property(TARGET v3_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinGW 设置
if(MINGW)
    target_link_options(v3_test PRIVATE -static -mwindows)
endif()
