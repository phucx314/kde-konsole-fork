/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>

class QFileSystemWatcher;
class QListWidget;
class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;

namespace Konsole
{
class Session;
}

/**
 * A panel that shows git status for the terminal's current working directory
 * and provides buttons for common git operations.  Commands are executed
 * via a QProcess so the UI stays responsive; the user can also send
 * commands directly to the terminal session.
 */
class GitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitPanel(QWidget *parent = nullptr);

    /** Set the working directory to inspect. */
    void setWorkingDirectory(const QString &path);

    /**
     * Attach to a live Konsole session so that "Send to Terminal" works.
     * Pass nullptr to detach.
     */
    void setSession(Konsole::Session *session);

private Q_SLOTS:
    void refreshStatus();
    void runGitAdd();
    void runGitCommit();
    void runGitPush();
    void runGitPull();
    void runGitDiff();
    void sendToTerminal(const QString &command);

private:
    void runGitCommand(const QStringList &args);

    QListWidget *m_statusList = nullptr;
    QLabel *m_branchLabel = nullptr;
    QLineEdit *m_commitInput = nullptr;
    QString m_workingDir;
    Konsole::Session *m_session = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
};

#endif // GITPANEL_H
