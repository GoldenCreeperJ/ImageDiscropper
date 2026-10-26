// ============================================================================
// 文件：app/menus/main_window_menus_canvas.cpp
// 作用：实现 MainWindowUi::buildMenus 的「图片 / 标注」域——图层栈 CRUD / 锚点枢轴九向子菜单、
//       标注撤销重做 / 叠放次序 / 填充开关 / 工具子菜单。
// 分块依据：见 main_window_menus.cpp 头注（按菜单域四 TU 拆分）；本 TU 只依赖菜单绑定助手
//       （main_window_menus_internal.h）与各面板/控制器的既有写回路径，域内控件/连接自持。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <vector>

#include "app/controllers/annotation_coordinator.h"
#include "app/controllers/session_controller.h"
#include "app/main_window.h"
#include "canvas/canvas_mode.h"   // CanvasMode（工具子菜单先切「标注绘制」态）
#include "canvas/canvas_view.h"   // CanvasView::canvasMode（工具子菜单判当前画布态）
#include "canvas/layer.h"         // canvas::ImageLayer（锚点/枢轴回写）
#include "generate/pattern.h"     // generate::ImageAnchor（九向枚举）
#include "model/annotation_bridge.h" // AnnoTool（标注工具枚举）
#include "panels/mode_tool_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

#include "main_window_menus_internal.h" // bindAddAction / bindAddMenu（四个菜单 TU 共用）

namespace idc::gui {

void MainWindowUi::buildLayerMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 图片（多层画布；图层栈 CRUD 独立成菜单；显隐开关在视图菜单）----
    QMenu* mLayers = w->mLayers_ = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuImage));
    QAction* aAddImageLayer = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kAddImageLayer));
    i18n::bindToolTip(w->retranslator_, aAddImageLayer, IDC_GT(tooltip, kAddImageLayer));
    QObject::connect(aAddImageLayer, &QAction::triggered, w, [w] { w->session_->onAddImageLayer(); });
    QAction* aClearLayers = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kClearAllImageLayers));
    i18n::bindToolTip(w->retranslator_, aClearLayers, IDC_GT(tooltip, kClearAllImageLayers));
    QObject::connect(aClearLayers, &QAction::triggered, w, [w] { w->session_->onClearAllLayers(); });
    mLayers->addSeparator();
    // 层序/删除：作用画布当前拾取层（「图片拾取」态点选图层即拾取）；无拾取层时 syncPanels 置灰。
    w->aLayerUp_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerUp));
    i18n::bindToolTip(w->retranslator_, w->aLayerUp_, IDC_GT(tooltip, kLayerUp));
    QObject::connect(w->aLayerUp_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerZOrder(*w->doc_.selectedLayerId(), true);
    });
    w->aLayerDown_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerDown));
    i18n::bindToolTip(w->retranslator_, w->aLayerDown_, IDC_GT(tooltip, kLayerDown));
    QObject::connect(w->aLayerDown_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerZOrder(*w->doc_.selectedLayerId(), false);
    });
    w->aLayerTop_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerToTop));
    QObject::connect(w->aLayerTop_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerToEdge(*w->doc_.selectedLayerId(), true);
    });
    w->aLayerBottom_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerToBottom));
    QObject::connect(w->aLayerBottom_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerToEdge(*w->doc_.selectedLayerId(), false);
    });
    w->aLayerRemove_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerRemove));
    i18n::bindToolTip(w->retranslator_, w->aLayerRemove_, IDC_GT(tooltip, kLayerRemove));
    QObject::connect(w->aLayerRemove_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.removeLayer(*w->doc_.selectedLayerId());
    });
    mLayers->addSeparator();
    // 锚点/枢轴（作用当前拾取层落位，9 向枚举镜像「图层编辑」面板两组下拉框；面板为真相源，
    // syncPanels 随拾取层回同步勾选态、无拾取层置灰）。
    const struct { generate::ImageAnchor anchor{}; i18n::TextRef label; } kAnchors[] = {
        { generate::ImageAnchor::TopLeft, IDC_GT(label, kDirTopLeft) },
        { generate::ImageAnchor::TopCenter, IDC_GT(label, kDirTopCenter) },
        { generate::ImageAnchor::TopRight, IDC_GT(label, kDirTopRight) },
        { generate::ImageAnchor::MiddleLeft, IDC_GT(label, kDirMiddleLeft) },
        { generate::ImageAnchor::Center, IDC_GT(label, kDirCenter) },
        { generate::ImageAnchor::MiddleRight, IDC_GT(label, kDirMiddleRight) },
        { generate::ImageAnchor::BottomLeft, IDC_GT(label, kDirBottomLeft) },
        { generate::ImageAnchor::BottomCenter, IDC_GT(label, kDirBottomCenter) },
        { generate::ImageAnchor::BottomRight, IDC_GT(label, kDirBottomRight) },
    };
    w->layerAnchorActions_.resize(9, nullptr);
    w->layerPivotActions_.resize(9, nullptr);
    QMenu* mAnchor = bindAddMenu(w->retranslator_, mLayers, IDC_GT(menu, kAnchor));
    w->mLayerAnchor_ = mAnchor;
    auto* anchorGroup = new QActionGroup(w);
    anchorGroup->setExclusive(true);
    QMenu* mPivot = bindAddMenu(w->retranslator_, mLayers, IDC_GT(menu, kPivot));
    w->mLayerPivot_ = mPivot;
    auto* pivotGroup = new QActionGroup(w);
    pivotGroup->setExclusive(true);
    for (const auto&[anchor, label] : kAnchors) {
        QAction* ac = bindAddAction(w->retranslator_, anchorGroup, label);
        ac->setCheckable(true);
        mAnchor->addAction(ac);
        w->layerAnchorActions_[static_cast<int>(anchor)] = ac;
        QObject::connect(ac, &QAction::triggered, w, [w, anchor = anchor] {
            if (w->doc_.selectedLayerId()) {
                if (const canvas::ImageLayer* layer = w->doc_.layerById(*w->doc_.selectedLayerId())) {
                    canvas::ImageLayer updated = *layer;
                    updated.transform.inCanvasAnchor = anchor;
                    w->doc_.updateLayer(*w->doc_.selectedLayerId(), updated);
                }
            }
        });
        QAction* pv = bindAddAction(w->retranslator_, pivotGroup, label);
        pv->setCheckable(true);
        mPivot->addAction(pv);
        w->layerPivotActions_[static_cast<int>(anchor)] = pv;
        QObject::connect(pv, &QAction::triggered, w, [w, anchor = anchor] {
            if (w->doc_.selectedLayerId()) {
                if (const canvas::ImageLayer* layer = w->doc_.layerById(*w->doc_.selectedLayerId())) {
                    canvas::ImageLayer updated = *layer;
                    updated.transform.layerPivot = anchor;
                    w->doc_.updateLayer(*w->doc_.selectedLayerId(), updated);
                }
            }
        });
    }

}

void MainWindowUi::buildAnnoMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 标注：撤销/重做/删除选中/清除全部/属性定位 ----
    // 标注撤销/重做复用 AnnotationEditor 内建条目级会话态；快捷键 Ctrl+Z/Y 统一挂在编辑菜单
    //（上下文路由：标注上下文时自动转发到此处），故本菜单项不绑定快捷键，避免冲突。
    QMenu* mAnno = w->mAnno_ = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuAnnotation));
    const QAction* aAnnoUndo = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoUndo));
    QObject::connect(aAnnoUndo, &QAction::triggered, w, [w] { w->annotation_->onAnnoUndo(); });
    const QAction* aAnnoRedo = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoRedo));
    QObject::connect(aAnnoRedo, &QAction::triggered, w, [w] { w->annotation_->onAnnoRedo(); });
    mAnno->addSeparator();
    QAction* aAnnoDel = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoDeleteSelected));
    i18n::bindToolTip(w->retranslator_, aAnnoDel, IDC_GT(tooltip, kAnnoDeleteSelected));
    QObject::connect(aAnnoDel, &QAction::triggered, w, [w] { w->annotation_->onAnnoDeleteSelected(); });
    const QAction* aAnnoClear = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoClearAll));
    QObject::connect(aAnnoClear, &QAction::triggered, w, [w] { w->annotation_->onAnnoClearAll(); });
    // 叠放次序（与标注属性页“叠放次序”区同一入口，作用选中项；无选中时 syncAnnoSelActions 置灰）。
    QMenu* mZ = bindAddMenu(w->retranslator_, mAnno, IDC_GT(menu, kAnnoStacking));
    w->aZRaise_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kLayerUp));
    QObject::connect(w->aZRaise_, &QAction::triggered, w, [w] { w->annotation_->onAnnoRaise(); });
    w->aZLower_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kLayerDown));
    QObject::connect(w->aZLower_, &QAction::triggered, w, [w] { w->annotation_->onAnnoLower(); });
    w->aZFront_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kAnnoToFront));
    QObject::connect(w->aZFront_, &QAction::triggered, w, [w] { w->annotation_->onAnnoBringToFront(); });
    w->aZBack_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kAnnoToBack));
    QObject::connect(w->aZBack_, &QAction::triggered, w, [w] { w->annotation_->onAnnoSendToBack(); });
    // 填充开关（勾选态随选中项/默认值回同步；无选中时设置下次绘制的默认值，与属性面板同语义）。
    w->aFill_ = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoFill));
    w->aFill_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aFill_, IDC_GT(tooltip, kAnnoFill));
    QObject::connect(w->aFill_, &QAction::triggered, w, [w](const bool on) { w->annotation_->onAnnoFillChanged(on); });
    mAnno->addSeparator();
    // 工具子菜单：与工具面板形状行镜像（面板为真相源、syncToolActions 回同步勾选态）；
    // 「选择」工具＝「标注选取」画布模式（在模式菜单）。绘制工具须处于「标注绘制」态，
    // 故非绘制态下先切画布模式（与用户在面板上先切模式再选工具的流程一致）。
    QMenu* mTools = bindAddMenu(w->retranslator_, mAnno, IDC_GT(menu, kAnnoTools));
    struct MenuToolEntry { AnnoTool tool{}; i18n::TextRef label; };
    const MenuToolEntry kMenuTools[] = {
        { AnnoTool::Rect, IDC_GT(label, kToolRect) },
        { AnnoTool::Square, IDC_GT(label, kToolSquare) },
        { AnnoTool::Rhombus, IDC_GT(label, kToolRhombus) },
        { AnnoTool::Circle, IDC_GT(label, kToolCircle) },
        { AnnoTool::Ellipse, IDC_GT(label, kToolEllipse) },
        { AnnoTool::IsosTri, IDC_GT(label, kToolIsoTriangle) },
        { AnnoTool::EquTri, IDC_GT(label, kToolEquTriangle) },
        { AnnoTool::RectTri, IDC_GT(label, kToolRightTriangle) },
        { AnnoTool::EquRectTri, IDC_GT(label, kToolIsoRightTri) },
        { AnnoTool::Line, IDC_GT(label, kToolLine) },
        { AnnoTool::Polyline, IDC_GT(label, kToolPolyline) },
        { AnnoTool::Text, IDC_GT(label, kToolText) },
        { AnnoTool::Brush, IDC_GT(label, kToolBrush) },
    };
    w->toolActions_.resize(14, nullptr);   // 按 AnnoTool 枚举序索引（Select=0 无菜单项）。
    auto* toolGroup = new QActionGroup(w);
    toolGroup->setExclusive(true);
    for (const auto&[tool, label] : kMenuTools) {
        QAction* a = bindAddAction(w->retranslator_, mTools, label);
        a->setCheckable(true);
        toolGroup->addAction(a);
        w->toolActions_[static_cast<int>(tool)] = a;
        QObject::connect(a, &QAction::triggered, w, [w, tool = tool] {
            if (w->view_ && w->view_->canvasMode() != CanvasMode::AnnoCreate)
                w->toolPanel_->setMode(CanvasMode::AnnoCreate);
            w->annotation_->onToolSelected(tool);
        });
    }

}

} // namespace idc::gui
