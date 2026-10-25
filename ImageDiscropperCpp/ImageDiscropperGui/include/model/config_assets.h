// ============================================================================
// 文件：model/config_assets.h
// 作用：配置「图层资产」解析与打包的共享逻辑（零 Qt）——GUI 保存/加载配置对话框的资产四选项
//       （是否导出 / 导出到哪里 / 是否导入 / 从哪里导入）落到本文件的纯数据函数上：
//         · resolveLayerAsset：file 相对名的查找链解析（assetsDir 字段 → <配置名>.assets/ → 配置目录）
//         · decodeFileLayers：加载侧「是否导入」落地——按链解码注入 decodedPixels（未注入＝降级占位）
//         · bundleFileLayers：保存侧「是否导出」落地——把 decodedPixels 按相对名写盘进资产目录
// 分块依据：只做「路径解析 + 像素搬运」，不含任何合成 / 引擎逻辑（CONTRIBUTING.md「分层纪律」）；
//       与 CLI 的 resolveAssetPath / bundleLayerAssets 同规则（本文件注释互指，单一真相源见 SPEC §7）。
// 说明：零 Qt 依赖，可被 CTest unit_tests 直接编译回归（同 annotation_editor 纪律）。
//       越界拒绝沿用既有契约：file 相对名 / 资产目录须为相对路径、不含「..」/ 绝对路径 / 盘符。
// ============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "engine/engine.h"

namespace idc::gui {

// file 相对名是否为安全资产名：相对、非空、不含「..」分量、不含根（绝对/盘符）。与 CLI 同规。
bool isSafeLayerAsset(const std::string& assetName);

// 解析一个 file 层源图的路径（查找链：assetsDirOverride（若有）→ assetsDirDeclared（config.assetsDir，
//   若声明）→ <配置名>.assets/ → 配置所在目录；命中首个存在者，均无返回空 path）。
// configPath 可为空（无配置文件：仅查配置所在目录＝当前工作目录，无 <配置名>.assets/ 层级）。
std::filesystem::path resolveLayerAsset(const std::string& configPath,
                                        const std::string& assetsDirDeclared,
                                        const std::string& assetsDirOverride,
                                        const std::string& assetName);

// 加载侧「导入图层资产」：遍历 config.layers 的 file 层按查找链解码注入 decodedPixels（Core readImageFile）；
// 未命中 → 保持空（降级占位）并计入 missing；file 名越界 → 写 err 返回 false（拒绝优先，不静默）。
// 无 layers / 全 recipe 层时无操作返回 true。
bool decodeFileLayers(engine::EngineConfig& config, const std::string& configPath,
                      const std::string& assetsDirOverride, int& missing, std::string& err);

// 保存侧「打包图层资产」：遍历 config.layers 的 file 层，把非空 decodedPixels 按相对名写盘进
//   targetDir（调用方传「导出到哪里」；为空则写进 <保存配置名>.assets/，由调用方算好传入）。
//   decodedPixels 为空 → 写 err 返回 false（缺像素不静默，调用方据此报因）；file 名越界 → err。
// 无 layers / 全 recipe 层时无操作返回 true。
bool bundleFileLayers(const engine::EngineConfig& config, const std::filesystem::path& targetDir,
                      std::string& err);

} // namespace idc::gui
