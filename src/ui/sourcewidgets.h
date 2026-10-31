#pragma once

#include <QWidget>
#include <functional>

class VideoSourceModel;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;
class QDialog;

class SourcesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SourcesPanel(VideoSourceModel *model, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &id);
    void filtersRequested(const QString &id);
    void propertiesRequested(const QString &id);
};

QDialog *createSourceAddDialog(QWidget *parent, VideoSourceModel *model,
                               ImageSourceController *images, DisplaySourceController *displays,
                               CameraSourceController *cameras);
QDialog *createSourcePropertiesDialog(QWidget *parent, VideoSourceModel *model, const QString &id,
                                      DisplaySourceController *displays,
                                      CameraSourceController *cameras);
QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceModel *model, const QString &id);
QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove);
