#include "ui/outputwidgets.h"
#include "output/outputsettings.h"
#include "output/recordingcontroller.h"
#include "output/streamingcontroller.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

QWidget *createOutputControls(QWidget *parent, OutputSettings *settings,
                              RecordingController *recorder, StreamingController *streamer)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName("controlsPanel");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QHBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    for (const auto &name : {"recordButton", "streamButton", "settingsButton"}) {
        auto *button = new QPushButton(content);
        button->setObjectName(name);
        button->setMinimumSize(110, 32);
        layout->addWidget(button);
    }
    layout->addStretch();
    scroll->setWidget(content);
    auto *record = content->findChild<QPushButton *>("recordButton");
    auto *stream = content->findChild<QPushButton *>("streamButton");
    auto *options = content->findChild<QPushButton *>("settingsButton");
    record->setIcon(QIcon(":/assets/icons/circle.svg"));
    stream->setIcon(QIcon(":/assets/icons/radio.svg"));
    options->setText(QObject::tr("设置"));
    options->setEnabled(settings);
    const auto refresh = [record, stream, recorder, streamer] {
        const auto rec = recorder ? recorder->state() : QString();
        record->setText(rec == "recording" ? QObject::tr("停止录制") : rec == "starting" ? QObject::tr("取消启动")
            : rec == "stopping" ? QObject::tr("正在停止…") : QObject::tr("开始录制"));
        record->setEnabled(recorder && rec != "stopping" && (recorder->busy() || recorder->ready()));
        const auto live = streamer ? streamer->state() : QString();
        stream->setText(live == "live" ? QObject::tr("停止直播") : live == "connecting" ? QObject::tr("取消连接")
            : live == "reconnecting" ? QObject::tr("停止重连") : live == "stopping" ? QObject::tr("正在停止…") : QObject::tr("开始直播"));
        stream->setEnabled(streamer && live != "stopping" && (streamer->busy() || streamer->ready()));
    };
    if (recorder) {
        QObject::connect(recorder, &RecordingController::statusChanged, content, refresh);
        QObject::connect(recorder, &RecordingController::availabilityChanged, content, refresh);
    }
    if (streamer) {
        QObject::connect(streamer, &StreamingController::statusChanged, content, refresh);
        QObject::connect(streamer, &StreamingController::availabilityChanged, content, refresh);
    }
    refresh();
    return scroll;
}

QDialog *createOutputSettingsDialog(QWidget *parent, OutputSettings *settings)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("outputSettingsDialog");
    dialog->setWindowTitle(QObject::tr("设置"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(620, 460);
    auto *layout = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget(dialog);
    tabs->setObjectName("outputSettingsTabs");
    layout->addWidget(tabs);
    auto *live = new QWidget(tabs);
    auto *record = new QWidget(tabs);
    tabs->addTab(live, QObject::tr("直播推流"));
    tabs->addTab(record, QObject::tr("录屏"));
    tabs->setCurrentIndex(1);
    auto *liveForm = new QFormLayout(live);
    auto *recForm = new QFormLayout(record);
    const auto input = [](const QString &text, const char *name, QWidget *owner) {
        auto *edit = new QLineEdit(text, owner);
        edit->setObjectName(name);
        edit->setMaxLength(4096);
        return edit;
    };
    auto *server = input(settings->streamServer(), "streamServerInput", live);
    server->setPlaceholderText("rtmp://server/app");
    auto *key = input(settings->streamKey(), "streamKeyInput", live);
    key->setEchoMode(QLineEdit::Password);
    auto *directory = input(settings->recordingDirectory(), "recordingDirectoryInput", record);
    const auto bitrate = [](int value, const char *name, QWidget *owner) {
        auto *spin = new QSpinBox(owner);
        spin->setObjectName(name);
        spin->setRange(500, 50000);
        spin->setValue(value);
        spin->setSuffix(" kbps");
        return spin;
    };
    auto *liveRate = bitrate(settings->streamingBitrate(), "streamingBitrateInput", live);
    auto *recRate = bitrate(settings->recordingBitrate(), "recordingBitrateInput", record);
    liveForm->addRow(QObject::tr("服务器地址"), server);
    liveForm->addRow(QObject::tr("串流密钥"), key);
    liveForm->addRow(QObject::tr("视频码率"), liveRate);
    liveForm->addRow(new QLabel(QObject::tr("RTMP · H.264 / AAC · 1080p · 30 FPS；连接和重连期间设置锁定。"), live));
    auto *directoryRow = new QHBoxLayout;
    directoryRow->addWidget(directory);
    auto *browse = new QPushButton(QObject::tr("浏览…"), record);
    browse->setObjectName("browseRecordingDirectoryButton");
    directoryRow->addWidget(browse);
    recForm->addRow(QObject::tr("保存目录"), directoryRow);
    recForm->addRow(QObject::tr("视频码率"), recRate);
    recForm->addRow(new QLabel(QObject::tr("MP4 · 1080p · 30 FPS；录制期间保存目录和码率保持不变。"), record));
    const auto refreshLocks = [=] {
        server->setEnabled(!settings->streamingLocked());
        key->setEnabled(!settings->streamingLocked());
        liveRate->setEnabled(!settings->streamingLocked());
        directory->setEnabled(!settings->recordingLocked());
        browse->setEnabled(!settings->recordingLocked());
        recRate->setEnabled(!settings->recordingLocked());
    };
    QObject::connect(settings, &OutputSettings::recordingLockedChanged, dialog, refreshLocks);
    QObject::connect(settings, &OutputSettings::streamingLockedChanged, dialog, refreshLocks);
    refreshLocks();
    QObject::connect(browse, &QPushButton::clicked, dialog, [=] {
        const auto path = QFileDialog::getExistingDirectory(dialog, QObject::tr("选择录制保存目录"), directory->text());
        if (!path.isEmpty()) directory->setText(path);
    });
    auto *error = new QLabel(dialog);
    error->setObjectName("outputSettingsError");
    error->setProperty("uiError", true);
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    layout->addWidget(new QLabel(QObject::tr("设置仅在本次运行中生效"), dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("applyOutputSettingsButton");
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelOutputSettingsButton");
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        recRate->interpretText();
        liveRate->interpretText();
        if (settings->apply(directory->text(), recRate->value(), server->text(), key->text(), liveRate->value())) dialog->accept();
        else error->setText(settings->lastError());
    });
    return dialog;
}
