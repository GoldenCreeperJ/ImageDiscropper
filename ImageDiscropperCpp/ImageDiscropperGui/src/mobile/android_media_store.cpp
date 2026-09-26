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
#include <QDateTime>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QStandardPaths>
#include <QUrl>
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

// 挂起异常详情经 ExceptionDescribe 进 logcat（System.err）后清除——真机排障可见
// 异常类与消息（否则静默吞掉只剩泛化错误文案）；无挂起异常时为零开销空操作。
void logAndClearException(QJniEnvironment& env) {
    if (env->ExceptionCheck()) env->ExceptionDescribe();
    env->ExceptionClear();
}

// 排障日志：写应用文档目录 export_debug.log（shell 可读，adb 直取）。logcat 在
// 部分设备上收不到 Qt/Java 输出（真机实测静默），文件日志不依赖任何日志通道；
// 追加模式：每次导出会话由 markExportDiagSession 分隔，历史记录不被覆盖。
void diagLog(const QString& msg) {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         + QStringLiteral("/export_debug.log");
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")).toUtf8());
        f.write(" ");
        f.write(msg.toUtf8());
        f.write("\n");
    }
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
    diagLog(QStringLiteral("write 进入 uri=%1").arg(contentUri.left(70)));
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    diagLog(QStringLiteral("  resolver有效=%1 uri有效=%2").arg(resolver.isValid() ? 1 : 0).arg(uri.isValid() ? 1 : 0));
    if (!resolver.isValid() || !uri.isValid()) {
        err = QStringLiteral("无法获取系统内容解析器");
        return false;
    }
    // fd 直写：openFileDescriptor + getFd → QFile 经裸 fd 写入（QFSFileEngine），
    // 绕过 Android 上 QFile 写 content:// 不支持的坏引擎，也绕开部分 OEM 对
    // openOutputStream 的兼容问题（真机实测 openOutputStream 在相册 pending 条目
    // 与 SAF 文档上均静默返回 null）。
    const QJniObject pfd = resolver.callObjectMethod(
        "openFileDescriptor",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
        uri.object(), QJniObject::fromString(QStringLiteral("w")).object<jstring>());
    const bool hadEx = env->ExceptionCheck();
    if (hadEx) env->ExceptionDescribe();
    diagLog(QStringLiteral("  openFileDescriptor 有效=%1 异常=%2")
                .arg(pfd.isValid() ? 1 : 0).arg(hadEx ? 1 : 0));
    env->ExceptionClear();
    if (!pfd.isValid()) {
        err = QStringLiteral("无法打开目标位置写入");
        return false;
    }
    const jint fd = pfd.callMethod<jint>("getFd");
    diagLog(QStringLiteral("  getFd=%1").arg(fd));
    QFile out;
    // 注意 DontCloseHandle：fd 属 ParcelFileDescriptor 所有（fdsan 标记），裸 close()
    // 会触发 fdsan abort（真机 SIGABRT：'expected to be unowned, actually owned by
    // ParcelFileDescriptor'）——写完由 Java 侧 pfd.close() 正确释放。
    const bool opened = out.open(fd, QIODevice::WriteOnly, QFileDevice::DontCloseHandle);
    diagLog(QStringLiteral("  QFile::open(fd)=%1 error=%2")
                .arg(opened ? 1 : 0).arg(out.errorString()));
    if (!opened) {
        err = QStringLiteral("无法打开目标位置写入");
        return false;
    }
    QFile src(srcFilePath);
    const bool srcOk = src.open(QIODevice::ReadOnly);
    diagLog(QStringLiteral("  src open=%1 size=%2").arg(srcOk ? 1 : 0).arg(src.size()));
    if (!srcOk) {
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
    diagLog(QStringLiteral("  write 结果=%1 err=%2").arg(ok ? 1 : 0).arg(err));
    return ok;
}

bool createDocumentInTree(const QString& treeUri, const QString& displayName,
                          const QString& mimeType, QString& outUri, QString& err) {
    diagLog(QStringLiteral("createDocument 进入 tree=%1 name=%2").arg(treeUri.left(50), displayName));
    QJniEnvironment env;
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
    const bool hadEx = env->ExceptionCheck();
    if (hadEx) env->ExceptionDescribe();
    diagLog(QStringLiteral("  createDocument 有效=%1 异常=%2")
                .arg(doc.isValid() ? 1 : 0).arg(hadEx ? 1 : 0));
    env->ExceptionClear();
    if (!doc.isValid()) {
        err = QStringLiteral("在所选目录创建文件失败（该目录可能不允许写入）");
        return false;
    }
    outUri = doc.toString();
    diagLog(QStringLiteral("  doc uri=%1").arg(outUri.left(70)));
    return true;
}

bool insertToGallery(const QString& displayName, const QString& relativePath,
                     QString& outUri, QString& err) {
    diagLog(QStringLiteral("insert 进入 display=%1 rel=%2").arg(displayName, relativePath));
    QJniEnvironment env;
    // RELATIVE_PATH 列需 API 29+；运行时防御（装机门槛另由
    // QT_ANDROID_MIN_SDK_VERSION 29 保证）。
    const jint sdk = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    diagLog(QStringLiteral("  sdkInt=%1").arg(sdk));
    if (sdk < 29) {
        err = QStringLiteral("相册发布需要 Android 10（API 29）及以上");
        return false;
    }
    const QJniObject resolver = contentResolver();
    const QJniObject collection = QJniObject::getStaticObjectField(
        "android/provider/MediaStore$Images$Media", "EXTERNAL_CONTENT_URI",
        "Landroid/net/Uri;");
    if (!resolver.isValid() || !collection.isValid()) {
        diagLog(QStringLiteral("  resolver/collection 无效"));
        err = QStringLiteral("无法获取系统媒体库");
        return false;
    }
    // 键名即 MediaStore.MediaColumns 常量值（注意 DISPLAY_NAME 为 _display_name，
    // 带下划线前缀——真机曾误写 display_name 被华为 MediaProvider 拒绝）。
    // 无 IS_PENDING 直插（见头文件说明：pending 三段式真机多设备不兼容）。
    QJniObject values("android/content/ContentValues");
    putString(values, QStringLiteral("_display_name"), displayName);
    putString(values, QStringLiteral("mime_type"), mimeTypeForFileName(displayName));
    putString(values, QStringLiteral("relative_path"), relativePath);
    const QJniObject inserted = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    // insert 失败返回 null（非异常），故须 isValid 判定。
    const bool hadEx = env->ExceptionCheck();
    if (hadEx) env->ExceptionDescribe();
    diagLog(QStringLiteral("  insert 有效=%1 异常=%2")
                .arg(inserted.isValid() ? 1 : 0).arg(hadEx ? 1 : 0));
    env->ExceptionClear();
    if (!inserted.isValid()) {
        err = QStringLiteral("媒体库插入失败（格式可能不被支持）");
        return false;
    }
    outUri = inserted.toString();
    return true;
}

QString pathFromSafUri(const QString& uriStr, QString& err) {
    // 纯字符串解析（不调提供者——华为 Downloads 提供者连 getDocumentId 也拒绝）：
    // SAF URI 的最后一段即文档/树 ID（URL 编码）——华为 raw: 形式直接是文件路径，
    // AOSP primary: 形式映射到主外部存储。
    const QStringList parts = uriStr.split(QLatin1Char('/'));
    if (parts.size() < 4) {
        err = QStringLiteral("无法解析所选位置");
        return {};
    }
    const QString id = QUrl::fromPercentEncoding(parts.last().toUtf8());
    diagLog(QStringLiteral("  saf id=%1").arg(id.left(70)));
    if (id.startsWith(QStringLiteral("raw:"))) return id.mid(4); // 华为：文档 ID 即文件路径。
    if (id.startsWith(QStringLiteral("primary:"))) // AOSP：主外部存储。
        return QStringLiteral("/storage/emulated/0/") + id.mid(8);
    err = QStringLiteral("所选位置形式不支持");
    return {};
}

QString queryMediaUriByName(const QString& displayName, const QString& relPath, QString& err) {
    // 按显示名+相对路径查现有媒体行（保存框占位文件即此情形——华为不允许删占位、
    // 也不允许同名再插入），返回其媒体 URI 供直接写入（OWNER_PACKAGE_NAME 为本应用，
    // 走与相册同一已验证通道）；无匹配返回空。
    diagLog(QStringLiteral("queryByName 进入 name=%1 rel=%2").arg(displayName, relPath));
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject collection = QJniObject::getStaticObjectField(
        "android/provider/MediaStore$Images$Media", "EXTERNAL_CONTENT_URI",
        "Landroid/net/Uri;");
    if (!resolver.isValid() || !collection.isValid()) {
        err = QStringLiteral("无法获取系统媒体库");
        return {};
    }
    const jclass strCls = env->FindClass("java/lang/String");
    jobjectArray proj = env->NewObjectArray(1, strCls, nullptr);
    jstring idCol = env->NewStringUTF("_id");
    env->SetObjectArrayElement(proj, 0, idCol);
    env->DeleteLocalRef(idCol);
    const QString relWithSlash = relPath.endsWith(QLatin1Char('/')) ? relPath : relPath + QLatin1Char('/');
    jobjectArray args = env->NewObjectArray(2, strCls, nullptr);
    jstring a0 = env->NewStringUTF(displayName.toUtf8().constData());
    jstring a1 = env->NewStringUTF(relWithSlash.toUtf8().constData());
    env->SetObjectArrayElement(args, 0, a0);
    env->SetObjectArrayElement(args, 1, a1);
    env->DeleteLocalRef(a0);
    env->DeleteLocalRef(a1);
    const QJniObject sel = QJniObject::fromString(
        QStringLiteral("_display_name=? AND relative_path=?"));
    const QJniObject cursor = resolver.callObjectMethod(
        "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        collection.object(), proj, sel.object<jstring>(), args, nullptr);
    env->DeleteLocalRef(proj);
    env->DeleteLocalRef(args);
    logAndClearException(env);
    if (!cursor.isValid()) {
        err = QStringLiteral("查询媒体库失败");
        return {};
    }
    QString result;
    if (cursor.callMethod<jboolean>("moveToFirst", "()Z")) {
        const jlong rowId = cursor.callMethod<jlong>("getLong", "(I)J", jint(0));
        result = QStringLiteral("content://media/external/images/media/") + QString::number(rowId);
    }
    cursor.callMethod<void>("close", "()V");
    logAndClearException(env);
    diagLog(QStringLiteral("  queryByName 命中=%1 uri=%2")
                .arg(result.isEmpty() ? 0 : 1).arg(result.left(60)));
    if (result.isEmpty()) {
        err = QStringLiteral("未找到目标文件记录");
        return {};
    }
    return result;
}

bool deleteContentUri(const QString& contentUri, QString& err) {
    diagLog(QStringLiteral("delete 进入 uri=%1").arg(contentUri.left(60)));
    QJniEnvironment env;
    const QJniObject resolver = contentResolver();
    const QJniObject uri = parseUri(contentUri);
    const jint rows = resolver.callMethod<jint>(
        "delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
        uri.object(), nullptr, nullptr);
    const bool hadEx = env->ExceptionCheck();
    if (hadEx) env->ExceptionDescribe();
    diagLog(QStringLiteral("  delete rows=%1 异常=%2").arg(rows).arg(hadEx ? 1 : 0));
    env->ExceptionClear();
    if (rows == 0) {
        err = QStringLiteral("清理未完成条目失败");
        return false;
    }
    return true;
}

void markExportDiagSession() {
    diagLog(QStringLiteral("========== 新导出会话 =========="));
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
bool deleteContentUri(const QString&, QString& err) {
    err = QStringLiteral("此功能仅支持 Android"); return false;
}
void markExportDiagSession() {}

#endif

} // namespace idc::gui
