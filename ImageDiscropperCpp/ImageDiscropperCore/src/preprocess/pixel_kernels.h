// ============================================================================
// 文件：src/preprocess/pixel_kernels.h
// 作用：preprocess 私有像素核声明——90° 系列旋转、水平/垂直翻转、按通道反色/分离，
//       以及两枚执行层融合核：channelMap（f(v)=(v&K)^J 一步位映射）与 remapD4（D4 单趟重排）。
//       几何为纯位置置换（GRAY 经 getGray→setGray 单字节直拷，保持字节级无损）。
// 分块依据：Core 私有内部头（仿 src/json_common.h 先例）——不放 include/、不对下游
//       暴露；仅 preprocess_pipeline.cpp 经 applyOne 分派使用。逐步执行语义同时充当
//       execPlan 融合执行等价性验证的参照 oracle。
//       缩放（resized/scaled）与取色（colorAt）、灰度化（toGray）核属 core::Image，
//       本头不重复声明。
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/image.h"
#include "preprocess/preprocess_config.h"  // 声明层类型（与预处理参数单一真相源对齐）

namespace idc::preprocess {

// 旋转：只支持 90 的整数倍（90 / 180 / 270），其他角度返回未旋转的副本。
// 参数 angle 单位为度，允许负值（内部会归一化到 [0, 360)）。
core::Image rotate(const core::Image& src, int angle);

// 翻转：horizontal = true 为左右翻转，false 为上下翻转。
core::Image flip(const core::Image& src, bool horizontal = true);

// 按通道反色：mask 为长度为 3 的字符串（形如 "101"），
// '1' 表示对应通道 R/G/B 取 255 - v，'0' 表示保持不变（alpha 始终不变）。
// 灰度图只有单一亮度通道，逐通道选择不适用：mask 全 '0'（"000"）与彩色路径一致视为空操作返回副本，否则对每字节取 255 - v。返回新对象。
// 多通道路径为 channelMap("111", mask) 的特例适配器。
core::Image invertChannels(const core::Image& src, const std::string& mask = "111");

// 按通道分离：mask 中 '1' 表示保留对应通道，'0' 表示置 0。
// 灰度图无 R/G/B 之分，直接返回副本。多通道路径为 channelMap(mask, "000") 的特例适配器。
core::Image splitChannels(const core::Image& src, const std::string& mask = "111");

// 融合位映射核：逐通道执行 f(v) = (v & K[c]) ^ J[c]，一步覆盖 直通(10)/反色(11)/强制0(00)/强制255(01) 四态。
// k / j 均为长度 3 的 '0'/'1' 串（R/G/B）：k 长度非 3 归一 "111"（AND 恒等），**j 长度非 3 归一 "000"（XOR 恒等）**。
// alpha 字节原样拷贝保留（不透明度由 canvas::ImageTransform.opacity 在合成期承载）。仅对多通道图有意义：
// GRAY 图防御性返回副本（灰度态取反请走 invertChannels 的 gray 分支）。
// splitChannels / invertChannels 的多通道路径即本核的 (K,"000") / ("111",J) 特例；
// execPlan 把任意 Split/Invert 序列折成单次 channelMap，是「(v&K)^J 融合」的落点。
core::Image channelMap(const core::Image& src, const std::string& k, const std::string& j);

// 融合几何核：单趟完成 g = Fh(flipH) ∘ Rot(quad·90)（先旋转 quad 象限，再按 flipH 水平镜像），
// 与任意 Rotate/Flip 逐步复合的 D4 规范形逐像素等价。quad 取 [0,4)（内部归一），奇数象限时输出宽高互换。
// 采样用闭式逆映射（免正向 scatter）；像素搬运经与 rotate/flip 相同的 copyPixel 口径（GRAY 单字节直拷，无损）。
core::Image remapD4(const core::Image& src, int quad, bool flipH);

} // namespace idc::preprocess
