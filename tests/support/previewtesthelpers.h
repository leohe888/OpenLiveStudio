#pragma once

#include "ui/previewwidget.h"
#include <QCoreApplication>

namespace {
inline QImage grabPreview(PreviewWidget &preview)
{
    QCoreApplication::processEvents();
    preview.compositor()->captureImage();
    QCoreApplication::processEvents();
    return preview.grabFramebuffer();
}
}

inline QTransform canvasToWidget(const QSizeF &widgetSize)
{
    const qreal scale = qMin(widgetSize.width() / 1920, widgetSize.height() / 1080);
    QTransform matrix;
    matrix.translate((widgetSize.width() - 1920 * scale) / 2,
                     (widgetSize.height() - 1080 * scale) / 2);
    matrix.scale(scale, scale);
    return matrix;
}
