@echo off
chcp 65001 >nul
setlocal

echo ╔═══════════════════════════════════════════╗
echo ║       v3 测试客户端 - 快速构建            ║
echo ╚═══════════════════════════════════════════╝
echo.

:: 检测编译器
where cl >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [INFO] 使用 MSVC 编译...
    goto :build_msvc
)

where gcc >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [INFO] 使用 MinGW 编译...
    goto :build_mingw
)

echo [ERROR] 未找到编译器！请安装 Visual Studio 或 MinGW-w64
exit /b 1

:build_msvc
:: MSVC 编译
if not exist build mkdir build
cd build

:: 编译资源
rc /nologo /fo app.res ..\app.rc 2>nul

:: 编译主程序
cl /nologo /O2 /MT /W4 /utf-8 ^
   /D "WIN32" /D "_UNICODE" /D "UNICODE" /D "NDEBUG" ^
   ..\main.cpp app.res ^
   /Fe:v3_test.exe ^
   /link user32.lib gdi32.lib shell32.lib advapi32.lib comctl32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup

if %ERRORLEVEL% equ 0 (
    echo.
    echo [OK] 构建成功: build\v3_test.exe
    copy v3_test.exe .. >nul 2>&1
) else (
    echo [ERROR] 构建失败
)
cd ..
goto :end

:build_mingw
:: MinGW 编译
if not exist build mkdir build

:: 编译资源
windres app.rc -o build\app.res 2>nul

:: 编译主程序
g++ -O2 -s -mwindows -municode ^
    -DWIN32 -D_UNICODE -DUNICODE ^
    main.cpp build\app.res ^
    -o build\v3_test.exe ^
    -luser32 -lgdi32 -lshell32 -ladvapi32 -lcomctl32 ^
    -static

if %ERRORLEVEL% equ 0 (
    echo.
    echo [OK] 构建成功: build\v3_test.exe
    copy build\v3_test.exe . >nul 2>&1
) else (
    echo [ERROR] 构建失败
)

:end
echo.
pause
