// ============================================================================
// 文件：app/forms/config_io_dialog.h
// 作用：保存 / 加载配置的模态对话框（双模式一类）——除配置路径外，把「图层资产」四选项显式化：
//       保存：☑ 打包图层资产（是否导出）+ 资产目录（导出到哪里，空＝默认 <配置名>.assets/）；
//       加载：☑ 导入图层资产（是否导入＝是否解析 file 层像素，不勾则全部降级占位）+
//             资产来源（从哪里导入，空＝自动：assetsDir 字段 → <配置名>.assets/ → 配置所在目录）。
// 分块依据：纯采参（不落像素、不解析资产——执行委托 model/config_assets 与 Controller，
//       CONTRIBUTING.md「分层纪律」）；对话框=一次性动作（菜单入口），不占面板。
// 说明：移动端（SPEC §8.3 形态差异）隐藏路径行（保存走固定目录、加载经列表选择，由 Controller 预设路径）；
//       资产分组与桌面同构。无 file 层时资产分组置灰（无资产可操作）。
// ============================================================================
#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace idc::gui {

class ConfigIoDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Save, Load };

    // mode＝保存/加载；hasFileLayers 决定资产分组可用性（无 file 层置灰）。
    ConfigIoDialog(Mode mode, QWidget* parent, bool hasFileLayers);

    // 配置路径（移动端由 Controller 预设）。
    QString path() const;
    void setPath(const QString& path);

    // 「是否导出 / 是否导入」（无 file 层恒 false）。
    bool assetActionEnabled() const;
    // 「导出到哪里 / 从哪里导入」；空串＝默认 / 自动（见文件头）。
    QString assetDir() const;

private:
    void pickPath();
    void pickAssetDir();

    Mode mode_;
    QLineEdit* pathEdit_{nullptr};
    QWidget* pathRow_{nullptr};
    QCheckBox* assetCheck_{nullptr};
    QLineEdit* assetDirEdit_{nullptr};
    QPushButton* assetDirBtn_{nullptr};
};

} // namespace idc::gui
