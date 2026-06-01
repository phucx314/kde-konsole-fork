/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gitpanel.h"
#include "session/Session.h"

#include <QFileSystemWatcher>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // --- branch label --------------------------------------------------------
    m_branchLabel = new QLabel(tr("Branch: —"), this);
    m_branchLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    mainLayout->addWidget(m_branchLabel);

    // --- git status list -----------------------------------------------------
    auto *statusGroup = new QGroupBox(tr("Git Status"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setContentsMargins(4, 4, 4, 4);

    m_statusList = new QListWidget(statusGroup);
    m_statusList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    statusLayout->addWidget(m_statusList);

    auto *refreshBtn = new QPushButton(tr("🔄 Refresh"), statusGroup);
    connect(refreshBtn, &QPushButton::clicked, this, &GitPanel::refreshStatus);
    statusLayout->addWidget(refreshBtn);

    mainLayout->addWidget(statusGroup);

    // --- commit input --------------------------------------------------------
    auto *commitGroup = new QGroupBox(tr("Commit"), this);
    auto *commitLayout = new QVBoxLayout(commitGroup);
    commitLayout->setContentsMargins(4, 4, 4, 4);

    m_commitInput = new QLineEdit(commitGroup);
    m_commitInput->setPlaceholderText(tr("Commit message…"));
    commitLayout->addWidget(m_commitInput);

    mainLayout->addWidget(commitGroup);

    // --- action buttons ------------------------------------------------------
    auto *actionsGroup = new QGroupBox(tr("Actions"), this);
    auto *actionsLayout = new QVBoxLayout(actionsGroup);
    actionsLayout->setContentsMargins(4, 4, 4, 4);
    actionsLayout->setSpacing(4);

    auto makeButton = [&](const QString &text, auto slot) -> QPushButton * {
        auto *btn = new QPushButton(text, actionsGroup);
        connect(btn, &QPushButton::clicked, this, slot);
        actionsLayout->addWidget(btn);
        return btn;
    };

    makeButton(tr("➕ git add -A"), &GitPanel::runGitAdd);
    makeButton(tr("💾 git commit"), &GitPanel::runGitCommit);
    makeButton(tr("⬆  git push"),  &GitPanel::runGitPush);
    makeButton(tr("⬇  git pull"),  &GitPanel::runGitPull);
    makeButton(tr("📄 git diff"),  &GitPanel::runGitDiff);

    mainLayout->addWidget(actionsGroup);
    mainLayout->addStretch(1);

    // --- file watcher with debounce for auto-refresh -------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(1000); // 1 second debounce
    connect(m_debounce, &QTimer::timeout, this, &GitPanel::refreshStatus);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        m_debounce->start(); // restart the debounce timer on every change
    });
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void GitPanel::setWorkingDirectory(const QString &path)
{
    if (path == m_workingDir) {
        return;
    }

    // Stop watching the old directory
    if (!m_workingDir.isEmpty()) {
        m_watcher->removePath(m_workingDir);
    }

    m_workingDir = path;

    // Watch the new directory for changes
    if (!path.isEmpty()) {
        m_watcher->addPath(path);
    }

    refreshStatus();
}

void GitPanel::setSession(Konsole::Session *session)
{
    m_session = session;
}

// ---------------------------------------------------------------------------
// Slots — run git commands via QProcess
// ---------------------------------------------------------------------------

void GitPanel::refreshStatus()
{
    if (m_workingDir.isEmpty()) {
        m_branchLabel->setText(tr("Branch: —"));
        m_statusList->clear();
        return;
    }

    // --- get current branch ---
    QProcess branchProc;
    branchProc.setWorkingDirectory(m_workingDir);
    branchProc.start(QStringLiteral("git"), {QStringLiteral("branch"), QStringLiteral("--show-current")});
    branchProc.waitForFinished(3000);
    const QString branch = QString::fromUtf8(branchProc.readAllStandardOutput()).trimmed();
    m_branchLabel->setText(branch.isEmpty() ? tr("Branch: (not a git repo)") : tr("Branch: %1").arg(branch));

    // --- get status ---
    QProcess statusProc;
    statusProc.setWorkingDirectory(m_workingDir);
    statusProc.start(QStringLiteral("git"),
                     {QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("--untracked-files=all")});
    statusProc.waitForFinished(5000);

    m_statusList->clear();
    const QString output = QString::fromUtf8(statusProc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        m_statusList->addItem(line.trimmed());
    }

    if (m_statusList->count() == 0) {
        m_statusList->addItem(tr("(clean working tree)"));
    }
}

void GitPanel::runGitAdd()
{
    sendToTerminal(QStringLiteral("git add -A"));
}

void GitPanel::runGitCommit()
{
    const QString msg = m_commitInput->text().trimmed();
    if (msg.isEmpty()) {
        m_commitInput->setPlaceholderText(tr("⚠ Enter a commit message first!"));
        return;
    }
    sendToTerminal(QStringLiteral("git commit -m \"%1\"").arg(msg));
    m_commitInput->clear();
}

void GitPanel::runGitPush()
{
    sendToTerminal(QStringLiteral("git push"));
}

void GitPanel::runGitPull()
{
    sendToTerminal(QStringLiteral("git pull"));
}

void GitPanel::runGitDiff()
{
    sendToTerminal(QStringLiteral("git diff"));
}

// ---------------------------------------------------------------------------
// Helper — send text to the attached Konsole session
// ---------------------------------------------------------------------------

void GitPanel::sendToTerminal(const QString &command)
{
    if (!m_session) {
        return;
    }
    m_session->sendTextToTerminal(command, QLatin1Char('\r'));
}

// ---------------------------------------------------------------------------
// Helper — run a git command via QProcess (unused directly but available)
// ---------------------------------------------------------------------------

void GitPanel::runGitCommand(const QStringList &args)
{
    if (m_workingDir.isEmpty()) {
        return;
    }
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(m_workingDir);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        if (!out.isEmpty()) {
            m_statusList->addItem(QStringLiteral("--- output ---"));
            m_statusList->addItem(out.trimmed());
        }
        proc->deleteLater();
    });
    proc->start(QStringLiteral("git"), args);
}
