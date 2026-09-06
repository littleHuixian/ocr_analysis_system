/**
 * @file ImageDisplayView.cpp
 * @brief 图片显示视图实现文件
 * @details 实现自定义QGraphicsView子类的所有功能：
 *          1. cv::Mat到QPixmap的格式转换（支持3通道BGR、1通道灰度、4通道BGRA）
 *          2. 鼠标滚轮缩放（以鼠标位置为中心，缩放范围0.05~20.0）
 *          3. 鼠标左键拖拽平移
 *          4. 适应窗口显示和1:1原始尺寸显示
 *          5. OCR文本框红色多边形标注与识别文本/置信度标签绘制
 * @author 
 */

#include "ImageDisplayView.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QFont>
#include <QPolygonF>
#include <QDebug>

// ============================================================
// 构造与析构
// ============================================================

/**
 * @brief 构造函数
 * @details 初始化图形场景、渲染参数、深色背景等
 * @param parent 父QWidget指针，遵循Qt父子对象内存管理
 */
ImageDisplayView::ImageDisplayView(QWidget *parent)
    : QGraphicsView(parent)               // 调用基类构造函数
    , graphicsScene(nullptr)              // 场景指针初始化为空
    , pixmapItem(nullptr)                 // 图元指针初始化为空
    , zoomFactor(1.0)                    // 缩放因子初始化为1.0（原始尺寸）
    , isDragging(false)                  // 拖拽状态初始化为false
{
    // 设置控件objectName，便于QSS样式选择和调试定位
    setObjectName("imageDisplayView");

    // 创建图形场景对象，this作为父对象实现自动内存管理
    graphicsScene = new QGraphicsScene(this);
    // 将场景设置到当前视图
    setScene(graphicsScene);

    // 创建图像像素图元项并添加到场景中（初始为空QPixmap）
    pixmapItem = graphicsScene->addPixmap(QPixmap());

    // 设置渲染参数：抗锯齿 + 平滑像素变换，提升图像和矢量图形显示质量
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // 设置深色背景（QColor(43,43,43)），与OCR工具整体深色UI风格一致
    setBackgroundBrush(QColor(43, 43, 43));

    // 设置视图变换锚点为无锚点（手动控制缩放中心，实现以鼠标为中心缩放）
    setTransformationAnchor(QGraphicsView::NoAnchor);

    // 设置拖拽模式为无（手动实现拖拽平移逻辑，不使用内置ScrollHandDrag）
    setDragMode(QGraphicsView::NoDrag);

    // 滚动条策略：按需显示，视觉简洁
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 设置视图最小尺寸，防止窗口过小时显示异常
    setMinimumSize(200, 200);
}

/**
 * @brief 析构函数
 * @details Qt父子对象机制自动管理所有内存：
 *          - graphicsScene作为this的子QObject，自动销毁
 *          - pixmapItem和textBoxItems属于graphicsScene，随场景一起销毁
 *          无需手动delete任何对象
 */
ImageDisplayView::~ImageDisplayView()
{
    // 所有图元和场景均由Qt父子对象机制自动管理，此处无需手动释放
}

// ============================================================
// 图像操作
// ============================================================

/**
 * @brief 设置OpenCV cv::Mat格式图像
 * @details 将cv::Mat转换为QPixmap后显示在场景中，并清除之前的文本框标注
 *          加载后自动重置为1:1原始尺寸
 * @param mat 输入的cv::Mat图像
 * @return true 设置成功，false 图像为空或转换失败
 */
bool ImageDisplayView::setMatImage(const cv::Mat &mat)
{
    // 检查输入图像是否为空
    if (mat.empty()) {
        qDebug() << "[ImageDisplayView] setMatImage: 输入图像为空";
        return false;
    }

    // 调用matToPixmap将cv::Mat转换为QPixmap
    originalPixmap = matToPixmap(mat);
    if (originalPixmap.isNull()) {
        qDebug() << "[ImageDisplayView] setMatImage: cv::Mat转QPixmap失败";
        return false;
    }

    // 更新像素图元显示新图像
    pixmapItem->setPixmap(originalPixmap);

    // 清除之前的文本框标注（新图像与旧标注不匹配）
    clearTextBoxes();

    // 重置缩放为1:1原始尺寸并居中显示
    resetToOriginalSize();

    return true;
}

/**
 * @brief 将cv::Mat转换为QPixmap
 * @details 支持三种常见通道格式转换：
 *          - 3通道BGR：OpenCV默认BGR顺序，转为RGB以匹配QImage::Format_RGB888
 *          - 1通道灰度：无需颜色转换，直接使用QImage::Format_Grayscale8
 *          - 4通道BGRA：带Alpha透明通道，转为RGBA以匹配QImage::Format_RGBA8888
 *          其他通道数尝试先转换为3通道BGR再处理
 * @param mat 输入的cv::Mat图像
 * @return 转换后的QPixmap对象，失败返回空QPixmap
 */
QPixmap ImageDisplayView::matToPixmap(const cv::Mat &mat) const
{
    // 检查图像有效性
    if (mat.empty()) {
        return QPixmap();
    }

    cv::Mat converted;  // 转换后的中间图像（局部变量，函数结束后自动释放）

    // 根据通道数选择颜色转换方式
    switch (mat.channels()) {
    case 3:
        // 3通道BGR图像：OpenCV默认使用BGR顺序，而QImage使用RGB顺序，需要转换
        cv::cvtColor(mat, converted, cv::COLOR_BGR2RGB);
        break;

    case 4:
        // 4通道BGRA图像：带Alpha透明通道，转为RGBA匹配QImage::Format_RGBA8888
        cv::cvtColor(mat, converted, cv::COLOR_BGRA2RGBA);
        break;

    case 1:
        // 1通道灰度图像：无需颜色转换，直接克隆使用
        converted = mat.clone();
        break;

    default:
        // 其他通道数（如2通道）：先转为3通道BGR，再转为RGB
        cv::cvtColor(mat, converted, cv::COLOR_BGR2RGB);
        break;
    }

    // 根据转换后的通道数选择对应的QImage格式
    QImage::Format format;
    if (converted.channels() == 4) {
        format = QImage::Format_RGBA8888;       // 4通道：RGBA格式
    } else if (converted.channels() == 3) {
        format = QImage::Format_RGB888;         // 3通道：RGB格式
    } else {
        format = QImage::Format_Grayscale8;     // 1通道：灰度格式
    }

    // 从cv::Mat数据创建QImage
    // 注意：QImage与cv::Mat共享内存，converted为局部变量，必须深拷贝确保数据独立
    QImage image(converted.data,                        // 图像数据指针
                 converted.cols,                        // 图像宽度
                 converted.rows,                        // 图像高度
                 static_cast<int>(converted.step),      // 每行字节数（步长）
                 format);                               // 像素格式

    // 深拷贝QImage，使数据独立于cv::Mat（避免局部变量converted释放后悬垂指针）
    QImage copiedImage = image.copy();

    // 将QImage转换为QPixmap并返回
    return QPixmap::fromImage(copiedImage);
}

// ============================================================
// OCR文本框标注
// ============================================================

/**
 * @brief 设置OCR检测到的文本框列表
 * @details 保存文本框数据并在场景上绘制红色标注框和识别文本标签
 *          每次调用会先清除已有标注再重新绘制
 * @param boxes OCR检测到的文本框列表
 */
void ImageDisplayView::setTextBoxes(const std::vector<TextBox> &boxes)
{
    // 保存文本框数据（拷贝赋值）
    textBoxes = boxes;

    // 在场景上绘制标注
    drawTextBoxesOnScene();
}

/**
 * @brief 清除所有文本框标注
 * @details 从场景中移除所有标注图元，并清空文本框数据列表
 */
void ImageDisplayView::clearTextBoxes()
{
    // 遍历所有标注图元，从场景移除并删除
    for (QGraphicsItem *item : textBoxItems) {
        graphicsScene->removeItem(item);  // 从场景移除
        delete item;                      // 释放内存（子图元随父图元一起删除）
    }
    textBoxItems.clear();  // 清空图元列表

    // 清空文本框数据
    textBoxes.clear();
}

/**
 * @brief 清空图像显示
 * @details 移除场景中的图像和所有标注，重置视图状态
 *          清空缓存的QPixmap，重置缩放因子为1.0
 */
void ImageDisplayView::clearImage()
{
    // 清空缓存的原始图像
    originalPixmap = QPixmap();

    // 清空像素图元显示
    pixmapItem->setPixmap(QPixmap());

    // 清除所有文本框标注
    clearTextBoxes();

    // 重置变换矩阵和缩放因子
    resetTransform();
    zoomFactor = 1.0;

    // 发送缩放变化信号
    emit zoomChanged(zoomFactor);
}

// ============================================================
// 视图缩放操作
// ============================================================

/**
 * @brief 适应窗口大小显示图像
 * @details 自动计算缩放比例，使图像完整显示在视口内（保持宽高比）
 *          缩放后从实际变换矩阵获取缩放因子
 */
void ImageDisplayView::fitToWindow()
{
    // 检查是否已加载图像
    if (originalPixmap.isNull()) {
        return;
    }

    // 调用QGraphicsView::fitInView，保持宽高比将图元适应到视口
    fitInView(pixmapItem->boundingRect(), Qt::KeepAspectRatio);

    // 从实际变换矩阵获取缩放因子（m11为水平缩放分量）
    zoomFactor = transform().m11();

    // 发送缩放变化信号（使用新式Qt5信号槽语法，外部通过函数指针连接）
    emit zoomChanged(zoomFactor);
}

/**
 * @brief 重置为1:1原始尺寸显示
 * @details 清除所有缩放变换矩阵，以图像原始像素尺寸显示并居中
 */
void ImageDisplayView::resetToOriginalSize()
{
    // 检查是否已加载图像
    if (originalPixmap.isNull()) {
        return;
    }

    // 重置变换矩阵为单位矩阵（1:1缩放，无旋转）
    resetTransform();

    // 更新缩放因子为1.0
    zoomFactor = 1.0;

    // 将图像居中显示在视口中
    centerOn(pixmapItem);

    // 发送缩放变化信号
    emit zoomChanged(zoomFactor);
}

/**
 * @brief 放大视图
 * @details 以视口中心为锚点放大，单次缩放步长为ZOOM_STEP(1.15)倍
 */
void ImageDisplayView::zoomIn()
{
    // 以视口中心位置为锚点，按ZOOM_STEP倍放大
    zoomAt(viewport()->rect().center(), ZOOM_STEP);
}

/**
 * @brief 缩小视图
 * @details 以视口中心为锚点缩小，单次缩放步长为1/ZOOM_STEP倍
 */
void ImageDisplayView::zoomOut()
{
    // 以视口中心位置为锚点，按1/ZOOM_STEP倍缩小
    zoomAt(viewport()->rect().center(), 1.0 / ZOOM_STEP);
}

/**
 * @brief 获取当前缩放因子
 * @return 当前缩放比例（1.0为原始尺寸）
 */
double ImageDisplayView::getZoomFactor() const
{
    return zoomFactor;
}

/**
 * @brief 判断是否已加载图像
 * @return true 已加载图像，false 未加载
 */
bool ImageDisplayView::hasImage() const
{
    return !originalPixmap.isNull();
}

// ============================================================
// 鼠标事件处理
// ============================================================

/**
 * @brief 鼠标滚轮事件：滚轮缩放
 * @details 向上滚动放大，向下滚动缩小，以鼠标当前位置为中心缩放
 *          缩放范围限制在MIN_ZOOM(0.05)~MAX_ZOOM(20.0)之间
 * @param event 滚轮事件指针
 */
void ImageDisplayView::wheelEvent(QWheelEvent *event)
{
    // 判断滚轮方向：angleDelta().y() > 0 为向上滚动（放大），否则缩小
    double factor = (event->angleDelta().y() > 0) ? ZOOM_STEP : (1.0 / ZOOM_STEP);

    // 以鼠标当前位置为锚点进行缩放
    zoomAt(event->position().toPoint(), factor);

    // 接受事件，阻止事件继续传递
    event->accept();
}

/**
 * @brief 鼠标按下事件：开始拖拽
 * @details 左键按下时记录起始位置，进入拖拽状态，设置闭合手型光标
 *          其他按键交给基类处理
 * @param event 鼠标事件指针
 */
void ImageDisplayView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 左键按下：进入拖拽状态
        isDragging = true;
        // 记录鼠标按下时的位置（用于计算拖拽偏移量）
        lastMousePos = event->pos();
        // 设置闭合手型光标，提示用户正在拖拽
        setCursor(Qt::ClosedHandCursor);
        // 接受事件
        event->accept();
    } else {
        // 非左键事件交给基类处理（保留右键菜单等默认行为）
        QGraphicsView::mousePressEvent(event);
    }
}

/**
 * @brief 鼠标移动事件：拖拽平移
 * @details 拖拽状态下，根据鼠标移动距离滚动视图，实现图像平移
 * @param event 鼠标事件指针
 */
void ImageDisplayView::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging) {
        // 计算鼠标自上次移动以来的偏移量
        QPoint delta = event->pos() - lastMousePos;
        // 更新上次鼠标位置
        lastMousePos = event->pos();

        // 通过调整滚动条值实现视图平移（鼠标右移 → 图像左移 → 滚动条值减小）
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

        event->accept();
    } else {
        // 非拖拽状态交给基类处理
        QGraphicsView::mouseMoveEvent(event);
    }
}

/**
 * @brief 鼠标释放事件：结束拖拽
 * @details 释放左键时退出拖拽状态，恢复标准箭头光标
 * @param event 鼠标事件指针
 */
void ImageDisplayView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 左键释放：结束拖拽状态
        isDragging = false;
        // 恢复标准箭头光标
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        // 非左键事件交给基类处理
        QGraphicsView::mouseReleaseEvent(event);
    }
}

// ============================================================
// 私有方法实现
// ============================================================

/**
 * @brief 在场景上绘制OCR文本框标注
 * @details 遍历所有TextBox，为每个文本框绘制：
 *          1. 红色多边形边框（标注文字区域边界，线宽2像素）
 *          2. 半透明黑色背景矩形（提高标签文字可读性）
 *          3. 白色识别文本和置信度文字标签
 *          标签默认显示在文本框上方，若超出图像顶部则显示在下方
 */
void ImageDisplayView::drawTextBoxesOnScene()
{
    // 先清除已有的标注图元
    clearTextBoxes();

    // 检查是否有文本框数据和已加载的图像
    if (textBoxes.empty() || originalPixmap.isNull()) {
        return;
    }

    // 创建标签字体（微软雅黑加粗，9号字，适配中文显示）
    QFont labelFont("Microsoft YaHei", 9, QFont::Bold);

    // 遍历每个文本框，逐个绘制标注
    for (const TextBox &box : textBoxes) {
        // 跳过没有顶点数据的文本框
        if (box.points.empty()) {
            continue;
        }

        // ---- 1. 构建并绘制红色多边形边框 ----
        QPolygonF polygon;
        for (const cv::Point2f &pt : box.points) {
            // 将cv::Point2f转换为QPointF，添加到多边形顶点列表
            polygon << QPointF(pt.x, pt.y);
        }

        // 创建多边形图元（红色边框，无填充）
        QGraphicsPolygonItem *polyItem = new QGraphicsPolygonItem(polygon);
        polyItem->setPen(QPen(QColor(255, 0, 0), 2));  // 红色画笔，线宽2像素
        polyItem->setBrush(Qt::NoBrush);                 // 无填充
        graphicsScene->addItem(polyItem);                 // 添加到场景
        textBoxItems.append(polyItem);                    // 加入列表便于后续清除

        // ---- 2. 构建识别文本标签内容 ----
        // 标签格式：识别文本 + [置信度:xx.x%]
        QString labelText = QString::fromUtf8(box.text.c_str());
        labelText += QString(" [置信度:%1%]").arg(box.recScore * 100.0, 0, 'f', 1);

        // 创建文本图元（暂无父对象和场景）
        QGraphicsTextItem *textItem = new QGraphicsTextItem(labelText);
        textItem->setFont(labelFont);                    // 设置标签字体
        textItem->setDefaultTextColor(Qt::white);        // 白色文字

        // 获取文本图元的边界矩形（用于确定背景矩形尺寸）
        QRectF textRect = textItem->boundingRect();

        // ---- 3. 创建半透明黑色背景矩形 ----
        // 背景矩形作为文本图元的父图元，使文字始终显示在背景之上
        QGraphicsRectItem *bgItem = new QGraphicsRectItem(textRect);
        bgItem->setBrush(QColor(0, 0, 0, 180));          // 半透明黑色背景（alpha=180）
        bgItem->setPen(QPen(QColor(255, 0, 0), 1));      // 红色细边框（1像素）

        // 将文本图元设为背景矩形的子图元（子图元自动显示在父图元之上）
        textItem->setParentItem(bgItem);
        textItem->setPos(0, 0);  // 文本相对于背景矩形左上角对齐

        // ---- 4. 定位标签到文本框附近 ----
        // 默认将标签放在文本框的左上角上方
        QPointF labelPos = polygon.boundingRect().topLeft();
        labelPos.setY(labelPos.y() - textRect.height());

        // 如果标签超出图像顶部边界，则放在文本框下方
        if (labelPos.y() < 0) {
            labelPos.setY(polygon.boundingRect().bottom());
        }

        // 设置背景矩形（含文本子图元）的位置
        bgItem->setPos(labelPos);

        // 将背景矩形添加到场景（子图元textItem自动随之添加）
        graphicsScene->addItem(bgItem);
        textBoxItems.append(bgItem);

        // 注意：textItem作为bgItem的子图元，删除bgItem时会自动删除textItem
        //       因此textItem不需要单独加入textBoxItems列表，避免重复删除
    }
}

/**
 * @brief 以指定视口位置为锚点进行缩放
 * @details 缩放时保持指定视口位置对应的场景点不动，实现以鼠标为中心的缩放效果
 *          缩放范围自动限制在MIN_ZOOM(0.05)~MAX_ZOOM(20.0)之间
 *          算法步骤：
 *          1. 记录缩放前锚点对应的场景坐标
 *          2. 应用缩放变换
 *          3. 计算缩放后锚点对应的场景坐标偏移
 *          4. 平移视图补偿偏移，使锚点场景点保持不动
 * @param viewportPos 视口坐标位置（缩放锚点）
 * @param factor 缩放因子（>1放大，<1缩小）
 */
void ImageDisplayView::zoomAt(const QPoint &viewportPos, double factor)
{
    // 步骤1：记录缩放前鼠标位置对应的场景坐标
    QPointF scenePosBefore = mapToScene(viewportPos);

    // 计算缩放后的目标缩放因子
    double targetZoom = zoomFactor * factor;

    // 限制缩放范围：超出边界时调整缩放因子，使其恰好到达边界值
    if (targetZoom < MIN_ZOOM) {
        // 目标低于最小值：调整因子使其恰好缩放到MIN_ZOOM
        factor = MIN_ZOOM / zoomFactor;
    } else if (targetZoom > MAX_ZOOM) {
        // 目标超过最大值：调整因子使其恰好缩放到MAX_ZOOM
        factor = MAX_ZOOM / zoomFactor;
    }

    // 如果调整后的因子接近1.0，说明已在缩放边界，无需操作
    if (qFuzzyCompare(factor, 1.0)) {
        return;
    }

    // 步骤2：应用缩放变换
    scale(factor, factor);
    // 更新当前缩放因子
    zoomFactor *= factor;

    // 步骤3：计算缩放后鼠标位置对应的场景坐标
    QPointF scenePosAfter = mapToScene(viewportPos);

    // 步骤4：计算场景坐标偏移量，并平移视图补偿
    // 使缩放前后的鼠标位置对应同一个场景点，实现以鼠标为中心的缩放
    QPointF delta = scenePosBefore - scenePosAfter;
    translate(delta.x(), delta.y());

    // 发送缩放变化信号（新式Qt5信号槽语法，外部通过函数指针连接）
    emit zoomChanged(zoomFactor);
}
