#include "ui/audiowidgets.h"
#include "audio/audiosourcemodel.h"

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

MixerPanel::MixerPanel(AudioSourceModel *model, QWidget *parent) : QWidget(parent)
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
        left->setPeak(model->masterPeakLeft());
        right->setPeak(model->masterPeakRight());
        clip->setText(model->masterClipped() ? tr("过载") : tr("输出"));
    };

    connect(model, &AudioSourceModel::masterLevelsChanged, this, masterLevels);
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
        const bool selected = !model->currentSourceId().isEmpty();
        remove->setEnabled(selected);
        properties->setEnabled(selected);
    };

    connect(model, &AudioSourceModel::currentSourceIdChanged, this, select);
    select();
    connect(add, &QToolButton::clicked, this, &MixerPanel::addRequested);
    connect(remove, &QToolButton::clicked, this, [=] {
        emit removalRequested(model->currentSourceId());
    });
    connect(properties, &QToolButton::clicked, this, [=] {
        emit propertiesRequested(model->currentSourceId());
    });

    const auto rebuild = [=] {
        while (auto *item = row->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        empty->setVisible(model->count() == 0);
        for (int n = 0; n < model->count(); ++n) {
            const auto id = model->index(n, 0).data(AudioSourceModel::IdRole).toString();
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
                const auto info = model->source(id);
                if (info.isEmpty())
                    return;
                name->setText(info.value("name").toString());
                name->setToolTip(info.value("deviceName").toString());
                volume->setAccessibleName(info.value("name").toString() + tr("音量"));
                {
                    QSignalBlocker block(volume);
                    volume->setValue(qRound(info.value("volume").toDouble() * 100));
                }
                const bool muted = info.value("muted").toBool();
                mute->setChecked(muted);
                mute->setIcon(
                    QIcon(muted ? ":/assets/icons/volume-x.svg" : ":/assets/icons/volume-2.svg"));
                mute->setToolTip(muted ? tr("取消静音") : tr("静音"));
                mute->setAccessibleName(mute->toolTip());
                value->setText(QString::number(volume->value()) + "%");

                const auto status = info.value("state").toString();
                state->setText(status == "error"      ? tr("等待设备恢复")
                               : status == "starting" ? tr("正在连接…")
                               : muted                ? tr("已静音")
                                                      : tr("采集中"));
                state->setToolTip(info.value("error").toString());
                l->setPeak(model->index(n, 0).data(AudioSourceModel::PeakLeftRole).toDouble());
                r->setPeak(model->index(n, 0).data(AudioSourceModel::PeakRightRole).toDouble());
                const bool selected = model->currentSourceId() == id;
                if (channel->property("selected").toBool() != selected) {
                    channel->setProperty("selected", selected);
                    channel->style()->unpolish(channel);
                    channel->style()->polish(channel);
                    channel->update();
                }
            };

            connect(model, &QAbstractItemModel::dataChanged, channel, update);
            connect(model, &AudioSourceModel::currentSourceIdChanged, channel, update);
            connect(name, &QPushButton::clicked, channel, [=] {
                model->selectSource(id);
            });
            connect(volume, &QSlider::valueChanged, channel, [=](int amount) {
                model->selectSource(id);
                model->setVolume(id, amount / 100.);
            });
            connect(mute, &QToolButton::clicked, channel, [=](bool muted) {
                model->selectSource(id);
                model->setMuted(id, muted);
            });
            update();
        }
        row->addStretch();
    };

    connect(model, &QAbstractItemModel::rowsInserted, this, rebuild);
    connect(model, &QAbstractItemModel::rowsRemoved, this, rebuild);
    connect(model, &QAbstractItemModel::modelReset, this, rebuild);
    rebuild();
}

QDialog *createAudioSourceDialog(QWidget *parent, AudioSourceModel *model, const QString &id)
{
    const bool editing = !id.isEmpty();

    const auto original = model->source(id);
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("audioSourceDialog");
    dialog->setProperty("audioSourceId", id);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(editing ? QObject::tr("声音来源属性") : QObject::tr("添加声音来源"));
    dialog->resize(540, 370);

    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *name = new QLineEdit(editing ? original.value("name").toString()
                                       : model->suggestedSourceName(QObject::tr("麦克风")),
                               dialog);
    name->setObjectName("soundNameInput");
    name->setMaxLength(80);
    form->addRow(QObject::tr("来源名称"), name);

    auto *type = new QComboBox(dialog);
    type->setObjectName("soundTypeInput");
    type->addItem(QObject::tr("麦克风"), "microphone");
    type->addItem(QObject::tr("系统声音"), "system");
    type->setCurrentIndex(original.value("type") == "system" ? 1 : 0);
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
            devices->addItem(original.value("deviceName").toString(), original.value("deviceId"));
        else
            for (const auto &value : model->devices()) {
                const auto device = value.toMap();
                if (device.value("type") == type->currentData() &&
                    !model->deviceInUse(device.value("id").toString()))
                    devices->addItem(device.value("name").toString(), device.value("id"));
            }
        const int index = devices->findData(previous);
        if (index >= 0)
            devices->setCurrentIndex(index);
        devices->setPlaceholderText(QObject::tr("暂无未使用的设备"));
        devices->setEnabled(!editing && !model->refreshing());
        refresh->setEnabled(!model->refreshing());
        error->setText(editing ? QString() : model->catalogError());
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       (editing || !devices->currentData().toString().isEmpty()));
        if (editing && model->source(id).isEmpty())
            dialog->close();
    };

    QObject::connect(model, &AudioSourceModel::devicesChanged, dialog, update);
    QObject::connect(model, &AudioSourceModel::countChanged, dialog, update);
    QObject::connect(type, &QComboBox::activated, dialog, [=] {
        name->setText(model->suggestedSourceName(type->currentText()));
        update();
    });
    QObject::connect(name, &QLineEdit::textChanged, dialog, [=] {
        ok->setEnabled(!name->text().trimmed().isEmpty() &&
                       (editing || !devices->currentData().toString().isEmpty()));
    });
    QObject::connect(refresh, &QPushButton::clicked, model, &AudioSourceModel::refreshDevices);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const bool success = editing
                                 ? model->renameSource(id, name->text())
                                 : !model
                                        ->addSource(name->text(), type->currentData().toString(),
                                                    devices->currentData().toString())
                                        .isEmpty();
        if (success)
            dialog->accept();
        else
            error->setText(QObject::tr("名称为空或重复，或设备已被其他声音来源使用。"));
    });
    update();
    if (!editing)
        model->refreshDevices();
    name->setFocus();
    name->selectAll();
    return dialog;
}

#include "audiowidgets.moc"
