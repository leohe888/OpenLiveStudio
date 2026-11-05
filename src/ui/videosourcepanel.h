#ifndef VIDEOSOURCEPANEL_H
#define VIDEOSOURCEPANEL_H

#include <QWidget>

class VideoSourceManager;

class VideoSourcePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSourcePanel(VideoSourceManager *manager, QWidget *parent = nullptr);

signals:
    void addRequested();
    void retryRequested(const QString &sourceId);
    void removalRequested(const QString &sourceId);
    void filtersRequested(const QString &sourceId);
};

#endif
