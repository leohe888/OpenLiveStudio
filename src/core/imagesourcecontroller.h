#ifndef IMAGESOURCECONTROLLER_H
#define IMAGESOURCECONTROLLER_H

#include "preview/previewsource.h"
#include <QObject>
#include <QHash>
#include <QThreadPool>
#include <functional>

class VideoSourceModel;

class ImageSourceController : public QObject
{
    Q_OBJECT
public:
    explicit ImageSourceController(VideoSourceModel &model, QObject *parent = nullptr);
    ~ImageSourceController() override;
    Q_INVOKABLE void addImage(const QString &name, const QString &path, QObject *requester);
    Q_INVOKABLE void cancelAdd(QObject *requester);
    Q_INVOKABLE QString browseImage(const QString &currentPath);

signals:
    void imageAdded(QObject *requester);
    void addFailed(QObject *requester, const QString &message);
    void sourcesChanged(const QList<PreviewSource> &sources);
    void sourceError(const QString &sourceId, const QString &message);

private:
    void synchronize();
    void loadImage(const QString &path, std::function<void(QImage, QString)> completion);
    VideoSourceModel &m_model;
    QThreadPool m_workers;
    QHash<QObject *, quint64> m_requests;
    struct ImageEntry {
        QString path;
        QImage image;
        quint64 generation = 0;
    };
    QHash<QString, ImageEntry> m_images;
    quint64 m_generation = 0;
};

#endif
