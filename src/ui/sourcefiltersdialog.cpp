#include "ui/sourcedialogs.h"
#include "ui/sourcewidgets.h"
#include "video/videosourcemanager.h"
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <array>

using namespace SourceWidgets;

QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId)
{
    auto *dialog = createSourceDialog(parent, "sourceFiltersDialog", QObject::tr("来源滤镜"));
    dialog->setProperty("filterSourceId", sourceId);
    dialog->resize(680, 380);

    auto *layout = new QVBoxLayout(dialog);
    auto *row = new QHBoxLayout;
    layout->addLayout(row, 1);

    auto *left = new QVBoxLayout;
    row->addLayout(left, 1);
    left->addWidget(createPlainLabel(dialog, "filterHint", QObject::tr("滤镜 · 从上到下处理")));

    auto *list = new QListWidget(dialog);
    list->setObjectName("filterList");
    left->addWidget(list);

    auto *actions = new QHBoxLayout;
    left->addLayout(actions);

    auto *add = createActionButton(dialog, "addFilterButton", QObject::tr("添加色彩校正"), "plus");
    actions->addWidget(add);

    auto *remove =
        createActionButton(dialog, "removeFilterButton", QObject::tr("删除滤镜"), "minus");
    actions->addWidget(remove);

    auto *up = createActionButton(dialog, "upFilterButton", QObject::tr("滤镜上移"), "arrow-up");
    actions->addWidget(up);

    auto *down =
        createActionButton(dialog, "downFilterButton", QObject::tr("滤镜下移"), "arrow-down");
    actions->addWidget(down);

    auto *form = new QFormLayout;
    row->addLayout(form, 1);
    form->addRow(
        createPlainLabel(dialog, "filterApplyHint", QObject::tr("修改立即生效，关闭后保留。")));
    QList<QDoubleSpinBox *> inputs;
    QList<QSlider *> sliders;
    for (const auto &key : {"brightness", "contrast", "saturation"}) {
        auto *input = new QDoubleSpinBox(dialog);
        input->setObjectName(QString(key) + "Input");
        input->setDecimals(2);
        input->setSingleStep(.01);
        input->setRange(QString(key) == "brightness" ? -1 : 0,
                        QString(key) == "brightness" ? 1 : 2);

        auto *slider = new QSlider(Qt::Horizontal, dialog);
        slider->setObjectName(QString(key) + "Slider");
        slider->setRange(int(input->minimum() * 100), int(input->maximum() * 100));

        auto *pair = new QHBoxLayout;
        pair->addWidget(slider, 1);
        pair->addWidget(input);
        form->addRow(QString(key) == "brightness" ? QObject::tr("亮度")
                     : QString(key) == "contrast" ? QObject::tr("对比度")
                                                  : QObject::tr("饱和度"),
                     pair);
        inputs.append(input);
        sliders.append(slider);
    }

    auto *reset = new QPushButton(QObject::tr("恢复默认"), dialog);
    reset->setObjectName("resetFilterButton");
    form->addRow(reset);

    const auto updateSelection = [=] {
        const auto filters = manager->sourceFilters(sourceId);
        const int index = list->currentRow();

        const VideoSourceFilter *filter =
            index >= 0 && index < filters.size() ? &filters[index] : nullptr;
        remove->setEnabled(filter != nullptr);
        const auto *correction =
            filter ? std::get_if<ColorCorrectionConfig>(&filter->config()) : nullptr;
        reset->setEnabled(correction != nullptr);
        up->setEnabled(manager->canMoveFilterUp(sourceId, manager->currentFilterId(sourceId)));
        down->setEnabled(manager->canMoveFilterDown(sourceId, manager->currentFilterId(sourceId)));

        std::array<double, 3> values{0, 1, 1};
        if (correction)
            values = {correction->brightness(), correction->contrast(), correction->saturation()};
        for (int n = 0; n < inputs.size(); ++n) {
            QSignalBlocker inputBlocker(inputs[n]);
            QSignalBlocker sliderBlocker(sliders[n]);
            inputs[n]->setValue(values[n]);
            sliders[n]->setValue(qRound(values[n] * 100));
            inputs[n]->setEnabled(correction != nullptr);
            sliders[n]->setEnabled(correction != nullptr);
        }
    };

    const auto syncSelection = [=] {
        QSignalBlocker blocker(list);
        int selectedRow = -1;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() ==
                manager->currentFilterId(sourceId)) {
                selectedRow = row;
                break;
            }
        }
        list->setCurrentRow(selectedRow);
        updateSelection();
    };

    const auto refresh = [=] {
        if (!manager->canFilterSource(sourceId)) {
            dialog->close();
            return;
        }

        QSignalBlocker blocker(list);
        list->clear();
        int index = 0;
        for (const auto &entry : manager->sourceFilters(sourceId)) {
            QString name;
            switch (entry.type()) {
            case VideoSourceFilterType::ColorCorrection:
                name = QObject::tr("色彩校正");
                break;
            }
            auto *item = new QListWidgetItem(QString("%1 %2").arg(name).arg(++index), list);
            item->setData(Qt::UserRole, entry.id());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(entry.enabled() ? Qt::Checked : Qt::Unchecked);
        }
        syncSelection();
    };

    const auto updateFilter = [=](const QString &filterId,
                                  const std::function<void(VideoSourceFilter &)> &update) {
        auto filters = manager->sourceFilters(sourceId);
        for (auto &filter : filters) {
            if (filter.id() == filterId) {
                update(filter);
                manager->setSourceFilters(sourceId, filters);
                return;
            }
        }
    };
    const auto applyConfig = [=] {
        if (!manager->currentFilterId(sourceId).isEmpty())
            updateFilter(manager->currentFilterId(sourceId), [=](VideoSourceFilter &filter) {
                filter.setConfig(ColorCorrectionConfig{float(inputs[0]->value()),
                                                       float(inputs[1]->value()),
                                                       float(inputs[2]->value())});
            });
    };
    for (int n = 0; n < inputs.size(); ++n) {
        QObject::connect(inputs[n], &QDoubleSpinBox::valueChanged, dialog, applyConfig);
        QObject::connect(sliders[n], &QSlider::valueChanged, dialog, [=](int value) {
            inputs[n]->setValue(value / 100.);
        });
    }
    QObject::connect(manager, &VideoSourceManager::sourceFiltersChanged, dialog,
                     [=](const QString &source) {
                         if (source == sourceId)
                             refresh();
                     });
    QObject::connect(manager, &VideoSourceManager::sourcesChanged, dialog, refresh);
    QObject::connect(manager, &VideoSourceManager::currentFilterIdChanged, dialog,
                     [=](const QString &source) {
                         if (source == sourceId)
                             syncSelection();
                     });
    QObject::connect(list, &QListWidget::currentRowChanged, dialog, [=] {
        const auto *item = list->currentItem();
        manager->selectFilter(sourceId, item ? item->data(Qt::UserRole).toString() : QString());
        updateSelection();
    });
    QObject::connect(list, &QListWidget::itemChanged, dialog, [=](QListWidgetItem *item) {
        updateFilter(item->data(Qt::UserRole).toString(), [=](VideoSourceFilter &filter) {
            filter.setEnabled(item->checkState() == Qt::Checked);
        });
    });
    QObject::connect(add, &QToolButton::clicked, dialog, [=] {
        const auto added = manager->addFilter(sourceId, VideoSourceFilterType::ColorCorrection);
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->data(Qt::UserRole).toString() == added)
                list->setCurrentRow(n);
    });
    QObject::connect(remove, &QToolButton::clicked, dialog, [=] {
        manager->removeFilter(sourceId, manager->currentFilterId(sourceId));
    });
    QObject::connect(up, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, manager->currentFilterId(sourceId), -1);
    });
    QObject::connect(down, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, manager->currentFilterId(sourceId), 1);
    });
    QObject::connect(reset, &QPushButton::clicked, dialog, [=] {
        updateFilter(manager->currentFilterId(sourceId), [](VideoSourceFilter &filter) {
            filter.setConfig(ColorCorrectionConfig{});
        });
    });

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    close->button(QDialogButtonBox::Close)->setObjectName("closeFiltersButton");
    close->button(QDialogButtonBox::Close)->setText(QObject::tr("关闭"));
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    refresh();
    return dialog;
}
