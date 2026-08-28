#pragma once

#include <QWidget>
#include <QLabel>
#include <QMap>

class QHBoxLayout;

namespace QtWlPlugin {

/// A compact status bar for WLX plugins.
///
/// Displays configurable sections separated by vertical lines.
/// Typical content: encoding, format name, row count, extra info.
///
/// Example: | UTF-8 | JSON | Rows: 4/4 |
/// Example: | Tables: 11 | Views: 0 | SQLite | Rows: 3/59 |
class PluginStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit PluginStatusBar(QWidget *parent = nullptr);

    void setEncoding(const QString &encoding);
    void setFormatInfo(const QString &info);
    void setRowCount(int filtered, int total);
    void setExtraInfo(const QString &key, const QString &value);
    void removeExtraInfo(const QString &key);

private:
    void rebuild();
    QFrame *createSeparator();

    QLabel *m_encodingLabel;
    QLabel *m_formatLabel;
    QLabel *m_rowLabel;
    QMap<QString, QLabel*> m_extras;
    QHBoxLayout *m_layout;
};

} // namespace QtWlPlugin
