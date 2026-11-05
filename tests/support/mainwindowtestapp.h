#pragma once

#include <QApplication>
#include <QFile>
#include <QSurfaceFormat>
#include <QtTest>

template <class Test> int runMainWindowTests(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QFile style(":/assets/styles/style.qss");
    if (!style.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("Cannot load application style sheet");
    application.setStyleSheet(QString::fromUtf8(style.readAll()));
    Test test;
    return QTest::qExec(&test, argc, argv);
}
