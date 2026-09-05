# ocr_analysis_system

基于 Qt + OpenCV + ONNX Runtime 的 PP-OCRv4 图片文字识别工具，支持单张/批量识别、文本框标注、结果编辑和 CSV 导出。

## 功能

- 文本检测（`ch_PP-OCRv4_det`）
- 方向分类（`ch_ppocr_mobile_v2.0_cls`）
- 文字识别（`ch_PP-OCRv4_rec`）
- 支持中文路径读取图片
- 批量识别与进度显示
- OCR 结果 CSV 导出

## 目录结构

```text
ocr_analysis_system/
├── ocr_analysis_system.pro             # qmake 工程（Windows/macOS 条件配置）
├── build.bat                 # Windows 命令行构建脚本
├── build/                    # Qt Creator 影子构建目录
├── src/                      # 源码
├── res/                      # QSS/资源
├── 3rd/
│   ├── include/              # OpenCV 4.5.2 + ONNX Runtime 头文件
│   ├── lib/                  # Windows MinGW 导入库/DLL 关联
│   └── macos/onnxruntime/    # macOS arm64 ONNX Runtime 1.16.3
├── bin/
│   ├── model/                # OCR 模型与字典
│   └── ocr_analysis_system.app         # macOS 构建产物
└── test_images/              # 测试图片
```

## 依赖

### macOS（arm64）

- macOS 26.x
- Qt 6（Homebrew 或 Qt Creator 自带 Kit）
- OpenCV 5.1.0：
  - 头文件：`/opt/opencv/include/opencv5`
  - 动态库：`/opt/opencv/lib`
- ONNX Runtime 1.16.3 macOS arm64：
  - 已内置在 `3rd/macos/onnxruntime/lib`

### Windows（MinGW 64-bit）

- Qt 5.14.0 `mingw73_64`
- OpenCV 4.5.2 MinGW
- ONNX Runtime 1.16.3 MinGW

Windows 依赖随工程放在 `3rd/` 与 `bin/` 下，可直接使用 `build.bat`。

## macOS 构建

### 方式一：Qt Creator

1. 打开 `ocr_analysis_system.pro`
2. 选择 Qt 6 arm64 构建套件（例如 `Qt_6_11_1_ARM`）
3. 执行构建
4. 运行 `bin/ocr_analysis_system.app`

### 方式二：命令行

```bash
cd ocr_analysis_system/build/Qt_6_11_1_ARM_Debug
/opt/homebrew/opt/qtbase/bin/qmake \
  ../../ocr_analysis_system.pro \
  -spec macx-clang \
  CONFIG+=debug CONFIG+=qml_debug
make -j4
```

产物位置：

```text
bin/ocr_analysis_system.app/Contents/MacOS/ocr_analysis_system
```

模型目录由程序自动从 `.app` 回退到：

```text
bin/model
```

## Windows 构建

双击或在命令行执行：

```bat
build.bat
```

产物位置：

```text
bin\ocr_analysis_system.exe
```

## 使用

1. 启动程序，确认状态栏显示“OCR引擎就绪”
2. 点击“导入图片”或“批量导入”
3. 点击“识别”或“批量识别”
4. 可在右侧编辑识别结果
5. 点击“导出 CSV”保存结果

## 验证输出

macOS 正常启动时控制台/运行日志中应出现：

```text
[RapidOCR] CPU线程数: N
[RapidOCR] 字典加载成功，字符数: 6623
[RapidOCR] 引擎初始化成功
```

## 常见问题

### qmake 提示找不到 OpenCV

确认 `/opt/opencv` 存在，且目录下包含：

```text
include/opencv5
lib/libopencv_core.5.1.0.dylib
```

### qmake 提示找不到 macOS ONNX Runtime

确认以下文件存在：

```text
3rd/macos/onnxruntime/lib/libonnxruntime.1.16.3.dylib
3rd/macos/onnxruntime/lib/libonnxruntime.dylib
```

如果缺失，可从 ONNX Runtime 官方 Release 下载
`onnxruntime-osx-arm64-1.16.3.tgz` 并放入上述目录。

### 提示 OCR 引擎初始化失败

确认模型文件位于 `bin/model/`：

```text
ch_PP-OCRv4_det_infer.onnx
ch_PP-OCRv4_rec_infer.onnx
ch_ppocr_mobile_v2.0_cls_infer.onnx
ppocr_keys_v1.txt
```

### macOS 版本

本机 `/opt/opencv` 为 macOS 26.0 构建，工程将部署目标设置为
`QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0`，请勿在低于该版本的
macOS 上运行构建产物。

## 第三方组件许可

ONNX Runtime 为 MIT 许可，macOS 版本许可文件位于：

```text
3rd/macos/onnxruntime/LICENSE
```
