#include "ui/audiowidgets.h"
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

QToolButton *action(QWidget *parent, const char *name, const QString &text, const char *icon)
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

MixerPanel::MixerPanel(AudioSourceManager *manager, QWidget *parent) : QWidget(parent)
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

    auto *left = new LevelMeter(false, this), *right = new LevelMeter(false, this);
    meterLayout->addWidget(left);
    meterLayout->addWidget(right);

    auto *clip = plainLabel(this);
    clip->setObjectName("mixedClipLabel");
    master->addWidget(clip);

    const auto masterLevels = [=] {
        left->setPeak(manager->masterPeakLeft());
        right->setPeak(manager->masterPeakRight());
        clip->setText(manager->masterClipped() ? tr("过载") : tr("输出"));
    };

    connect(manager, &AudioSourceManager::masterLevelsChanged, this, masterLevels);
    masterLevels();

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

    auto *add = action(this, "addSoundSourceButton", tr("添加声音来源"), "plus");
    actions->addWidget(add);

    auto *remove = action(this, "removeSoundSourceButton", tr("移除声音来源"), "minus");
    actions->addWidget(remove);
    actions->addStretch();

    auto *properties = action(this, "soundSourcePropertiesButton", tr("声音来源属性"), "settings");
    actions->addWidget(properties);

    const auto select = [=] {
        const bool selected = !manager->currentSourceId().isEmpty();
        remove->setEnabled(selected);
        properties->setEnabled(selected);
    };

    connect(manager, &AudioSourceManager::currentSourceIdChanged, this, select);
    select();
    connect(add, &QToolButton::clicked, this, &MixerPanel::addRequested);
    connect(remove, &QToolButton::clicked, this, [=] {
        emit removalRequested(manager->currentSourceId());
    });
    connect(properties, &QToolButton::clicked, this, [=] {
        emit propertiesRequested(manager->currentSourceId());
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

            auto *l = new LevelMeter(true, channel), *r = new LevelMeter(true, channel);
            levels->addWidget(l);
            levels->addWidget(r);

            auto *volume = new QSlider(Qt::Vertical, channel);
            volume->setObjectName(id + "VolumeSlider");
            volume->setRange(0, 100);
            volume->setFixedHeight(78);
            levels->addWidget(volume);

            auto *muteRow = new QHBoxLayout;
            column->addLayout(muteRow);

            auto *mute = action(channel, qPrintable(id + "MuteButton"), tr("静音"), "volume-2");
            mute->setCheckable(true);
            muteRow->addWidget(mute);

            auto *value = plainLabel(channel);
            value->setObjectName("channelValue");
            muteRow->addWidget(value);

            auto *state = plainLabel(channel);
            state->setObjectName("channelStatus");
            column->addWidget(state);
            column->addStretch();
            row->addWidget(channel);

            const auto update = [=] {
                const auto *info = manager->source(id);
                if (!info)
                    return;
                name->setText(info->name);
                name->setToolTip(info->deviceName);
                volume->setAccessibleName(info->name + tr("音量"));
                {
                    QSignalBlocker block(volume);
                    volume->setValue(qRound(info->volume * 100));
                }
                const bool muted = info->muted;
                mute->setChecked(muted);
                mute->setIcon(
                    QIcon(muted ? ":/assets/icons/volume-x.svg" : ":/assets/icons/volume-2.svg"));
                mute->setToolTip(muted ? tr("取消静音") : tr("静音"));
                mute->setAccessibleName(mute->toolTip());
                value->setText(QString::number(volume->value()) + "%");

                const auto status = info->state;
                state->setText(status == "error"      ? tr("等待设备恢复")
                               : status == "starting" ? tr("正在连接…")
                               : muted                ? tr("已静音")
                                                      : tr("采集中"));
                state->setToolTip(info->error);
                const bool selected = manager->currentSourceId() == id;
                if (channel->property("selected").toBool() != selected) {
                    channel->setProperty("selected", selected);
                    channel->style()->unpolish(channel);
                    channel->style()->polish(channel);
                    channel->update();
                }
            };

            const auto updateLevels = [=] {
                if (const auto *info = manager->source(id)) {
                    l->setPeak(info->peak.left);
                    r->setPeak(info->peak.right);
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

QDialog *createAudioSourceDialog(QWidget *parent, AudioSourceManager *manager,
                                 const QString &sourceId)
{
    const bool editing = !sourceId.isEmpty();

    const auto *originalSource = manager->source(sourceId);
    const AudioSource original = originalSource ? *originalSource : AudioSource{};
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("audioSourceDialog");
    dialog->setProperty("audioSourceId", sourceId);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(editing ? QObject::tr("声音来源属性") : QObject::tr("添加声音来源"));
    dialog->resize(540, 370);

    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *name = new QLineEdit(
        editing ? original.name : manager->suggestedSourceName(QObject::tr("麦克风")), dialog);
    name->setObjectName("soundNameInput");
    name->setMaxLength(80);
    form->addRow(QObject::tr("来源名称"), name);

    auto *type = new QComboBox(dialog);
    type->setObjectName("soundTypeInput");
    type->addItem(QObject::tr("麦克风"), QVariant::fromValue(AudioSourceType::Microphone));
    type->addItem(QObject::tr("系统声音"), QVariant::fromValue(AudioSourceType::SystemAudio));
    type->setCurrentIndex(original.type == AudioSourceType::SystemAudio ? 1 : 0);
    type->setEnabled(!editing);
    form->addRow(QObject::tr("来源类型"), type);

    auto *devices = new QComboBox(dialog);
    devices->setObjectName("soundDeviceInput");
    devices->setMinimumContentsLength(15);
    devices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(devices, 1);

    auto *refresh = new QPushButton(QObject::tr("刷新"), dialog);
    refresh->setObjectName("refreshSoundDevicesButton");
    refresh->setVisible(!editing);
    deviceRow->addWidget(refresh);
    form->addRow(QObject::tr("设备"), deviceRow);

    auto *error = plainLabel(dialog);
    error->setObjectName("soundSourceError");
    error->setProperty("uiError", true);
    layout->addWidget(error);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);

    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setObjectName("confirmSoundSourceButton");
    ok->setText(editing ? QObject::tr("确定") : QObject::tr("添加"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("取消"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelSoundSourceButton");

    const auto update = [=] {
        const auto previous = devices->currentData().toString();
        QSignalBlocker block(devices);
        devices->clear();
        if (editing)
            devices->addItem(original.deviceName, original.deviceId);
        else
            for (const auto &device : manager->devices()) {
                if (device.type == type->currentData().value<AudioSourceType>() &&
                    !manager->deviceInUse(device.id))
                    devices->addItem(device.name, device.id);
            }
        const int index = devices->findData(previous);
        if (index >= 0)
            devices->setCurrentIndex(index);
        devices->setPlaceholderText(QObject::tr("暂无未使用的设备"));
        devices->setEnabled(!editing && !manager->refreshing());
        refresh->setEnabled(!manager->refreshing());
        error->setText(editing ? QString() : manager->catalogError());
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       (editing || !devices->currentData().toString().isEmpty()));
        if (editing && !manager->source(sourceId))
            dialog->close();
    };

    QObject::connect(manager, &AudioSourceManager::devicesChanged, dialog, update);
    QObject::connect(manager, &AudioSourceManager::sourcesChanged, dialog, update);
    QObject::connect(type, &QComboBox::activated, dialog, [=] {
        name->setText(manager->suggestedSourceName(type->currentText()));
        update();
    });
    QObject::connect(name, &QLineEdit::textChanged, dialog, [=] {
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       (editing || !devices->currentData().toString().isEmpty()));
    });
    QObject::connect(refresh, &QPushButton::clicked, manager, &AudioSourceManager::refreshDevices);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const bool success =
            editing ? manager->renameSource(sourceId, name->text())
                    : !manager
                           ->addSource(name->text(), type->currentData().value<AudioSourceType>(),
                                       devices->currentData().toString())
                           .isEmpty();
        if (success)
            dialog->accept();
        else
            error->setText(QObject::tr("名称为空或重复，或设备已被其他声音来源使用。"));
    });
    update();
    if (!editing)
        manager->refreshDevices();
    name->setFocus();
    name->selectAll();
    return dialog;
}

#include "audiowidgets.moc"
