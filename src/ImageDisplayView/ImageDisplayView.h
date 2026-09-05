/**
 * @file ImageDisplayView.h
 * @brief 图片显示视图头文件
 * @details 自定义QGraphicsView子类，用于显示OCR图片和文本框标注
 *          支持鼠标滚轮缩放、拖拽平移、适应窗口、1:1原始尺寸显示
 *          支持加载OpenCV cv::Mat格式图像并转换为QPixmap显示
 *          在图像上用红框标注OCR检测到的文字区域，显示识别文本和置信度
 * @author 先瞳编码, 关注微信公众号"先瞳编码"，获取最新技术分享
 */

#ifndef IMAGEDISPLAYVIEW_H
#define IMAGEDISPLAYVIEW_H

#include <QGraphicsView>
#include <QPixmap>
#include <QList>
#include <QPoint>
#include <QWheelEvent>
#include <QMouseEvent>
#include <opencv2/opencv.hpp>
#include <vector>

#include "RapidOcrEngine/RapidOcrEngine.h"

// 前置声明，减少头文件依赖，加快编译速度
class QGraphicsScene;          ///< 图形场景前置声明
class QGraphicsPixmapItem;     ///< 图像像素图元前置声明
class QGraphicsItem;           ///< 图元基类前置声明

/**
 * @brief 图片显示视图类
 * @details 继承自QGraphicsView，提供图片显示、缩放、平移、OCR标注等完整功能
 *          核心功能：
 *          1. 加载OpenCV cv::Mat图像并转换为QPixmap显示（支持BGR/灰度/BGRA三种格式）
 *          2. 鼠标滚轮缩放（范围0.05~20.0），以鼠标位置为中心缩放
 *          3. 鼠标左键拖拽平移图像
 *          4. 适应窗口显示（fitToWindow）和1:1原始尺寸显示（resetToOriginalSize）
 *          5. 在图像上绘制红色多边形框标注OCR检测到的文字区域
 *          6. 显示识别文本和置信度标签（半透明背景，提高可读性）
 */
class ImageDisplayView : public QGraphicsView
{
    Q_OBJECT

public:
    /**
     * @brief 显式构造函数
     * @details 初始化图形场景、渲染参数、深色背景等
     * @param parent 父QWidget指针，遵循Qt父子对象内存管理
     */
    explicit ImageDisplayView(QWidget *parent = nullptr);

    /**
     * @brief 析构函数
     * @details Qt父子对象机制自动管理内存，无需手动释放
     */
    ~ImageDisplayView();

    // ==================== 图像操作接口 ====================

    /**
     * @brief 设置OpenCV cv::Mat格式图像
     * @details 将cv::Mat转换为QPixmap后显示在场景中，并清除之前的文本框标注
     *          加载后自动重置为1:1原始尺寸
     * @param mat 输入的cv::Mat图像（支持3通道BGR、1通道灰度、4通道BGRA格式）
     * @return true 设置成功，false 图像为空或转换失败
     */
    bool setMatImage(const cv::Mat &mat);

    /**
     * @brief 将cv::Mat转换为QPixmap
     * @details 支持三种常见通道格式转换：
     *          - 3通道BGR：OpenCV默认格式，转为RGB后生成QPixmap
     *          - 1通道灰度：直接生成灰度QPixmap
     *          - 4通道BGRA：带透明通道，转为RGBA后生成QPixmap
     * @param mat 输入的cv::Mat图像
     * @return 转换后的QPixmap对象，失败返回空QPixmap
     */
    QPixmap matToPixmap(const cv::Mat &mat) const;

    // ==================== OCR文本框标注接口 ====================

    /**
     * @brief 设置OCR检测到的文本框列表
     * @details 在图像上绘制红色多边形框标注文字区域，并显示识别文本和置信度标签
     *          每次调用会先清除已有的标注
     * @param boxes OCR检测到的文本框列表（包含位置坐标、识别文本、各阶段置信度）
     */
    void setTextBoxes(const std::vector<TextBox> &boxes);

    /**
     * @brief 清除所有文本框标注
     * @details 从场景中移除所有标注图元，并清空文本框数据
     */
    void clearTextBoxes();

    /**
     * @brief 清空图像显示
     * @details 移除场景中的图像和所有标注，重置视图状态
     *          清空缓存的QPixmap，重置缩放因子
     */
    void clearImage();

    // ==================== 视图缩放操作接口 ====================

    /**
     * @brief 适应窗口大小显示图像
     * @details 自动计算缩放比例，使图像完整显示在视口内（保持宽高比）
     */
    void fitToWindow();

    /**
     * @brief 重置为1:1原始尺寸显示
     * @details 清除所有缩放变换，以图像原始像素尺寸显示并居中
     */
    void resetToOriginalSize();

    /**
     * @brief 放大视图
     * @details 以视口中心为锚点放大，单次缩放步长为1.15倍
     */
    void zoomIn();

    /**
     * @brief 缩小视图
     * @details 以视口中心为锚点缩小，单次缩放步长为1/1.15倍
     */
    void zoomOut();

    /**
     * @brief 获取当前缩放因子
     * @return 当前缩放比例（1.0为原始尺寸）
     */
    double getZoomFactor() const;

    /**
     * @brief 判断是否已加载图像
     * @return true 已加载图像，false 未加载
     */
    bool hasImage() const;

signals:
    /**
     * @brief 缩放因子变化信号
     * @details 每次缩放操作后发送，通知外部更新缩放显示等信息
     *          使用新式Qt5信号槽语法连接：connect(view, &ImageDisplayView::zoomChanged, ...)
     * @param factor 新的缩放因子
     */
    void zoomChanged(double factor);

protected:
    // ==================== 鼠标事件重载 ====================

    /**
     * @brief 鼠标滚轮事件：滚轮缩放
     * @details 向上滚动放大，向下滚动缩小，以鼠标当前位置为中心缩放
     *          缩放范围限制在0.05~20.0之间
     * @param event 滚轮事件指针
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @brief 鼠标按下事件：开始拖拽
     * @details 左键按下时记录起始位置，进入拖拽状态，设置闭合手型光标
     * @param event 鼠标事件指针
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件：拖拽平移
     * @details 拖拽状态下根据鼠标移动距离滚动视图，实现图像平移
     * @param event 鼠标事件指针
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件：结束拖拽
     * @details 释放左键时退出拖拽状态，恢复标准光标
     * @param event 鼠标事件指针
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /**
     * @brief 在场景上绘制OCR文本框标注
     * @details 遍历所有TextBox，为每个文本框绘制：
     *          1. 红色多边形边框（标注文字区域边界）
     *          2. 半透明黑色背景标签（提高文字可读性）
     *          3. 白色识别文本和置信度文字
     *          标签默认显示在文本框上方，若超出图像顶部则显示在下方
     */
    void drawTextBoxesOnScene();

    /**
     * @brief 以指定视口位置为锚点进行缩放
     * @details 缩放时保持指定视口位置对应的场景点不动，实现以鼠标为中心的缩放效果
     *          缩放范围自动限制在MIN_ZOOM(0.05)~MAX_ZOOM(20.0)之间
     * @param viewportPos 视口坐标位置（缩放锚点）
     * @param factor 缩放因子（>1放大，<1缩小）
     */
    void zoomAt(const QPoint &viewportPos, double factor);

private:
    // ==================== 场景与图元 ====================
    QGraphicsScene *graphicsScene;        ///< 图形场景对象（管理所有图元）
    QGraphicsPixmapItem *pixmapItem;      ///< 图像像素图元项（显示QPixmap）

    // ==================== 图像数据 ====================
    QPixmap originalPixmap;               ///< 原始图像QPixmap缓存
    std::vector<TextBox> textBoxes;       ///< OCR文本框数据列表
    QList<QGraphicsItem*> textBoxItems;   ///< 文本框标注图元列表（用于批量清除）

    // ==================== 交互状态 ====================
    double zoomFactor;                    ///< 当前缩放因子（1.0为原始尺寸）
    bool isDragging;                      ///< 是否正在拖拽平移
    QPoint lastMousePos;                  ///< 上次鼠标位置（拖拽距离计算用）

    // ==================== 缩放范围常量 ====================
    static constexpr double MIN_ZOOM = 0.05;   ///< 最小缩放比例（5%）
    static constexpr double MAX_ZOOM = 20.0;   ///< 最大缩放比例（2000%）
    static constexpr double ZOOM_STEP = 1.15;  ///< 单次滚轮缩放步长（1.15倍）
};

#endif // IMAGEDISPLAYVIEW_H
