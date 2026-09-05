@echo off
chcp 65001 >nul
REM ============================================================
REM ocr_analysis_system 编译脚本
REM 环境要求: Qt5.14.0 mingw73_64 + OpenCV4.5.2 + ONNX Runtime1.16.3 (MinGW 64-bit)
REM 作者: 先瞳编码, 关注微信公众号"先瞳编码"，获取最新技术分享
REM ============================================================

REM 设置 Qt 与 MinGW 环境变量
set QT_DIR=E:\program\Qt\5.14.0\mingw73_64
set MINGW_DIR=E:\program\Qt\Tools\mingw730_64
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
