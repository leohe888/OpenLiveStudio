#pragma once

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
    void removalRequested(const QString &id);
    void propertiesRequested(const QString &id);
};

QDialog *createAudioSourceDialog(QWidget *parent, AudioSourceManager *manager, const QString &id);
