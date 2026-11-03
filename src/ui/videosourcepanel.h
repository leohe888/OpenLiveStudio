#ifndef VIDEOSOURCEPANEL_H
#define VIDEOSOURCEPANEL_H

#include <QWidget>
#include <functional>

class VideoSourceManager;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;
class QDialog;

class VideoSourcePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSourcePanel(VideoSourceManager *manager, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &sourceId);
    void filtersRequested(const QString &sourceId);
};

QDialog *createSourceAddDialog(QWidget *parent, VideoSourceManager *manager,
                               ImageSourceController *images, DisplaySourceController *displays,
                               CameraSourceController *cameras);
QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId);
QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove);

#endif
