// ============================================================================
// 文件：mobile/android_media_store.h
// 作用：Android 媒体库发布层——把本地导出文件写入系统相册公共目录
//       （Pictures/ImageDiscropper，相册/文件管理器可见）。
// 分块依据：
//   - 本层只做「本地文件 → 媒体行」的插入与字节搬运，不含导出业务语义
//     （临时目录编排、失败兜底、状态栏通知归 MainWindow::onExport）；
//   - 头文件平台中立（仅 QString）：mobile/ 源同样编入桌面 idc_gui_mobile 目标，
//     而桌面 Qt 构建无 JNI 头——QJniObject 依赖全部隔离在 .cpp 的
//     #if defined(Q_OS_ANDROID) 内；本层函数无桌面调用者，非 Android 编译单元
//     不含实现亦无链接引用。
// 说明：首选 IS_PENDING 三段式（真机实测无 pending 直插会被提供者扫描竞态随机删行）；
//       拒绝 is_pending 列的设备自动降级无 pending。RELATIVE_PATH 列需 API 29+
//       （装机门槛由 QT_ANDROID_MIN_SDK_VERSION 保证）。
// ============================================================================
#pragma once

#include <QString>

namespace idc::gui {

// 把本地文件字节流式写入 content URI（ContentResolver.openFileDescriptor + fd 写入；
// fd 属 ParcelFileDescriptor，由 Java 侧关闭——裸 close 触发 fdsan abort）。
bool writeToContentUri(const QString& contentUri, const QString& srcFilePath, QString& err);

// 向系统相册 MediaStore 插入条目。relativePath = RELATIVE_PATH 列值（相对外部存储，
// 如 "Pictures/ImageDiscropper"，不含前导斜杠）。首选标准 IS_PENDING=1 流程
//（outPending=true，写入完成后须调 finalizePending；pending 行不参与媒体扫描，
// 避免「插入即 0 字节被提供者扫描删行、字节后写、文件在行已丢」的真机竞态）；
// 个别拒绝 is_pending 列的设备自动降级为无 pending 直插（outPending=false）。
// 失败（含 API<29）返回 false。
bool insertToGallery(const QString& displayName, const QString& relativePath,
                     QString& outUri, bool& outPending, QString& err);

// 完成 MediaStore pending 条目（IS_PENDING=0），使其在相册中可见。
bool finalizePending(const QString& contentUri, QString& err);

// 删除 content URI 对应条目（发布失败时清理失败行）。
bool deleteContentUri(const QString& contentUri, QString& err);

// 文件名 → MIME（png/jpg/jpeg/bmp/webp → image/*；未知返回 application/octet-stream）。
QString mimeTypeForFileName(const QString& fileName);

} // namespace idc::gui
