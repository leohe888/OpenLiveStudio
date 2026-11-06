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

ImageSourceController::ImageSourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    m_imageLoadPool.setMaxThreadCount(2);
    const auto synchronizeSourcesFromManager = [this] {
        synchronizeSources();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, synchronizeSourcesFromManager);
    connect(&manager, &VideoSourceManager::sourceChanged, this, synchronizeSourcesFromManager);
    // 延后首次发布，让调用方先连接快照消费者，避免遗漏初始来源。
    QMetaObject::invokeMethod(this, synchronizeSourcesFromManager, Qt::QueuedConnection);
}

ImageSourceController::~ImageSourceController()
{
    m_imageLoadPool.clear();
    // 任务只投递结果，不等待界面线程，因此析构时等待任务结束不会形成循环等待。
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
                  // 对话框关闭或请求取消后，迟到的加载结果不能再添加来源。
                  if (!owner || m_pendingAddRequests.value(owner) != requestGeneration)
                      return;
                  m_pendingAddRequests.remove(owner);
                  if (!error.isEmpty()) {
                      emit addFailed(owner, error);
                      return;
                  }
                  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                  m_images.insert(id, {absolutePath, image, ++m_generation});
                  auto source = std::make_unique<ImageSource>(id, name.trimmed(), absolutePath);
                  source->updateStatus(image.size(), VideoSourceState::Ready);
                  if (!m_sourceManager.addSource(std::move(source))) {
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
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = dynamic_cast<const ImageSource *>(m_sourceManager.source(id));
        if (!item)
            continue;
        const QString path = item->filePath();
        RenderSourceSnapshot snapshot{
            id, {}, item->visible(), item->transform(), item->locked(), item->filters()};
        retainedSourceIds.insert(id);
        auto it = m_images.find(id);
        if (it == m_images.end() || it->path != path) {
            const quint64 generation = ++m_generation;
            it = m_images.insert(id, {path, {}, generation});
            m_sourceManager.setSourceState(id, VideoSourceState::Loading, {});
            loadImage(path, [this, id, generation](QImage image, QString error) {
                auto entry = m_images.find(id);
                // 来源删除或配置已变更时，不能用旧任务的结果覆盖当前图像。
                if (entry == m_images.end() || entry->generation != generation)
                    return;
                entry->image = image;
                m_sourceManager.updateSourceStatus(
                    id, image.size(),
                    error.isEmpty() ? VideoSourceState::Ready : VideoSourceState::Error, error);
                if (!error.isEmpty())
                    emit sourceFailed(id, error);
                synchronizeSources();
            });
        }
        // 状态通知可能同步删除来源和缓存，不能继续使用通知前取得的迭代器。
        it = m_images.find(id);
        if (it == m_images.end())
            continue;
        snapshot.image = it->image;
        if (m_sourceManager.source(id))
            sources.append(snapshot);
    }
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (!retainedSourceIds.contains(it.key()))
            it = m_images.erase(it);
        else
            ++it;
    }
    emit renderSourcesChanged(sources);
}
