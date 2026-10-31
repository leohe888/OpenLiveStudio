#pragma once
#include <QWidget>
class QDialog;
class OutputSettings;
class RecordingController;
class StreamingController;

QWidget *createOutputControls(QWidget *parent, OutputSettings *settings,
                              RecordingController *recorder, StreamingController *streamer);
QDialog *createOutputSettingsDialog(QWidget *parent, OutputSettings *settings);
