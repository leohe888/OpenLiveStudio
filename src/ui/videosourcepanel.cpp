#include "ui/videosourcepanel.h"
#include "ui/sourcewidgets.h"
#include "video/videosourcemanager.h"
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QVBoxLayout>

using namespace SourceWidgets;

VideoSourcePanel::VideoSourcePanel(VideoSourceManager *manager, QWidget *parent) : QWidget(parent)
{
    setObjectName("sourcesPanel");
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *content = new QWidget(this);
    auto *stack = new QStackedLayout(content);
    stack->setStackingMode(QStackedLayout::StackAll);
    layout->addWidget(content, 1);

    auto *list = new QListWidget(content);
    list->setObjectName("sourceList");
    stack->addWidget(list);

    auto *empty = createPlainLabel(content, "emptySourcesLabel", tr("暂无来源"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    stack->addWidget(empty);
    stack->setCurrentWidget(empty);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    layout->addLayout(buttons);

    auto *add = createActionButton(this, "addSourceButton", tr("添加来源"), "plus");
    buttons->addWidget(add);

    auto *remove = createActionButton(this, "removeSourceButton", tr("移除来源"), "minus");
    buttons->addWidget(remove);

    auto *up = createActionButton(this, "moveSourceUpButton", tr("来源上移"), "arrow-up");
    buttons->addWidget(up);

    auto *down = createActionButton(this, "moveSourceDownButton", tr("来源下移"), "arrow-down");
    buttons->addWidget(down);

    auto *filters =
        createActionButton(this, "sourceFiltersButton", tr("来源滤镜"), "sliders-horizontal");
    buttons->addWidget(filters);
    buttons->addStretch();

    const auto selection = [=] {
        QSignalBlocker block(list);
        int selected = -1;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == manager->currentSourceId())
                selected = row;
        list->setCurrentRow(selected);
        remove->setEnabled(selected >= 0);
        up->setEnabled(manager->canMoveCurrentSourceUp());
        down->setEnabled(manager->canMoveCurrentSourceDown());
        filters->setEnabled(manager->canFilterCurrentSource());
    };

    const auto updateRow = [=](const QString &sourceId) {
        const auto name = manager->sourceName(sourceId);
        if (name.isNull())
            return;
        const auto *source = manager->source(sourceId);
        const bool image = source && source->type() == VideoSourceType::Image;
        QListWidgetItem *item = nullptr;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == sourceId) {
                item = list->item(row);
                break;
            }
        }
        if (!item)
            return;
        auto *widget = list->itemWidget(item);
        const auto state = image ? VideoSourceState::Idle : source->state();
        const QString error = image ? QString() : source->error();
        const auto *camera = dynamic_cast<const CameraSource *>(source);
        const auto description = camera ? camera->captureDescription() : QString();
        auto *text = widget->findChild<QLabel *>();
        text->setText(name + (state == VideoSourceState::Error ? tr(" · 采集失败") : QString()));
        text->setToolTip(error.isEmpty()
                             ? name + (description.isEmpty() ? QString() : "\n" + description)
                             : error);
        item->setData(Qt::AccessibleTextRole, name);
        item->setData(Qt::AccessibleDescriptionRole, error);

        auto *visibility = widget->findChild<QToolButton *>("sourceVisibilityButton");
        visibility->setChecked(manager->sourceVisible(sourceId));
        visibility->setIcon(QIcon(manager->sourceVisible(sourceId) ? ":/assets/icons/eye.svg"
                                                                   : ":/assets/icons/eye-off.svg"));
        auto *retry = widget->findChild<QPushButton *>("retryDisplayButton");
        if (retry)
            retry->setVisible(source && source->type() == VideoSourceType::DisplayCapture &&
                              state == VideoSourceState::Error);
        auto *lock = widget->findChild<QToolButton *>("sourceLockButton");
        lock->setChecked(manager->sourceLocked(sourceId));
        lock->setIcon(QIcon(manager->sourceLocked(sourceId) ? ":/assets/icons/lock.svg"
                                                            : ":/assets/icons/lock-open.svg"));
    };

    const auto rebuild = [=] {
        QSignalBlocker block(list);
        list->clear();
        for (const auto &source : manager->sources()) {
            const auto id = source->id();
            auto *item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(180, 34));

            auto *widget = new QWidget(list);
            widget->setObjectName("sourceRow_" + id);

            auto *rowLayout = new QHBoxLayout(widget);
            rowLayout->setContentsMargins(8, 0, 0, 0);
            rowLayout->setSpacing(0);

            auto *text = createPlainLabel(widget, "sourceName");
            text->setWordWrap(false);
            text->setMinimumWidth(0);
            text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            text->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(text, 1);

            auto *retry = new QPushButton(tr("重试"), widget);
            retry->setObjectName("retryDisplayButton");
            retry->hide();
            rowLayout->addWidget(retry);
            connect(retry, &QPushButton::clicked, this, [this, id] { emit retryRequested(id); });

            auto *visible =
                createActionButton(widget, "sourceVisibilityButton", tr("显示/隐藏来源"), "eye");
            rowLayout->addWidget(visible);

            auto *locked =
                createActionButton(widget, "sourceLockButton", tr("锁定/解锁来源"), "lock-open");
            rowLayout->addWidget(locked);
            visible->setCheckable(true);
            locked->setCheckable(true);
            connect(visible, &QToolButton::clicked, this, [=](bool value) {
                manager->setSourceVisible(id, value);
            });
            connect(locked, &QToolButton::clicked, this, [=](bool value) {
                manager->setSourceLocked(id, value);
            });
            list->setItemWidget(item, widget);
        }
        empty->setVisible(manager->sources().isEmpty());
        for (const auto &source : manager->sources())
            updateRow(source->id());
        selection();
    };

    connect(manager, &VideoSourceManager::sourcesChanged, this, rebuild);
    connect(manager, &VideoSourceManager::sourceChanged, this, updateRow);
    connect(manager, &VideoSourceManager::sourceStatusChanged, this, updateRow);
    connect(manager, &VideoSourceManager::currentSourceIdChanged, this, selection);
    connect(manager, &VideoSourceManager::currentSourceMoveAvailabilityChanged, this, selection);
    connect(list, &QListWidget::currentRowChanged, this, [=](int row) {
        manager->selectSource(row < 0 ? QString() : list->item(row)->data(Qt::UserRole).toString());
    });
    connect(add, &QToolButton::clicked, this, &VideoSourcePanel::addRequested);
    connect(remove, &QToolButton::clicked, this, [=] {
        emit removalRequested(manager->currentSourceId());
    });
    connect(up, &QToolButton::clicked, this, [=] {
        manager->moveSource(manager->currentSourceId(), -1);
    });
    connect(down, &QToolButton::clicked, this, [=] {
        manager->moveSource(manager->currentSourceId(), 1);
    });
    connect(filters, &QToolButton::clicked, this, [=] {
        emit filtersRequested(manager->currentSourceId());
    });
    rebuild();
}
