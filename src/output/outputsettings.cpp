#include "outputsettings.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

OutputSettings::OutputSettings(QObject *parent) : QObject(parent)
{
    auto videos=QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) videos=QDir::home().filePath(QStringLiteral("Videos"));
    m_directory=QDir(videos).filePath(QStringLiteral("OpenLiveStudio"));
}
bool OutputSettings::fail(const QString &message) { m_error=message; emit lastErrorChanged(); return false; }
void OutputSettings::setRecordingLocked(bool locked)
{ if (m_locked!=locked) { m_locked=locked; emit recordingLockedChanged(); } }
bool OutputSettings::apply(const QString &directory,int quality,const QString &server,const QString &key)
{
    const QString path=QDir::cleanPath(directory.trimmed());
    if (directory.trimmed().isEmpty() || !QFileInfo(path).isAbsolute() || quality<0 || quality>2)
        return fail(tr("请选择绝对保存路径和有效的录制质量。"));
    if (m_locked && (path!=m_directory || quality!=m_quality)) return fail(tr("录制停止后才能修改录屏设置。"));
    QFileInfo ancestor(path);
    while (!ancestor.exists()) {
        const QFileInfo parent(ancestor.absolutePath());
        if (parent.absoluteFilePath()==ancestor.absoluteFilePath()) return fail(tr("保存目录所在位置不可用。"));
        ancestor=parent;
    }
    if (!ancestor.isDir() || !ancestor.isWritable()) return fail(tr("保存路径不是可写目录。"));
    const QString endpoint=server.trimmed();
    if (!endpoint.isEmpty()) {
        const QUrl url(endpoint,QUrl::StrictMode);
        if (!url.isValid() || (url.scheme()!="rtmp" && url.scheme()!="rtmps") || url.host().isEmpty())
            return fail(tr("请输入有效的 RTMP 或 RTMPS 服务器地址。"));
    }
    if (endpoint.size()>4096 || key.size()>4096) return fail(tr("推流配置过长。"));
    m_directory=path; m_quality=quality; m_server=endpoint; m_key=key;
    m_error.clear(); emit lastErrorChanged(); emit settingsChanged(); return true;
}
QString OutputSettings::nextRecordingPath()
{
    if (!QDir().mkpath(m_directory)) { fail(tr("无法创建录制保存目录，请修改设置。")); return {}; }
    const QString base=QStringLiteral("Recording_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    for (int index=0; index<10000; ++index) {
        const auto path=QDir(m_directory).filePath(base+(index ? QStringLiteral("_%1").arg(index) : QString())+".mp4");
        if (!QFileInfo::exists(path)) { m_error.clear(); emit lastErrorChanged(); return path; }
    }
    fail(tr("无法生成唯一录制文件名。")); return {};
}
