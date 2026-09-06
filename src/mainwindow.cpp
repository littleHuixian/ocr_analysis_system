/**
 * @file mainwindow.cpp
 * @brief 主窗口实现文件
 * @details 实现ocr_analysis_system主窗口的全部功能逻辑：
 *          - 图片导入（单张/按文件夹批量导入，支持中文路径）
 *          - OCR识别（单张/批量，批量导入时自动识别）
 *          - 结果显示与编辑
 *          - CSV导出
 *          - QSS样式加载
 *          - 分割器比例设置（文件列表1/3，图像+结果2/3）
 * @author 
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ImageDisplayView/ImageDisplayView.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QStatusBar>
#include <QProgressBar>
#include <QSplitter>
#include <QDebug>
#include <QByteArray>
#include <QShowEvent>
#include <QTimer>
#include <opencv2/imgcodecs.hpp>

/**
 * @brief 构造函数
 * @details 初始化UI、加载QSS样式、设置分割器比例、创建OCR引擎和CSV导出器
 * @param parent 父窗口指针
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ocrEngine(nullptr)
    , csvExporter(new CsvExporter(this))
    , currentImageIndex(-1)
    , isBatchProcessing(false)
    , splitterRatioSet(false)
{
    // 初始化UI（从mainwindow.ui加载界面）
    ui = new Ui::MainWindow();
    ui->setupUi(this);

    // 获取UI控件指针
    fileListWidget = ui->fileListWidget;
    imageView = ui->imageView;
    resultTextEdit = ui->resultTextEdit;
    mainSplitter = ui->mainSplitter;
    rightSplitter = ui->rightSplitter;

    // 创建状态栏标签和进度条
    statusLabel = new QLabel(QString::fromUtf8("就绪"), this);
    progressBar = new QProgressBar(this);
    progressBar->setMinimumWidth(200);
    progressBar->setVisible(false);
    statusBar()->addWidget(statusLabel, 1);
    statusBar()->addPermanentWidget(progressBar);

    // 加载QSS样式
    loadQssStyle();

    // 初始化OCR引擎（模型文件在可执行文件目录下的model子目录）
    QString modelDir = QApplication::applicationDirPath() + "/model";
#ifdef Q_OS_MACOS
    if (!QDir(modelDir).exists()) {
        // macOS 下 Qt 生成 .app 包，模型目录位于 .app 外侧的 bin/model
        QDir appDir(QApplication::applicationDirPath());
        appDir.cdUp();
        appDir.cdUp();
        appDir.cdUp();
        modelDir = appDir.filePath("model");
    }
#endif
    ocrEngine = new RapidOcrEngine(modelDir, this);

    // 初始化信号槽连接
    initConnections();

    // 默认加载工程 test_images 目录中的图片到文件列表
    QString defaultImagesDir = QDir(QApplication::applicationDirPath() + "/../test_images").absolutePath();
    ui->leFilePath->setText(defaultImagesDir);
    loadImagesFromDirectory(defaultImagesDir);

    // 检查OCR引擎是否初始化成功
    if (!ocrEngine->isReady()) {
        statusLabel->setText(QString::fromUtf8("OCR引擎初始化失败，请检查模型文件"));
        QMessageBox::warning(this, QString::fromUtf8("警告"),
                             QString::fromUtf8("OCR引擎初始化失败！\n请确保model目录下存在以下文件：\n"
                                               "ch_PP-OCRv4_det_infer.onnx\n"
                                               "ch_PP-OCRv4_rec_infer.onnx\n"
                                               "ch_ppocr_mobile_v2.0_cls_infer.onnx\n"
                                               "ppocr_keys_v1.txt"));
    } else {
        statusLabel->setText(QString::fromUtf8("OCR引擎就绪"));
    }
}

/**
 * @brief 析构函数
 */
MainWindow::~MainWindow()
{
    delete ui;
}

/**
 * @brief 初始化信号槽连接
 * @details 使用新式Qt5信号槽语法连接所有按钮和控件的信号到对应槽函数
 */
void MainWindow::initConnections()
{
    // 按钮点击信号连接
    connect(ui->btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshFilePath);

    // 文件列表选中变化
    connect(fileListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onFileSelected);

    // OCR引擎信号连接
    connect(ocrEngine, &RapidOcrEngine::progressChanged, this, &MainWindow::onProgressChanged);
    connect(ocrEngine, &RapidOcrEngine::logMessage, this, &MainWindow::onLogMessage);
}

/**
 * @brief 初始化分割器比例
 * @details 设置水平分割器为1:2（文件列表:右侧区域）
 *          设置垂直分割器为2:1（图像显示:识别结果）
 */
void MainWindow::initSplitterRatio()
{
    // 水平分割器：文件列表占1/3，右侧区域占2/3
    int totalWidth = mainSplitter->width();
    if (totalWidth > 0) {
        mainSplitter->setSizes(QList<int>() << totalWidth / 3 << totalWidth * 2 / 3);
    }

    // 垂直分割器：图像显示占2/3，识别结果占1/3
    int totalHeight = rightSplitter->height();
    if (totalHeight > 0) {
        rightSplitter->setSizes(QList<int>() << totalHeight * 2 / 3 << totalHeight / 3);
    }
}

/**
 * @brief 窗口显示事件
 * @details 首次显示时设置分割器比例（构造函数中窗口尺寸尚未最终确定）
 * @param event 显示事件指针
 */
void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    // 首次显示时设置分割器比例
    if (!splitterRatioSet) {
        splitterRatioSet = true;
        initSplitterRatio();

        // 等窗口激活后再把焦点移开路径输入框（避免出现文本光标）
        QTimer::singleShot(0, this, [this]() {
            ui->leFilePath->clearFocus();
            imageView->setFocus(Qt::OtherFocusReason);
        });
    }
}

/**
 * @brief 从资源文件加载QSS样式
 * @details 从Qt资源系统加载全局样式base.qss，应用到整个应用程序
 */
void MainWindow::loadQssStyle()
{
    // 从资源路径加载QSS文件
    QFile qssFile(":/qss/base.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text))
    {
        QTextStream stream(&qssFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        stream.setEncoding(QStringConverter::Utf8); // 设置编码为UTF-8
#else
        stream.setCodec("UTF-8"); // 设置编码为UTF-8
#endif
        QString qssContent = stream.readAll();
        qssFile.close();

        // 应用QSS样式到整个应用程序
        qApp->setStyleSheet(qssContent);
    } else {
        qWarning() << "[MainWindow] 无法加载QSS样式文件: :/qss/base.qss";
    }
}

//导入单张图片 打开文件选择对话框，选择一张图片导入到文件列表
void MainWindow::onImportImage()
{
    // 打开文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("选择图片"),
        QDir::homePath(),
        QString::fromUtf8("图片文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        return; // 用户取消选择
    }

    // 添加到文件列表
    addFileToList(filePath);
}

/**
 * @brief 批量导入图片（按文件夹导入）
 * @details 打开文件夹选择对话框，自动搜索该文件夹下的所有图片文件，
 *          导入到文件列表，并依次自动执行OCR识别
 */
void MainWindow::onBatchImport()
{
    // 打开文件夹选择对话框
    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        QString::fromUtf8("选择图片文件夹"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dirPath.isEmpty()) {
        return; // 用户取消选择
    }

    // 搜索文件夹下所有图片文件
    QDir directory(dirPath);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tif" << "*.tiff";
    QStringList files = directory.entryList(filters, QDir::Files, QDir::Name);

    if (files.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("所选文件夹中没有找到图片文件"));
        return;
    }

    ui->leFilePath->setText(dirPath);
    // 逐个添加到文件列表
    for (const QString &file : files) {
        QString filePath = directory.absoluteFilePath(file);
        addFileToList(filePath);
    }

    statusLabel->setText(QString::fromUtf8("已导入 %1 张图片").arg(imageFiles.size()));

    // 自动执行批量识别
    // if (!imageFiles.isEmpty()) {
    //     onBatchRecognize();
    // }
}

// 按路径输入框中的目录刷新文件列表
void MainWindow::onRefreshFilePath()
{
    QString dirPath = ui->leFilePath->text().trimmed();
    if (dirPath.isEmpty()) {
        // 输入为空时回退到默认 test_images 目录
        dirPath = QDir(QApplication::applicationDirPath() + "/../test_images").absolutePath();
        ui->leFilePath->setText(dirPath);
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("目录不存在: %1").arg(dirPath));
        return;
    }

    ui->leFilePath->setText(QDir::cleanPath(dir.absolutePath()));
    loadImagesFromDirectory(dir.absolutePath());
}

/**
 * @brief 加载指定目录下的图片到文件列表
 * @details 先清空旧数据，再按文件名排序加载目录中的图片；
 *          只填充列表，不自动选中任何一行
 * @param dirPath 图片目录路径
 */
void MainWindow::loadImagesFromDirectory(const QString &dirPath)
{
    QDir directory(dirPath);
    if (!directory.exists()) {
        statusLabel->setText(QString::fromUtf8("目录不存在: %1").arg(dirPath));
        return;
    }

    // 清空旧的图片列表、OCR结果和图像显示
    imageFiles.clear();
    fileListWidget->clear();
    ocrRecords.clear();
    ocrBoxesList.clear();
    ocrResultImgs.clear();
    resultTextEdit->clear();
    imageView->clearImage();
    currentImageIndex = -1;

    // 搜索目录下的所有图片文件
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tif" << "*.tiff";
    QStringList files = directory.entryList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        statusLabel->setText(QString::fromUtf8("目录中没有找到图片: %1").arg(dirPath));
        return;
    }

    // 批量加入文件列表，期间屏蔽信号避免逐个触发选中
    fileListWidget->blockSignals(true);
    for (const QString &file : files) {
        addFileToList(directory.absoluteFilePath(file));
    }
    // addFileToList 内部会自动设置当前行，加载完成后清除选中态
    fileListWidget->setCurrentRow(-1);
    fileListWidget->blockSignals(false);

    statusLabel->setText(QString::fromUtf8("已加载 %1 张图片").arg(imageFiles.size()));
}

/**
 * @brief 添加图片到文件列表
 * @param filePath 图片文件路径
 */
void MainWindow::addFileToList(const QString &filePath)
{
    // 避免重复添加
    if (imageFiles.contains(filePath)) {
        return;
    }

    // 添加到数据列表
    imageFiles.append(filePath);

    // 添加到UI列表控件（显示文件名）
    QFileInfo fileInfo(filePath);
    QListWidgetItem *item = new QListWidgetItem(fileInfo.fileName());
    item->setToolTip(filePath); // 鼠标悬停显示完整路径
    item->setData(Qt::UserRole, filePath); // 存储完整路径
    fileListWidget->addItem(item);

    // 自动选中新添加的项
    fileListWidget->setCurrentRow(fileListWidget->count() - 1);
}

/**
 * @brief 文件列表选中项变化
 * @details 切换图片时，如果该图片已识别过，恢复显示带文本框标注的结果图像；
 *          否则显示原图
 * @param currentRow 当前选中行索引
 */
void MainWindow::onFileSelected(int currentRow)
{
    if (currentRow < 0 || currentRow >= imageFiles.size()) {
        return;
    }

    currentImageIndex = currentRow;

    // 显示OCR标注结果（已识别则恢复标注，未识别则显示原图）
    displayOcrResult(currentRow);

    // 如果该图片已识别，显示识别结果
    if (currentRow < ocrRecords.size()) {
        resultTextEdit->setPlainText(ocrRecords[currentRow].result);
    } else {
        resultTextEdit->clear();
    }
}

/**
 * @brief 显示指定路径的图片（支持中文路径）
 * @details 使用QFile读取文件字节，再用cv::imdecode解码，
 *          解决OpenCV的cv::imread不支持中文路径的问题
 * @param filePath 图片路径
 */
void MainWindow::displayImage(const QString &filePath)
{
    // 使用QFile读取文件（支持中文路径）
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("无法加载图片: %1").arg(filePath));
        return;
    }

    // 读取文件全部字节
    QByteArray fileData = file.readAll();
    file.close();

    // 使用cv::imdecode从内存数据解码图片（绕过中文路径问题）
    cv::Mat img = cv::imdecode(cv::Mat(1, fileData.size(), CV_8UC1, fileData.data()), cv::IMREAD_COLOR);
    if (img.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("无法解码图片: %1").arg(filePath));
        return;
    }

    // 在ImageDisplayView中显示图片
    imageView->setMatImage(img);
    imageView->fitToWindow();
}

/**
 * @brief 执行当前图片OCR识别
 */
void MainWindow::onRecognize()
{
    if (currentImageIndex < 0 || currentImageIndex >= imageFiles.size()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一张图片"));
        return;
    }

    if (!ocrEngine->isReady()) {
        QMessageBox::warning(this, QString::fromUtf8("警告"),
                             QString::fromUtf8("OCR引擎未初始化，请检查模型文件"));
        return;
    }

    // 禁用按钮防止重复操作
    ui->action_recognize->setEnabled(false);
    ui->action_ocrs->setEnabled(false);

    // 执行识别
    QString filePath = imageFiles[currentImageIndex];
    QString result = recognizeImage(filePath);

    // 显示识别结果
    resultTextEdit->setPlainText(result);

    // 保存识别记录（用于CSV导出）
    OcrRecord record;
    record.fileName = QFileInfo(filePath).fileName();
    record.result = result;
    record.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    // 更新或添加识别记录
    if (currentImageIndex < ocrRecords.size()) {
        ocrRecords[currentImageIndex] = record;
    } else {
        // 补齐缺失的记录
        while (ocrRecords.size() < currentImageIndex) {
            OcrRecord emptyRecord;
            emptyRecord.fileName = QFileInfo(imageFiles[ocrRecords.size()]).fileName();
            emptyRecord.result = "";
            emptyRecord.timestamp = "";
            ocrRecords.append(emptyRecord);
        }
        ocrRecords.append(record);
    }

    // 恢复按钮状态
    ui->action_recognize->setEnabled(true);
    ui->action_ocrs->setEnabled(true);

    statusLabel->setText(QString::fromUtf8("识别完成"));
}

// 批量识别所有图片
void MainWindow::onBatchRecognize()
{
    if (imageFiles.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先导入图片"));
        return;
    }

    if (!ocrEngine->isReady()) {
        QMessageBox::warning(this, QString::fromUtf8("警告"),
                             QString::fromUtf8("OCR引擎未初始化，请检查模型文件"));
        return;
    }

    // 清空之前的识别结果
    ocrRecords.clear();
    ocrBoxesList.clear();
    ocrResultImgs.clear();
    isBatchProcessing = true;

    // 禁用按钮
    ui->actionSelectFile->setEnabled(false);
    ui->action_add->setEnabled(false);
    ui->action_recognize->setEnabled(false);
    ui->action_ocrs->setEnabled(false);


    // 显示进度条
    progressBar->setVisible(true);
    progressBar->setRange(0, imageFiles.size());
    progressBar->setValue(0);

    // 逐张识别
    for (int i = 0; i < imageFiles.size(); ++i) {
        progressBar->setValue(i);
        statusLabel->setText(QString::fromUtf8("正在识别 %1/%2: %3")
                             .arg(i + 1)
                             .arg(imageFiles.size())
                             .arg(QFileInfo(imageFiles[i]).fileName()));

        // 处理事件循环，保持UI响应
        QCoreApplication::processEvents();

        // 执行识别
        QString result = recognizeImage(imageFiles[i]);

        // 生成时间戳
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

        // 添加到记录列表
        OcrRecord record;
        record.fileName = QFileInfo(imageFiles[i]).fileName();
        record.result = result;
        record.timestamp = timestamp;
        ocrRecords.append(record);

        // 更新文件列表选中项，显示当前识别的图片
        fileListWidget->setCurrentRow(i);

        // 显示识别结果
        resultTextEdit->setPlainText(result);
    }

    // 恢复状态
    progressBar->setValue(imageFiles.size());
    progressBar->setVisible(false);
    isBatchProcessing = false;

    // 恢复按钮
    ui->actionSelectFile->setEnabled(true);
    ui->action_add->setEnabled(true);
    ui->action_recognize->setEnabled(true);
    ui->action_ocrs->setEnabled(true);


    statusLabel->setText(QString::fromUtf8("批量识别完成，共 %1 张").arg(imageFiles.size()));
}

/**
 * @brief 对单张图片执行OCR识别
 * @details 调用OCR引擎识别图片，识别成功后保存文本框和结果图像到成员列表，
 *          以便切换图片时恢复标注显示
 * @param filePath 图片路径
 * @return 识别结果文本
 */
QString MainWindow::recognizeImage(const QString &filePath)
{
    std::vector<TextBox> boxes;
    QString recText;
    cv::Mat resultImg;

    // 调用OCR引擎执行识别
    bool success = ocrEngine->run(filePath, boxes, recText, resultImg);

    if (success && !resultImg.empty()) {
        // 在ImageDisplayView中显示带标注的结果图像
        imageView->setMatImage(resultImg);
        imageView->setTextBoxes(boxes);
        imageView->fitToWindow();
    } else {
        // 识别失败，显示原图
        displayImage(filePath);
    }

    // 保存OCR结果到对应索引位置（用于切换图片时恢复标注）
    int index = imageFiles.indexOf(filePath);
    if (index >= 0) {
        // 确保列表长度足够
        while (ocrBoxesList.size() <= index) {
            ocrBoxesList.append(std::vector<TextBox>());
        }
        while (ocrResultImgs.size() <= index) {
            ocrResultImgs.append(cv::Mat());
        }
        ocrBoxesList[index] = boxes;
        ocrResultImgs[index] = resultImg;
    }

    return recText;
}

/**
 * @brief 显示指定索引图片的OCR标注结果
 * @details 切换文件列表时，如果该图片已识别过，则恢复显示带文本框标注的结果图像；
 *          未识别过则显示原图
 * @param index 图片索引
 */
void MainWindow::displayOcrResult(int index)
{
    if (index < 0 || index >= imageFiles.size()) {
        return;
    }

    // 检查该图片是否有保存的OCR结果
    if (index < ocrResultImgs.size() && !ocrResultImgs[index].empty()) {
        // 有OCR结果，恢复显示带标注的结果图像和文本框
        imageView->setMatImage(ocrResultImgs[index]);
        if (index < ocrBoxesList.size() && !ocrBoxesList[index].empty()) {
            imageView->setTextBoxes(ocrBoxesList[index]);
        } else {
            imageView->clearTextBoxes();
        }
        imageView->fitToWindow();
    } else {
        // 无OCR结果，显示原图
        displayImage(imageFiles[index]);
    }
}

/**
 * @brief 导出识别结果为CSV
 */
void MainWindow::onExportCsv()
{
    if (ocrRecords.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("没有可导出的识别记录"));
        return;
    }

    // 打开保存文件对话框
    QString savePath = QFileDialog::getSaveFileName(
        this,
        QString::fromUtf8("导出CSV"),
        QDir::homePath() + "/ocr_results.csv",
        QString::fromUtf8("CSV文件 (*.csv);;所有文件 (*.*)"));

    if (savePath.isEmpty())
    {
        return;
    }

    // 执行导出
    if (csvExporter->exportToCsv(savePath, ocrRecords)) {
        QMessageBox::information(this, QString::fromUtf8("成功"),
                                 QString::fromUtf8("导出成功: %1\n共 %2 条记录")
                                 .arg(savePath)
                                 .arg(ocrRecords.size()));
        statusLabel->setText(QString::fromUtf8("CSV导出成功"));
    } else {
        QMessageBox::critical(this, QString::fromUtf8("失败"),
                              QString::fromUtf8("导出失败，请检查文件路径权限"));
    }
}

/**
 * @brief OCR进度更新
 * @param current 当前处理索引
 * @param total 总数
 */
void MainWindow::onProgressChanged(int current, int total)
{
    if (!isBatchProcessing)
    {
        // 单张识别时显示文本框识别进度
        statusLabel->setText(QString::fromUtf8("识别进度: %1/%2").arg(current).arg(total));
    }
}

/**
 * @brief OCR日志信息
 * @param message 日志内容
 */
void MainWindow::onLogMessage(const QString &message)
{
    statusLabel->setText(message);
    qDebug() << "[OCR]" << message;
}

// 适应窗口显示
void MainWindow::onFitWindow()
{
    imageView->fitToWindow();
}

// 恢复1:1原始尺寸
void MainWindow::onResetSize()
{
    imageView->resetToOriginalSize();
}

//清空所有数据（包括图片显示）
void MainWindow::onClearAll()
{
    // 清空文件列表
    imageFiles.clear();
    fileListWidget->clear();

    // 清空识别记录
    ocrRecords.clear();
    ocrBoxesList.clear();
    ocrResultImgs.clear();

    // 清空文本框
    resultTextEdit->clear();

    // 清空图片显示
    imageView->clearImage();

    // 重置索引
    currentImageIndex = -1;

    statusLabel->setText(QString::fromUtf8("已清空所有数据"));
}


void MainWindow::on_actionSelectFile_triggered()
{
    onBatchImport();
}

void MainWindow::on_action_add_triggered()
{
    onImportImage();
}

void MainWindow::on_action_recognize_triggered()
{
    onRecognize();
}

void MainWindow::on_action_ocrs_triggered()
{
    onBatchRecognize();
}

void MainWindow::on_action_clear_triggered()
{
    onClearAll();
    onRefreshFilePath();
}

void MainWindow::on_action_CSV_triggered()
{
    onExportCsv();
}

void MainWindow::on_action_resize_triggered()
{
    onFitWindow();
}

void MainWindow::on_action_full_triggered()
{
    onResetSize();
}

