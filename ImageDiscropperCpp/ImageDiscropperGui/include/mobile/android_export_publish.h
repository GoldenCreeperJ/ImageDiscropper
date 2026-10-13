// ============================================================================
// 文件：mobile/android_export_publish.h
// 作用：Android 导出发布编排（原驻 main_window.cpp 匿名命名空间，拆出与 android_media_store
//       同居 mobile/）——Core 已把产物写在临时区后，逐文件经 MediaStore 发布到相册公共目录，
//       失败兜底到应用文档目录。仅 Q_OS_ANDROID 编译；由 ExportController 的 Android 分支调用。
// 分块依据：导出域编排已拆到 app/ExportController，其中平台发布子编排属移动形态，
//           与 android_media_store 同居（CONTRIBUTING.md「分层纪律」——平台差异归 mobile/）。
// ============================================================================
#pragma once

#include <QString>

#include "engine/composition.h"   // engine::ExportFormat（Core 按扩展名选择编码器）

namespace idc::gui {

// 导出格式 → 文件扩展名（Core 按扩展名选择编码器，临时文件必须真实后缀）。
QString exportFormatExt(engine::ExportFormat fmt);

// 发布失败兜底：把产物复制到应用文档目录（Android 11+ 受限、文件管理器读不到但数据不丢），
// 组装含原因与兜底路径的警告消息；返回 false（发布整体按失败处理）。
// 注意用复制而非 rename：cache 与 /sdcard 跨挂载点，rename 会失败（真机实测文件
// 随临时区清空丢失），复制成功后删源。
bool fallbackToDocuments(const QString& src, const QString& fallbackPath,
                         const QString& reason, QString& msg);

// Android 导出发布编排（SPEC §8.3 形态差异）：逐文件经 MediaStore 无 pending 直插 + fd 写入
// 发布到相册公共目录（Pictures/<galleryRel>）；失败产物复制兜底到应用文档目录。msg 恒为
// 面向用户的最终消息；返回是否整体成功。
bool publishExport(const QString& workPath, const QString& galleryRel,
                   const QString& fallbackSub, const QString& singleDisplay,
                   QString& msg);

} // namespace idc::gui
