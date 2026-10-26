// ============================================================================
// 文件：model/derived_cache.h
// 作用：统一「签名 → 派生产物」缓存原语（零 Qt、header-only）——DerivedCache<Key, Value>
//       以 Key（覆盖产物全部输入的声明签名）判命中：同 Key 零重算直接回缓存引用，Key 变则
//       下次 get 现算覆盖。**失效策略归一**：显式挂钩 / 前缀比对 / 内容指纹三套历史策略
//       全部收敛为「Key 比对」一套——Key 覆盖全部输入即自愈，无挂钩可漏（friend 直改字段
//       的隐患自然消解）。
// 分块依据：纯数据 + 纯查改、不跑像素；与 PreprocessSession / LayerSessionStore 同为零 Qt，
//       可被无头 unit_tests 直编回归。
// 实例：Document 的底图淡出缓存（Key＝底图内容签名+不透明度+色彩步）、PreprocessSession
//       的 resize 重放缓存（Key＝[resize?] 前缀）、LayerSessionStore 的逐层物化缓存
//       （Key＝LayerSig，现结构已符合同一律，仅类型形态保留以便零拷贝直传 Core composeCanvas）。
// 短路纪律保留：恒等特例（如无淡出、空前缀）由调用方早退返回源引用，不进缓存。
// ============================================================================
#pragma once

#include <optional>
#include <utility>

namespace idc::gui {

template <typename Key, typename Value>
class DerivedCache {
public:
    // 命中同 Key → 返回缓存引用（零拷贝零重算）；否则以 compute(key) 现算、缓存后返回。
    template <typename Fn>
    const Value& get(const Key& key, Fn&& compute) {
        if (key_ && *key_ == key) return value_;
        value_ = std::forward<Fn>(compute)(key);
        key_ = key;
        return value_;
    }

    // 该 Key 是否命中（供失效判定/只读检查；不触发现算）。
    bool hit(const Key& key) const { return key_.has_value() && *key_ == key; }

    // 当前缓存产物 / 键（调用方保证 valid() 后再读；键可空＝无缓存）。
    const Value& value() const { return value_; }
    const std::optional<Key>& key() const { return key_; }
    bool valid() const { return key_.has_value(); }

    // 作废（换图 / 重置 / 回到恒等态回收内存）。
    void clear() {
        key_.reset();
        value_ = Value{};
    }

private:
    std::optional<Key> key_;
    Value value_{};
};

} // namespace idc::gui
