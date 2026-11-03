#ifndef AUDIOSOURCEPANEL_H
#define AUDIOSOURCEPANEL_H

#include <QWidget>

class AudioSourceManager;
class QDialog;

class AudioSourcePanel : public QWidget
{
    Q_OBJECT

public:
    explicit AudioSourcePanel(AudioSourceManager *manager, QWidget *parent = nullptr);

signals:
    void addRequested();
    void removalRequested(const QString &sourceId);
};

QDialog *createAudioSourceAddDialog(QWidget *parent, AudioSourceManager *manager);

#endif
