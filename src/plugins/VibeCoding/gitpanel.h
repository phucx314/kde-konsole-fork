/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef GITPANEL_H
#define GITPANEL_H

#include <QTimer>
#include <QWidget>

class QEvent;
class QFileSystemWatcher;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QProcess;
class QColor;

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void refreshStatus();
    void runGitAdd();
    void runGitAddSelected();
    void runGitAddAllUnstaged();
    void runGitCommit();
    void runGitUnstageSelected();
    void runGitUnstageAll();
    void runGitRevertSelected();
    void runGitPush();
    void runGitPull();
    void runGitDiff();
    void sendToTerminal(const QString &command);

private:
    enum class ItemKind {
        Meta,
        FileStatus,
    };

    enum FileStateFlag {
        StagedState = 0x1,
        UnstagedState = 0x2,
        UntrackedState = 0x4,
    };

    void refreshRepositoryPaths();
    void rebuildWatcher();
    void setStatusMessage(const QString &text);
    void updateElidedPaths();
    QStringList selectedPaths(int requiredFlags, bool *hasIneligibleSelection = nullptr) const;
    void addSectionHeader(const QString &label, const QString &actionText, const QString &actionToolTip, const char *slot);
    void addStatusItem(const QString &code, const QString &path, int flags, const QColor &color, const QString &toolTip);
    void runGitCommand(const QStringList &args);

    QListWidget *m_statusList = nullptr;
    QLabel *m_branchLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusMessageLabel = nullptr;
    QLineEdit *m_commitInput = nullptr;
    QString m_workingDir;
    QString m_repoRoot;
    QString m_gitDir;
    Konsole::Session *m_session = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
};

#endif // GITPANEL_H
