// ============================================================================
// 文件：util/coord_spin.h
// 作用：坐标输入框 CoordSpinBox——整数像素之外另接受百分比串（"50%" / "-12.5%"），供参数页、
//       导出重排、图层落位、预处理画布尺寸等几何输入点共用。
// 分块依据：百分比只是输入糖——提交时按调用方给的基准换算成像素，随后照常以整数显示与保存；
//       语法与换算复用 core::parsePercent / core::resolvePercent（与 §7 配置、CLI 几何选项
//       同一口径，换算规则只此一处）。Document 恒存像素：GUI 是画布直控，拖动或预处理改尺寸后
//       不保留比例意图，与 §7 配置里「未被编辑则原样回写百分比」不同（SPEC §2.1）。
// 类型：坐标框类与构造为 header-only inline（同 widget_factory.h 风格）；仅 GUI 目标引用，
//       故可含 <QSpinBox> / <QString>。
// ============================================================================
#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <QSpinBox>
#include <QString>

#include "core/percent.h"

class QWidget;

namespace idc::gui {

// 坐标输入框：整数像素之外另接受百分比串。无 Q_OBJECT（不需信号槽），故不参与 moc。
class CoordSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;

    // 换算基准（横向量取宽、纵向量取高，SPEC §2.1 分方向取基准）；返回 <= 0（基准未知）则不认百分比。
    void setBasisProvider(std::function<int()> provider) { basis_ = std::move(provider); }

protected:
    QValidator::State validate(QString& input, int& pos) const override {
        if (percentPixels(input)) return QValidator::Acceptable;
        return QSpinBox::validate(input, pos);
    }

    int valueFromText(const QString& text) const override {
        if (const std::optional<int> px = percentPixels(text)) return *px;
        return QSpinBox::valueFromText(text);
    }

private:
    // "50%" → 像素（按基准四舍五入，夹到本框取值域）；非百分比串 / 基准未知 → nullopt（交回基类按整数解析）。
    std::optional<int> percentPixels(const QString& text) const {
        const QString token = text.trimmed();
        if (!token.endsWith(QLatin1Char('%'))) return std::nullopt;
        const std::optional<double> ratio = core::parsePercent(token.toStdString());
        const int basis = basis_ ? basis_() : 0;
        if (!ratio || basis <= 0) return std::nullopt;
        return std::clamp(core::resolvePercent(*ratio, basis), minimum(), maximum());
    }

    std::function<int()> basis_;   // 基准提供者（空 = 只认整数像素）
};

// 创建坐标框：设取值范围，返回裸指针交由调用方继续 setSuffix / setKeyboardTracking / 定宽 /
// 信号连接等站点装配（与 makeSpin 同一定位，区别仅在类型与百分比识别）。
inline CoordSpinBox* makeCoordSpin(QWidget* parent, const int minVal, const int maxVal) {
    auto* spin = new CoordSpinBox(parent);
    spin->setRange(minVal, maxVal);
    return spin;
}

} // namespace idc::gui
