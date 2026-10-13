// ============================================================================
// 文件：app/palette.h
// 作用：画布语义色配置单例——六项核心语义色（保留绿/删除红/选区橙/标注蓝/网格灰/画布背景深灰）
//       的读写与 QSettings 应用级持久化；渲染图元（canvas/items/*）绘制期直接取色。
// 分块依据：
//   - 配色是纯呈现偏好，不进 §7 EngineConfig（作业参数态，CLI 不感知颜色）、不进 DocHistory
//     撤销栈（改配色不构成可撤销的一步）；持久化走 QSettings（跨会话，对标一般软件「外观设置」）；
//   - 派生色不入配置面：高亮橙（选区 R 通道压暗）、角标黑底、按钮选中蓝、旋转手柄白底
//     均由取色处或代码内常量派生，保持颜色语言整体协调、防误调出不可读组合。
// 说明：
//   · 色相以 RGB 存储，遮罩/选区填充的 alpha 单独成项（拆开的理由：同一个色相在一次 paint 里既当
//     实色描边又当半透明填充，浓度不能内嵌进 QColor；出厂默认只存 palette.cpp 的缺省表一处，
//     本头不复制数值，以免两处各自演化）；
//   · changed() 在任何值变更后发出并同步落盘；CanvasView 监听该信号重设背景刷并全场景重绘
//     （图元 paint 期取色，无需逐个重建图元）。
// ============================================================================
#pragma once

#include <QColor>
#include <QObject>

namespace idc::gui {

// ---------------------------------------------------------------------------
// Palette：画布语义色单例（进程内唯一，首次访问惰性从 QSettings 载入）。
// ---------------------------------------------------------------------------
class Palette : public QObject {
    Q_OBJECT
public:
    static Palette& instance();

    // 六项语义色（RGB，不含 alpha；填充 alpha 走独立取值器）。
    QColor keepColor() const { return keep_; }        // 保留块遮罩（默认绿）
    QColor deleteColor() const { return delete_; }   // 删除块遮罩（默认红）
    QColor selectionColor() const { return selection_; }        // 选区/切割线（默认橙）
    QColor selectionHighlight() const { return selectionHi_; }  // 派生：选区高亮态（R 通道压暗）
    QColor annotationColor() const { return annotation_; }      // 标注高亮/手柄（默认蓝）
    QColor gridColor() const { return grid_; }                  // L3 诱导网格线（默认灰）
    QColor background() const { return background_; }           // 画布空白区背景（默认深灰）

    // 填充 alpha（0–255）。
    int keepAlpha() const { return keepAlpha_; }
    int deleteAlpha() const { return deleteAlpha_; }
    int selectionFillAlpha() const { return selectionFillAlpha_; }
    int selectionFillHighlightAlpha() const { return selectionFillHiAlpha_; } // 派生：默认 = 普通填充 ×2

    void setKeepColor(const QColor&);
    void setDeleteColor(const QColor&);
    void setSelectionColor(const QColor&);
    void setAnnotationColor(const QColor&);
    void setGridColor(const QColor&);
    void setBackgroundColor(const QColor&);
    void setKeepAlpha(int);
    void setDeleteAlpha(int);
    void setSelectionFillAlpha(int);

    // 恢复出厂配色（清 QSettings 相应键 + 内存回缺省）。
    void restoreDefaults();

signals:
    // 任一配色变更后发出：CanvasView 借此重设背景刷并全场景重绘。
    void changed();

private:
    explicit Palette(QObject* parent = nullptr);
    // 变更后统一处理：发 changed()（值比较去重）+ 全量落盘。
    void touched();
    void save() const;

    QColor keep_;
    QColor delete_;
    QColor selection_;
    QColor selectionHi_;   // selectionColor 的派生高亮态（setSelectionColor 时重算）
    QColor annotation_;
    QColor grid_;
    QColor background_;
    // 四个 alpha 成员不在本处赋初值：构造函数从 QSettings（出厂值取自 palette.cpp 缺省表）无条件写入，
    // 头文件里的任何初值都读不到，留着只会与缺省表重复并漂移。
    int keepAlpha_;
    int deleteAlpha_;
    int selectionFillAlpha_;
    int selectionFillHiAlpha_;   // 派生：随 selectionFillAlpha_ ×2
};

// 取色助手：以 Palette 的 RGB 色相 + 指定 alpha 合成绘制用色（图元 paint 期常用）。
inline QColor withAlpha(const QColor& rgb, const int alpha) {
    return QColor(rgb.red(), rgb.green(), rgb.blue(), alpha);
}

} // namespace idc::gui
