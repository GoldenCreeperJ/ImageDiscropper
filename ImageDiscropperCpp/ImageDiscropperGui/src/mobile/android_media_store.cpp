// ============================================================================
// 文件：mobile/android_media_store.cpp
// 作用：见同名头文件说明——Android 媒体库发布的 JNI 实现（QJniObject）。
// 说明：QJniObject/QJniEnvironment 仅在 Q_OS_ANDROID 下包含（桌面 Qt 构建无 JNI 头；
//       本层函数无桌面调用者，非 Android 编译单元不含实现亦无链接引用）；
//       变参 JNI 调用一律传原始引用（.object<jstring>() 等，QJniObject 实参无法编译）。
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
// 应用 Context → ContentResolver（媒体库写入的公共入口）。
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
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    if (!resolver.isValid() || !uri.isValid()) {
        err = QStringLiteral("无法获取系统内容解析器");
        return false;
    }
    // fd 直写：openFileDescriptor + getFd → QFile 经裸 fd 写入（QFSFileEngine），
    // 绕过 Android 上 QFile 写 content:// 不支持的坏引擎（真机实测 openOutputStream
    // 在部分提供者上静默返回 null）。
    const QJniObject pfd = resolver.callObjectMethod(
        "openFileDescriptor",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
        uri.object(), QJniObject::fromString(QStringLiteral("w")).object<jstring>());
    if (env->ExceptionCheck()) env->ExceptionDescribe();
    env->ExceptionClear();
    if (!pfd.isValid()) {
        err = QStringLiteral("无法打开目标位置写入");
        return false;
    }
    const jint fd = pfd.callMethod<jint>("getFd");
    QFile out;
    // 注意 DontCloseHandle：fd 属 ParcelFileDescriptor 所有（fdsan 标记），裸 close()
    // 会触发 fdsan abort（真机 SIGABRT：'expected to be unowned, actually owned by
    // ParcelFileDescriptor'）——写完由 Java 侧 pfd.close() 正确释放。
    const bool opened = out.open(fd, QIODevice::WriteOnly, QFileDevice::DontCloseHandle);
    if (!opened) {
        err = QStringLiteral("无法打开目标位置写入");
        return false;
    }
    QFile src(srcFilePath);
    if (!src.open(QIODevice::ReadOnly)) {
        err = QStringLiteral("无法读取临时导出文件：%1").arg(srcFilePath);
        return false;
    }
    constexpr int kChunk = 64 * 1024;
    char buf[kChunk];
    bool ok = true;
    while (!src.atEnd()) {
        const qint64 n = src.read(buf, kChunk);
        if (n <= 0) { ok = false; err = QStringLiteral("读取临时导出文件失败"); break; }
        if (out.write(buf, n) != n) { ok = false; err = QStringLiteral("写入目标位置失败"); break; }
    }
    src.close();
    out.close();                         // 只关 QFile 缓冲，fd 保持打开（DontCloseHandle）。
    pfd.callMethod<void>("close", "()V"); // Java 侧关闭：fdsan 所有权正确释放（裸 close 会 abort）。
    return ok;
}

bool insertToGallery(const QString& displayName, const QString& relativePath,
                     QString& outUri, bool& outPending, QString& err) {
    QJniEnvironment env;
    // RELATIVE_PATH 列需 API 29+；运行时防御（装机门槛另由
    // QT_ANDROID_MIN_SDK_VERSION 29 保证）。
    const jint sdk = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    if (sdk < 29) {
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
    // 键名即 MediaStore.MediaColumns 常量值（注意 DISPLAY_NAME 为 _display_name，
    // 带下划线前缀——真机曾误写 display_name 被华为 MediaProvider 拒绝）。
    // 首选 IS_PENDING=1（见头文件说明：无 pending 直插会被扫描竞态随机删行）；
    // 拒绝该列的设备降级无 pending。
    const auto tryInsert = [&](const bool pending) {
        QJniObject values("android/content/ContentValues");
        putString(values, QStringLiteral("_display_name"), displayName);
        putString(values, QStringLiteral("mime_type"), mimeTypeForFileName(displayName));
        putString(values, QStringLiteral("relative_path"), relativePath);
        if (pending) putInt(values, QStringLiteral("is_pending"), 1);
        const QJniObject inserted = resolver.callObjectMethod(
            "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
            collection.object(), values.object());
        // insert 失败返回 null（非异常），故须 isValid 判定。
        if (env->ExceptionCheck()) env->ExceptionDescribe();
        env->ExceptionClear();
        if (inserted.isValid()) {
            outUri = inserted.toString();
            return true;
        }
        return false;
    };
    if (tryInsert(true)) { outPending = true; return true; }
    if (tryInsert(false)) { outPending = false; return true; }
    err = QStringLiteral("媒体库插入失败（格式可能不被支持）");
    return false;
}

bool finalizePending(const QString& contentUri, QString& err) {
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    QJniObject values("android/content/ContentValues");
    putInt(values, QStringLiteral("is_pending"), 0);
    const jint rows = resolver.callMethod<jint>(
        "update",
        "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), values.object(), nullptr, nullptr);
    if (env->ExceptionCheck()) env->ExceptionDescribe();
    env->ExceptionClear();
    if (rows == 0) { // rows==0：行已消失或被提供者拒绝。
        err = QStringLiteral("媒体库条目完成失败");
        return false;
    }
    return true;
}

bool deleteContentUri(const QString& contentUri, QString& err) {
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    const jint rows = resolver.callMethod<jint>(
        "delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), nullptr, nullptr);
    if (env->ExceptionCheck()) env->ExceptionDescribe();
    env->ExceptionClear();
    if (rows == 0) {
        err = QStringLiteral("清理未完成条目失败");
        return false;
    }
    return true;
}

#endif // Q_OS_ANDROID

} // namespace idc::gui
