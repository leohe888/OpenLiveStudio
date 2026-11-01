#ifndef SOURCEWIDGETS_H
#define SOURCEWIDGETS_H

#include <QWidget>
#include <functional>

class VideoSourceManager;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;
class QDialog;

class SourcesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SourcesPanel(VideoSourceManager *manager, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &sourceId);
    void filtersRequested(const QString &sourceId);
    void propertiesRequested(const QString &sourceId);
};

QDialog *createSourceAddDialog(QWidget *parent, VideoSourceManager *manager,
                               ImageSourceController *images, DisplaySourceController *displays,
                               CameraSourceController *cameras);
QDialog *createSourcePropertiesDialog(QWidget *parent, VideoSourceManager *manager,
                                      const QString &sourceId, DisplaySourceController *displays,
                                      CameraSourceController *cameras);
QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId);
QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove);

#endif
