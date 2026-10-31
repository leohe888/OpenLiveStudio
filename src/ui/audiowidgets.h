#pragma once

#include <QWidget>

class AudioSourceModel;
class QDialog;

class MixerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MixerPanel(AudioSourceModel *model, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &id);
    void propertiesRequested(const QString &id);
};

QDialog *createAudioSourceDialog(QWidget *parent, AudioSourceModel *model, const QString &id);
