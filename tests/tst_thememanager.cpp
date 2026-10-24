#include "thememanager.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class ThemeManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultsWithoutWriting()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("nested/settings.ini");
        ThemeManager manager(path);
        QVERIFY(manager.isReady());
        QCOMPARE(manager.themeId(), "dark");
        QVERIFY(manager.lastError().isEmpty());
        QCOMPARE(manager.color("text.primary"), QColor("#E6E9EE"));
        QCOMPARE(manager.colors().value("text").toMap().value("primary").value<QColor>(),
                 manager.color("text.primary"));
        QVERIFY(!QFile::exists(path));
        QVERIFY(!QDir(directory.filePath("nested")).exists());
        QCOMPARE(ThemeManager::defaultSettingsFile(),
                 QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                     .filePath("OpenLiveStudio/settings.ini"));
        for (const auto &key : {"", "text", "unknown.primary", "text.unknown", ".text.primary", "text.primary."})
            QVERIFY(!manager.color(key).isValid());
    }

    void switchesPersistsAndRestores()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("nested/settings.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("other/keep", 42);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        ThemeManager manager(path);
        QSignalSpy changed(&manager, &ThemeManager::themeChanged);
        QVERIFY(manager.setTheme("light"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(manager.themeId(), "light");
        QCOMPARE(manager.color("text.primary"), QColor("#202936"));
        QCOMPARE(manager.color("preview.canvas"), QColor(Qt::black));
        QVERIFY(manager.setTheme("light"));
        QCOMPARE(changed.size(), 1);
        {
            QSettings settings(path, QSettings::IniFormat);
            QCOMPARE(settings.value("appearance/theme").toString(), "light");
            QCOMPARE(settings.value("other/keep").toInt(), 42);
            QCOMPARE(settings.allKeys().size(), 2); // No palette copied to user INI.
        }
        ThemeManager restored(path);
        QCOMPARE(restored.themeId(), "light");
        QCOMPARE(restored.colors(), manager.colors());
        QVERIFY(restored.setTheme("dark"));
        ThemeManager secondRestore(path);
        QCOMPARE(secondRestore.themeId(), "dark");
        QCOMPARE(secondRestore.color("button.background.pressed"), QColor("#3F4854"));
        // Existing instances retain their own state; there is no global singleton.
        QCOMPARE(manager.themeId(), "light");
    }

    void rejectsInvalidThemeWithoutChangingState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        ThemeManager manager(path);
        QVERIFY(manager.setTheme("light"));
        const auto colors = manager.colors();
        QSignalSpy changed(&manager, &ThemeManager::themeChanged);
        QSignalSpy errors(&manager, &ThemeManager::lastErrorChanged);
        for (const auto &id : {"", "LIGHT", "system", "../dark"}) {
            QVERIFY(!manager.setTheme(id));
            QVERIFY(!manager.lastError().isEmpty());
            QCOMPARE(manager.themeId(), "light");
            QCOMPARE(manager.colors(), colors);
            QSettings settings(path, QSettings::IniFormat);
            QCOMPARE(settings.value("appearance/theme").toString(), "light");
        }
        QCOMPARE(changed.size(), 0);
        QVERIFY(!errors.isEmpty());
        QVERIFY(manager.setTheme("light"));
        QVERIFY(manager.lastError().isEmpty());
        QCOMPARE(changed.size(), 0);
    }

    void invalidConfiguration_data()
    {
        QTest::addColumn<QByteArray>("ini");
        QTest::newRow("unknown-theme") << QByteArray("[appearance]\ntheme=system\n");
        QTest::newRow("empty-theme") << QByteArray("[appearance]\ntheme=\n");
        QTest::newRow("malformed-ini") << QByteArray("[broken\ntheme=light\n");
    }

    void invalidConfiguration()
    {
        QFETCH(QByteArray, ini);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(ini), qint64(ini.size()));
        }
        ThemeManager manager(path);
        QVERIFY(manager.isReady());
        QCOMPARE(manager.themeId(), "dark");
        QVERIFY(!manager.lastError().isEmpty());
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), ini);
    }

    void writeFailureAndRecovery()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto blocker = directory.filePath("blocker");
        {
            QFile file(blocker);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("keep"), qint64(4));
        }
        const auto path = blocker + "/settings.ini";
        ThemeManager manager(path);
        const auto colors = manager.colors();
        QSignalSpy changed(&manager, &ThemeManager::themeChanged);
        QVERIFY(!manager.setTheme("light"));
        QVERIFY(!manager.lastError().isEmpty());
        QCOMPARE(manager.themeId(), "dark");
        QCOMPARE(manager.colors(), colors);
        QCOMPARE(changed.size(), 0);
        QVERIFY(QFile::remove(blocker));
        QVERIFY(manager.setTheme("light"));
        QCOMPARE(changed.size(), 1);
        QVERIFY(manager.lastError().isEmpty());
        ThemeManager restored(path);
        QCOMPARE(restored.themeId(), "light");
    }

    void rejectsDirectoryAsIniFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        QVERIFY(QDir().mkpath(path));
        ThemeManager manager(path);
        QSignalSpy changed(&manager, &ThemeManager::themeChanged);
        QVERIFY(!manager.setTheme("light"));
        QVERIFY(!manager.lastError().isEmpty());
        QCOMPARE(manager.themeId(), "dark");
        QCOMPARE(changed.size(), 0);
        QVERIFY(QDir(path).exists());
        QVERIFY(QDir().rmdir(path));
        // A failed QSettings sync must not leave a cached light preference.
        ThemeManager restored(path);
        QCOMPARE(restored.themeId(), "dark");
        QVERIFY(!QFile::exists(path));
    }

    void missingResourcesDoNotPretendToBeReady()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        {
            Q_CLEANUP_RESOURCE(test_themes);
            const auto restore = qScopeGuard([] { Q_INIT_RESOURCE(test_themes); });
            ThemeManager manager(path);
            QVERIFY(!manager.isReady());
            QVERIFY(manager.themeId().isEmpty());
            QVERIFY(!manager.lastError().isEmpty());
            QVERIFY(!manager.color("text.primary").isValid());
            QVERIFY(!manager.setTheme("light"));
            QVERIFY(!QFile::exists(path));
        }
        ThemeManager restored(path);
        QVERIFY(restored.isReady());
        QCOMPARE(restored.themeId(), "dark");
    }
};

QTEST_GUILESS_MAIN(ThemeManagerTest)
#include "tst_thememanager.moc"
