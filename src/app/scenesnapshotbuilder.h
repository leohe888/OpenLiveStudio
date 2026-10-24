#ifndef SCENESNAPSHOTBUILDER_H
#define SCENESNAPSHOTBUILDER_H

#include "imagecompositor.h"
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

class StudioState;

// GUI-thread adapter. Copies model data before decoding on a single-worker pool.
// Emits decoded snapshots only; session binding belongs to application assembly.
class SceneSnapshotBuilder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
public:
    struct Snapshot {
        QString sceneId;
        quint64 revision = 0;
        QSize canvas{1920, 1080};
        QStringList sourceIds; // Aligned with layers; independent scene entries.
        QList<ImageCompositor::Layer> layers;
    };
    struct SourceError { QString sceneId, sourceId, name, filePath, message; };
    struct DecodeResult { QImage image; QString error; };
    // Optional decoder permits controlled IO and cancellation-order tests. Runs
    // off the GUI thread and must not touch QObject/model/UI state or throw.
    using Decoder = std::function<DecodeResult(const QString &)>;

    explicit SceneSnapshotBuilder(StudioState *state, QObject *parent = nullptr, Decoder decoder = {});
    // Cancels pending work and joins the decoder before destroying QObject state.
    // A QImageReader::read() already in progress cannot be forcibly interrupted.
    ~SceneSnapshotBuilder() override;
    bool isLoading() const;
    // Explicit refresh also retries missing/corrupt files; no filesystem watcher
    // or cross-scene image cache. Existing pixels stay valid while loading.
    Q_INVOKABLE void requestSnapshot();

signals:
    void snapshotReady(const SceneSnapshotBuilder::Snapshot &snapshot);
    void sourceError(const SceneSnapshotBuilder::SourceError &error);
    void loadingChanged();

private:
    void capture();
    class Private;
    std::unique_ptr<Private> d;
};

Q_DECLARE_METATYPE(SceneSnapshotBuilder::Snapshot)
Q_DECLARE_METATYPE(SceneSnapshotBuilder::SourceError)

#endif // SCENESNAPSHOTBUILDER_H
