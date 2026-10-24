#include "scenesnapshotbuilder.h"
#include "studiostate.h"

#include <QImageReader>
#include <QPointer>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <optional>

class SceneSnapshotBuilder::Private
{
public:
    struct Entry { QString id, name, path, type; };
    struct Job {
        Snapshot snapshot;
        QList<Entry> entries;
        QList<SourceError> errors;
    };
    QPointer<StudioState> state;
    QPointer<QAbstractItemModel> model;
    QThreadPool pool;
    Decoder decoder;
    std::shared_ptr<std::atomic<quint64>> revision = std::make_shared<std::atomic<quint64>>(0);
    std::optional<Job> pending;
    bool loading = false, queued = false, running = false;

    void launch(SceneSnapshotBuilder *owner, Job job)
    {
        running = true;
        pool.start(QRunnable::create([owner, job = std::move(job), decode = decoder, latest = revision]() mutable {
            for (const auto &entry : job.entries) {
                if (latest->load() != job.snapshot.revision) break;
                DecodeResult result;
                if (entry.type != "image")
                    result.error = tr("不支持的来源类型：%1").arg(entry.type);
                else
                    result = decode(entry.path);
                if (latest->load() != job.snapshot.revision) break;
                if (result.image.isNull() || !result.error.isEmpty()) {
                    job.errors.append({job.snapshot.sceneId, entry.id, entry.name, entry.path,
                                       result.error.isEmpty() ? tr("图像解码未返回有效画面。") : result.error});
                } else {
                    job.snapshot.sourceIds.append(entry.id);
                    job.snapshot.layers.append({result.image});
                }
            }
            // Destructor joins this pool while owner is still alive. No worker
            // reads models or accesses owner fields; delivery is queued to GUI.
            QMetaObject::invokeMethod(owner, [owner, job = std::move(job)]() mutable {
                owner->d->complete(owner, std::move(job));
            }, Qt::QueuedConnection);
        }));
    }

    void complete(SceneSnapshotBuilder *owner, Job job)
    {
        running = false;
        const quint64 completed = job.snapshot.revision;
        QPointer<SceneSnapshotBuilder> guard(owner);
        for (const auto &error : job.errors) {
            if (revision->load() != completed) break;
            emit owner->sourceError(error);
            if (!guard) return;
        }
        if (revision->load() == completed) {
            emit owner->snapshotReady(job.snapshot);
            if (!guard) return;
        }
        if (pending && !running) {
            Job next = std::move(*pending);
            pending.reset();
            if (next.snapshot.revision == revision->load()) launch(owner, std::move(next));
        }
        // Reentrant signal handlers can request a newer snapshot or delete us.
        // Never end its loading state using the completion of an older request.
        if (revision->load() == completed && !running && !queued && loading) {
            loading = false;
            emit owner->loadingChanged();
        }
    }
};

SceneSnapshotBuilder::SceneSnapshotBuilder(StudioState *state, QObject *parent, Decoder decoder)
    : QObject(parent), d(std::make_unique<Private>())
{
    Q_ASSERT(state && state->thread() == thread() && QThread::currentThread() == thread());
    d->state = state;
    d->model = state->sourceModel();
    d->pool.setMaxThreadCount(1);
    d->decoder = decoder ? std::move(decoder) : Decoder([](const QString &path) {
        QImageReader reader(path);
        QImage image = reader.read();
        const QString error = image.isNull() ? tr("无法读取图像：%1").arg(reader.errorString()) : QString();
        return DecodeResult{image, error};
    });
    connect(state, &StudioState::currentSceneIdChanged, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(state, &QObject::destroyed, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::modelReset, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::rowsInserted, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::rowsRemoved, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::rowsMoved, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::layoutChanged, this, &SceneSnapshotBuilder::requestSnapshot);
    connect(d->model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() || roles.contains(StudioState::VisibleRole) || roles.contains(StudioState::FilePathRole)
            || roles.contains(StudioState::TypeRole) || roles.contains(StudioState::OriginalSizeRole))
            requestSnapshot();
    });
    requestSnapshot();
}

SceneSnapshotBuilder::~SceneSnapshotBuilder()
{
    ++*d->revision;
    d->pending.reset();
    d->pool.waitForDone();
}

bool SceneSnapshotBuilder::isLoading() const { return d->loading; }

void SceneSnapshotBuilder::requestSnapshot()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++*d->revision; // Invalidate active decode immediately, before queued capture.
    const bool changed = !d->loading;
    d->loading = true;
    if (!d->queued) {
        d->queued = true;
        QMetaObject::invokeMethod(this, [this] {
            d->queued = false;
            capture();
        }, Qt::QueuedConnection);
    }
    // Queue first: reentrant handlers must not enqueue a duplicate capture after
    // pumping the first one. No access to this after emitting a public signal.
    if (changed) emit loadingChanged();
}

void SceneSnapshotBuilder::capture()
{
    Private::Job job;
    job.snapshot.revision = d->revision->load();
    if (d->state && d->model) {
        job.snapshot.sceneId = d->state->currentSceneId();
        for (int row = 0; row < d->model->rowCount(); ++row) {
            const QModelIndex index = d->model->index(row, 0);
            if (!index.data(StudioState::VisibleRole).toBool()) continue;
            job.entries.append({index.data(StudioState::IdRole).toString(), index.data(StudioState::NameRole).toString(),
                                index.data(StudioState::FilePathRole).toString(), index.data(StudioState::TypeRole).toString()});
        }
    }
    if (d->running) d->pending = std::move(job);
    else d->launch(this, std::move(job));
}
