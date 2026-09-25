set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 15)
set(VCPKG_BUILD_TYPE release)
# autotools 端口（libb2）需与 --build 不同名的 --host 才触发交叉编译模式：
# vcpkg 自动推导的是 aarch64-apple-darwin，在 arm64 mac 构建机上恰好等于 --build，
# autoconf 判定「本机构建」→ 运行测试二进制 → iOS 二进制无法执行 → exit 77。
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-ios")
