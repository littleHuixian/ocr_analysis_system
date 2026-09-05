/**
 * @file CsvExporter.h
 * @brief CSV导出器头文件
 * @details 提供OCR识别结果的CSV文件导出功能：
 *          1. 定义OcrRecord结构体，存储单条OCR识别记录（图片名、识别结果、时间戳）
 *          2. CsvExporter类继承QObject，封装CSV文件写入逻辑
 *          3. 支持UTF-8 BOM编码，确保Excel等表格软件正确识别中文
 *          4. 自动处理字段中的逗号、双引号等特殊字符，符合RFC 4180规范
 * @author 先瞳编码, 关注微信公众号"先瞳编码"，获取最新技术分享
 */

#ifndef CSVEXPORTER_H
#define CSVEXPORTER_H

#include <QObject>
#include <QString>
#include <QList>

/**
 * @brief OCR识别记录结构体
 * @details 存储单张图片的OCR识别结果，作为CSV导出的一条数据行
 *          每个字段对应CSV文件中的一个列
 */
struct OcrRecord
{
    QString fileName;    ///< 图片名（含扩展名，如"test.jpg"）
    QString result;      ///< 识别结果（OCR识别出的完整文本内容）
    QString timestamp;   ///< 时间戳（识别时间，如"2024-01-01 12:00:00"）
};

/**
 * @brief CSV导出器类
 * @details 继承QObject，提供将OCR识别记录批量导出为CSV文件的功能
 *          导出的CSV文件特征：
 *          - UTF-8 BOM编码，兼容Excel直接打开中文不乱码
 *          - 第一行为表头：图片名,识别结果,时间戳
 *          - 每行一条记录，字段以逗号分隔
 *          - 包含特殊字符（逗号、双引号、换行）的字段自动用双引号包裹
 */
class CsvExporter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 显式构造函数
     * @param parent 父QObject指针，用于Qt对象树管理
     */
    explicit CsvExporter(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CsvExporter();

    /**
     * @brief 导出OCR识别记录到CSV文件
     * @details 将OCR识别记录列表写入指定路径的CSV文件：
     *          1. 以UTF-8 BOM编码写入，确保中文在Excel中正确显示
     *          2. 第一行写入表头：图片名,识别结果,时间戳
     *          3. 逐行写入识别记录，字段以逗号分隔
     *          4. 自动转义包含特殊字符的字段（逗号、双引号、换行符）
     * @param filePath 目标CSV文件路径（如"D:/output/result.csv"）
     * @param records OCR识别记录列表
     * @return true 导出成功，false 导出失败（文件无法打开等）
     */
    bool exportToCsv(const QString &filePath, const QList<OcrRecord> &records);

private:
    /**
     * @brief 转义CSV字段中的特殊字符
     * @details 根据RFC 4180规范处理字段：
     *          - 若字段包含逗号(,)、双引号(")或换行符(\n \r)，
     *            则用双引号包裹整个字段
     *          - 字段内部的双引号替换为两个双引号("")进行转义
     * @param field 原始字段内容
     * @return 转义后的字段内容
     */
    static QString escapeCsvField(const QString &field);
};

#endif // CSVEXPORTER_H
