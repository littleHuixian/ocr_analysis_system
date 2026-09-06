@echo off
chcp 65001 >nul
REM ============================================================
REM ocr_analysis_system 编译脚本
REM 环境要求: Qt6.11.1 mingw_64 + OpenCV4.5.3(D:\OpenCV453) + ONNX Runtime1.16.3 (MinGW 64-bit)
REM 作者: 
REM ============================================================

REM 设置 Qt 与 MinGW 环境变量
set QT_DIR=D:\Qt\Qt6.11\6.11.1\mingw_64
set MINGW_DIR=D:\Qt\Qt6.11\Tools\mingw1310_64
set PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%

REM 记录工程根目录
set PROJECT_ROOT=%~dp0

echo ============================================================
echo  [1/3] 运行 qmake 生成 Makefile ...
echo ============================================================
cd /d "%PROJECT_ROOT%"
"%QT_DIR%\bin\qmake.exe" ocr_analysis_system.pro -spec win32-g++ -o Makefile
if errorlevel 1 (
    echo 错误: qmake 失败!
    pause
    exit /b 1
)

echo [2/3] 运行 mingw32-make 编译 ...
mingw32-make -f Makefile -j4
if errorlevel 1 (
    echo 错误: 编译失败!
    pause
    exit /b 1
)

echo [3/3] 编译完成!
echo ============================================================
echo  可执行文件: %PROJECT_ROOT%bin\ocr_analysis_system.exe
echo  运行目录  : %QT_DIR%\bin
echo ============================================================
pause
