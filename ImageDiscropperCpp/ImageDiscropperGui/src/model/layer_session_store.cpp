// ============================================================================
// 文件：model/layer_session_store.cpp
// 作用：实现 LayerSessionStore——每层会话（声明态 + 签名）与物化位图的按 id 查改与生命周期回收，
//       以及图层物化签名推演（makeSig，见同名头文件说明）。纯数据、零 Qt、不跑像素。
// ============================================================================
#include "model/layer_session_store.h"

namespace idc::gui {

// 依图层现声明算其物化签名：覆盖 materializeLayer（整层位图：组装含底色框→缩放→预处理）读取的
// 全部输入——来源态（kind / 逐层 preprocess / 文件名 / 解码像素布局 / 配方参数）+ 目标尺寸（所有 kind）+
// 内层落位与图层框（hasImagePlacement / box / fit / 锚点枢轴偏移 / boxBackdrop）；恒不含 transform.x/y/
// anchor/layerPivot/opacity/zOrder——改落位或不透明度只重组、不重跑（缺口 1）。
LayerSig LayerSessionStore::makeSig(const canvas::ImageLayer& layer) {
    LayerSig s;
    s.kind = static_cast<int>(layer.sourceKind);
    s.pre = layer.preprocess;
    s.fileName = layer.fileName;
    s.decEmpty = layer.decodedPixels.empty();
    s.decW = layer.decodedPixels.width();
    s.decH = layer.decodedPixels.height();
    s.tw = layer.transform.width;
    s.th = layer.transform.height;
    s.hasRecipe = layer.recipe.has_value();
    s.recipe = layer.recipe;
    s.hasPlacement = layer.hasImagePlacement;
    s.boxW = layer.boxWidth;
    s.boxH = layer.boxHeight;
    s.imageFit = static_cast<int>(layer.imageFit);
    s.imageAnchor = static_cast<int>(layer.imageAnchor);
    s.imagePivot = static_cast<int>(layer.imagePivot);
    s.imageX = layer.imageX;
    s.imageY = layer.imageY;
    s.hasBackdrop = layer.boxBackdrop.has_value();
    s.backdrop = layer.boxBackdrop;
    return s;
}

LayerSession* LayerSessionStore::find(const int id) {
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}
const LayerSession* LayerSessionStore::find(const int id) const {
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

LayerSession& LayerSessionStore::ensure(const int id) { return sessions_[id]; }

void LayerSessionStore::remove(const int id) {
    sessions_.erase(id);
    materialized_.erase(id);
}

void LayerSessionStore::clear() {
    sessions_.clear();
    materialized_.clear();
}

// 剔除已删层：会话与物化位图一并回收，避免陈旧位图驻留内存，并防 id 复用时误命中。
void LayerSessionStore::prune(const std::unordered_set<int>& alive) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (!alive.contains(it->first)) {
            materialized_.erase(it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace idc::gui
