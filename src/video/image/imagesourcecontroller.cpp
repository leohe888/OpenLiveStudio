#include "video/image/imagesourcecontroller.h"
#include "video/videosourcemanager.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QRunnable>
#include <QSet>
#include <QUuid>
#include <QWidget>

namespace {
void setRuntimeStatus(VideoSourceManager &manager, const QString &sourceId,
                      VideoSourceState state, const QString &error)
{
    auto runtime = manager.sourceRuntime(sourceId);
    runtime.setState(state);
    runtime.setError(error);
    manager.setSourceRuntime(sourceId, runtime);
}
}

ImageSourceController::ImageSourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    m_imageLoadPool.setMaxThreadCount(2);
    const auto synchronizeSourcesFromManager = [this] {
        synchronizeSources();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, synchronizeSourcesFromManager);
    connect(&manager, &VideoSourceManager::sourceChanged, this, synchronizeSourcesFromManager);
    // Allow the caller to connect the preview before publishing initial entries.
    QMetaObject::invokeMethod(this, synchronizeSourcesFromManager, Qt::QueuedConnection);
}

ImageSourceController::~ImageSourceController()
{
    m_imageLoadPool.clear();
    // Workers only enqueue results; they never wait for the GUI thread.
    m_imageLoadPool.waitForDone();
}

void ImageSourceController::loadImage(const QString &path,
                                      std::function<void(QImage, QString)> completion)
{
    m_imageLoadPool.start(QRunnable::create([this, path, completion = std::move(completion)] {
        QImage image;
        QString error;
        const QFileInfo file(path);
        if (!file.isFile() || !file.isReadable()) {
            error = tr("图片文件不存在或无法读取。");
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.isNull())
                error = tr("无法加载图片：%1").arg(reader.errorString());
        }
        QMetaObject::invokeMethod(
            this,
            [completion, image, error] {
                completion(image, error);
            },
            Qt::QueuedConnection);
    }));
}

void ImageSourceController::addImage(const QString &name, const QString &path, QObject *requester)
{
    if (!requester || m_pendingAddRequests.contains(requester))
        return;
    if (name.trimmed().isEmpty() || path.trimmed().isEmpty() || path.contains("://") ||
        path.startsWith(":/")) {
        emit addFailed(requester, tr("请输入来源名称和本地图片路径。"));
        return;
    }
    const QString absolutePath = QFileInfo(path.trimmed()).absoluteFilePath();
    const quint64 requestGeneration = ++m_generation;
    m_pendingAddRequests.insert(requester, requestGeneration);
    connect(requester, &QObject::destroyed, this, [this, requester] {
        cancelAdd(requester);
    });
    QPointer<QObject> owner(requester);
    loadImage(absolutePath,
              [this, owner, requestGeneration, name, absolutePath](QImage image, QString error) {
                  if (!owner || m_pendingAddRequests.value(owner) != requestGeneration)
                      return;
                  m_pendingAddRequests.remove(owner);
                  if (!error.isEmpty()) {
                      emit addFailed(owner, error);
                      return;
                  }
                  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                  m_images.insert(id, {absolutePath, image, ++m_generation});
                  VideoSource source{id, name.trimmed(), ImageSourceConfig{absolutePath}};
                  source.setRuntime({image.size(), VideoSourceState::Ready, {}});
                  if (!m_sourceManager.addSource(source)) {
                      m_images.remove(id);
                      emit addFailed(owner, tr("无法添加图片来源。"));
                      return;
                  }
                  emit imageAdded(owner);
              });
}

void ImageSourceController::cancelAdd(QObject *requester)
{
    m_pendingAddRequests.remove(requester);
}

QString ImageSourceController::browseImage(const QString &currentPath)
{
    QStringList patterns;
    for (const auto &format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    return QFileDialog::getOpenFileName(qobject_cast<QWidget *>(parent()), tr("选择图片"),
                                        currentPath,
                                        tr("图片 (%1);;所有文件 (*)").arg(patterns.join(' ')));
}

void ImageSourceController::synchronizeSources()
{
    QSet<QString> retainedSourceIds;
    QList<RenderSourceSnapshot> sources;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        if (item.type() != VideoSourceType::Image)
            continue;
        const QString id = item.id();
        const QString path = std::get<ImageSourceConfig>(item.config()).filePath();
        retainedSourceIds.insert(id);
        auto it = m_images.find(id);
        if (it == m_images.end() || it->path != path) {
            const quint64 generation = ++m_generation;
            it = m_images.insert(id, {path, {}, generation});
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Loading, {});
            loadImage(path, [this, id, generation](QImage image, QString error) {
                auto entry = m_images.find(id);
                if (entry == m_images.end() || entry->generation != generation)
                    return;
                entry->image = image;
                auto runtime = m_sourceManager.sourceRuntime(id);
                runtime.setPixelSize(image.size());
                runtime.setState(error.isEmpty() ? VideoSourceState::Ready : VideoSourceState::Error);
                runtime.setError(error);
                m_sourceManager.setSourceRuntime(id, runtime);
                if (!error.isEmpty())
                    emit sourceFailed(id, error);
                synchronizeSources();
            });
        }
        sources.append({id, it->image, item.visible(), item.transform(), item.locked(), item.filters()});
    }
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (!retainedSourceIds.contains(it.key()))
            it = m_images.erase(it);
        else
            ++it;
    }
    emit renderSourcesChanged(sources);
}
