#ifndef UICOLORS_H
#define UICOLORS_H

#include <QColor>
#include <QVariantMap>

// Fixed light palette shared by Widgets and QML.
namespace UiColors {
inline const QVariantMap &values()
{
    static const QVariantMap colors{
        {QStringLiteral("text"), QVariantMap{
            {QStringLiteral("primary"), QColor(QStringLiteral("#202936"))},
            {QStringLiteral("secondary"), QColor(QStringLiteral("#566579"))},
            {QStringLiteral("disabled"), QColor(QStringLiteral("#8491A2"))},
            {QStringLiteral("onAccent"), QColor(QStringLiteral("#FFFFFF"))},
        }},
        {QStringLiteral("background"), QVariantMap{
            {QStringLiteral("window"), QColor(QStringLiteral("#E9EDF2"))},
            {QStringLiteral("panel"), QColor(QStringLiteral("#F8FAFC"))},
            {QStringLiteral("header"), QColor(QStringLiteral("#F0F3F7"))},
            {QStringLiteral("input"), QColor(QStringLiteral("#FFFFFF"))},
            {QStringLiteral("hovered"), QColor(QStringLiteral("#E4EAF2"))},
            {QStringLiteral("selected"), QColor(QStringLiteral("#DBE9FF"))},
        }},
        {QStringLiteral("border"), QVariantMap{
            {QStringLiteral("default"), QColor(QStringLiteral("#B8C2CF"))},
            {QStringLiteral("subtle"), QColor(QStringLiteral("#D6DEE8"))},
            {QStringLiteral("focus"), QColor(QStringLiteral("#245FBB"))},
        }},
        {QStringLiteral("icon"), QVariantMap{
            {QStringLiteral("primary"), QColor(QStringLiteral("#202936"))},
            {QStringLiteral("secondary"), QColor(QStringLiteral("#566579"))},
            {QStringLiteral("disabled"), QColor(QStringLiteral("#8491A2"))},
        }},
        {QStringLiteral("button"), QVariantMap{
            {QStringLiteral("background"), QVariantMap{
                {QStringLiteral("normal"), QColor(QStringLiteral("#F0F3F7"))},
                {QStringLiteral("hovered"), QColor(QStringLiteral("#E4EAF2"))},
                {QStringLiteral("pressed"), QColor(QStringLiteral("#D6DFEB"))},
                {QStringLiteral("disabled"), QColor(QStringLiteral("#EDF0F4"))},
                {QStringLiteral("accent"), QColor(QStringLiteral("#245FBB"))},
            }},
        }},
        {QStringLiteral("scrollbar"), QVariantMap{
            {QStringLiteral("thumb"), QVariantMap{
                {QStringLiteral("normal"), QColor(QStringLiteral("#8491A2"))},
                {QStringLiteral("hovered"), QColor(QStringLiteral("#65758B"))},
                {QStringLiteral("pressed"), QColor(QStringLiteral("#4D607A"))},
            }},
        }},
        {QStringLiteral("slider"), QVariantMap{
            {QStringLiteral("track"), QColor(QStringLiteral("#D6DEE8"))},
            {QStringLiteral("handle"), QColor(QStringLiteral("#566579"))},
            {QStringLiteral("handleDisabled"), QColor(QStringLiteral("#8491A2"))},
        }},
        {QStringLiteral("status"), QVariantMap{
            {QStringLiteral("success"), QColor(QStringLiteral("#197349"))},
            {QStringLiteral("warning"), QColor(QStringLiteral("#8B5C0A"))},
            {QStringLiteral("error"), QColor(QStringLiteral("#B72C3C"))},
        }},
        {QStringLiteral("recording"), QVariantMap{
            {QStringLiteral("indicator"), QColor(QStringLiteral("#B72C3C"))},
        }},
        {QStringLiteral("streaming"), QVariantMap{
            {QStringLiteral("indicator"), QColor(QStringLiteral("#197349"))},
        }},
        {QStringLiteral("preview"), QVariantMap{
            {QStringLiteral("canvas"), QColor(QStringLiteral("#F8FAFC"))},
            {QStringLiteral("surrounding"), QColor(QStringLiteral("#D4D7DC"))},
        }},
    };
    return colors;
}

inline QColor color(const QString &path)
{
    QVariant value(values());
    for (const auto &part : path.split('.')) {
        if (part.isEmpty() || value.metaType() != QMetaType::fromType<QVariantMap>()) return {};
        value = value.toMap().value(part);
    }
    return value.value<QColor>();
}
} // namespace UiColors

#endif // UICOLORS_H
