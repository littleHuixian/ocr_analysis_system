/**
 * @file RapidOcrEngine.cpp
 * @brief RapidOCR 字符识别引擎实现文件
 * @details 完整实现PP-OCRv4三阶段OCR流程（使用ONNX Runtime C API）：
 *          1. 检测模型预处理→推理→后处理（热力图→文本框）
 *          2. 文本区域裁剪→分类模型预处理→推理（方向判断）
 *          3. 识别模型预处理→推理（CTC解码→文本输出）
 *          直接使用C API调用ONNX Runtime，避免C++封装层在MinGW下的编译兼容性问题
 * @author 
 */

#include "RapidOcrEngine.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <thread>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

// ============================================================
// 模型固定参数（与PP-OCRv4官方对齐）
// ============================================================

const int DET_INPUT_HEIGHT = 640;       ///< 检测模型输入高度
const int DET_INPUT_WIDTH = 640;        ///< 检测模型输入宽度
const float DET_SCORE_THRESHOLD = 0.01f; ///< 检测置信度阈值
const float DET_NMS_THRESHOLD = 0.2f;    ///< NMS非极大值抑制阈值
const int DET_BOX_THRESHOLD = 5;         ///< 文本框最小尺寸阈值（像素）

const int CLS_INPUT_HEIGHT = 48;         ///< 分类模型输入高度
const int CLS_INPUT_WIDTH = 192;         ///< 分类模型输入宽度
const float CLS_THRESHOLD = 0.9f;        ///< 分类置信度阈值

const int REC_INPUT_HEIGHT = 48;         ///< 识别模型输入高度
const int REC_INPUT_WIDTH = 640;         ///< 识别模型输入宽度
const float MEAN_VAL = 127.5f;           ///< 归一化均值
const float NORM_VAL = 1.0f / 127.5f;    ///< 归一化缩放因子

// ============================================================
// 内部实现结构体（ONNX Runtime C API核心组件）
// ============================================================

/**
 * @brief RapidOcrEngine内部实现结构体
 * @details 封装ONNX Runtime C API环境、会话选项、三个模型的Session指针及张量名称
 *          使用C API原生指针，在析构时手动释放资源
 */
struct RapidOcrEngine::Impl
{
    const OrtApi *ortApi;            ///< ONNX Runtime C API函数表指针
    OrtEnv *ortEnv;                  ///< ONNX Runtime环境指针
    OrtSessionOptions *sessionOpts;  ///< 会话配置选项指针
    OrtSession *detSession;          ///< 检测模型会话指针
    OrtSession *recSession;          ///< 识别模型会话指针
    OrtSession *clsSession;          ///< 分类模型会话指针

    // ONNX 张量输入输出名称（与PP-OCRv4官方模型对齐）
    const char *detInputName;        ///< 检测模型输入张量名
    const char *detOutputName;       ///< 检测模型输出张量名
    const char *clsInputName;        ///< 分类模型输入张量名
    const char *clsOutputName;       ///< 分类模型输出张量名
    const char *recInputName;        ///< 识别模型输入张量名
    const char *recOutputName;       ///< 识别模型输出张量名

    // 模型文件路径
    std::string detModelPath;        ///< 检测模型路径
    std::string recModelPath;        ///< 识别模型路径
    std::string clsModelPath;        ///< 分类模型路径
    std::string dictPath;            ///< 字典文件路径

    /**
     * @brief 构造函数：初始化所有指针为空
     */
    Impl()
        : ortApi(nullptr)
        , ortEnv(nullptr)
        , sessionOpts(nullptr)
        , detSession(nullptr)
        , recSession(nullptr)
        , clsSession(nullptr)
        , detInputName("x")
        , detOutputName("sigmoid_0.tmp_0")
        , clsInputName("x")
        , clsOutputName("save_infer_model/scale_0.tmp_1")
        , recInputName("x")
        , recOutputName("softmax_11.tmp_0")
    {
    }

    /**
     * @brief 析构函数：释放所有ONNX Runtime资源
     */
    ~Impl()
    {
        // 按相反顺序释放资源（先释放会话，再释放选项和环境）
        if (detSession && ortApi) { ortApi->ReleaseSession(detSession); detSession = nullptr; }
        if (recSession && ortApi) { ortApi->ReleaseSession(recSession); recSession = nullptr; }
        if (clsSession && ortApi) { ortApi->ReleaseSession(clsSession); clsSession = nullptr; }
        if (sessionOpts && ortApi) { ortApi->ReleaseSessionOptions(sessionOpts); sessionOpts = nullptr; }
        if (ortEnv && ortApi) { ortApi->ReleaseEnv(ortEnv); ortEnv = nullptr; }
    }
};

// ============================================================
// 构造与析构函数
// ============================================================

/**
 * @brief 构造函数：初始化OCR引擎，加载模型和字典
 * @param modelDir 模型文件目录路径
 * @param parent 父QObject指针
 */
RapidOcrEngine::RapidOcrEngine(const QString &modelDir, QObject *parent)
    : QObject(parent)
    , pImpl_(std::shared_ptr<Impl>(new Impl()))
    , ready_(false)
    , detScaleRatio_(1.0f)
    , detPadW_(0)
    , detPadH_(0)
{
    // 禁用OpenCV的INFO级别日志，只保留ERROR级别
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

    // 获取ONNX Runtime C API函数表
    const OrtApiBase *apiBase = OrtGetApiBase();
    if (!apiBase) {
        qWarning() << "[RapidOCR] 无法获取ONNX Runtime API Base";
        return;
    }
    pImpl_->ortApi = apiBase->GetApi(ORT_API_VERSION);
    if (!pImpl_->ortApi) {
        qWarning() << "[RapidOCR] 无法获取ONNX Runtime API版本:" << ORT_API_VERSION;
        return;
    }

    // 创建ONNX Runtime环境
    OrtStatus *status = pImpl_->ortApi->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "RAPIDOCR", &pImpl_->ortEnv);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 创建ONNX Runtime环境失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return;
    }

    // 创建会话选项
    status = pImpl_->ortApi->CreateSessionOptions(&pImpl_->sessionOpts);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 创建会话选项失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return;
    }

    // 根据CPU核心数设置线程数（取一半核心，最少2线程）
    int numThreads = static_cast<int>(std::thread::hardware_concurrency());
    numThreads = (numThreads / 2 > 0) ? numThreads / 2 : 2;
    qDebug() << "[RapidOCR] CPU线程数:" << numThreads;
    status = pImpl_->ortApi->SetIntraOpNumThreads(pImpl_->sessionOpts, numThreads);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 设置推理线程数失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return;
    }
    status = pImpl_->ortApi->SetInterOpNumThreads(pImpl_->sessionOpts, numThreads);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 设置算子线程数失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return;
    }
    // 开启图优化
    status = pImpl_->ortApi->SetSessionGraphOptimizationLevel(pImpl_->sessionOpts, ORT_ENABLE_ALL);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 设置图优化失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return;
    }

    // 将QString转换为std::string（UTF-8编码，解决中文路径问题）
    std::string dir = modelDir.toUtf8().toStdString();

    // 初始化模型文件路径
    pImpl_->detModelPath = dir + "/ch_PP-OCRv4_det_infer.onnx";
    pImpl_->recModelPath = dir + "/ch_PP-OCRv4_rec_infer.onnx";
    pImpl_->clsModelPath = dir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx";
    pImpl_->dictPath = dir + "/ppocr_keys_v1.txt";

    // 加载字符字典
    if (!loadDict(pImpl_->dictPath)) {
        qWarning() << "[RapidOCR] 加载字典文件失败:" << QString::fromStdString(pImpl_->dictPath);
        emit logMessage(QString::fromUtf8("加载字典文件失败: %1").arg(modelDir));
        return;
    }
    qDebug() << "[RapidOCR] 字典加载成功，字符数:" << charDict_.size();

    // 加载三个ONNX模型会话
#ifdef _WIN32
    // Windows平台：使用QString::toStdWString正确转换中文路径为宽字符串
    std::wstring wDetPath = (modelDir + "/ch_PP-OCRv4_det_infer.onnx").toStdWString();
    std::wstring wRecPath = (modelDir + "/ch_PP-OCRv4_rec_infer.onnx").toStdWString();
    std::wstring wClsPath = (modelDir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx").toStdWString();

    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, wDetPath.c_str(), pImpl_->sessionOpts, &pImpl_->detSession);
    if (status != nullptr) goto LOAD_ERROR;

    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, wRecPath.c_str(), pImpl_->sessionOpts, &pImpl_->recSession);
    if (status != nullptr) goto LOAD_ERROR;

    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, wClsPath.c_str(), pImpl_->sessionOpts, &pImpl_->clsSession);
    if (status != nullptr) goto LOAD_ERROR;
#else
    // Linux平台：直接使用char*路径
    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, pImpl_->detModelPath.c_str(), pImpl_->sessionOpts, &pImpl_->detSession);
    if (status != nullptr) goto LOAD_ERROR;

    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, pImpl_->recModelPath.c_str(), pImpl_->sessionOpts, &pImpl_->recSession);
    if (status != nullptr) goto LOAD_ERROR;

    status = pImpl_->ortApi->CreateSession(pImpl_->ortEnv, pImpl_->clsModelPath.c_str(), pImpl_->sessionOpts, &pImpl_->clsSession);
    if (status != nullptr) goto LOAD_ERROR;
#endif

    ready_ = true;
    qDebug() << "[RapidOCR] 引擎初始化成功";
    emit logMessage(QString::fromUtf8("RapidOCR引擎初始化成功"));
    return;

LOAD_ERROR:
    {
        qWarning() << "[RapidOCR] 加载ONNX模型失败:" << pImpl_->ortApi->GetErrorMessage(status);
        emit logMessage(QString::fromUtf8("加载ONNX模型失败: %1").arg(pImpl_->ortApi->GetErrorMessage(status)));
        pImpl_->ortApi->ReleaseStatus(status);
    }
}

/**
 * @brief 析构函数
 */
RapidOcrEngine::~RapidOcrEngine()
{
    // Impl的析构函数会自动释放所有ONNX Runtime资源
}

/**
 * @brief 获取引擎是否初始化成功
 */
bool RapidOcrEngine::isReady() const
{
    return ready_;
}

// ============================================================
// 字典加载
// ============================================================

/**
 * @brief 加载字符字典文件
 * @details 读取PP-OCR官方字典ppocr_keys_v1.txt，每行一个字符
 *          在字典开头插入空白符（对应CTC解码的blank占位符，索引0）
 *          使用QFile读取，支持中文路径
 * @param dictPath 字典文件路径
 * @return true 加载成功，false 加载失败
 */
bool RapidOcrEngine::loadDict(const std::string &dictPath)
{
    // 使用QFile读取字典文件（支持中文路径）
    QString qDictPath = QString::fromStdString(dictPath);
    QFile dictFile(qDictPath);
    if (!dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[RapidOCR] 无法打开字典文件:" << qDictPath;
        return false;
    }

    QTextStream stream(&dictFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8); // Qt6: 字典文件使用UTF-8编码
#else
    stream.setCodec("UTF-8"); // 字典文件使用UTF-8编码
#endif
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (!line.isEmpty()) {
            charDict_.push_back(line.toStdString());
        }
    }
    dictFile.close();

    // 在字典开头插入空白符（PP-OCR字典规范，索引0为CTC blank）
    charDict_.insert(charDict_.begin(), "");
    return true;
}

// ============================================================
// 图像预处理
// ============================================================

/**
 * @brief 检测模型预处理
 * @details 将任意分辨率图像缩放填充到640×640，进行归一化和NCHW格式转换
 *          保持长宽比缩放，不足部分用白色填充
 * @param src 原始图像
 * @param padImg 输出：填充后的640×640图像
 * @param dst 输出：NCHW格式一维浮点数组（1×3×640×640）
 * @return true 预处理成功
 */
bool RapidOcrEngine::preprocessDet(const cv::Mat &src, cv::Mat &padImg, cv::Mat &dst)
{
    if (src.empty()) return false;

    // 计算等比缩放比例（取宽高最小比例，确保图像完整放入640×640）
    int srcH = src.rows;
    int srcW = src.cols;
    float scaleX = static_cast<float>(DET_INPUT_WIDTH) / srcW;
    float scaleY = static_cast<float>(DET_INPUT_HEIGHT) / srcH;
    float scale = std::min(scaleX, scaleY);

    // 保存缩放比例（用于后续坐标映射回原图）
    detScaleRatio_ = scale;

    // 按比例缩放图像
    int resizeW = static_cast<int>(srcW * scale);
    int resizeH = static_cast<int>(srcH * scale);
    cv::Mat imgResized;
    cv::resize(src, imgResized, cv::Size(resizeW, resizeH), 0, 0, cv::INTER_LINEAR);

    // 计算填充尺寸（居中填充）
    int padW = (DET_INPUT_WIDTH - resizeW) / 2;
    int padH = (DET_INPUT_HEIGHT - resizeH) / 2;

    // 保存填充偏移（用于后续坐标映射回原图）
    detPadW_ = padW;
    detPadH_ = padH;

    // 四周填充白色（255,255,255），使图像尺寸达到640×640
    cv::Mat imgPadded;
    cv::copyMakeBorder(imgResized, imgPadded, padH, DET_INPUT_HEIGHT - resizeH - padH,
                       padW, DET_INPUT_WIDTH - resizeW - padW,
                       cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));

    // 归一化：像素值从[0,255]缩放到[0,1]
    cv::Mat imgFloat;
    imgPadded.convertTo(imgFloat, CV_32F, 1.0 / 255.0);

    // NHWC → NCHW 格式转换（分离通道后按CHW顺序排列）
    std::vector<cv::Mat> chwChannels(3);
    cv::split(imgFloat, chwChannels);

    // 构造一维NCHW数组
    std::vector<float> nchwData;
    nchwData.reserve(3 * DET_INPUT_HEIGHT * DET_INPUT_WIDTH);
    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < DET_INPUT_HEIGHT; ++h) {
            const float *rowPtr = chwChannels[c].ptr<float>(h);
            nchwData.insert(nchwData.end(), rowPtr, rowPtr + DET_INPUT_WIDTH);
        }
    }

    // 转换为cv::Mat输出
    cv::Mat img = cv::Mat(1, 3 * DET_INPUT_HEIGHT * DET_INPUT_WIDTH, CV_32F, nchwData.data()).clone();
    if (img.empty()) return false;
    padImg = imgPadded;
    dst = img.clone();
    return true;
}

/**
 * @brief 分类模型预处理
 * @details 将文本区域图像缩放到192×48，归一化并做BGR→RGB通道交换
 * @param src 裁剪出的文本区域图像
 * @param dst 输出：预处理后的数据
 * @return true 预处理成功
 */
bool RapidOcrEngine::preprocessCls(const cv::Mat &src, cv::Mat &dst)
{
    if (src.empty()) return false;

    // 缩放到分类模型输入尺寸（192×48）
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(CLS_INPUT_WIDTH, CLS_INPUT_HEIGHT), cv::INTER_LINEAR);

    // 归一化到[0,1]
    resized.convertTo(resized, CV_32F, 1.0 / 255.0);

    // BGR → RGB 通道交换
    std::vector<cv::Mat> channels(3);
    cv::split(resized, channels);
    std::swap(channels[0], channels[2]); // 交换B和R通道
    cv::Mat img;
    cv::merge(channels, img);
    img = img.reshape(1, 3); // 重排为3行（每行一个通道的平面数据）

    if (img.empty()) return false;
    dst = img.clone();
    return true;
}

/**
 * @brief 识别模型预处理
 * @details 通道转换→直方图均衡化增强→等比缩放→右侧补白→归一化
 * @param src 文本区域图像（方向校正后）
 * @param dst 输出：预处理后的数据
 * @return true 预处理成功
 */
bool RapidOcrEngine::preprocessRec(const cv::Mat &src, cv::Mat &dst)
{
    if (src.empty()) return false;

    cv::Mat img;
    // 确保图像为3通道BGR
    if (src.channels() == 1) {
        cvtColor(src, img, cv::COLOR_GRAY2BGR);
    } else if (src.channels() == 3) {
        img = src.clone();
    } else {
        return false;
    }

    // 图像增强：灰度化→直方图均衡化→转回BGR（提升文字对比度）
    cv::Mat gray;
    cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    equalizeHist(gray, gray);
    cvtColor(gray, img, cv::COLOR_GRAY2BGR);

    // 等比缩放：高度缩放到48，宽度按比例缩放（最大640）
    int srcW = img.cols;
    int srcH = img.rows;
    float scale = static_cast<float>(REC_INPUT_HEIGHT) / static_cast<float>(srcH);
    int resizeW = static_cast<int>(srcW * scale);
    resizeW = std::min(resizeW, REC_INPUT_WIDTH);

    cv::Mat resizeImg;
    cv::resize(img, resizeImg, cv::Size(resizeW, REC_INPUT_HEIGHT), cv::INTER_LINEAR);

    // 右侧补白边，使宽度达到640
    int padRight = REC_INPUT_WIDTH - resizeW;
    padRight = std::max(padRight, 0);
    cv::copyMakeBorder(resizeImg, img, 0, 0, 0, padRight, cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));

    // 数据类型转换为浮点
    img.convertTo(img, CV_32F);

    // 归一化：(pixel - 127.5) / 127.5，映射到[-1,1]
    img = (img - MEAN_VAL) * NORM_VAL;

    if (img.empty()) return false;
    dst = img.clone();
    return true;
}

// ============================================================
// ONNX Runtime C API推理
// ============================================================

/**
 * @brief 使用ONNX Runtime C API运行模型推理
 * @details 封装C API的推理流程：创建输入张量→运行推理→获取输出张量信息
 * @param session ONNX会话指针
 * @param inputName 输入张量名称
 * @param outputName 输出张量名称
 * @param inputData 输入数据
 * @param inputSize 输入数据元素个数
 * @param inputDims 输入张量维度
 * @param outputData 输出：结果数据指针
 * @param outputDims 输出：结果张量维度
 * @return true 推理成功
 */
bool RapidOcrEngine::runInference(OrtSession *session, const char *inputName, const char *outputName,
                                  const float *inputData, size_t inputSize,
                                  const std::vector<int64_t> &inputDims,
                                  float **outputData, std::vector<int64_t> &outputDims)
{
    if (!session || !pImpl_->ortApi) return false;

    OrtStatus *status = nullptr;
    OrtValue *inputTensor = nullptr;
    OrtValue *outputTensor = nullptr;

    // 创建CPU内存信息
    OrtMemoryInfo *memoryInfo = nullptr;
    status = pImpl_->ortApi->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 创建内存信息失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return false;
    }

    // 创建输入张量
    status = pImpl_->ortApi->CreateTensorWithDataAsOrtValue(
        memoryInfo, const_cast<float *>(inputData), inputSize * sizeof(float),
        inputDims.data(), inputDims.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor);
    pImpl_->ortApi->ReleaseMemoryInfo(memoryInfo);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 创建输入张量失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return false;
    }

    // 运行模型推理
    const char *inputNames[] = {inputName};
    const char *outputNames[] = {outputName};
    status = pImpl_->ortApi->Run(session, nullptr, inputNames, &inputTensor, 1,
                                 outputNames, 1, &outputTensor);
    pImpl_->ortApi->ReleaseValue(inputTensor);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 推理运行失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        return false;
    }

    // 获取输出张量信息
    OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
    status = pImpl_->ortApi->GetTensorTypeAndShape(outputTensor, &tensorInfo);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 获取张量信息失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        pImpl_->ortApi->ReleaseValue(outputTensor);
        return false;
    }

    // 获取输出维度
    size_t dimCount = 0;
    status = pImpl_->ortApi->GetDimensionsCount(tensorInfo, &dimCount);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 获取输出维度数量失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        pImpl_->ortApi->ReleaseTensorTypeAndShapeInfo(tensorInfo);
        pImpl_->ortApi->ReleaseValue(outputTensor);
        return false;
    }
    outputDims.resize(dimCount);
    status = pImpl_->ortApi->GetDimensions(tensorInfo, outputDims.data(), dimCount);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 获取输出维度失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        pImpl_->ortApi->ReleaseTensorTypeAndShapeInfo(tensorInfo);
        pImpl_->ortApi->ReleaseValue(outputTensor);
        return false;
    }

    // 获取输出数据指针
    status = pImpl_->ortApi->GetTensorMutableData(outputTensor, reinterpret_cast<void **>(outputData));
    pImpl_->ortApi->ReleaseTensorTypeAndShapeInfo(tensorInfo);
    if (status != nullptr) {
        qWarning() << "[RapidOCR] 获取输出数据失败:" << pImpl_->ortApi->GetErrorMessage(status);
        pImpl_->ortApi->ReleaseStatus(status);
        pImpl_->ortApi->ReleaseValue(outputTensor);
        return false;
    }

    // 注意：outputTensor不能在这里释放，因为outputData指向其内部内存
    // 调用者使用完数据后需要自行释放outputTensor
    // 这里将outputTensor指针保存以便后续释放（简化处理：数据拷贝后立即释放）
    // 由于数据指针可能在使用期间被释放，这里直接拷贝数据
    // 实际上GetTensorMutableData返回的指针在outputTensor释放前有效
    // 所以我们需要在调用者使用完之前不释放outputTensor
    // 为了简化，我们拷贝数据到调用者提供的缓冲区

    // 计算输出数据总元素数
    size_t totalElements = 1;
    for (auto d : outputDims) totalElements *= d;

    // 拷贝数据（因为释放outputTensor后数据将无效）
    float *dataCopy = new float[totalElements];
    std::memcpy(dataCopy, *outputData, totalElements * sizeof(float));
    *outputData = dataCopy;

    // 释放outputTensor
    pImpl_->ortApi->ReleaseValue(outputTensor);

    return true;
}

// ============================================================
// 模型推理
// ============================================================

/**
 * @brief 检测模型推理
 * @details 构建输入张量→运行检测模型→调用后处理解析文本框
 * @param src 填充后的图像（用于后处理坐标映射）
 * @param input 预处理后的NCHW格式输入数据
 * @param textBoxes 输出：检测到的文本框列表
 * @return true 推理成功
 */
bool RapidOcrEngine::inferDet(const cv::Mat &src, const cv::Mat &input, std::vector<TextBox> &textBoxes)
{
    // 构建输入张量维度 [1, 3, 640, 640]
    std::vector<int64_t> inputDims = {1, 3, DET_INPUT_HEIGHT, DET_INPUT_WIDTH};
    size_t inputSize = 1 * 3 * DET_INPUT_HEIGHT * DET_INPUT_WIDTH;

    // 运行推理
    float *outputData = nullptr;
    std::vector<int64_t> outputDims;
    if (!runInference(pImpl_->detSession, pImpl_->detInputName, pImpl_->detOutputName,
                      input.ptr<float>(), inputSize, inputDims, &outputData, outputDims)) {
        return false;
    }

    // 解析输出：[1, 1, H, W]
    int outputH = static_cast<int>(outputDims[2]);
    int outputW = static_cast<int>(outputDims[3]);

    // 调用后处理
    bool result = postprocessDet(outputData, outputH, outputW, src, textBoxes);

    // 释放拷贝的输出数据
    delete[] outputData;

    return result;
}

/**
 * @brief 检测结果后处理：解析热力图为文本框
 * @details 模型输出为概率热力图，通过二值化→轮廓检测→NMS筛选得到文本框
 * @param outputData 模型输出数据指针
 * @param outputH 输出特征图高度
 * @param outputW 输出特征图宽度
 * @param src 填充后的图像（保留接口一致性，当前未使用）
 * @param textBoxes 输出：解析出的文本框列表
 * @return true 后处理成功
 */
bool RapidOcrEngine::postprocessDet(const float *outputData, int outputH, int outputW,
                                    const cv::Mat &src, std::vector<TextBox> &textBoxes)
{
    (void)src; // 参数保留用于接口一致性，坐标约束已改用outputW/outputH
    std::vector<TextBox> boxes;

    // 将输出数据构造为概率热力图
    cv::Mat scoreMap(outputH, outputW, CV_32F, const_cast<float *>(outputData));

    // 二值化：高于阈值的区域为前景（文字区域）
    cv::Mat scoreMask;
    cv::threshold(scoreMap, scoreMask, DET_SCORE_THRESHOLD, 1.0f, cv::THRESH_BINARY);

    // 转换为8位无符号整型（findContours要求CV_8UC1）
    cv::Mat scoreMask8u;
    scoreMask.convertTo(scoreMask8u, CV_8UC1, 255.0f);

    // 查找轮廓（外轮廓模式，简化轮廓）
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(scoreMask8u, contours, hierarchy,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 遍历每个轮廓，计算文本框
    for (const auto &contour : contours) {
        // 计算轮廓的外接矩形
        cv::Rect rect = cv::boundingRect(contour);

        // 过滤过小的噪声区域
        if (rect.width < DET_BOX_THRESHOLD || rect.height < DET_BOX_THRESHOLD) {
            continue;
        }

        // 字符区域扩展阈值
        int det_extend_val;

        //计算字符扩展阈值，并根据阈值重新计算字符区域
        det_extend_val = (DET_INPUT_HEIGHT /rect.height > rect.height/2) ? (rect.height / 2) : (DET_INPUT_HEIGHT / rect.height);
        int x = rect.x - det_extend_val;
        int y = rect.y - det_extend_val;
        int w = rect.width + det_extend_val * 2;
        int h = rect.height + det_extend_val * 2;

        // 边界检查（约束在填充图640×640范围内）
        x = std::max(0, x);
        y = std::max(0, y);
        w = std::min(w, outputW - x);
        h = std::min(h, outputH - y);
        if (w <= 0 || h <= 0) continue;

        // 构造文本框四个顶点坐标
        std::vector<cv::Point2f> boxPoints = {
            cv::Point2f(x, y),
            cv::Point2f(x + w, y),
            cv::Point2f(x + w, y + h),
            cv::Point2f(x, y + h)};

        // 坐标约束到填充图范围内
        for (auto &point : boxPoints) {
            point.x = std::max(0.0f, std::min(static_cast<float>(outputW - 1), point.x));
            point.y = std::max(0.0f, std::min(static_cast<float>(outputH - 1), point.y));
        }

        // 计算文本框区域的平均置信度
        cv::Mat roiScore = scoreMap(cv::Rect(x, y, w, h));
        float score = static_cast<float>(cv::mean(roiScore)[0]);

        // 添加到候选文本框列表
        TextBox box;
        box.points = boxPoints;
        box.detScore = score;
        boxes.push_back(box);
    }

    // 非极大值抑制（NMS）：去除重叠的文本框
    std::vector<int> indices;
    std::vector<cv::Rect> rects;
    std::vector<float> scores;
    for (const auto &box : boxes) {
        cv::Rect rect = cv::boundingRect(box.points);
        rects.push_back(rect);
        scores.push_back(box.detScore);
    }
    cv::dnn::NMSBoxes(rects, scores, DET_SCORE_THRESHOLD, DET_NMS_THRESHOLD, indices);

    // 筛选NMS后的文本框
    std::vector<TextBox> finalBoxes;
    for (int idx : indices) {
        finalBoxes.push_back(boxes[idx]);
    }

    textBoxes = finalBoxes;
    return !textBoxes.empty();
}

/**
 * @brief 分类模型推理（判断文字方向）
 * @details 输出两个概率值：score_0（正方向）、score_1（反方向）
 *          若score_1 > 阈值，则文字需要旋转180度
 * @param input 预处理后的输入数据
 * @return true 正方向，false 需旋转180度
 */
bool RapidOcrEngine::inferCls(const cv::Mat &input)
{
    // 构建输入张量
    std::vector<int64_t> inputShape = {1, 3, CLS_INPUT_HEIGHT, CLS_INPUT_WIDTH};
    size_t inputSize = 1 * 3 * CLS_INPUT_HEIGHT * CLS_INPUT_WIDTH;

    // 运行推理
    float *outputData = nullptr;
    std::vector<int64_t> outputDims;
    if (!runInference(pImpl_->clsSession, pImpl_->clsInputName, pImpl_->clsOutputName,
                      input.ptr<float>(), inputSize, inputShape, &outputData, outputDims)) {
        return true; // 默认正方向
    }

    // 解析输出：两个概率值分别对应正方向和反方向
    float score1 = outputData[1]; // 反方向概率

    // 释放数据
    delete[] outputData;

    // 若反方向概率大于阈值，返回false（需要旋转）
    return (score1 <= CLS_THRESHOLD);
}

/**
 * @brief 识别模型推理（文字区域→文本）
 * @details 构建输入张量→运行识别模型→CTC解码输出文本
 * @param src 原始文本区域图像（未使用，保留接口一致性）
 * @param input 预处理后的输入数据
 * @param recScore 输出：识别置信度（0~1）
 * @param text 输出：识别出的文本
 * @return true 识别成功
 */
bool RapidOcrEngine::inferRec(const cv::Mat &src, const cv::Mat &input, float &recScore, std::string &text)
{
    (void)src; // 避免未使用参数警告

    // 构建输入张量 [1, 3, 48, 640]
    std::vector<int64_t> inputShape = {1, 3, REC_INPUT_HEIGHT, REC_INPUT_WIDTH};
    size_t inputTensorSize = 1 * 3 * REC_INPUT_HEIGHT * REC_INPUT_WIDTH;
    std::vector<float> inputTensorData(inputTensorSize);

    // 将HWC格式数据转换为CHW格式（模型输入要求）
    const float *imgDataPtr = input.ptr<float>();
    int hw = REC_INPUT_HEIGHT * REC_INPUT_WIDTH;
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < hw; ++i) {
            inputTensorData[c * hw + i] = imgDataPtr[i * 3 + c];
        }
    }

    // 运行推理
    float *outputData = nullptr;
    std::vector<int64_t> outputDims;
    if (!runInference(pImpl_->recSession, pImpl_->recInputName, pImpl_->recOutputName,
                      inputTensorData.data(), inputTensorSize, inputShape, &outputData, outputDims)) {
        return false;
    }

    // 解析推理输出：[1, T, C] → 每个时间步取最大概率字符
    int seqLen = static_cast<int>(outputDims[1]);  // 时间步长度
    int dictSize = static_cast<int>(outputDims[2]); // 字典大小

    // CTC解码：遍历每个时间步，取最大概率字符
    std::string recText;
    float totalScore = 0.0f;
    int validCharCount = 0;
    int prevValidIndex = 0;

    for (int i = 0; i < seqLen; i++) {
        int maxIndex = 0;
        float maxProb = 0.0f;

        // 在当前时间步的所有字符中找最大概率
        for (int j = 0; j < dictSize; ++j) {
            float prob = outputData[i * dictSize + j];
            if (prob > maxProb) {
                maxProb = prob;
                maxIndex = j;
            }
        }

        // CTC解码核心：跳过blank(索引0)和重复字符
        if (maxIndex != 0 && maxIndex != prevValidIndex) {
            if (maxIndex < static_cast<int>(charDict_.size())) {
                recText += charDict_[maxIndex];
                totalScore += maxProb;
                validCharCount++;
            }
            prevValidIndex = maxIndex;
        }
    }

    // 计算平均置信度
    if (validCharCount > 0) {
        recScore = totalScore / validCharCount;
        recScore = std::min(1.0f, std::max(0.0f, recScore));
    } else {
        recScore = 0.0f;
    }

    // 释放数据
    delete[] outputData;

    text = recText;
    return !text.empty();
}

// ============================================================
// 文本框裁剪与标注
// ============================================================

/**
 * @brief 从图像中裁剪文本区域（透视变换）
 * @details 使用透视变换将倾斜的文本框矫正为水平矩形
 * @param src 原始图像
 * @param box 文本框信息（四个顶点坐标）
 * @param dst 输出：裁剪并矫正后的文本区域图像
 * @return true 裁剪成功
 */
bool RapidOcrEngine::cropTextBox(const cv::Mat &src, const TextBox &box, cv::Mat &dst)
{
    if (src.empty() || box.points.size() != 4) return false;

    // 计算文本框的宽度和高度（取对边的最大值）
    float width1 = static_cast<float>(cv::norm(box.points[0] - box.points[1]));
    float width2 = static_cast<float>(cv::norm(box.points[2] - box.points[3]));
    float height1 = static_cast<float>(cv::norm(box.points[1] - box.points[2]));
    float height2 = static_cast<float>(cv::norm(box.points[3] - box.points[0]));
    float maxWidth = std::max(width1, width2);
    float maxHeight = std::max(height1, height2);

    if (maxWidth < 1 || maxHeight < 1) return false;

    // 目标矩形四个顶点
    std::vector<cv::Point2f> dstPts = {
        cv::Point2f(0, 0),
        cv::Point2f(maxWidth, 0),
        cv::Point2f(maxWidth, maxHeight),
        cv::Point2f(0, maxHeight)};

    // 计算透视变换矩阵并执行变换
    cv::Mat warpMat = cv::getPerspectiveTransform(box.points, dstPts);
    cv::Mat img;
    cv::warpPerspective(src, img, warpMat, cv::Size(static_cast<int>(maxWidth), static_cast<int>(maxHeight)),
                        cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    if (img.empty()) return false;
    dst = img.clone();
    return true;
}

/**
 * @brief 在图像上标注文本框位置
 * @details 仅用红色矩形框标注文字区域，不绘制文字标签
 *          文字标签由ImageDisplayView的setTextBoxes使用QGraphicsTextItem绘制（支持中文）
 *          避免使用cv::putText导致中文乱码问题
 * @param src 原始图像
 * @param textBoxes 文本框列表（包含识别结果）
 * @param dst 输出：标注后的图像
 * @return true 标注成功
 */
bool RapidOcrEngine::drawTextBoxes(const cv::Mat &src, const std::vector<TextBox> &textBoxes, cv::Mat &dst)
{
    cv::Mat img = src.clone();
    for (const auto &box : textBoxes) {
        // 仅绘制文本框边框（红色，线宽2），不绘制文字标签
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            cv::line(img, box.points[i], box.points[j], cv::Scalar(0, 0, 255), 2);
        }
    }

    if (img.empty()) return false;
    dst = img.clone();
    return true;
}

// ============================================================
// 核心OCR流程
// ============================================================

/**
 * @brief 核心OCR流程：检测→分类→识别
 * @details 完整流程：
 *          1. 读取图片
 *          2. 检测模型预处理→推理→后处理（得到文本框）
 *          3. 遍历文本框：裁剪→分类（方向判断）→识别（文字输出）
 *          4. 在原图上标注结果
 * @param imagePath 图片路径
 * @param boxes 输出：文本框列表（含识别结果）
 * @param recText 输出：完整识别文本
 * @param resultImg 输出：标注后的结果图像
 * @return true 识别成功
 */
bool RapidOcrEngine::run(const QString &imagePath, std::vector<TextBox> &boxes,
                         QString &recText, cv::Mat &resultImg)
{
    if (!ready_) {
        emit logMessage(QString::fromUtf8("OCR引擎未初始化"));
        return false;
    }

    emit logMessage(QString::fromUtf8("开始OCR识别: %1").arg(imagePath));

    // 使用QFile读取图片字节（支持中文路径），再用cv::imdecode解码
    QFile imageFile(imagePath);
    if (!imageFile.open(QIODevice::ReadOnly)) {
        emit logMessage(QString::fromUtf8("读取图片失败: %1").arg(imagePath));
        return false;
    }
    QByteArray imageData = imageFile.readAll();
    imageFile.close();

    cv::Mat img = cv::imdecode(cv::Mat(1, imageData.size(), CV_8UC1, imageData.data()), cv::IMREAD_COLOR);
    if (img.empty()) {
        emit logMessage(QString::fromUtf8("解码图片失败: %1").arg(imagePath));
        return false;
    }

    // 保存原始图像副本（用于后续裁剪和绘制）
    cv::Mat originalImg = img.clone();

    // ---- 第一阶段：文本检测 ----
    emit logMessage(QString::fromUtf8("阶段1: 文本检测..."));

    cv::Mat padImg, inputMat;
    if (!preprocessDet(img, padImg, inputMat)) {
        emit logMessage(QString::fromUtf8("检测预处理失败"));
        return false;
    }

    if (!inferDet(padImg, inputMat, boxes)) {
        emit logMessage(QString::fromUtf8("未检测到文本区域"));
        return false;
    }
    emit logMessage(QString::fromUtf8("检测到 %1 个文本框").arg(boxes.size()));

    // 将文本框坐标从640×640填充图坐标系映射回原始图像坐标系
    // 逆变换公式：origX = (padX - padW) / scale, origY = (padY - padH) / scale
    for (auto &box : boxes) {
        for (auto &pt : box.points) {
            float origX = (pt.x - detPadW_) / detScaleRatio_;
            float origY = (pt.y - detPadH_) / detScaleRatio_;
            // 约束到原图范围内
            pt.x = std::max(0.0f, std::min(static_cast<float>(originalImg.cols - 1), origX));
            pt.y = std::max(0.0f, std::min(static_cast<float>(originalImg.rows - 1), origY));
        }
    }

    // 对文本框按从上到下、从左到右排序（确保识别结果顺序符合阅读习惯）
    // 排序规则：先按顶部 y 坐标排序（从上到下），同一行再按左侧 x 坐标排序（从左到右）
    std::sort(boxes.begin(), boxes.end(), [](const TextBox &a, const TextBox &b) {
        // 取文本框所有顶点中最小的 y 值作为顶部坐标
        float aTopY = std::min({a.points[0].y, a.points[1].y, a.points[2].y, a.points[3].y});
        float bTopY = std::min({b.points[0].y, b.points[1].y, b.points[2].y, b.points[3].y});
        // 取文本框所有顶点中最小的 x 值作为左侧坐标
        float aLeftX = std::min({a.points[0].x, a.points[1].x, a.points[2].x, a.points[3].x});
        float bLeftX = std::min({b.points[0].x, b.points[1].x, b.points[2].x, b.points[3].x});

        // y 坐标差距超过文本框高度的1/2视为不同行，按 y 排序
        float aHeight = std::max({a.points[0].y, a.points[1].y, a.points[2].y, a.points[3].y}) - aTopY;
        float bHeight = std::max({b.points[0].y, b.points[1].y, b.points[2].y, b.points[3].y}) - bTopY;
        float rowThreshold = std::max(aHeight, bHeight) * 0.5f;
        if (std::abs(aTopY - bTopY) > rowThreshold) {
            return aTopY < bTopY;  // 从上到下
        }
        // 同一行，按 x 坐标排序（从左到右）
        return aLeftX < bLeftX;
    });

    // ---- 第二阶段：遍历文本框，执行分类与识别 ----
    emit logMessage(QString::fromUtf8("阶段2: 文字识别..."));

    int totalBoxes = static_cast<int>(boxes.size());
    for (int i = 0; i < totalBoxes; ++i) {
        auto &box = boxes[i];
        emit progressChanged(i + 1, totalBoxes);

        // 从原始图像裁剪文本区域（坐标已映射回原图空间）
        cv::Mat textMat;
        if (!cropTextBox(originalImg, box, textMat)) {
            qDebug() << "[RapidOCR] 裁剪文本区域失败, 索引:" << i;
            continue;
        }

        // 方向分类
        cv::Mat clsInput;
        if (!preprocessCls(textMat, clsInput)) {
            qDebug() << "[RapidOCR] 分类预处理失败, 索引:" << i;
            continue;
        }

        // 若分类模型判断文字方向反了，旋转180度
        if (!inferCls(clsInput)) {
            cv::rotate(textMat, textMat, cv::ROTATE_180);
        }

        // 文字识别
        cv::Mat recInput;
        if (!preprocessRec(textMat, recInput)) {
            qDebug() << "[RapidOCR] 识别预处理失败, 索引:" << i;
            continue;
        }

        float recScore = 0.0f;
        std::string text;
        if (!inferRec(textMat, recInput, recScore, text)) {
            qDebug() << "[RapidOCR] 文字识别失败, 索引:" << i;
            continue;
        }

        // 保存识别结果
        box.text = text;
        box.recScore = recScore;
        recText += QString::fromUtf8(text.c_str());
    }

    // ---- 在原始图像上标注识别结果（仅框线，文字由ImageDisplayView绘制） ----
    drawTextBoxes(originalImg, boxes, resultImg);

    // 检查识别结果
    if (recText.isEmpty()) {
        emit logMessage(QString::fromUtf8("识别结果为空"));
        return false;
    }

    emit logMessage(QString::fromUtf8("OCR识别完成, 结果: %1").arg(recText));
    emit progressChanged(totalBoxes, totalBoxes);
    return true;
}
