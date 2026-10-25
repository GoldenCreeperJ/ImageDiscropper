// ============================================================================
// 文件：app/forms/config_io_dialog.cpp
// 作用：实现 ConfigIoDialog（见同名头文件说明）——路径行 + 「图层资产」分组（开关 + 目录行）。
// 分块依据：只做「排版 + 字段采集」，资产解析 / 打包全在 model/config_assets（零 Qt，可直编回归）。
// ============================================================================
#include "app/forms/config_io_dialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>

#include "app/palette.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"
#include "util/gui_nums.h"
#include "util/scrollable_dialog.h"   // limitDialogToScreen（小屏/横屏护栏，与其余对话框同规）

namespace idc::gui {

ConfigIoDialog::ConfigIoDialog(const Mode mode, QWidget* parent, const bool hasFileLayers)
    : QDialog(parent), mode_(mode) {
    if (mode == Mode::Save) setWindowTitle(IDC_GTR(label, kDlgTitleSaveConfig));
    else setWindowTitle(IDC_GTR(label, kDlgTitleLoadConfig));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    // ---- 配置路径行（移动端隐藏：由 Controller 预设固定目录 / 列表选择结果）----
    pathRow_ = new QWidget(this);
    auto* pathLay = new QHBoxLayout(pathRow_);
    pathLay->setContentsMargins(0, 0, 0, 0);
    pathEdit_ = new QLineEdit(pathRow_);
    pathEdit_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
    auto* browseBtn = new QPushButton(IDC_GTR(label, kBrowseButton), pathRow_);
    browseBtn->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
    connect(browseBtn, &QPushButton::clicked, this, &ConfigIoDialog::pickPath);
    pathLay->addWidget(pathEdit_, 1);
    pathLay->addWidget(browseBtn);
    root->addWidget(pathRow_);
#ifdef Q_OS_ANDROID
    pathRow_->setVisible(false);   // 形态差异（SPEC §8.3）：移动端路径由 Controller 预设。
#else
    const QRect pg = parentWidget() ? parentWidget()->window()->geometry() : QRect();
    const QSize hint = pathEdit_->sizeHint();
    pathEdit_->setMinimumWidth(std::min(420, std::max(280, pg.width() * 2 / 3 - 40)));
#endif

    // ---- 图层资产分组（四选项的载体）----
    auto* assetBox = new QGroupBox(IDC_GTR(label, kConfigAssetBoxTitle), this);
    auto* assetLay = new QVBoxLayout(assetBox);
    assetLay->setSpacing(nums::panels::kPanelSpacingPx);

    assetCheck_ = new QCheckBox(assetBox);
    if (mode == Mode::Save) assetCheck_->setText(IDC_GTR(label, kConfigBundleAssets));
    else assetCheck_->setText(IDC_GTR(label, kConfigImportAssets));
    assetCheck_->setChecked(hasFileLayers);   // 默认勾选（有 file 层时）——用户可显式改。
    assetCheck_->setEnabled(hasFileLayers);
    assetLay->addWidget(assetCheck_);

    auto* dirRow = new QWidget(assetBox);
    auto* dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    assetDirEdit_ = new QLineEdit(dirRow);
    assetDirEdit_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
    if (mode == Mode::Save) assetDirEdit_->setPlaceholderText(IDC_GTR(label, kConfigAssetDirPlaceholder));
    else assetDirEdit_->setPlaceholderText(IDC_GTR(label, kConfigAssetSourcePlaceholder));
    assetDirEdit_->setEnabled(hasFileLayers);
    assetDirBtn_ = new QPushButton(IDC_GTR(label, kBrowseButton), dirRow);
    assetDirBtn_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
    assetDirBtn_->setEnabled(hasFileLayers);
    connect(assetDirBtn_, &QPushButton::clicked, this, &ConfigIoDialog::pickAssetDir);
    dirLay->addWidget(assetDirEdit_, 1);
    dirLay->addWidget(assetDirBtn_);
    assetLay->addWidget(dirRow);
#ifdef Q_OS_ANDROID
    // 移动端形态差异（SPEC §8.3）：应用沙箱内自定义资产目录不可达（QFileDialog 目录选择在 Android
    // 不可靠、外部路径不可写），目录恒默认（打包进 <配置名>.assets/）/ 自动（查找链）——只保留开关。
    dirRow->setVisible(false);
#endif
    root->addWidget(assetBox);

    // ---- 确定 / 取消 ----
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(IDC_GTR(label, kDialogOkButton));
    buttons->button(QDialogButtonBox::Cancel)->setText(IDC_GTR(label, kDialogCancelButton));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // 目录行随开关联动（未启用打包/导入时目录无意义）。
    connect(assetCheck_, &QCheckBox::toggled, this, [this](const bool on) {
        assetDirEdit_->setEnabled(on && assetCheck_->isEnabled());
        assetDirBtn_->setEnabled(on && assetCheck_->isEnabled());
    });
    // 构造初态按开关刷新一次。
    assetDirEdit_->setEnabled(assetCheck_->isChecked());
    assetDirBtn_->setEnabled(assetCheck_->isChecked());

    limitDialogToScreen(this);
}

QString ConfigIoDialog::path() const { return pathEdit_->text().trimmed(); }
void ConfigIoDialog::setPath(const QString& path) { pathEdit_->setText(path); }

bool ConfigIoDialog::assetActionEnabled() const { return assetCheck_->isEnabled() && assetCheck_->isChecked(); }
QString ConfigIoDialog::assetDir() const { return assetDirEdit_->text().trimmed(); }

void ConfigIoDialog::pickPath() {
    const QString p = mode_ == Mode::Save
        ? QFileDialog::getSaveFileName(this, IDC_GTR(label, kDlgTitleSaveConfig), path(),
                                       IDC_GTR(label, kFilterConfigJson))
        : QFileDialog::getOpenFileName(this, IDC_GTR(label, kDlgTitleLoadConfig), path(),
                                       IDC_GTR(label, kFilterConfigJson));
    if (!p.isEmpty()) pathEdit_->setText(p);
}

void ConfigIoDialog::pickAssetDir() {
    const QString d = QFileDialog::getExistingDirectory(this, IDC_GTR(label, kDlgTitlePickAssetDir),
                                                        assetDirEdit_->text());
    if (!d.isEmpty()) assetDirEdit_->setText(QDir::toNativeSeparators(d));
}

} // namespace idc::gui
