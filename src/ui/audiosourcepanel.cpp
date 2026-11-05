#include "ui/audiosourcepanel.h"
#include "audio/audiosourcemanager.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
class LevelMeter : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor normalColor MEMBER m_normalColor)
    Q_PROPERTY(QColor warningColor MEMBER m_warningColor)
    Q_PROPERTY(QColor clippingColor MEMBER m_clippingColor)
    QColor m_normalColor, m_warningColor, m_clippingColor;
    double m_peak = 0;
    bool m_vertical;

public:
    explicit LevelMeter(bool vertical, QWidget *parent) : QWidget(parent), m_vertical(vertical)
    {
        setObjectName("levelTrack");
        if (vertical)
            setFixedSize(7, 78);
        else {
            setMinimumSize(40, 5);
            setMaximumHeight(5);
        }
        setAccessibleName(tr("音频电平"));
    }

    void setPeak(double peak)
    {
        if (m_peak != peak) {
            m_peak = peak;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));

        const auto color = m_peak >= 1    ? m_clippingColor
                           : m_peak >= .8 ? m_warningColor
                                          : m_normalColor;
        const double peak = qBound(0., m_peak, 1.);

        auto fill = rect();
        if (m_vertical)
            fill.setTop(height() - qRound(height() * peak));
        else
            fill.setWidth(qRound(width() * peak));
        painter.fillRect(fill, color.isValid() ? color : palette().color(QPalette::Highlight));
    }
};

QToolButton *createActionButton(QWidget *parent, const char *name, const QString &text,
                                const char *icon)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setIcon(QIcon(QString(":/assets/icons/%1.svg").arg(icon)));
    button->setFixedSize(28, 28);
    return button;
}

QLabel *plainLabel(QWidget *parent, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
}

AudioSourcePanel::AudioSourcePanel(AudioSourceManager *manager, QWidget *parent) : QWidget(parent)
{
    setObjectName("mixerPanel");
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    auto *master = new QHBoxLayout;
    layout->addLayout(master);
    master->addWidget(plainLabel(this, tr("混音")));

    auto *meterLayout = new QVBoxLayout;
    master->addLayout(meterLayout, 1);

    auto *masterLeftMeter = new LevelMeter(false, this),
         *masterRightMeter = new LevelMeter(false, this);
    meterLayout->addWidget(masterLeftMeter);
    meterLayout->addWidget(masterRightMeter);

    auto *clippingLabel = plainLabel(this);
    clippingLabel->setObjectName("mixedClipLabel");
    master->addWidget(clippingLabel);

    const auto refreshMasterMeters = [=] {
        masterLeftMeter->setPeak(manager->masterPeakLeft());
        masterRightMeter->setPeak(manager->masterPeakRight());
        clippingLabel->setText(manager->masterClipped() ? tr("过载") : tr("输出"));
    };

    connect(manager, &AudioSourceManager::masterLevelsChanged, this, refreshMasterMeters);
    refreshMasterMeters();

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("mixerScrollView");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(this);
    auto *stack = new QStackedLayout(content);
    stack->setStackingMode(QStackedLayout::StackAll);
    layout->addWidget(content, 1);
    stack->addWidget(scroll);

    auto *channels = new QWidget(scroll);
    channels->setObjectName("mixerChannels");

    auto *row = new QHBoxLayout(channels);
    row->setContentsMargins(6, 6, 6, 6);
    row->setSpacing(6);
    scroll->setWidget(channels);

    auto *empty = plainLabel(this, tr("暂无声音来源"));
    empty->setObjectName("emptySoundSourcesLabel");
    empty->setAlignment(Qt::AlignCenter);
    empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    stack->addWidget(empty);
    stack->setCurrentWidget(empty);

    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);

    auto *add = createActionButton(this, "addSoundSourceButton", tr("添加声音来源"), "plus");
    actions->addWidget(add);

    auto *remove = createActionButton(this, "removeSoundSourceButton", tr("移除声音来源"), "minus");
    actions->addWidget(remove);
    actions->addStretch();

    const auto select = [=] {
        const bool selected = !manager->currentSourceId().isEmpty();
        remove->setEnabled(selected);
    };

    connect(manager, &AudioSourceManager::currentSourceIdChanged, this, select);
    select();
    connect(add, &QToolButton::clicked, this, &AudioSourcePanel::addRequested);
    connect(remove, &QToolButton::clicked, this, [=] {
        emit removalRequested(manager->currentSourceId());
    });

    const auto rebuild = [=] {
        while (auto *item = row->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        empty->setVisible(manager->count() == 0);
        for (const auto &source : manager->sources()) {
            const auto id = source.id;
            auto *channel = new QFrame(channels);
            channel->setFrameShape(QFrame::StyledPanel);
            channel->setProperty("audioChannel", true);
            channel->setObjectName("soundChannel_" + id);
            channel->setFixedWidth(100);

            auto *column = new QVBoxLayout(channel);
            column->setContentsMargins(4, 4, 4, 4);
            column->setSpacing(3);

            auto *name = new QPushButton(channel);
            name->setObjectName("soundNameButton");
            name->setFlat(true);
            column->addWidget(name);

            auto *levels = new QHBoxLayout;
            column->addLayout(levels);

            auto *leftMeter = new LevelMeter(true, channel),
                 *rightMeter = new LevelMeter(true, channel);
            levels->addWidget(leftMeter);
            levels->addWidget(rightMeter);

            auto *volume = new QSlider(Qt::Vertical, channel);
            volume->setObjectName(id + "VolumeSlider");
            volume->setRange(0, 100);
            volume->setFixedHeight(78);
            levels->addWidget(volume);

            auto *muteRow = new QHBoxLayout;
            column->addLayout(muteRow);

            auto *mute =
                createActionButton(channel, qPrintable(id + "MuteButton"), tr("静音"), "volume-2");
            mute->setCheckable(true);
            muteRow->addWidget(mute);

            auto *volumeLabel = plainLabel(channel);
            volumeLabel->setObjectName("channelValue");
            muteRow->addWidget(volumeLabel);

            auto *statusLabel = plainLabel(channel);
            statusLabel->setObjectName("channelStatus");
            column->addWidget(statusLabel);
            column->addStretch();
            row->addWidget(channel);

            const auto update = [=] {
                const auto *sourceInfo = manager->source(id);
                if (!sourceInfo)
                    return;
                name->setText(sourceInfo->name);
                name->setToolTip(sourceInfo->deviceName);
                volume->setAccessibleName(sourceInfo->name + tr("音量"));
                {
                    QSignalBlocker block(volume);
                    volume->setValue(qRound(sourceInfo->volume * 100));
                }
                const bool muted = sourceInfo->muted;
                mute->setChecked(muted);
                mute->setIcon(
                    QIcon(muted ? ":/assets/icons/volume-x.svg" : ":/assets/icons/volume-2.svg"));
                mute->setToolTip(muted ? tr("取消静音") : tr("静音"));
                mute->setAccessibleName(mute->toolTip());
                volumeLabel->setText(QString::number(volume->value()) + "%");

                const auto status = sourceInfo->state;
                statusLabel->setText(status == "error"      ? tr("等待设备恢复")
                               : status == "starting" ? tr("正在连接…")
                               : muted                ? tr("已静音")
                                                      : tr("采集中"));
                statusLabel->setToolTip(sourceInfo->error);
                const bool selected = manager->currentSourceId() == id;
                if (channel->property("selected").toBool() != selected) {
                    channel->setProperty("selected", selected);
                    channel->style()->unpolish(channel);
                    channel->style()->polish(channel);
                    channel->update();
                }
            };

            const auto updateLevels = [=] {
                if (const auto *sourceInfo = manager->source(id)) {
                    leftMeter->setPeak(sourceInfo->peak.left);
                    rightMeter->setPeak(sourceInfo->peak.right);
                }
            };
            connect(manager, &AudioSourceManager::sourceChanged, channel,
                    [=](const QString &changedId) {
                        if (changedId == id)
                            update();
                    });
            connect(manager, &AudioSourceManager::sourceLevelsChanged, channel,
                    [=](const QString &changedId) {
                        if (changedId == id)
                            updateLevels();
                    });
            connect(manager, &AudioSourceManager::currentSourceIdChanged, channel, update);
            connect(name, &QPushButton::clicked, channel, [=] {
                manager->selectSource(id);
            });
            connect(volume, &QSlider::valueChanged, channel, [=](int amount) {
                manager->selectSource(id);
                manager->setVolume(id, amount / 100.);
            });
            connect(mute, &QToolButton::clicked, channel, [=](bool muted) {
                manager->selectSource(id);
                manager->setMuted(id, muted);
            });
            update();
            updateLevels();
        }
        row->addStretch();
    };

    connect(manager, &AudioSourceManager::sourcesChanged, this, rebuild);
    rebuild();
}

QDialog *createAudioSourceAddDialog(QWidget *parent, AudioSourceManager *manager)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("audioSourceAddDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("添加声音来源"));
    dialog->resize(540, 370);

    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *name = new QLineEdit(
        manager->suggestedSourceName(QObject::tr("麦克风")), dialog);
    name->setObjectName("soundNameInput");
    name->setMaxLength(80);
    form->addRow(QObject::tr("来源名称"), name);

    auto *type = new QComboBox(dialog);
    type->setObjectName("soundTypeInput");
    type->addItem(QObject::tr("麦克风"), QVariant::fromValue(AudioSourceType::Microphone));
    type->addItem(QObject::tr("系统声音"), QVariant::fromValue(AudioSourceType::SystemAudio));
    form->addRow(QObject::tr("来源类型"), type);

    auto *devices = new QComboBox(dialog);
    devices->setObjectName("soundDeviceInput");
    devices->setMinimumContentsLength(15);
    devices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(devices, 1);

    auto *refresh = new QPushButton(QObject::tr("刷新"), dialog);
    refresh->setObjectName("refreshSoundDevicesButton");
    deviceRow->addWidget(refresh);
    form->addRow(QObject::tr("设备"), deviceRow);

    auto *hint = plainLabel(dialog);
    layout->addWidget(hint);

    auto *error = plainLabel(dialog);
    error->setObjectName("soundSourceError");
    error->setProperty("uiError", true);
    layout->addWidget(error);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);

    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setObjectName("confirmSoundSourceButton");
    ok->setText(QObject::tr("添加"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("取消"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelSoundSourceButton");

    const auto update = [=] {
        hint->setText(type->currentData().value<AudioSourceType>() == AudioSourceType::SystemAudio
            ? QObject::tr("采集所选输出设备播放的全部声音；默认选项会跟随系统切换。")
            : QObject::tr("采集所选输入设备的声音；默认选项会跟随系统切换。"));
        const auto previous = devices->currentData().toString();
        QSignalBlocker block(devices);
        devices->clear();
        for (const auto &device : manager->devices()) {
            if (device.type == type->currentData().value<AudioSourceType>() &&
                !manager->deviceInUse(device.id))
                devices->addItem(device.name, device.id);
        }
        const int index = devices->findData(previous);
        if (index >= 0)
            devices->setCurrentIndex(index);
        devices->setPlaceholderText(QObject::tr("暂无未使用的设备"));
        devices->setEnabled(!manager->refreshing());
        refresh->setEnabled(!manager->refreshing());
        error->setText(manager->catalogError());
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       !devices->currentData().toString().isEmpty());
    };

    QObject::connect(manager, &AudioSourceManager::devicesChanged, dialog, update);
    QObject::connect(manager, &AudioSourceManager::sourcesChanged, dialog, update);
    QObject::connect(type, &QComboBox::activated, dialog, [=] {
        name->setText(manager->suggestedSourceName(type->currentText()));
        update();
    });
    QObject::connect(name, &QLineEdit::textChanged, dialog, [=] {
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       !devices->currentData().toString().isEmpty());
    });
    QObject::connect(refresh, &QPushButton::clicked, manager, &AudioSourceManager::refreshDevices);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const bool success =
            !manager->addSource(name->text(), type->currentData().value<AudioSourceType>(),
                                devices->currentData().toString()).isEmpty();
        if (success)
            dialog->accept();
        else
            error->setText(QObject::tr("名称为空或重复，或设备已被其他声音来源使用。"));
    });
    update();
    manager->refreshDevices();
    name->setFocus();
    name->selectAll();
    return dialog;
}

#include "audiosourcepanel.moc"
