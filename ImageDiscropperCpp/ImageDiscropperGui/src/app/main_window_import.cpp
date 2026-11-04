// ============================================================================
// 文件：app/main_window_import.cpp
// 作用：实现 MainWindow 的外部导入簇——桌面拖拽导入（dragEnterEvent/dropEvent：单张本地图片或
//       单个 JSON 才接受）与剪贴板粘贴（onPasteImage：位图落临时 PNG / 文件 URL 直走同一链），
//       图片角色三选框（chooseImageRoleAndApply：作底图 vs 作图片图层）与后缀判据助手
//       （isImagePath/isConfigPath，与新 Canvas 对话框同口径）。
// 分块依据：见 main_window.cpp 头注（装配壳按簇拆 TU）；本 TU 只做入口判定与角色分派，载入/建层
//       一律走既有 session_/config_ 链路，不实现任何像素或图层逻辑。
// ============================================================================
#include "app/main_window.h"

#include <QAbstractButton>
#include <QApplication>   // clipboard()->mimeData()（粘贴导入）
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QUrl>

#include "app/controllers/config_controller.h"   // loadConfigFromPath（JSON 拖入走载入链）
#include "app/controllers/session_controller.h"  // openImageFromPath / onAddImageLayerFromPath（图片拖入/粘贴定角色后走同一链）
#include "app/status_bar.h"                      // status_->notify（粘贴失败/无图回报）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {
namespace {
// 拖拽导入后缀判据（与新 Canvas 对话框同口径）：图片集与 label::kFilterOpenImage / kFilterSourceImage 扩展名同步；大小写不敏感。
bool isImagePath(const QString& path) {
    static const QSet kImage{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("bmp"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("tga")};
    return kImage.contains(QFileInfo(path).suffix().toLower());
}
bool isConfigPath(const QString& path) { return QFileInfo(path).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0; }
} // namespace

// 拖拽导入：仅当拖入的是单张本地图片或单个本地 JSON 才接受（多文件/非支持后缀不识别）。
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile()) return;
    if (const QString path = urls.first().toLocalFile(); isImagePath(path) || isConfigPath(path)) event->acceptProposedAction();
}

// 拖拽落地：单文件（dragEnterEvent 已按此筛过）——图片弹三选框定底图/图层，JSON 直接走载入链。
// 模态框/载入一律经 singleShot(0) 延到本次事件栈退出后再做：绝不原地在原生 drop 事件里 exec——
// 嵌套事件循环会打乱 Qt 拖放子系统的收尾（Windows 下 OLE Drop 返回后的内部清理），令本次之后
// 不再投递 dragEnter，表现为「只能拖入一次」。先 acceptProposedAction 再返回，使本次拖放干净收口。
void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile()) return;
    if (const QString path = urls.first().toLocalFile(); isImagePath(path)) {
        event->acceptProposedAction();
        QTimer::singleShot(0, this, [this, path] { chooseImageRoleAndApply(path); });
    } else if (isConfigPath(path)) {
        event->acceptProposedAction();
        QTimer::singleShot(0, this, [this, path] { config_->loadConfigFromPath(path); });
    }
}

// 拖入/粘贴共用：就图片定角色——作底图 vs 作图片图层二选一（取消则不做任何事）。
void MainWindow::chooseImageRoleAndApply(const QString& path) {
    // 角色三选框与后续「载入/建层」（大图会自起模态忙碌框）不能交叠：exec() 仅隐藏 box、其对象在本函数
    //   返回前仍存活，此刻再 show 一个 ApplicationModal 忙碌框会与之在同一模态栈里打架，令忙碌框收不到
    //   正常事件/绘制而显示为全空白（图片仍正常打开，只是框空）。故先用作用域把 box 彻底析构、清空模态栈，
    //   再据角色触发下游流程（菜单直接打开无此前驱对话框，故不受影响）。
    enum class Role { None, Base, Layer } role = Role::None;
    {
        QMessageBox box(QMessageBox::Question, IDC_GTR(label, kDropAskTitle), IDC_GTR(label, kDropAskText),
                        QMessageBox::NoButton, this);
        const auto* baseBtn  = box.addButton(IDC_GTR(label, kDropAsBase),  QMessageBox::AcceptRole);
        const auto* layerBtn = box.addButton(IDC_GTR(label, kDropAsLayer), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == baseBtn)       role = Role::Base;
        else if (box.clickedButton() == layerBtn) role = Role::Layer;
    }   // box 在此析构：模态栈清空。
    // 强制处理 box 关闭产生的 DeferredDelete（原生资源释放），确保彻底清除上一个模态循环的残留。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // 再经 singleShot(0) 延到下一次事件循环迭代才触发下游载入/建层（其内部会自起忙碌 exec()）——
    //   避免 box.exec() 刚退出就“同一个调用栏”里紧接第二个模态循环，导致 Windows 上后者的
    //   layout 激活被跳过、子控件零几何不绘制（实测拖放/粘贴忙碌框全白而菜单打开正常）。
    //   这与本文件 dropEvent 已有的「绝不原地在 drop 事件里 exec」原则同构，只是层级更深一层。
    if (role == Role::Base)
        QTimer::singleShot(0, this, [this, path] { session_->openImageFromPath(path); });
    else if (role == Role::Layer)
        QTimer::singleShot(0, this, [this, path] { session_->onAddImageLayerFromPath(path); });
}

// 剪贴板粘贴图片（仅桌面）：优先取剪贴板位图（截图/图片查看器等）落临时 PNG 再定角色；
// 若剪贴板只有本地图片文件 URL（从资源管理器复制）则直接走拖入同一路径。二者皆无则回报。
void MainWindow::onPasteImage() {
    const QMimeData* md = QApplication::clipboard()->mimeData();
    if (!md) return;
    // 1) 剪贴板位图：写临时 PNG（带唯一名防覆写），以同一链加载。载入已在 chooseImageRoleAndApply
    //    内同步完成（对话框 exec 把像素拷进 Document/图层，导出资产亦取内存 decodedPixels），故用即删。
    if (md->hasImage()) {
        if (const auto img = qvariant_cast<QImage>(md->imageData()); !img.isNull()) {
            const QString tmp = QDir(QDir::tempPath()).filePath(
                QStringLiteral("idc_paste_%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
            if (!img.save(tmp, "PNG")) {
                status_->notify(IDC_GTR(status, kSessionPasteSaveFailed), true);
                return;
            }
            chooseImageRoleAndApply(tmp);
            QFile::remove(tmp);   // 回收暂存：不删则每次粘贴在系统临时目录永久积一张 PNG。
            return;
        }
    }
    // 2) 剪贴板携带单张本地图片文件（与拖入同口径）。
    if (md->hasUrls()) {
        if (const QList<QUrl> urls = md->urls(); urls.size() == 1 && urls.first().isLocalFile() && isImagePath(urls.first().toLocalFile())) {
            chooseImageRoleAndApply(urls.first().toLocalFile());
            return;
        }
    }
    status_->notify(IDC_GTR(status, kSessionPasteNoImage), true);
}

} // namespace idc::gui
