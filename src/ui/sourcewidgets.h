#ifndef SOURCEWIDGETS_H
#define SOURCEWIDGETS_H

#include <QDialog>
#include <QLabel>
#include <QToolButton>

// Small shared building blocks for source panels and dialogs.
namespace SourceWidgets {
inline QToolButton *createActionButton(QWidget *parent, const char *name, const QString &text,
                                       const char *icon)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setIcon(QIcon(QString(":/assets/icons/%1.svg").arg(icon)));
    button->setFixedSize(28, 28);
    return button;
}

inline QLabel *createPlainLabel(QWidget *parent, const char *name, const QString &text = {})
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}

inline QDialog *createSourceDialog(QWidget *parent, const char *name, const QString &title)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(560, 380);
    return dialog;
}

}

#endif
