// ============================================================================
// 文件：mobile/android_media_store.cpp
// 作用：见同名头文件说明——Android 媒体库/SAF 发布的 JNI 实现（QJniObject）。
// 说明：QJniObject/QJniEnvironment 仅在 Q_OS_ANDROID 下包含（桌面 Qt 构建无 JNI 头，
//       idc_gui_mobile 桌面目标编入本文件时走桩）；变参 JNI 调用一律传原始引用
//       （.object<jstring>() 等，QJniObject 实参无法编译）；jbyteArray 分块写每块
//       即时 DeleteLocalRef（512 本地引用表上限），禁止把数组包进 QJniObject(jobject)
//       （该构造接管所有权，会与 DeleteLocalRef 双重释放）。
// ============================================================================
#include "mobile/android_media_store.h"

#include <QFileInfo>

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace idc::gui {
namespace {

#if defined(Q_OS_ANDROID)
// 应用 Context → ContentResolver（媒体库/SAF 两路写入的公共入口）。
QJniObject contentResolver() {
    const QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    return ctx.callObjectMethod("getContentResolver",
                                "()Landroid/content/ContentResolver;");
}

// 字符串 → android.net.Uri 对象。
QJniObject parseUri(const QString& s) {
    return QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(s).object<jstring>());
}

// ContentValues.put(String, String)。
void putString(const QJniObject& values, const QString& key, const QString& value) {
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                            QJniObject::fromString(key).object<jstring>(),
                            QJniObject::fromString(value).object<jstring>());
}

// ContentValues.put(String, Integer)。
void putInt(const QJniObject& values, const QString& key, const int value) {
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
                            QJniObject::fromString(key).object<jstring>(),
                            QJniObject("java/lang/Integer", "(I)V", jint(value)).object());
}
#endif

} // namespace

QString mimeTypeForFileName(const QString& fileName) {
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == QStringLiteral("png")) return QStringLiteral("image/png");
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) return QStringLiteral("image/jpeg");
    if (ext == QStringLiteral("bmp")) return QStringLiteral("image/bmp");
    if (ext == QStringLiteral("webp")) return QStringLiteral("image/webp");
    return QStringLiteral("application/octet-stream");
}

#if defined(Q_OS_ANDROID)

bool writeToContentUri(const QString& contentUri, const QString& srcFilePath, QString& err) {
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    if (!resolver.isValid() || !uri.isValid()) {
        err = QStringLiteral("无法获取系统内容解析器");
        return false;
    }
    const QJniObject stream = resolver.callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", uri.object());
    if (!stream.isValid() || QJniEnvironment::checkAndClearExceptions()) {
        err = QStringLiteral("无法打开目标位置写入");
        return false;
    }
    QFile src(srcFilePath);
    if (!src.open(QIODevice::ReadOnly)) {
        stream.callMethod<void>("close", "()V");
        err = QStringLiteral("无法读取临时导出文件：%1").arg(srcFilePath);
        return false;
    }
    QJniEnvironment env;
    constexpr int kChunk = 64 * 1024;
    bool ok = true;
    while (!src.atEnd()) {
        const QByteArray chunk = src.read(kChunk);
        if (chunk.isEmpty()) break;
        jbyteArray arr = env->NewByteArray(static_cast<jsize>(chunk.size()));
        if (!arr) { ok = false; err = QStringLiteral("内存不足，写入中断"); break; }
        env->SetByteArrayRegion(arr, 0, static_cast<jsize>(chunk.size()),
                                reinterpret_cast<const jbyte*>(chunk.constData()));
        stream.callMethod<void>("write", "([B)V", arr);
        env->DeleteLocalRef(arr);
        if (QJniEnvironment::checkAndClearExceptions()) {
            ok = false; err = QStringLiteral("写入目标位置失败"); break;
        }
    }
    stream.callMethod<void>("flush", "()V");
    stream.callMethod<void>("close", "()V");
    QJniEnvironment::checkAndClearExceptions(); // 吸收 flush/close 残留异常。
    if (!ok && err.isEmpty()) err = QStringLiteral("写入目标位置失败");
    return ok;
}

bool createDocumentInTree(const QString& treeUri, const QString& displayName,
                          const QString& mimeType, QString& outUri, QString& err) {
    const QJniObject resolver = contentResolver();
    const QJniObject tree = parseUri(treeUri);
    if (!resolver.isValid() || !tree.isValid()) {
        err = QStringLiteral("无法获取系统内容解析器");
        return false;
    }
    const QJniObject doc = QJniObject::callStaticObjectMethod(
        "android/provider/DocumentsContract", "createDocument",
        "(Landroid/content/ContentResolver;Landroid/net/Uri;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;",
        resolver.object(), tree.object(),
        QJniObject::fromString(mimeType).object<jstring>(),
        QJniObject::fromString(displayName).object<jstring>());
    if (!doc.isValid() || QJniEnvironment::checkAndClearExceptions()) {
        err = QStringLiteral("在所选目录创建文件失败（该目录可能不允许写入）");
        return false;
    }
    outUri = doc.toString();
    return true;
}

bool insertToGallery(const QString& displayName, const QString& relativePath,
                     QString& outUri, QString& err) {
    if (QNativeInterface::QAndroidApplication::sdkInt() < 29) {
        err = QStringLiteral("相册发布需要 Android 10（API 29）及以上");
        return false;
    }
    const QJniObject resolver = contentResolver();
    const QJniObject collection = QJniObject::getStaticObjectField(
        "android/provider/MediaStore$Images$Media", "EXTERNAL_CONTENT_URI",
        "Landroid/net/Uri;");
    if (!resolver.isValid() || !collection.isValid()) {
        err = QStringLiteral("无法获取系统媒体库");
        return false;
    }
    // 键名即 MediaStore.MediaColumns 常量值（display_name/mime_type/relative_path/is_pending）。
    QJniObject values("android/content/ContentValues");
    putString(values, QStringLiteral("display_name"), displayName);
    putString(values, QStringLiteral("mime_type"), mimeTypeForFileName(displayName));
    putString(values, QStringLiteral("relative_path"), QStringLiteral("Pictures/") + relativePath);
    putInt(values, QStringLiteral("is_pending"), 1);
    const QJniObject inserted = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    // insert 失败返回 null（非异常），故须 isValid 判定。
    if (!inserted.isValid() || QJniEnvironment::checkAndClearExceptions()) {
        err = QStringLiteral("媒体库插入失败（格式可能不被支持）");
        return false;
    }
    outUri = inserted.toString();
    return true;
}

bool finalizePending(const QString& contentUri, QString& err) {
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    QJniObject values("android/content/ContentValues");
    putInt(values, QStringLiteral("is_pending"), 0);
    const jint rows = resolver.callMethod<jint>(
        "update",
        "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), values.object(), nullptr, nullptr);
    if (QJniEnvironment::checkAndClearExceptions() || rows == 0) { // rows==0：行已消失。
        err = QStringLiteral("媒体库条目完成失败");
        return false;
    }
    return true;
}

bool deleteContentUri(const QString& contentUri, QString& err) {
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    const jint rows = resolver.callMethod<jint>(
        "delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), nullptr, nullptr);
    if (QJniEnvironment::checkAndClearExceptions() || rows == 0) {
        err = QStringLiteral("清理未完成条目失败");
        return false;
    }
    return true;
}

#else // !Q_OS_ANDROID

// 非 Android 平台桩：mobile/ 源同样编入桌面 idc_gui_mobile 目标，
// 但桌面 Qt 无 JNI 头，本功能仅 Android 可用。
bool writeToContentUri(const QString&, const QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}
bool createDocumentInTree(const QString&, const QString&, const QString&,
                          QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}
bool insertToGallery(const QString&, const QString&, QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}
bool finalizePending(const QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}
bool deleteContentUri(const QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}

#endif

} // namespace idc::gui
