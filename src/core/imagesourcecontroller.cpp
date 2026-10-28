#include "imagesourcecontroller.h"
#include "sourcelistmodel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QRunnable>
#include <QSet>
#include <QUuid>
#include <QWidget>

ImageSourceController::ImageSourceController(SourceListModel &model, QObject *parent)
    : QObject(parent), m_model(model)
{
    m_workers.setMaxThreadCount(2);
    const auto sync = [this] { synchronize(); };
    connect(&model, &QAbstractItemModel::rowsInserted, this, sync);
    connect(&model, &QAbstractItemModel::rowsRemoved, this, sync);
    connect(&model, &QAbstractItemModel::rowsMoved, this, sync);
    connect(&model, &QAbstractItemModel::modelReset, this, sync);
    connect(&model, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
            if (roles.isEmpty() || roles.contains(SourceListModel::FilePathRole)
                || roles.contains(SourceListModel::TypeRole) || roles.contains(SourceListModel::VisibleRole)
                || roles.contains(SourceListModel::TransformRole) || roles.contains(SourceListModel::LockedRole)
                || roles.contains(SourceListModel::FiltersRole))
                synchronize();
        });
    // Allow the caller to connect the preview before publishing initial entries.
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}

ImageSourceController::~ImageSourceController()
{
    m_workers.clear();
    // Workers only enqueue results; they never wait for the GUI thread.
    m_workers.waitForDone();
}

void ImageSourceController::loadImage(const QString &path, std::function<void(QImage, QString)> completion)
{
    m_workers.start(QRunnable::create([this, path, completion = std::move(completion)] {
        QImage image;
        QString error;
        const QFileInfo file(path);
        if (!file.isFile() || !file.isReadable()) {
            error = tr("图片文件不存在或无法读取。");
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.isNull()) error = tr("无法加载图片：%1").arg(reader.errorString());
        }
        QMetaObject::invokeMethod(this,
            [completion, image, error] { completion(image, error); }, Qt::QueuedConnection);
    }));
}

void ImageSourceController::addImage(const QString &name, const QString &path, QObject *requester)
{
    if (!requester || m_requests.contains(requester)) return;
    if (name.trimmed().isEmpty() || path.trimmed().isEmpty() || path.contains("://")
        || path.startsWith(":/")) {
        emit addFailed(requester, tr("请输入来源名称和本地图片路径。"));
        return;
    }
    const QString absolutePath = QFileInfo(path.trimmed()).absoluteFilePath();
    const quint64 request = ++m_generation;
    m_requests.insert(requester, request);
    connect(requester, &QObject::destroyed, this, [this, requester] { cancelAdd(requester); });
    QPointer<QObject> owner(requester);
    loadImage(absolutePath, [this, owner, request, name, absolutePath](QImage image, QString error) {
        if (!owner || m_requests.value(owner) != request) return;
        m_requests.remove(owner);
        if (!error.isEmpty()) {
            emit addFailed(owner, error);
            return;
        }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_images.insert(id, {absolutePath, image, ++m_generation});
        SourceData source{id, name.trimmed(), ImageConfig{absolutePath}, {image.size()}};
        if (!m_model.addSource(source)) {
            m_images.remove(id);
            emit addFailed(owner, tr("无法添加图片来源。"));
            return;
        }
        emit imageAdded(owner);
    });
}

void ImageSourceController::cancelAdd(QObject *requester)
{
    m_requests.remove(requester);
}

QString ImageSourceController::browseImage(const QString &currentPath)
{
    QStringList patterns;
    for (const auto &format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    return QFileDialog::getOpenFileName(qobject_cast<QWidget *>(parent()), tr("选择图片"),
        currentPath, tr("图片 (%1);;所有文件 (*)").arg(patterns.join(' ')));
}

void ImageSourceController::synchronize()
{
    QSet<QString> retained;
    QList<PreviewSource> sources;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        if (index.data(SourceListModel::TypeRole).toString() != QStringLiteral("image")) continue;
        const QString id = index.data(SourceListModel::IdRole).toString();
        const QString path = index.data(SourceListModel::FilePathRole).toString();
        retained.insert(id);
        auto it = m_images.find(id);
        if (it == m_images.end() || it->path != path) {
            const quint64 generation = ++m_generation;
            it = m_images.insert(id, {path, {}, generation});
            loadImage(path, [this, id, generation](QImage image, QString error) {
                auto entry = m_images.find(id);
                if (entry == m_images.end() || entry->generation != generation) return;
                entry->image = image;
                m_model.setSourceOriginalSize(id, image.size());
                if (!error.isEmpty()) emit sourceError(id, error);
                synchronize();
            });
        }
        sources.append({id, it->image, index.data(SourceListModel::VisibleRole).toBool(),
            index.data(SourceListModel::TransformRole).value<SourceTransform>(),
            index.data(SourceListModel::LockedRole).toBool(),
            index.data(SourceListModel::FiltersRole).value<QList<SourceFilter>>()});
    }
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (!retained.contains(it.key())) it = m_images.erase(it);
        else ++it;
    }
    emit sourcesChanged(sources);
}
