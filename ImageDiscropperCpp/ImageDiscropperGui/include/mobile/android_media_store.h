// ============================================================================
// 文件：mobile/android_media_store.h
// 作用：Android 媒体库/SAF 发布层——把本地导出文件流式写入最终目的地：
//       MediaStore 公共相册目录（Pictures/ImageDiscropper，相册/文件管理器可见）
//       或 SAF 所选树目录/单个文档 URI（content://，QFile 不可写，须经 ContentResolver 流写入）。
// 分块依据：
//   - 本层只做「本地文件 → content URI」的字节搬运，不含导出业务语义
//     （临时目录编排、失败兜底、状态栏通知归 MainWindow::onExport）；
//   - 头文件平台中立（仅 QString）：mobile/ 源同样编入桌面 idc_gui_mobile 目标，
//     而桌面 Qt 构建无 JNI 头——QJniObject 依赖全部隔离在 .cpp 的
//     #if defined(Q_OS_ANDROID) 内，非 Android 平台返回失败桩。
// 说明：MediaStore 写入走 IS_PENDING=1 → 写字节 → IS_PENDING=0 三段式
//       （Android 文档规定，漏掉 finalize 条目永久不可见）；写入失败时调用方应
//       deleteContentUri 清理僵尸 pending 行。RELATIVE_PATH 列需 API 29+。
// ============================================================================
#pragma once

#include <QString>

namespace idc::gui {

// 把本地文件字节流式写入 content URI（ContentResolver.openOutputStream + 分块写）。
// 适用于 MediaStore pending 条目与 SAF 新建文档 URI 两类目的地。
bool writeToContentUri(const QString& contentUri, const QString& srcFilePath, QString& err);

// 在 SAF 树目录下新建文档（DocumentsContract.createDocument），返回文档 URI。
// 注意：树根直放，不承诺子目录（MIME_TYPE_DIR 的创建能力提供方可选）。
bool createDocumentInTree(const QString& treeUri, const QString& displayName,
                          const QString& mimeType, QString& outUri, QString& err);

// 向系统相册 MediaStore 插入条目。relativePath = RELATIVE_PATH 列值（相对外部存储，
// 如 "Pictures/ImageDiscropper" 或 "Download/xx"，不含前导斜杠）。首选标准
// IS_PENDING=1 流程（outPending=true，写完后须调 finalizePending）；部分设备
//（华为实测）拒绝 is_pending 列导致插入静默失败——自动降级为无 pending 直接插入
//（outPending=false）。失败（含 API<29）返回 false。
bool insertToGallery(const QString& displayName, const QString& relativePath,
                     QString& outUri, bool& outPending, QString& err);

// 从 SAF 文档/树 URI 提取目标文件系统路径：华为 raw: 形式直接取路径，
// AOSP primary: 形式映射到 /storage/emulated/0/…；其他形式返回空并置 err。
//（纯字符串解析，不调用提供者——华为 Downloads 提供者连 getDocumentId 也拒绝。）
QString pathFromSafUri(const QString& uri, QString& err);

// 完成 MediaStore pending 条目（IS_PENDING=0），使其在相册中可见。
bool finalizePending(const QString& contentUri, QString& err);

// 删除 content URI 对应条目（发布失败时清理僵尸 pending 行 / 0 字节占位文档）。
bool deleteContentUri(const QString& contentUri, QString& err);

// 文件名 → MIME（png/jpg/jpeg/bmp/webp → image/*；未知返回 application/octet-stream）。
QString mimeTypeForFileName(const QString& fileName);

// 新一轮导出的排障日志加会话分隔（追加模式，历史不被覆盖；诊断完成后整体移除）。
void markExportDiagSession();

} // namespace idc::gui
