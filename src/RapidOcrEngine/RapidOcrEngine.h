/**
 * @file RapidOcrEngine.h
 * @brief RapidOCR 字符识别引擎头文件
 * @details 基于ONNX Runtime C API的PP-OCRv4字符识别引擎，完整实现检测→分类→识别三阶段流程：
 *          1. 文本检测模型(ch_PP-OCRv4_det)：定位图片中的文字区域
 *          2. 方向分类模型(ch_ppocr_mobile_v2.0_cls)：判断文字方向是否需要旋转
 *          3. 文字识别模型(ch_PP-OCRv4_rec)：将文字区域图像转换为文本
 *          支持汉字、数字、英文字母、常见符号识别，自适应不同分辨率图片
 *          使用C API直接调用ONNX Runtime，避免C++封装层的MinGW兼容性问题
 * @author 
 */

#ifndef RAPIDOCRENGINE_H
#define RAPIDOCRENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include <QFile>
#include <QByteArray>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <onnxruntime/mingw_compat.h>  // MinGW SAL注解兼容层（必须在onnxruntime头文件之前包含）
#include <onnxruntime/core/session/onnxruntime_c_api.h>  // 使用C API，避免C++封装层兼容性问题
#include <memory>
#include <vector>
#include <string>

/**
 * @brief 文本框结构体（检测结果+识别结果）
 * @details 存储一个文字区域的完整信息：位置坐标、识别文本、各阶段置信度
 */
struct TextBox {
    std::vector<cv::Point2f> points;  ///< 文本框四个顶点（左上、右上、右下、左下）
    std::string text;                 ///< 识别出的文本内容
    float detScore = 0.0f;            ///< 检测置信度（0~1）
    float clsScore = 0.0f;            ///< 分类置信度（0~1）
    float recScore = 0.0f;            ///< 识别置信度（0~1）
};

/**
 * @brief RapidOCR字符识别引擎类
 * @details 封装PP-OCRv4三阶段OCR流程，继承QObject支持信号槽通信
 *          核心流程：检测(det) → 分类(cls) → 识别(rec)
 *          使用Pimpl模式隐藏ONNX Runtime C API实现细节
 */
class RapidOcrEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 显式构造函数
     * @param modelDir 模型文件目录路径（包含det/cls/rec三个onnx模型及字典文件）
     * @param parent 父QObject指针
     */
    explicit RapidOcrEngine(const QString &modelDir, QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~RapidOcrEngine();

    /**
     * @brief 禁止拷贝构造
     */
    RapidOcrEngine(const RapidOcrEngine &) = delete;
    RapidOcrEngine &operator=(const RapidOcrEngine &) = delete;

    /**
     * @brief 核心OCR接口：对单张图片执行完整识别流程
     * @param imagePath 输入图片路径
     * @param boxes 输出参数：检测到的文本框列表（包含识别结果）
     * @param recText 输出参数：拼接后的完整识别文本
     * @param resultImg 输出参数：标注了文本框的结果图像
     * @return true 识别成功，false 识别失败
     */
    bool run(const QString &imagePath, std::vector<TextBox> &boxes,
             QString &recText, cv::Mat &resultImg);

    /**
     * @brief 获取引擎是否初始化成功
     * @return true 模型加载成功，false 加载失败
     */
    bool isReady() const;

signals:
    /**
     * @brief 识别进度信号
     * @param current 当前处理的文本框索引
     * @param total 文本框总数
     */
    void progressChanged(int current, int total);

    /**
     * @brief 日志信息信号
     * @param message 日志内容
     */
    void logMessage(const QString &message);

private:
    /**
     * @brief 加载字符字典文件
     * @param dictPath 字典文件路径
     * @return true 加载成功
     */
    bool loadDict(const std::string &dictPath);

    /**
     * @brief 检测模型预处理：缩放+填充+归一化+NCHW转换
     * @param src 原始图像
     * @param padImg 输出：填充后的图像（用于后续裁剪文本区域）
     * @param dst 输出：NCHW格式的一维浮点数据（用于模型输入）
     * @return true 预处理成功
     */
    bool preprocessDet(const cv::Mat &src, cv::Mat &padImg, cv::Mat &dst);

    /**
     * @brief 分类模型预处理：缩放+归一化+通道转换
     * @param src 原始图像（裁剪后的文本区域）
     * @param dst 输出：预处理后的数据
     * @return true 预处理成功
     */
    bool preprocessCls(const cv::Mat &src, cv::Mat &dst);

    /**
     * @brief 识别模型预处理：通道转换+图像增强+缩放+归一化
     * @param src 原始图像（方向校正后的文本区域）
     * @param dst 输出：预处理后的数据
     * @return true 预处理成功
     */
    bool preprocessRec(const cv::Mat &src, cv::Mat &dst);

    /**
     * @brief 检测模型推理
     * @param src 填充后的图像（用于后处理坐标映射）
     * @param input 预处理后的输入数据
     * @param textBoxes 输出：检测到的文本框列表
     * @return true 推理成功
     */
    bool inferDet(const cv::Mat &src, const cv::Mat &input, std::vector<TextBox> &textBoxes);

    /**
     * @brief 分类模型推理（判断文字方向）
     * @param input 预处理后的输入数据
     * @return true 正方向，false 需旋转180度
     */
    bool inferCls(const cv::Mat &input);

    /**
     * @brief 识别模型推理（文字区域→文本）
     * @param src 原始文本区域图像
     * @param input 预处理后的输入数据
     * @param recScore 输出：识别置信度
     * @param text 输出：识别出的文本
     * @return true 识别成功
     */
    bool inferRec(const cv::Mat &src, const cv::Mat &input, float &recScore, std::string &text);

    /**
     * @brief 检测结果后处理：解析热力图为文本框
     * @param outputData 模型输出数据指针
     * @param outputH 输出特征图高度
     * @param outputW 输出特征图宽度
     * @param src 原始图像
     * @param textBoxes 输出：解析出的文本框列表
     * @return true 后处理成功
     */
    bool postprocessDet(const float *outputData, int outputH, int outputW,
                        const cv::Mat &src, std::vector<TextBox> &textBoxes);

    /**
     * @brief 在图像上标注文本框
     * @param src 原始图像
     * @param textBoxes 文本框列表
     * @param dst 输出：标注后的图像
     * @return true 标注成功
     */
    bool drawTextBoxes(const cv::Mat &src, const std::vector<TextBox> &textBoxes, cv::Mat &dst);

    /**
     * @brief 从图像中裁剪文本区域（透视变换）
     * @param src 原始图像
     * @param box 文本框信息
     * @param dst 输出：裁剪出的文本区域图像
     * @return true 裁剪成功
     */
    bool cropTextBox(const cv::Mat &src, const TextBox &box, cv::Mat &dst);

    /**
     * @brief 使用ONNX Runtime C API运行模型推理
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
    bool runInference(OrtSession *session, const char *inputName, const char *outputName,
                      const float *inputData, size_t inputSize,
                      const std::vector<int64_t> &inputDims,
                      float **outputData, std::vector<int64_t> &outputDims);

private:
    /**
     * @brief 内部实现结构体（Pimpl模式，隐藏ONNX Runtime C API细节）
     */
    struct Impl;
    std::shared_ptr<Impl> pImpl_;             ///< ONNX Runtime相关组件
    std::vector<std::string> charDict_;       ///< 字符字典数据
    bool ready_;                              ///< 引擎是否初始化成功标志

    // 检测预处理坐标映射参数（用于将640x640填充图坐标映射回原图坐标）
    float detScaleRatio_;                     ///< 检测预处理等比缩放比例
    int detPadW_;                             ///< 检测预处理水平填充偏移（像素）
    int detPadH_;                             ///< 检测预处理垂直填充偏移（像素）
};

#endif // RAPIDOCRENGINE_H
