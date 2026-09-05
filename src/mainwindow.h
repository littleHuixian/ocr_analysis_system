/**
 * @file mainwindow.h
 * @brief 主窗口头文件
 * @details ocr_analysis_system主窗口类，整合OCR引擎、图片显示、文件列表、
 *          结果文本编辑器，提供完整的文字识别交互界面
 * @author 先瞳编码, 关注微信公众号"先瞳编码"，获取最新技术分享
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QList>
#include <vector>
#include "RapidOcrEngine/RapidOcrEngine.h"
#include "CsvExporter/CsvExporter.h"

// 前向声明，减少头文件依赖
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
class QListWidget;
class QPlainTextEdit;
class QLabel;
class QProgressBar;
class QSplitter;
class ImageDisplayView;

/**
 * @brief 主窗口类
 * @details 整合所有功能模块的主界面，包含：
 *          - 顶部工具栏：导入、批量导入、识别、批量识别、导出、清空按钮
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口指针
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~MainWindow();

private slots:
    /**
     * @brief 导入单张图片
     */
    void onImportImage();

    /**
     * @brief 批量导入图片（按文件夹导入，自动搜索图片并依次识别）
     */
    void onBatchImport();

    /**
     * @brief 执行当前图片OCR识别
     */
    void onRecognize();

    /**
     * @brief 批量识别所有图片
     */
    void onBatchRecognize();

    /**
     * @brief 导出识别结果为CSV
     */
    void onExportCsv();

    /**
     * @brief 文件列表选中项变化
     * @param currentRow 当前选中行索引
     */
    void onFileSelected(int currentRow);

    /**
     * @brief OCR进度更新
     * @param current 当前处理索引
     * @param total 总数
     */
    void onProgressChanged(int current, int total);

    /**
     * @brief OCR日志信息
     * @param message 日志内容
     */
    void onLogMessage(const QString &message);

    /**
     * @brief 适应窗口显示
     */
    void onFitWindow();

    /**
     * @brief 恢复1:1原始尺寸
     */
    void onResetSize();

    /**
     * @brief 清空所有数据（包括图片显示）
     */
    void onClearAll();

protected:
    /**
     * @brief 窗口显示事件
     * @details 首次显示时设置分割器比例（构造函数中窗口尺寸尚未确定）
     * @param event 显示事件指针
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * @brief 初始化UI控件连接
     */
    void initConnections();

    /**
     * @brief 初始化分割器比例
     * @details 设置水平分割器为1:2（文件列表:右侧区域）
     *          设置垂直分割器为2:1（图像显示:识别结果）
     */
    void initSplitterRatio();

    /**
     * @brief 从资源文件加载QSS样式
     */
    void loadQssStyle();

    /**
     * @brief 添加图片到文件列表
     * @param filePath 图片文件路径
     */
    void addFileToList(const QString &filePath);

    /**
     * @brief 显示指定路径的图片（支持中文路径）
     * @param filePath 图片路径
     */
    void displayImage(const QString &filePath);

    /**
     * @brief 对单张图片执行OCR识别
     * @param filePath 图片路径
     * @return 识别结果文本
     */
    QString recognizeImage(const QString &filePath);

    /**
     * @brief 显示指定索引图片的OCR标注结果
     * @details 切换文件列表时，如果该图片已识别过，则恢复显示带文本框标注的结果图像
     * @param index 图片索引
     */
    void displayOcrResult(int index);

    Ui::MainWindow *ui;                 ///< UI界面对象指针

    // ---- UI控件指针 ----
    QListWidget *fileListWidget;        ///< 左侧文件列表
    ImageDisplayView *imageView;        ///< 图片显示视图
    QPlainTextEdit *resultTextEdit;     ///< 底部识别结果文本框
    QLabel *statusLabel;                ///< 状态栏标签
    QProgressBar *progressBar;          ///< 进度条
    QSplitter *mainSplitter;            ///< 主水平分割器
    QSplitter *rightSplitter;           ///< 右侧垂直分割器

    // ---- 功能模块 ----
    RapidOcrEngine *ocrEngine;          ///< OCR识别引擎
    CsvExporter *csvExporter;           ///< CSV导出器

    // ---- 数据成员 ----
    QStringList imageFiles;             ///< 当前导入的图片路径列表
    QList<OcrRecord> ocrRecords;        ///< 识别记录列表
    QList<std::vector<TextBox>> ocrBoxesList;  ///< 每张图片的OCR文本框列表（用于切换时恢复标注）
    QList<cv::Mat> ocrResultImgs;       ///< 每张图片的OCR标注结果图像（用于切换时恢复显示）
    int currentImageIndex;              ///< 当前显示的图片索引
    bool isBatchProcessing;             ///< 是否正在批量处理
    bool splitterRatioSet;              ///< 分割器比例是否已设置（防止重复设置）
};

#endif // MAINWINDOW_H
