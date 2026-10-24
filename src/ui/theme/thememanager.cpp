#include "thememanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

namespace {
const QString preferenceKey = QStringLiteral("appearance/theme");

bool decodeColors(const QJsonObject &object, const QString &prefix,
                  QVariantMap *colors, QSet<QString> *paths)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (object.isEmpty()) return false;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString path = prefix.isEmpty() ? it.key() : prefix + '.' + it.key();
        if (it->isObject()) {
            QVariantMap group;
            if (!decodeColors(it->toObject(), path, &group, paths)) return false;
            colors->insert(it.key(), group);
        } else {
            if (!it->isString() || !hex.match(it->toString()).hasMatch()) return false;
            colors->insert(it.key(), QColor(it->toString()));
            paths->insert(path);
        }
    }
    return true;
}
}

ThemeManager::ThemeManager(QObject *parent) : ThemeManager(defaultSettingsFile(), parent) {}

ThemeManager::ThemeManager(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(settingsFile.isEmpty()
          ? QString() : QFileInfo(settingsFile).absoluteFilePath())
{
    QString error;
    if (!loadTheme(QStringLiteral("dark"), &m_colors, &error)) {
        setError(error);
        return;
    }
    m_themeId = QStringLiteral("dark");
    if (m_settingsFile.isEmpty()) {
        setError(tr("主题配置路径为空。"));
        return;
    }
    // Reading must not create, repair or overwrite user configuration.
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    const QString id = settings.value(preferenceKey, QStringLiteral("dark")).toString();
    if (settings.status() != QSettings::NoError) {
        setError(tr("无法读取主题配置：%1").arg(m_settingsFile));
        return;
    }
    QVariantMap restored;
    if (!loadTheme(id, &restored, &error)) {
        setError(error);
        return;
    }
    m_themeId = id;
    m_colors = restored;
}

QString ThemeManager::defaultSettingsFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("OpenLiveStudio/settings.ini"));
}

bool ThemeManager::loadTheme(const QString &id, QVariantMap *colors, QString *error)
{
    if (id != QStringLiteral("dark") && id != QStringLiteral("light")) {
        *error = tr("不支持的主题：%1").arg(id);
        return false;
    }
    QFile resource(QStringLiteral(":/assets/themes/%1.json").arg(id));
    if (!resource.open(QIODevice::ReadOnly)) {
        *error = tr("无法读取内置主题：%1").arg(id);
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(resource.readAll(), &parseError);
    const auto object = document.object();
    // These names are the shared theme contract, not a second color palette.
    static const QSet<QString> required = {
        "text.primary", "text.secondary", "text.disabled", "text.onAccent",
        "background.window", "background.panel", "background.header", "background.input",
        "background.hovered", "background.selected", "border.default", "border.subtle",
        "border.focus", "icon.primary", "icon.secondary", "icon.disabled",
        "button.background.normal", "button.background.hovered", "button.background.pressed",
        "button.background.disabled", "button.background.accent", "scrollbar.thumb.normal",
        "scrollbar.thumb.hovered", "scrollbar.thumb.pressed", "slider.track", "slider.handle",
        "slider.handleDisabled", "status.success", "status.warning", "status.error",
        "recording.indicator", "streaming.indicator", "preview.canvas"
    };
    QVariantMap decoded;
    QSet<QString> paths;
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || object.size() != 3 || object.value("schemaVersion").toDouble(-1) != 1
        || object.value("id").toString() != id || !object.value("colors").isObject()
        || !decodeColors(object.value("colors").toObject(), {}, &decoded, &paths)
        || paths != required
        || decoded.value("preview").toMap().value("canvas").value<QColor>() != QColor(Qt::black)) {
        *error = tr("内置主题格式或颜色定义无效：%1").arg(id);
        return false;
    }
    *colors = decoded;
    return true;
}

QColor ThemeManager::color(const QString &path) const
{
    QVariant value(m_colors);
    for (const auto &part : path.split('.')) {
        if (part.isEmpty() || value.metaType() != QMetaType::fromType<QVariantMap>()) return {};
        value = value.toMap().value(part);
    }
    return value.metaType() == QMetaType::fromType<QColor>() ? value.value<QColor>() : QColor();
}

bool ThemeManager::setTheme(const QString &id)
{
    QVariantMap decoded;
    QString error;
    if (!loadTheme(id, &decoded, &error)) {
        setError(error);
        return false;
    }
    if (m_settingsFile.isEmpty() || !QDir().mkpath(QFileInfo(m_settingsFile).absolutePath())) {
        setError(tr("无法创建主题配置目录：%1").arg(m_settingsFile));
        return false;
    }
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    const bool hadValue = settings.contains(preferenceKey);
    const auto previous = settings.value(preferenceKey);
    if (settings.status() != QSettings::NoError) {
        setError(tr("主题配置不可读，未保存：%1").arg(m_settingsFile));
        return false;
    }
    settings.setAtomicSyncRequired(true);
    settings.setValue(preferenceKey, id);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // QSettings caches values across instances. Restore that cache too,
        // so failed writes cannot leak the requested theme into a later read.
        if (hadValue) settings.setValue(preferenceKey, previous);
        else settings.remove(preferenceKey);
        setError(tr("无法保存主题配置：%1").arg(m_settingsFile));
        return false;
    }
    const bool changed = m_themeId != id;
    m_themeId = id;
    m_colors = decoded;
    setError({});
    if (changed) emit themeChanged();
    return true;
}

void ThemeManager::setError(const QString &error)
{
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}
