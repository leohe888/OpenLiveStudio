#include "output/outputsettings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

OutputSettings::OutputSettings(QObject *parent) : QObject(parent)
{
    auto videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::home().filePath(QStringLiteral("Videos"));
    m_recordingDirectory = QDir(videos).filePath(QStringLiteral("OpenLiveStudio"));
}

bool OutputSettings::fail(const QString &message)
{
    m_lastError = message;
    return false;
}

void OutputSettings::setRecordingLocked(bool locked)
{
    if (m_recordingLocked != locked) {
        m_recordingLocked = locked;
        emit recordingLockedChanged();
    }
}

void OutputSettings::setStreamingLocked(bool locked)
{
    if (m_streamingLocked != locked) {
        m_streamingLocked = locked;
        emit streamingLockedChanged();
    }
}

QString OutputSettings::validateStream(const QString &server, const QString &key, int bitrateKbps,
                                       bool allowEmpty)
{
    if (bitrateKbps < 500 || bitrateKbps > 50000)
        return tr("视频码率应为 500–50000 kbps。");

    const auto endpoint = server.trimmed();
    if (allowEmpty && endpoint.isEmpty() && key.isEmpty())
        return {};
    if (endpoint.size() > 4096 || key.size() > 4096)
        return tr("推流配置过长。");

    const QUrl url(endpoint, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "rtmp" || url.host().isEmpty() || url.path().isEmpty() ||
        url.path() == "/" || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty() ||
        url.port(1935) <= 0 || url.port(1935) > 65535)
        return tr(
            "首版仅支持 RTMP，请填写 rtmp://服务器/应用，勿在服务器地址中填写账号、查询参数或片段。");
    if (key.isEmpty() || key.startsWith('/') || key.contains('#'))
        return tr("请输入有效的串流密钥。");
    for (const auto c : endpoint + key)
        if (c.isSpace() || c.category() == QChar::Other_Control)
            return tr("推流配置不能包含空白或控制字符。");

    const QUrl combined(endpoint + "/" + key, QUrl::StrictMode);
    if (!combined.isValid())
        return tr("推流地址或串流密钥格式无效。");
    return {};
}

bool OutputSettings::apply(const QString &directory, int recordingBitrate, const QString &server,
                           const QString &key, int streamingBitrate)
{
    const QString path = QDir::cleanPath(directory.trimmed());
    if (directory.trimmed().isEmpty() || !QFileInfo(path).isAbsolute() || recordingBitrate < 500 ||
        recordingBitrate > 50000)
        return fail(tr("请选择绝对保存路径和 500–50000 kbps 的录制码率。"));
    if (m_recordingLocked &&
        (path != m_recordingDirectory || recordingBitrate != m_recordingBitrate))
        return fail(tr("录制停止后才能修改录屏设置。"));

    QFileInfo ancestor(path);
    while (!ancestor.exists()) {
        const QFileInfo parent(ancestor.absolutePath());
        if (parent.absoluteFilePath() == ancestor.absoluteFilePath())
            return fail(tr("保存目录所在位置不可用。"));
        ancestor = parent;
    }
    if (!ancestor.isDir() || !ancestor.isWritable())
        return fail(tr("保存路径不是可写目录。"));

    const QString endpoint = server.trimmed();
    const auto error = validateStream(endpoint, key, streamingBitrate);
    if (!error.isEmpty())
        return fail(error);
    if (m_streamingLocked && (endpoint != m_streamServer || key != m_streamKey ||
                              streamingBitrate != m_streamingBitrate))
        return fail(tr("停止直播后才能修改推流设置。"));

    m_recordingDirectory = path;
    m_recordingBitrate = recordingBitrate;
    m_streamServer = endpoint;
    m_streamKey = key;
    m_streamingBitrate = streamingBitrate;
    m_lastError.clear();
    return true;
}

QString OutputSettings::nextRecordingPath()
{
    if (!QDir().mkpath(m_recordingDirectory)) {
        fail(tr("无法创建录制保存目录，请修改设置。"));
        return {};
    }

    const QString base =
        QStringLiteral("Recording_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    for (int index = 0; index < 10000; ++index) {
        const auto path =
            QDir(m_recordingDirectory)
                .filePath(base + (index ? QStringLiteral("_%1").arg(index) : QString()) + ".mp4");
        if (!QFileInfo::exists(path)) {
            m_lastError.clear();
            return path;
        }
    }
    fail(tr("无法生成唯一录制文件名。"));
    return {};
}
