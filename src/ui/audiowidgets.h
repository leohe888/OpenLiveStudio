#ifndef AUDIOWIDGETS_H
#define AUDIOWIDGETS_H

#include <QWidget>

class AudioSourceManager;
class QDialog;

class MixerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MixerPanel(AudioSourceManager *manager, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &sourceId);
    void propertiesRequested(const QString &sourceId);
};

QDialog *createAudioSourceDialog(QWidget *parent, AudioSourceManager *manager,
                                 const QString &sourceId);

#endif
