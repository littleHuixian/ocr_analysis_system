/**
 * @file CsvExporter.cpp
 * @brief CSV导出器实现文件
 * @details 实现OCR识别结果的CSV文件导出功能：
 *          1. 使用QFile打开目标文件，QTextStream进行文本写入
 *          2. 设置QTextStream编码为UTF-8，并写入BOM头
 *          3. 写入CSV表头行（图片名,识别结果,时间戳）
 *          4. 遍历识别记录，逐行写入并处理特殊字符转义
 *          兼容Qt5（QTextCodec）和Qt6（QStringConverter）的编码设置接口
 * @author 先瞳编码, 关注微信公众号"先瞳编码"，获取最新技术分享
 */

#include "CsvExporter.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QtGlobal>

// Qt5需要QTextCodec设置编码，Qt6则使用QStringConverter
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif

// ============================================================
// 构造与析构函数
// ============================================================

/**
 * @brief 构造函数
 * @param parent 父QObject指针
 */
CsvExporter::CsvExporter(QObject *parent)
    : QObject(parent)
{
    // CsvExporter为无状态工具类，构造函数无需额外初始化
}

/**
 * @brief 析构函数
 */
CsvExporter::~CsvExporter()
{
    // 无动态分配资源，无需手动释放
}

// ============================================================
// CSV字段转义
// ============================================================

/**
 * @brief 转义CSV字段中的特殊字符
 * @details 遵循RFC 4180规范：
 *          1. 检测字段是否包含逗号、双引号或换行符
 *          2. 若包含，则将字段用双引号包裹
 *          3. 字段内已有的双引号转义为两个连续双引号
 * @param field 原始字段内容
 * @return 转义处理后的字段字符串
 */
QString CsvExporter::escapeCsvField(const QString &field)
{
    // 检测字段是否包含需要转义的特殊字符：逗号、双引号、回车、换行
    bool needEscape = field.contains(',')    // 逗号：CSV字段分隔符
                   || field.contains('"')    // 双引号：CSV转义字符
                   || field.contains('\n')   // 换行符：会破坏CSV行结构
                   || field.contains('\r');  // 回车符：会破坏CSV行结构

    if (!needEscape)
    {
        // 字段不含特殊字符，直接返回原值
        return field;
    }

    // 将字段内的双引号替换为两个双引号（RFC 4180转义规则）
    QString escapedField = field;
    escapedField.replace('"', "\"\"");

    // 用双引号包裹整个字段
    return '\"' + escapedField + '\"';
}

// ============================================================
// CSV导出核心方法
// ============================================================

/**
 * @brief 导出OCR识别记录到CSV文件
 * @details 完整导出流程：
 *          1. 以只写方式打开目标文件
 *          2. 创建QTextStream并设置UTF-8编码
 *          3. 写入UTF-8 BOM头（0xFEFF），确保Excel正确识别编码
 *          4. 写入CSV表头行
 *          5. 遍历记录列表，逐行写入并转义特殊字符
 *          6. 刷新流缓冲并关闭文件
 * @param filePath 目标CSV文件路径
 * @param records OCR识别记录列表
 * @return true 导出成功，false 导出失败
 */
bool CsvExporter::exportToCsv(const QString &filePath, const QList<OcrRecord> &records)
{
    // 校验文件路径是否有效
    if (filePath.isEmpty())
    {
        qWarning() << "[CsvExporter] 文件路径为空，导出失败";
        return false;
    }

    // 以只写+文本模式打开文件
    // QIODevice::Text：在Windows平台上将换行符\n自动转换为\r\n（CRLF），
    // 符合RFC 4180规范中CSV文件使用CRLF作为行结束符的要求
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "[CsvExporter] 无法打开文件:" << filePath
                   << "错误:" << file.errorString();
        return false;
    }

    // 创建文本输出流，绑定到文件
    QTextStream out(&file);

    // 设置输出流编码为UTF-8（兼容Qt5和Qt6的接口差异）
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6：使用QStringConverter设置编码
    out.setEncoding(QStringConverter::Utf8);
#else
    // Qt5：使用QTextCodec设置编码
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif

    // 写入UTF-8 BOM头（字节顺序标记 EF BB BF）
    // QChar(0xFEFF)是Unicode字节顺序标记字符，经UTF-8编码后会输出BOM字节序列
    // 作用：让Excel等表格软件正确识别文件编码，避免中文显示乱码
    out << QChar(0xFEFF);

    // 写入CSV表头行：图片名,识别结果,时间戳
    // 使用QString::fromUtf8确保中文字符串以UTF-8方式正确构造
    out << QString::fromUtf8("图片名") << ','
        << QString::fromUtf8("识别结果") << ','
        << QString::fromUtf8("时间戳") << '\n';

    // 遍历所有OCR识别记录，逐行写入
    for (const OcrRecord &record : records)
    {
        // 对每个字段进行CSV转义处理后写入，字段间以逗号分隔
        out << escapeCsvField(record.fileName) << ','
            << escapeCsvField(record.result) << ','
            << escapeCsvField(record.timestamp) << '\n';
    }

    // 刷新流缓冲，确保所有数据写入文件
    out.flush();

    // 检查写入过程中是否发生错误
    if (out.status() != QTextStream::Ok)
    {
        qWarning() << "[CsvExporter] 写入文件时发生错误:" << filePath;
        file.close();
        return false;
    }

    // 关闭文件，释放文件句柄
    file.close();

    qDebug() << "[CsvExporter] CSV导出成功，共" << records.size()
             << "条记录，路径:" << filePath;
    return true;
}
