# triplets/

## 职责边界

本目录存放 **overlay triplet**：内容照抄**钉死基线**（e6f9e70a29）的社区标准 triplet 并追加
`VCPKG_BUILD_TYPE`，把依赖树锁定为**单一构建配置**（Debug 或 Release），避免 vcpkg 默认的
dbg + rel 双配置重复构建（构建量减半）。

**自包含写法**（不 include 社区文件）：基线钉死后社区 triplet 内容即固定，照抄即冻结语义、
消除隐式耦合；代价是基线升级时需对照官方文件手工同步（见「变更注意」）。
文件保持纯配置行、无注释，所有说明集中于此。

## 文件清单

| 文件                        | 照抄自（基线 e6f9e70a29）   | 构建配置    | 使用方                                            |
|---------------------------|----------------------|---------|------------------------------------------------|
| `x64-windows-dbg.cmake`   | `x64-windows`（动态）    | debug   | 本地开发预设 `windows-debug`、dbg-verify（目标 + 宿主）     |
| `x64-windows-rel.cmake`   | `x64-windows-static` | release | 预设 `windows-release`（目标 + 宿主，CI + CD，静态单文件）    |
| `x64-linux-dbg.cmake`     | `x64-linux`          | debug   | 预设 `linux-debug`、dbg-verify（目标 + 宿主）           |
| `x64-linux-rel.cmake`     | `x64-linux`          | release | 预设 `linux-release`（目标 + 宿主，CI + CD）            |
| `arm64-osx-dbg.cmake`     | `arm64-osx`          | debug   | 预设 `macos-debug`、dbg-verify（目标 + 宿主）           |
| `arm64-osx-rel.cmake`     | `arm64-osx`          | release | 预设 `macos-release`（目标 + 宿主，CI + CD）            |
| `arm64-android-rel.cmake` | `arm64-android`      | release | 预设 `android` 的**目标**（交叉编译：宿主用 `x64-linux-rel`） |
| `arm64-android-dbg.cmake` | `arm64-android`      | debug   | 预设 `android-dbg` 的**目标**（宿主 `x64-linux-dbg`）   |
| `arm64-ios-rel.cmake`     | vcpkg iOS 惯例自写       | release | 预设 `ios` 的**目标**（宿主 `arm64-osx-rel`）           |
| `arm64-ios-dbg.cmake`     | vcpkg iOS 惯例自写       | debug   | 预设 `ios-dbg` 的**目标**（宿主 `arm64-osx-dbg`）       |

> **移动端为何自写而非用社区三元组**：`triplets/community/` 虽有 `arm64-android-release` /
> `arm64-ios-release`，但**无 `-debug` 变体**——为命名一致与 dbg/rel 配套（贡献者真机调试需要
> debug 构建），统一按本目录模式自写全套，保持一致性。

## 背景（为什么必须这样做）

`VCPKG_BUILD_TYPE` 唯一的生效通道是 triplet 上下文：

- vcpkg.cmake 工具链**不转发** CMake 缓存变量（基线源码无任何引用，`vcpkg install` 也无对应参数）；
- vcpkg-tool **不读环境变量**——实证：triplet 求值运行在净化环境中，shell 传入的
  `VCPKG_BUILD_TYPE` 在 triplet 内读到为空；
- 因此单配置构建只能经 overlay triplet 声明字面量，由预设中的 `VCPKG_TARGET_TRIPLET` +
  `VCPKG_OVERLAY_TRIPLETS` 启用（见 `../CMakePresets.json`）。
- 官方虽有 `x64-windows-release` 等 `-release`/`-debug` 单配置三元组，但仅覆盖动态形态、
  且无 static+release 组合——本项目所需六种组合均无官方对应物，必须自定义。
- **宿主三元组必须同时指定**（`VCPKG_HOST_TRIPLET`）：2026 版 vcpkg 把带工具的目标包
  （如 qtbase 的 moc/rcc/uic）作为独立计划条目按宿主三元组安装。若宿主保持社区默认
  三元组，qtbase 会被**完整构建两次**（宿主双配置 + 目标单配置），构建时间与树体积双双膨胀；
  宿主/目标同指 overlay 三元组后按名字去重，只构建一次。Windows 的宿主同样用静态
  三元组（工具静态链接运行，与 mac/linux 一致）——若宿主拆成动态 rel，名字不同、去重
  失效，Windows 会退回双构建（1.5 小时 + 3.5G 树）。
- **交叉编译（Android/iOS）**：宿主 = 构建机原生三元组（`x64-linux-*` /
  `arm64-osx-*`），目标 = 设备三元组（`arm64-android-*` / `arm64-ios-*`）——
  名字必然不同、不去重（qtbase 仍会构建宿主+目标两份，符合预期）；要点是
  **宿主也必须是单配置**（否则回到双构建陷阱）。
- **宿主 qtbase 的默认特性膨胀**：vcpkg-tool 对「自动选中」的包（含全部宿主包）
  无条件补发端口默认特性（源码 `create_install_info`）——qtbase 端口宿主条目声明的
  `default-features: false` 因此形同虚设，宿主 qtbase 以全默认特性构建（icu/openssl/
  libpq/sqlite3/dbus 全家桶，移动腿缓存的大头）。解法见 `../vcpkg.json`：消费者清单加
  `{ "name": "qtbase", "host": true, "default-features": false }` 条目——顶层条目被标记为
  用户请求，豁免默认特性补发，宿主只装基座（core，含 moc/rcc/uic/androiddeployqt 工具）。
  该 `host` 字段消费者清单支持但未见于 vcpkg 文档；桌面腿宿主=目标同名合并，此条目零影响。

## 变更注意

- 本目录任何文件变更 = 依赖树 ABI 变化。CI 的 binary cache key 已含本目录哈希
  （`hashFiles('ImageDiscropperCpp/triplets/*.cmake')`），变更自动换 key、旧缓存条目作废。
- triplet 文件名同时决定 `vcpkg_installed/<triplet>/` 的目录名，重命名同样触发全量重建。
- **基线升级时**：对照新基线 `triplets/` 下的同名社区文件，把除 `VCPKG_BUILD_TYPE`
  之外的所有变量差异同步进来（自包含写法的唯一维护义务）。
- **autotools 端口的交叉编译标记（iOS 自加）**：iOS 三元组追加
  `VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-ios"`（android 官方三元组自带同款，
  非本项目发明）。vcpkg 的 make 助手对 darwin 系目标自动推导 `--host=aarch64-apple-darwin`，
  在 arm64 mac 构建机上与 `--build` 恰好同名 → autoconf 判定「本机构建」→ 运行测试二进制 →
  iOS 二进制无法执行 → configure exit 77。`--host` 与 `--build` 不同名即触发交叉模式
  （改用缓存提示、不再运行测试程序）。libb2 是 iOS 目标唯一的 autotools 端口，
  故此前从未踩中。
