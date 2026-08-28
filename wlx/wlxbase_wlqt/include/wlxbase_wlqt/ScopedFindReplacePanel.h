#pragma once

#include <wlxbase_wlqt/FindReplacePanel.h>

class QComboBox;

namespace QtWlPlugin {

/// FindReplacePanel subclass that adds configurable scope options via a QComboBox.
///
/// The consumer sets scope labels (e.g. "All Cells", "Current Column") via setScopes(),
/// then reads currentScope() in their signal handlers to filter the search.
class ScopedFindReplacePanel : public FindReplacePanel {
    Q_OBJECT
public:
    explicit ScopedFindReplacePanel(FocusManager *fm, QWidget *parent = nullptr);

    void setScopes(const QStringList &scopes);
    QString currentScope() const;

private:
    QComboBox *m_comboScope;
};

} // namespace QtWlPlugin
