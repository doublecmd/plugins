#pragma once

#include <QSplitter>

namespace QtWlPlugin {

/// Reusable left-panel + right-content splitter.
///
/// Both structview (tree) and dbview (table list) use this pattern.
/// Sets sensible defaults: left panel 180px, stretch on right.
class PluginSplitView : public QSplitter {
    Q_OBJECT
public:
    explicit PluginSplitView(QWidget *leftPanel, QWidget *rightContent,
                             QWidget *parent = nullptr);

    void setLeftWidth(int pixels);
    QWidget *leftPanel() const;
    QWidget *rightContent() const;

private:
    QWidget *m_left;
    QWidget *m_right;
};

} // namespace QtWlPlugin
