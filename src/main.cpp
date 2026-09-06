/**
 * @file main.cpp
 * @brief 应用程序入口文件
 * @details ocr_analysis_system程序主入口，初始化Qt应用程序、设置UTF-8编码、创建主窗口
 * @author
 */

#include "mainwindow.h"
#include <QApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif

/**
 * @brief 主函数
 * @details 初始化Qt应用程序，设置中文编码，创建并显示主窗口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 */
int main(int argc, char *argv[])
{
    // 创建Qt应用程序对象
    QApplication app(argc, argv);

    // 设置应用程序信息
    app.setApplicationName("ocr_analysis_system");
    app.setOrganizationName("Huixian");
    app.setApplicationVersion("1.0.0");

    // 解决中文乱码：设置源码编码为UTF-8
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
#endif

    // 创建主窗口
    MainWindow mainWindow;
    mainWindow.setWindowTitle("ocr_analysis_system（肖珲贤：1207162512@qq.com）");
    mainWindow.show();

    // 进入Qt事件循环
    return app.exec();
}
