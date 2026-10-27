// ============================================================================
// 文件：src/engine/config_json/percent_coords.cpp
// 作用：percent_coords.h 的实现——百分比坐标的读侧预归一化（"50%" → 像素）与保存侧回写（"N%"）。
//       基准＝该字段所在坐标系的尺寸：cut / export.merge / generate / layers.transform / resize 目标
//       尺寸取 source（画布）宽高；annotations 取工作图尺寸（source 经 preprocess 几何步推导）。
//       每处换算记一条 PercentHint（JSON Pointer + 比例 + 像素），保存时据此原样回写 "N%"。
// 分块依据：百分比坐标换算是一套独立机制（就地改写 + 记账 + 保存侧幂等回写），与 codec 的字段映射、
//       严格校验正交；独立成文件避免 engine_config_json.cpp 沦为上帝文件。仅三个入口被序列化侧调用，
//       其余换算助手（mutableAt / joinPtr / resolve* 族 / workSizeOf）全部内聚于本文件匿名命名空间。
// ============================================================================
#include "percent_coords.h"

#include "engine_config_json_internal.h" // detail: keys/defs/at/jStr/jInt + generatorFromName 枚举映射
#include "i18n/core_i18n.h"              // IDC_KTRF：换算失败定位报因取译文（缺译文回退常量层）

#include <cstddef>
#include <string>
#include <utility> // std::swap（workSizeOf 的 rotate 90/270 宽高交换）

namespace idc::engine {
namespace {
using namespace detail;   // keys/defs/at/jStr/jInt 等简写（单一真相源见 engine_config_json_internal.h）

// 可变子节点：键缺失或值为 null → nullptr（jsonutil::at 返回 const 引用，无法用于就地替换）。
json* mutableAt(json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    const auto it = obj.find(key);
    return it == obj.end() || it->is_null() ? nullptr : &*it;
}

// JSON Pointer 片段追加（"/cut" + "/params" → "/cut/params"）：与 engineConfigToJson 的输出结构一一对应。
std::string joinPtr(const std::string& base, const std::string& leaf) { return base + "/" + leaf; }

// 单个坐标叶子：整数 / null / 缺失不动；"N%" → 依 basis 四舍五入为像素并记账。
//   path＝点分路径（报因定位），ptr＝JSON Pointer（保存回写），二者描述同一字段。
bool resolveLeaf(json& node, const int basis, const std::string& path, const std::string& ptr,
                 core::PercentHints& hints, std::string& err) {
    if (!node.is_string()) return true;
    const auto ratio = core::parsePercent(node.get_ref<const std::string&>());
    if (!ratio) { err = IDC_KTRF(msg, kErrConfigNotCoord, path, node.dump()); return false; }
    if (basis <= 0) { err = IDC_KTRF(msg, kErrConfigPercentNoBasis, path); return false; }
    const int px = core::resolvePercent(*ratio, basis);
    hints.push_back(core::PercentHint{ptr, *ratio, static_cast<double>(px)});
    node = px;
    return true;
}

bool resolveCoord(json& obj, const char* key, const int basis, const std::string& path, const std::string& ptr,
                  core::PercentHints& hints, std::string& err) {
    json* node = mutableAt(obj, key);
    return node == nullptr || resolveLeaf(*node, basis, path, ptr, hints, err);
}

// 数组元素（按索引）的百分比换算：points / segments[].coords 用。
bool resolveIndexed(json& arr, const std::size_t idx, const int basis, const std::string& path, const std::string& ptr,
                    core::PercentHints& hints, std::string& err) {
    if (!arr.is_array() || idx >= arr.size()) return true;
    return resolveLeaf(arr[idx], basis, path, ptr, hints, err);
}

// 矩形四键：x 轴取宽、y 轴取高（缺失键跳过，故对参数对象同样适用）。
bool resolveRectCoords(json& o, const BoxSize& box, const std::string& path, const std::string& ptr,
                       core::PercentHints& hints, std::string& err) {
    if (!o.is_object()) return true;
    const auto one = [&](const char* k, const int basis) {
        return resolveCoord(o, k, basis, path + "." + k, joinPtr(ptr, k), hints, err);
    };
    return one(keys::common::kX1, box.width) && one(keys::common::kY1, box.height)
        && one(keys::common::kX2, box.width) && one(keys::common::kY2, box.height);
}

// 网格参数：平铺形态四键（基准点与单元宽取宽轴、高取高轴）；分轴形态按 x / y 子对象逐轴换算
//   （x 轴取宽、y 轴取高）。weights 是相对比例而非坐标，不参与百分比→像素换算。
bool resolveGridCoords(json& o, const BoxSize& box, const std::string& path, const std::string& ptr,
                       core::PercentHints& hints, std::string& err) {
    if (!o.is_object()) return true;
    const auto one = [&](const char* k, const int basis) {
        return resolveCoord(o, k, basis, path + "." + k, joinPtr(ptr, k), hints, err);
    };
    if (json* ax = mutableAt(o, keys::layer::kAxisX); ax != nullptr && ax->is_object()) {
        const std::string ap = path + "." + keys::layer::kAxisX;
        const std::string apt = joinPtr(ptr, keys::layer::kAxisX);
        if (!resolveCoord(*ax, keys::layer::kAxisOrigin, box.width, ap + "." + keys::layer::kAxisOrigin,
                          joinPtr(apt, keys::layer::kAxisOrigin), hints, err)) return false;
        if (!resolveCoord(*ax, keys::layer::kAxisCell, box.width, ap + "." + keys::layer::kAxisCell,
                          joinPtr(apt, keys::layer::kAxisCell), hints, err)) return false;
    }
    if (json* ay = mutableAt(o, keys::layer::kAxisY); ay != nullptr && ay->is_object()) {
        const std::string ap = path + "." + keys::layer::kAxisY;
        const std::string apt = joinPtr(ptr, keys::layer::kAxisY);
        if (!resolveCoord(*ay, keys::layer::kAxisOrigin, box.height, ap + "." + keys::layer::kAxisOrigin,
                          joinPtr(apt, keys::layer::kAxisOrigin), hints, err)) return false;
        if (!resolveCoord(*ay, keys::layer::kAxisCell, box.height, ap + "." + keys::layer::kAxisCell,
                          joinPtr(apt, keys::layer::kAxisCell), hints, err)) return false;
    }
    return one(keys::layer::kOriginX, box.width) && one(keys::layer::kOriginY, box.height)
        && one(keys::config::kCellWidth, box.width) && one(keys::config::kCellHeight, box.height);
}

// 工作图尺寸（annotations 的坐标基准）：source 尺寸经 preprocess 几何步推导——rotate 90/270 交换宽高；
//   resize 正尺寸覆盖（非正＝保持原尺寸，与 preprocess 单步语义一致）；颜色步（gray/split/invert/flip）不改尺寸。
BoxSize workSizeOf(const json& root) {
    const json& src = at(root, keys::config::kSource);
    BoxSize box{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};
    const json& pre = at(root, keys::common::kPreprocess);
    if (!pre.is_array()) return box;
    for (const json& step : pre) {
        if (const std::string op = jStr(at(step, keys::pre::kOp), ""); op == "rotate") {
            if (const int deg = (jInt(at(step, keys::pre::kDeg), 0) % 360 + 360) % 360; deg == 90 || deg == 270) std::swap(box.width, box.height);
        } else if (op == "resize") {
            const int w = jInt(at(step, keys::common::kWidth), 0);
            const int h = jInt(at(step, keys::common::kHeight), 0);
            if (w > 0) box.width = w;
            if (h > 0) box.height = h;
        }
    }
    return box;
}

// 标注几何：x/y（文字落点）、x1..y2（锚点图元 / 圆 / 椭圆）、points（直线 / 折线顶点）、
//   segments[].coords（path 控制点，偶索引 x、奇索引 y）。只动这四处已知几何键——样式与 transform
//   快照（matrix / scale / rotation）是相对量，不作坐标换算。
bool resolveAnnotationCoords(json& a, const BoxSize& box, const std::string& path, const std::string& ptr,
                             core::PercentHints& hints, std::string& err) {
    if (!a.is_object()) return true;
    if (!resolveCoord(a, keys::common::kX, box.width, path + "." + keys::common::kX, joinPtr(ptr, keys::common::kX), hints, err)) return false;
    if (!resolveCoord(a, keys::common::kY, box.height, path + "." + keys::common::kY, joinPtr(ptr, keys::common::kY), hints, err)) return false;
    if (!resolveRectCoords(a, box, path, ptr, hints, err)) return false;
    if (json* pts = mutableAt(a, keys::common::kPoints); pts != nullptr && pts->is_array()) {
        for (std::size_t i = 0; i < pts->size(); ++i) {
            json& p = (*pts)[i];
            if (!p.is_array()) continue;
            const std::string ip = std::to_string(i);
            const std::string ppath = path + "." + keys::common::kPoints + "[" + ip + "]";
            const std::string pptr = joinPtr(joinPtr(ptr, keys::common::kPoints), ip);
            if (!resolveIndexed(p, 0, box.width, ppath + "[0]", joinPtr(pptr, "0"), hints, err)) return false;
            if (!resolveIndexed(p, 1, box.height, ppath + "[1]", joinPtr(pptr, "1"), hints, err)) return false;
        }
    }
    if (json* segs = mutableAt(a, keys::anno::kSegments); segs != nullptr && segs->is_array()) {
        for (std::size_t i = 0; i < segs->size(); ++i) {
            json* coords = mutableAt((*segs)[i], keys::anno::kCoords);
            if (coords == nullptr || !coords->is_array()) continue;
            const std::string ip = std::to_string(i);
            const std::string spath = path + "." + keys::anno::kSegments + "[" + ip + "]." + keys::anno::kCoords;
            const std::string sptr = joinPtr(joinPtr(joinPtr(ptr, keys::anno::kSegments), ip), keys::anno::kCoords);
            for (std::size_t j = 0; j < coords->size(); ++j) {
                if (const std::string jp = std::to_string(j); !resolveIndexed(*coords, j, j % 2 == 0 ? box.width : box.height,
                                                                              spath + "[" + jp + "]", joinPtr(sptr, jp), hints, err)) return false;
            }
        }
    }
    return true;
}

} // namespace

// 全档预归一化入口（见 percent_coords.h）：按 schema 段序逐段换算百分比坐标。annotations 置于
//   preprocess 之后——其基准需先拿到 resize 归一后的工作图尺寸。
bool resolvePercentCoords(jsonutil::json& root, core::PercentHints& hints, std::string& err) {
    using namespace detail;
    const json& src = at(root, keys::config::kSource);
    const BoxSize canvas{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};

    // cut：params 依 generator 分派（rect / multi-rect / grid），基准＝画布。
    if (json* cut = mutableAt(root, keys::config::kCut)) {
        const CutGenerator gen = generatorFromName(jStr(at(*cut, keys::config::kGenerator), defs::kGeneratorRect));
        if (json* params = mutableAt(*cut, keys::config::kParams)) {
            const std::string pp = std::string(keys::config::kCut) + "." + keys::config::kParams;
            const std::string ppt = joinPtr(joinPtr("", keys::config::kCut), keys::config::kParams);
            if (gen == CutGenerator::MultiRect) {
                if (json* rs = mutableAt(*params, keys::config::kRects); rs != nullptr && rs->is_array()) {
                    for (std::size_t i = 0; i < rs->size(); ++i) {
                        if (const std::string n = std::to_string(i); !resolveRectCoords((*rs)[i], canvas, pp + "." + keys::config::kRects + "[" + n + "]",
                                                                                        joinPtr(joinPtr(ppt, keys::config::kRects), n), hints, err)) return false;
                    }
                }
            } else if (gen == CutGenerator::Grid) {
                if (!resolveGridCoords(*params, canvas, pp, ppt, hints, err)) return false;
            } else {
                if (!resolveRectCoords(*params, canvas, pp, ppt, hints, err)) return false;
            }
        }
    }

    // export.merge：单元尺寸可写百分比（相对画布）。
    if (json* ex = mutableAt(root, keys::config::kExport)) {
        if (json* mg = mutableAt(*ex, keys::config::kMerge)) {
            const std::string mp = std::string(keys::config::kExport) + "." + keys::config::kMerge;
            const std::string mpt = joinPtr(joinPtr("", keys::config::kExport), keys::config::kMerge);
            if (!resolveCoord(*mg, keys::config::kCellWidth, canvas.width, mp + "." + keys::config::kCellWidth, joinPtr(mpt, keys::config::kCellWidth), hints, err)) return false;
            if (!resolveCoord(*mg, keys::config::kCellHeight, canvas.height, mp + "." + keys::config::kCellHeight, joinPtr(mpt, keys::config::kCellHeight), hints, err)) return false;
        }
    }

    // generate：底图落位 x/y（v2 在 image 元素内；落位框恒＝画布）。
    if (json* gen = mutableAt(root, keys::config::kGenerate)) {
        if (json* im = mutableAt(*gen, keys::layer::kImage)) {
            const std::string gpt = joinPtr(joinPtr("", keys::config::kGenerate), keys::layer::kImage);
            const std::string gp = std::string(keys::config::kGenerate) + "." + keys::layer::kImage;
            if (!resolveCoord(*im, keys::common::kX, canvas.width, gp + "." + keys::common::kX, joinPtr(gpt, keys::common::kX), hints, err)) return false;
            if (!resolveCoord(*im, keys::common::kY, canvas.height, gp + "." + keys::common::kY, joinPtr(gpt, keys::common::kY), hints, err)) return false;
        }
    }

    // layers：transform 的位置与尺寸（画布坐标）。
    if (json* ls = mutableAt(root, keys::config::kLayers); ls != nullptr && ls->is_array()) {
        for (std::size_t i = 0; i < ls->size(); ++i) {
            const std::string n = std::to_string(i);
            json* tf = mutableAt((*ls)[i], keys::common::kTransform);
            if (tf == nullptr) continue;
            const std::string tp = std::string(keys::config::kLayers) + "[" + n + "]." + keys::common::kTransform;
            const std::string tpt = joinPtr(joinPtr(joinPtr("", keys::config::kLayers), n), keys::common::kTransform);
            if (!resolveCoord(*tf, keys::common::kX, canvas.width, tp + "." + keys::common::kX, joinPtr(tpt, keys::common::kX), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kY, canvas.height, tp + "." + keys::common::kY, joinPtr(tpt, keys::common::kY), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kWidth, canvas.width, tp + "." + keys::common::kWidth, joinPtr(tpt, keys::common::kWidth), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kHeight, canvas.height, tp + "." + keys::common::kHeight, joinPtr(tpt, keys::common::kHeight), hints, err)) return false;
        }
    }

    // preprocess：仅 resize 的目标尺寸（基准＝source）。
    if (json* pre = mutableAt(root, keys::common::kPreprocess); pre != nullptr && pre->is_array()) {
        for (std::size_t i = 0; i < pre->size(); ++i) {
            json& step = (*pre)[i];
            if (jStr(at(step, keys::pre::kOp), "") != "resize") continue;
            const std::string n = std::to_string(i);
            const std::string sp = std::string(keys::common::kPreprocess) + "[" + n + "]";
            const std::string spt = joinPtr(joinPtr("", keys::common::kPreprocess), n);
            if (!resolveCoord(step, keys::common::kWidth, canvas.width, sp + "." + keys::common::kWidth, joinPtr(spt, keys::common::kWidth), hints, err)) return false;
            if (!resolveCoord(step, keys::common::kHeight, canvas.height, sp + "." + keys::common::kHeight, joinPtr(spt, keys::common::kHeight), hints, err)) return false;
        }
    }

    // annotations：基准＝工作图尺寸（resize 已在上段归一）。
    if (json* annos = mutableAt(root, keys::config::kAnnotations); annos != nullptr && annos->is_array()) {
        const BoxSize work = workSizeOf(root);
        for (std::size_t i = 0; i < annos->size(); ++i) {
            if (const std::string n = std::to_string(i); !resolveAnnotationCoords((*annos)[i], work, std::string(keys::config::kAnnotations) + "[" + n + "]",
                                                                                  joinPtr(joinPtr("", keys::config::kAnnotations), n), hints, err)) return false;
        }
    }
    return true;
}

// 保存侧回写（见 percent_coords.h）。
void applyPercentHints(jsonutil::json& root, const core::PercentHints& hints) {
    using namespace detail;
    if (hints.empty()) return;
    const json& src = at(root, keys::config::kSource);
    const BoxSize canvas{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};
    BoxSize work = canvas;
    bool workComputed = false;   // workSizeOf 只在遇首条 annotations 记录时算一次。
    for (const auto&[pointer, ratio, resolved] : hints) {
        const json::json_pointer ptr(pointer);
        if (!root.contains(ptr)) continue;
        json& node = root[ptr];
        if (!node.is_number() || node.get<double>() != resolved) continue;
        const bool anno = pointer.rfind("/annotations/", 0) == 0;   // 标注基准是工作图，其余字段是画布。
        if (anno && !workComputed) { work = workSizeOf(root); workComputed = true; }
        const auto&[width, height] = anno ? work : canvas;
        if (const int px = static_cast<int>(resolved); core::resolvePercent(ratio, width) != px && core::resolvePercent(ratio, height) != px) continue;
        node = core::formatPercent(ratio);
    }
}

// 段级入口的图层百分比换算（见 percent_coords.h）。
bool resolveLayerPercentInPlace(jsonutil::json& item, const BoxSize& canvas, const std::string& path, std::string& err) {
    using namespace detail;
    json* tf = mutableAt(item, keys::common::kTransform);
    if (tf == nullptr || !tf->is_object()) return true;
    const std::string tp = path + "." + keys::common::kTransform;
    const auto one = [&](const char* key, const int basis) -> bool {
        json* node = mutableAt(*tf, key);
        if (node == nullptr || !node->is_string()) return true;
        const std::string field = tp + "." + key;
        const auto ratio = core::parsePercent(node->get_ref<const std::string&>());
        if (!ratio) { err = IDC_KTRF(msg, kErrConfigNotCoord, field, node->dump()); return false; }
        if (basis <= 0) { err = IDC_KTRF(msg, kErrLayerPercentNoBasis, field); return false; }
        *node = core::resolvePercent(*ratio, basis);
        return true;
    };
    return one(keys::common::kX, canvas.width) && one(keys::common::kY, canvas.height)
        && one(keys::common::kWidth, canvas.width) && one(keys::common::kHeight, canvas.height);
}

} // namespace idc::engine
