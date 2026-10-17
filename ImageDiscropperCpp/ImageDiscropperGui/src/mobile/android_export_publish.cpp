// ============================================================================
// 文件：mobile/android_export_publish.cpp
// 作用：实现 Android 导出发布编排（见同名头文件说明）——MediaStore 插入 / fd 写入 /
//       pending finalize / 文档目录兜底；仅 Q_OS_ANDROID 编译。
// ============================================================================
#include "mobile/android_export_publish.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "mobile/android_media_store.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

#ifdef Q_OS_ANDROID

QString exportFormatExt(const engine::ExportFormat fmt) {
    switch (fmt) {
        case engine::ExportFormat::Jpeg: return QStringLiteral("jpg");
        case engine::ExportFormat::Bmp:  return QStringLiteral("bmp");
        case engine::ExportFormat::Webp: return QStringLiteral("webp");
        default:                         return QStringLiteral("png");
    }
}

bool fallbackToDocuments(const QString& src, const QString& fallbackPath,
                         const QString& reason, QString& msg) {
    QDir().mkpath(QFileInfo(fallbackPath).dir().absolutePath());
    if (!QFile::copy(src, fallbackPath)) {
        msg = IDC_GTR(status, kExportFallbackCopyFailed).arg(reason, src);
        return false;
    }
    QFile::remove(src);
    msg = IDC_GTR(status, kExportFallbackSaved).arg(reason, fallbackPath);
    return false;
}

bool publishExport(const QString& workPath, const QString& galleryRel,
                   const QString& fallbackSub, const QString& singleDisplay,
                   QString& msg) {
    const QString docsRoot = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                             + u'/' + QString::fromUtf8(texts::ident::kGalleryAppDir);
    // 共用发布件：MediaStore 插入（标准 pending 流程，个别设备自动降级无 pending）
    // → fd 写入 → pending 则 finalize。pending 行不参与媒体扫描，避免「插入即
    // 0 字节被扫描删行」的真机竞态（相册行随机丢失、文件管理器文件齐全）。
    const auto publishToRelDir = [&](const QString& srcPath, const QString& displayName,
                                     const QString& relDir, QString& err) {
        QString uri;
        bool pending = false;
        if (!insertToGallery(displayName, relDir, uri, pending, err)
            || !writeToContentUri(uri, srcPath, err)
            || (pending && !finalizePending(uri, err))) {
            if (!uri.isEmpty()) { QString e2; deleteContentUri(uri, e2); } // 清理失败行。
            return false;
        }
        return true;
    };
    const QFileInfo wi(workPath);
    if (!wi.isDir()) {
        // 合并单文件。
        const QString baseName = wi.fileName();
        QString err;
        QString name = singleDisplay.isEmpty() ? baseName : singleDisplay;
        if (!publishToRelDir(workPath, name, QString::fromUtf8(texts::ident::kGalleryRoot) + u'/' + galleryRel, err)) {
            // 同名冲突：时间戳后缀重试（保证落地，不覆盖既有文件）。
            name = QFileInfo(name).completeBaseName()
                   + QDateTime::currentDateTime().toString(QStringLiteral("-HHmmss"))
                   + QLatin1Char('.') + QFileInfo(name).suffix();
            if (!publishToRelDir(workPath, name, QString::fromUtf8(texts::ident::kGalleryRoot) + u'/' + galleryRel, err))
                return fallbackToDocuments(workPath, docsRoot + u'/' + baseName, err, msg);
            msg = IDC_GTR(status, kExportGalleryOkSingleDup)
                      .arg(galleryRel, singleDisplay, name);
            return true;
        }
        msg = IDC_GTR(status, kExportGalleryOkSingle).arg(galleryRel, name);
        return true;
    }
    // 分离多文件：逐文件发布，失败文件单独兜底，已发布部分保留。
    const QFileInfoList files = QDir(workPath).entryInfoList(QDir::Files, QDir::Name);
    if (files.isEmpty()) { msg = IDC_GTR(status, kExportNoOutputFiles); return false; }
    int okCount = 0;
    QString firstErr;
    const QString fallbackDir = docsRoot + u'/' + fallbackSub;
    for (const QFileInfo& f : files) {
        QString err;
        if (publishToRelDir(f.absoluteFilePath(), f.fileName(),
                            QString::fromUtf8(texts::ident::kGalleryRoot) + u'/' + galleryRel, err)) {
            ++okCount;
            continue;
        }
        if (firstErr.isEmpty()) firstErr = err.isEmpty() ? IDC_GTR(status, kExportUnknownError) : err;
        // 复制而非 rename：cache 与 /sdcard 跨挂载点，rename 会失败（数据丢失实测）。
        QDir().mkpath(fallbackDir);
        if (QFile::copy(f.absoluteFilePath(), fallbackDir + u'/' + f.fileName()))
            QFile::remove(f.absoluteFilePath());
    }
    if (okCount == 0) {
        msg = IDC_GTR(status, kExportGalleryFailedAll).arg(firstErr, fallbackDir);
        return false;
    }
    if (okCount == files.size()) {
        msg = IDC_GTR(status, kExportGalleryOkMulti).arg(galleryRel).arg(okCount);
        return true;
    }
    msg = IDC_GTR(status, kExportGalleryPartial)
              .arg(okCount).arg(files.size()).arg(fallbackDir);
    return false;
}

#endif

} // namespace idc::gui
