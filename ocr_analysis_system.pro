# ============================================================
# 项目名: ocr_analysis_system
# 描述: Qt + OpenCV + ONNX Runtime 字符识别工具
#       基于PP-OCRv4模型，支持图片文字检测、方向分类、文字识别
#       支持批量识别、结果编辑、CSV导出
# Qt版本: Windows: Qt6.11.1 (mingw_64); macOS: Qt6 (Homebrew arm64)
# 构建系统: qmake
# OpenCV版本: Windows: 4.5.3 (本机 D:\OpenCV453, MinGW 64-bit)，缺省回退 4.5.2
#             macOS: 5.1.0 (/opt/opencv, arm64)
# ONNX Runtime: 1.16.3 (Windows: MinGW 64-bit; macOS: arm64)
# ============================================================

# 引入Qt基础模块：核心模块、GUI模块、Widgets模块
QT += core gui widgets

# 开启C++14标准支持（ONNX Runtime的constexpr构造函数需要C++14）
CONFIG += c++14

# 目标输出目录（可执行文件输出到源码树的bin目录，与DLL和模型文件同目录）
DESTDIR = $$PWD/bin
TARGET = ocr_analysis_system

# 中间文件目录（编译过程中产生的.obj/.moc/.rcc/.ui文件）
# 使用 $$OUT_PWD 构建绝对路径：
#   - QtCreator影子构建时，OUT_PWD为影子目录，中间文件不污染源码树
#   - 命令行in-source构建时，OUT_PWD等于PWD，中间文件仍在源码树bin下
OBJECTS_DIR = $$OUT_PWD/bin/obj
MOC_DIR = $$OUT_PWD/bin/moc
RCC_DIR = $$OUT_PWD/bin/rcc
UI_DIR = $$OUT_PWD/bin/ui

# 启用自动MOC处理（包含Q_OBJECT宏的头文件自动生成moc文件）
CONFIG += automoc

# ============================================================
# 头文件搜索路径
# src和3rd使用源码树绝对路径（$$PWD），UI_DIR使用构建目录（$$OUT_PWD）
# 这样无论影子构建还是源内构建，都能正确找到uic生成的ui_mainwindow.h
# ============================================================
INCLUDEPATH += \
    $$PWD/src \
    $$UI_DIR

# Windows: 使用本机 D:\OpenCV453 提供的 OpenCV 4.5.3 头文件；
# 本机安装目录不存在时回退到 3rd/include 中随工程提供的 4.5.2。
# 注意该路径必须排在 3rd/include 之前，避免先命中旧版 OpenCV 头文件。
win32:OPENCV_DIR = D:/OpenCV453/build/install

win32 {
    exists($$OPENCV_DIR/include/opencv2/opencv.hpp) {
        INCLUDEPATH += \
            $$OPENCV_DIR/include \
            $$OPENCV_DIR/include/opencv2
    }
}

# macOS下优先使用 /opt/opencv 提供的 OpenCV 5 头文件，
# 避免命中 3rd/include 中仅适用于 Windows 的 OpenCV 4.5.2 头文件。
macx {
    INCLUDEPATH += /opt/opencv/include/opencv5
}

# ONNX Runtime 头文件仍复用 3rd/include（Windows OpenCV 优先使用上方本机 4.5.3）
INCLUDEPATH += $$PWD/3rd/include

# ============================================================
# OpenCV 库链接路径
# Windows: 本机 OpenCV 4.5.3 (D:\OpenCV453)；
#          本机安装目录不存在时回退到随工程打包的 4.5.2 (3rd/lib)
# ============================================================
win32 {
    # ONNX Runtime C++头文件与MinGW存在类型转换兼容性问题，需要-fpermissive降级为警告
    QMAKE_CXXFLAGS += -fpermissive

    exists($$OPENCV_DIR/x64/mingw/lib/libopencv_core453.dll.a) {
        # 将 x64/mingw/lib 下全部 libopencv_*.dll.a 作为显式库文件链接
        LIBS += $$files($$OPENCV_DIR/x64/mingw/lib/libopencv_*.a)
    } else {
        LIBS += -L$$PWD/3rd/lib \
            -lopencv_core452 \
            -lopencv_imgproc452 \
            -lopencv_imgcodecs452 \
            -lopencv_highgui452 \
            -lopencv_dnn452
    }
}

# macOS: 链接本机 /opt/opencv 的 OpenCV 5.1.0 arm64 动态库
macx {
    # /opt/opencv 5.1.0 针对 macOS 26.0 构建，避免与更低的部署目标产生链接告警
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0

    exists(/opt/opencv/lib/libopencv_core.dylib) {
        LIBS += -L/opt/opencv/lib \
            -lopencv_core \
            -lopencv_imgproc \
            -lopencv_geometry \
            -lopencv_imgcodecs \
            -lopencv_highgui \
            -lopencv_dnn
        QMAKE_RPATHDIR += /opt/opencv/lib
    } else {
        error("未找到 /opt/opencv/lib 下的 OpenCV 5 动态库")
    }
}

# ============================================================
# ONNX Runtime 库链接
# Windows: MinGW 64-bit (3rd/lib)
# macOS:   arm64 1.16.3 (3rd/macos/onnxruntime/lib)
# ============================================================
win32 {
    LIBS += -L$$PWD/3rd/lib -lonnxruntime
}

macx {
    ONNXRUNTIME_DIR = $$PWD/3rd/macos/onnxruntime
    exists($$ONNXRUNTIME_DIR/lib/libonnxruntime.dylib) {
        LIBS += -L$$ONNXRUNTIME_DIR/lib -lonnxruntime
        QMAKE_RPATHDIR += $$ONNXRUNTIME_DIR/lib
    } else {
        error("未找到 macOS ONNX Runtime: $$ONNXRUNTIME_DIR/lib/libonnxruntime.dylib")
    }
}

# ============================================================
# 资源文件（QSS样式表、图标等）
# ============================================================
RESOURCES += $$PWD/res/resource.qrc
RESOURCES += $$PWD/images/images.qrc

# ============================================================
# 头文件（包含Q_OBJECT宏的头文件需列出以处理moc）
# ============================================================
HEADERS += \
    src/mainwindow.h \
    src/RapidOcrEngine/RapidOcrEngine.h \
    src/ImageDisplayView/ImageDisplayView.h \
    src/CsvExporter/CsvExporter.h

# ============================================================
# 源文件
# ============================================================
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/RapidOcrEngine/RapidOcrEngine.cpp \
    src/ImageDisplayView/ImageDisplayView.cpp \
    src/CsvExporter/CsvExporter.cpp

# ============================================================
# 界面文件（Qt Designer格式）
# ============================================================
FORMS += \
    src/mainwindow.ui
