#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QColor>
#include <QObject>
#include <QVariantMap>

// GUI-thread service. The application owns one instance; tests may use isolated
// instances and INI paths. Does not apply palettes or register QML contexts.
class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY themeChanged)
    Q_PROPERTY(QString settingsFile READ settingsFile CONSTANT)
public:
    explicit ThemeManager(QObject *parent = nullptr);
    explicit ThemeManager(const QString &settingsFile, QObject *parent = nullptr);

    static QString defaultSettingsFile();
    QString settingsFile() const { return m_settingsFile; }
    QString themeId() const { return m_themeId; }
    QVariantMap colors() const { return m_colors; }
    QString lastError() const { return m_lastError; }
    bool isReady() const { return !m_themeId.isEmpty(); }
    // Dotted semantic path, e.g. "text.primary". Unknown paths return an
    // invalid QColor, so a missing token cannot silently become black.
    Q_INVOKABLE QColor color(const QString &path) const;
    // Saves first, then publishes the new theme. Failure preserves the active
    // theme and exposes lastError; repeated selections do not emit themeChanged.
    Q_INVOKABLE bool setTheme(const QString &id);

signals:
    void themeChanged();
    void lastErrorChanged();

private:
    static bool loadTheme(const QString &id, QVariantMap *colors, QString *error);
    void setError(const QString &error);
    QString m_settingsFile;
    QString m_themeId;
    QVariantMap m_colors;
    QString m_lastError;
};

#endif // THEMEMANAGER_H
