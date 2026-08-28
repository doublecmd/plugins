#pragma once

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;

namespace QtWlPlugin {

class FocusManager;

/// Base find & replace panel with no scope concept.
///
/// Provides the UI shell (find/replace inputs, match options, action buttons)
/// and emits signals. The consumer implements the actual matching logic by
/// connecting to findRequested, replaceRequested, and replaceAllRequested.
///
/// Scope is entirely the consumer's responsibility at this level.
/// For built-in scope support, use ScopedFindReplacePanel instead.
class FindReplacePanel : public QWidget {
    Q_OBJECT
public:
    explicit FindReplacePanel(FocusManager *fm, QWidget *parent = nullptr);

    // --- Configuration ---
    void setReplaceEnabled(bool enabled);

    // --- State ---
    QString findText() const;
    QString replaceText() const;
    bool matchCase() const;
    bool matchEntireCell() const;
    bool useRegex() const;

    // --- Status feedback ---
    void setStatusText(const QString &text);

    // --- Visibility ---
    void showPanel(bool show);
    bool isPanelVisible() const;

    // --- Access for subclasses ---
    FocusManager *focusManager() const;
    QLabel *statusLabel() const;

signals:
    void findRequested(bool forward);
    void replaceRequested();
    void replaceAllRequested();
    void panelClosed();

protected:
    /// Hook for subclasses to insert widgets into the options row.
    QHBoxLayout *optionsRow() const;

private:
    FocusManager *m_fm;
    QLineEdit *m_txtFind;
    QLineEdit *m_txtReplace;
    QLabel *m_lblReplace;
    QCheckBox *m_chkMatchCase;
    QCheckBox *m_chkMatchEntire;
    QCheckBox *m_chkRegex;
    QLabel *m_lblStatus;
    QHBoxLayout *m_optionsRow;
    QPushButton *m_btnReplace;
    QPushButton *m_btnReplaceAll;
};

} // namespace QtWlPlugin
