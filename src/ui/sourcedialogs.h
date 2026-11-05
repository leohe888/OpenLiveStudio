#ifndef SOURCEDIALOGS_H
#define SOURCEDIALOGS_H

#include <QString>
#include <functional>

class QWidget;
class QDialog;
class VideoSourceManager;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;

// UI-thread factories. Dialogs belong to parent and delete themselves on close.
// Managers and controllers must outlive the dialog.
QDialog *createSourceAddDialog(QWidget *parent, VideoSourceManager *manager,
                               ImageSourceController *images, DisplaySourceController *displays,
                               CameraSourceController *cameras);
QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId);
QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove);

#endif
