# triplets/

## 职责边界

本目录存放 **overlay triplet**：继承 vcpkg 社区标准 triplet，追加 `VCPKG_BUILD_TYPE` 把依赖树锁定为
**单一构建配置**（Debug 或 Release），避免 vcpkg 默认的 dbg + rel 双配置重复构建（构建量减半）。

本目录文件刻意保持**纯两行**（include + set，无注释），所有说明集中于此。

## 文件清单

| 文件 | 继承自 | 构建配置 | 使用方 |
|------|--------|---------|--------|
| `x64-windows-dbg.cmake` | `x64-windows`（动态） | debug | 本地开发预设 `windows`、dbg-verify |
| `x64-windows-static-rel.cmake` | `x64-windows-static` | release | 预设 `windows-release`（CI + CD，静态单文件） |
| `x64-linux-dbg.cmake` | `x64-linux` | debug | 预设 `linux-debug`、dbg-verify |
| `x64-linux-rel.cmake` | `x64-linux` | release | 预设 `linux-release`（CI + CD） |
| `arm64-osx-dbg.cmake` | `arm64-osx` | debug | 预设 `macos-debug`、dbg-verify |
| `arm64-osx-rel.cmake` | `arm64-osx` | release | 预设 `macos-release`（CI + CD） |

## 背景（为什么必须这样做）

`VCPKG_BUILD_TYPE` 唯一的生效通道是 triplet 上下文：

- vcpkg.cmake 工具链**不转发** CMake 缓存变量（基线源码无任何引用，`vcpkg install` 也无对应参数）；
- vcpkg-tool **不读环境变量**——实证：triplet 求值运行在净化环境中，shell 传入的
  `VCPKG_BUILD_TYPE` 在 triplet 内读到为空；
- 因此单配置构建只能经 overlay triplet 声明字面量，由预设中的 `VCPKG_TARGET_TRIPLET` +
  `VCPKG_OVERLAY_TRIPLETS` 启用（见 `../CMakePresets.json`）。

## 变更注意

- 本目录任何文件变更 = 依赖树 ABI 变化。CI 的 binary cache key 已含本目录哈希
  （`hashFiles('ImageDiscropperCpp/triplets/*.cmake')`），变更自动换 key、旧缓存条目作废。
- triplet 文件名同时决定 `vcpkg_installed/<triplet>/` 的目录名，重命名同样触发全量重建。
