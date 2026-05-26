#include "LogViewerWidget.h"
#include "LogModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QSettings>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QColorDial
#include <QMessageBox>
#include <QLabel>
#include <QDir>
#include <re2/re2.h>

extern QString g_iniPath;

class RuleDialog : public QDialog {
public:
    RuleDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Edit Highlight Rule");
        QVBoxLayout *layout = new QVBoxLayout(this);

        QHBoxLayout *patternLayout = new QHBoxLayout();
        patternLayout->addWidget(new QLabel("Regex Pattern:", this));
        m_patternEdit = new QLineEdit(this);
        patternLayout->addWidget(m_patternEdit);
        layout->addLayout(patternLayout);

        QHBoxLayout *colorLayout = new QHBoxLayout();
        
        QVBoxLayout *fgLayout = new QVBoxLayout();
        fgLayout->addWidget(new QLabel("Foreground", this));
        m_fgButton = new QPushButton(this);
        fgLayout->addWidget(m_fgButton);
        
        QVBoxLayout *bgLayout = new QVBoxLayout();
        bgLayout->addWidget(new QLabel("Background", this));
        m_bgButton = new QPushButton(this);
        bgLayout->addWidget(m_bgButton);
        
        colorLayout->addLayout(fgLayout);
        colorLayout->addLayout(bgLayout);
        layout->addLayout(colorLayout);

        connect(m_fgButton, &QPushButton::clicked, this, [this]() {
            QColor col = QColorDialog::getColor(m_fgColor, this, "Choose Foreground Color");
            if (col.isValid()) {
                m_fgColor = col;
                updateButtonColors();
            }
        });

        connect(m_bgButton, &QPushButton::clicked, this, [this]() {
            QColor col = QColorDialog::getColor(m_bgColor, this, "Choose Background Color");
            if (col.isValid()) {
                m_bgColor = col;
                updateButtonColors();
            }
        });

        QHBoxLayout *buttonsLayout = new QHBoxLayout();
        QPushButton *btnOk = new QPushButton("OK", this);
        QPushButton *btnCancel = new QPushButton("Cancel", this);
        buttonsLayout->addWidget(btnOk);
        buttonsLayout->addWidget(btnCancel);
        layout->addLayout(buttonsLayout);

        connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

        m_fgColor = Qt::white;
        m_bgColor = Qt::black;
        updateButtonColors();
    }

    void setRule(const QString &pattern, QColor fg, QColor bg) {
        m_patternEdit->setText(pattern);
        m_fgColor = fg;
        m_bgColor = bg;
        updateButtonColors();
    }

    QString pattern() const { return m_patternEdit->text().trimmed(); }
    QColor foregroundColor() const { return m_fgColor; }
    QColor backgroundColor() const { return m_bgColor; }

private:
    QLineEdit *m_patternEdit;
    QPushButton *m_fgButton;
    QPushButton *m_bgButton;
    QColor m_fgColor;
    QColor m_bgColor;

    void updateButtonColors() {
        m_fgButton->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid gray;")
            .arg(m_fgColor.name())
            .arg(m_fgColor.lightness() > 128 ? "black" : "white"));
        m_fgButton->setText(m_fgColor.name());

        m_bgButton->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid gray;")
            .arg(m_bgColor.name())
            .arg(m_bgColor.lightness() > 128 ? "black" : "white"));
        m_bgButton->setText(m_bgColor.name());
    }
};

class SettingsDialog : public QDialog {
public:
    SettingsDialog(const std::vector<HighlightRule> &rules, QWidget *parent = nullptr) 
        : QDialog(parent), m_rules(rules) {
        setWindowTitle("Highlighting Rules");
        resize(600, 400);

        QHBoxLayout *mainLayout = new QHBoxLayout();

        // Rules Table
        m_table = new QTableWidget(this);
        m_table->setColumnCount(2);
        m_table->setHorizontalHeaderLabels({"Priority", "Regex Pattern"});
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        mainLayout->addWidget(m_table);

        // Control Buttons
        QVBoxLayout *btnLayout = new QVBoxLayout();
        QPushButton *btnAdd = new QPushButton("Add", this);
        QPushButton *btnEdit = new QPushButton("Edit", this);
        QPushButton *btnDelete = new QPushButton("Delete", this);
        QPushButton *btnDefault = new QPushButton("Add Default Rules", this);
        QPushButton *btnUp = new QPushButton("Move Up", this);
        QPushButton *btnDown = new QPushButton("Move Down", this);
        btnLayout->addWidget(btnAdd);
        btnLayout->addWidget(btnEdit);
        btnLayout->addWidget(btnDelete);
        btnLayout->addWidget(btnDefault);
        btnLayout->addSpacing(20);
        btnLayout->addWidget(btnUp);
        btnLayout->addWidget(btnDown);
        btnLayout->addStretch();
        mainLayout->addLayout(btnLayout);

        QHBoxLayout *okCancelLayout = new QHBoxLayout();
        QPushButton *btnOk = new QPushButton("OK", this);
        QPushButton *btnCancel = new QPushButton("Cancel", this);
        okCancelLayout->addWidget(btnOk);
        okCancelLayout->addWidget(btnCancel);
        
        QVBoxLayout *outerLayout = new QVBoxLayout(this);
        outerLayout->addLayout(mainLayout);
        outerLayout->addLayout(okCancelLayout);

        connect(btnAdd, &QPushButton::clicked, this, &SettingsDialog::onAdd);
        connect(btnEdit, &QPushButton::clicked, this, &SettingsDialog::onEdit);
        connect(btnDelete, &QPushButton::clicked, this, &SettingsDialog::onDelete);
        connect(btnDefault, &QPushButton::clicked, this, &SettingsDialog::onAddDefaults);
        connect(btnUp, &QPushButton::clicked, this, &SettingsDialog::onMoveUp);
        connect(btnDown, &QPushButton::clicked, this, &SettingsDialog::onMoveDown);
        connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_table, &QTableWidget::cellDoubleClicked, this, &SettingsDialog::onEdit);

        populateTable();
    }

    std::vector<HighlightRule> rules() const { return m_rules; }

private:
    QTableWidget *m_table;
    std::vector<HighlightRule> m_rules;

    void populateTable() {
        m_table->setRowCount(0);
        for (size_t i = 0; i < m_rules.size(); ++i) {
            int row = m_table->rowCount();
            m_table->insertRow(row);

            // Priority
            QTableWidgetItem *itemPriority = new QTableWidgetItem(QString::number(i + 1));
            itemPriority->setFlags(itemPriority->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(row, 0, itemPriority);

            // Pattern
            QTableWidgetItem *itemPattern = new QTableWidgetItem(m_rules[i].pattern);
            itemPattern->setBackground(m_rules[i].backgroundColor);
            itemPattern->setForeground(m_rules[i].foregroundColor);
            itemPattern->setFlags(itemPattern->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(row, 1, itemPattern);
        }
    }

    void onAdd() {
        RuleDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            QString pat = dlg.pattern();
            if (pat.isEmpty()) return;
            // Enforce uniqueness
            for (const auto &r : m_rules) {
                if (r.pattern == pat) {
                    QMessageBox::warning(this, "Duplicate Rule", "A rule with this regex pattern already exists.");
                    return;
                }
            }
            auto re = std::make_shared<re2::RE2>(pat.toStdString());
            if (!re->ok()) {
                QMessageBox::warning(this, "Invalid Regex", "The regex pattern is invalid.");
                return;
            }
            HighlightRule r;
            r.pattern = pat;
            r.foregroundColor = dlg.foregroundColor();
            r.backgroundColor = dlg.backgroundColor();
            r.compiledRegex = re;
            m_rules.push_back(r);
            populateTable();
        }
    }

    void onAddDefaults() {
        int insertIdx = 0;
        QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
        if (!selectedItems.isEmpty()) {
            int minRow = m_rules.size();
            for (auto *item : selectedItems) {
                if (item->row() < minRow) {
                    minRow = item->row();
                }
            }
            insertIdx = minRow;
        }

        struct DefaultRule {
            QString pat;
            QString fg;
            QString bg;
        } defaults[] = {
            { ".*TRACE.*", "#9CA3AF", "#000000" },
            { ".*DEBUG.*", "#60A5FA", "#000000" },
            { ".*INFO.*", "#4ADE80", "#000000" },
            { ".*WARN.*", "#FBBF24", "#000000" },
            { ".*ERROR.*", "#F87171", "#000000" },
            { ".*FATAL.*", "#C084FC", "#000000" }
        };
        for (int i = 5; i >= 0; --i) {
            const auto &d = defaults[i];
            bool exists = false;
            for (const auto &r : m_rules) {
                if (r.pattern == d.pat) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            HighlightRule r;
            r.pattern = d.pat;
            r.foregroundColor = QColor(d.fg);
            r.backgroundColor = QColor(d.bg);
            auto re = std::make_shared<re2::RE2>(d.pat.toStdString());
            if (re->ok()) {
                r.compiledRegex = re;
                m_rules.insert(m_rules.begin() + insertIdx, r);
            }
        }
        populateTable();
    }

    void onEdit() {
        int row = m_table->currentRow();
        if (row < 0 || row >= static_cast<int>(m_rules.size())) return;

        RuleDialog dlg(this);
        dlg.setRule(m_rules[row].pattern, m_rules[row].foregroundColor, m_rules[row].backgroundColor);
        if (dlg.exec() == QDialog::Accepted) {
            QString pat = dlg.pattern();
            if (pat.isEmpty()) return;
            // Enforce uniqueness
            for (int i = 0; i < static_cast<int>(m_rules.size()); ++i) {
                if (i != row && m_rules[i].pattern == pat) {
                    QMessageBox::warning(this, "Duplicate Rule", "A rule with this regex pattern already exists.");
                    return;
                }
            }
            auto re = std::make_shared<re2::RE2>(pat.toStdString());
            if (!re->ok()) {
                QMessageBox::warning(this, "Invalid Regex", "The regex pattern is invalid.");
                return;
            }
            m_rules[row].pattern = pat;
            m_rules[row].foregroundColor = dlg.foregroundColor();
            m_rules[row].backgroundColor = dlg.backgroundColor();
            m_rules[row].compiledRegex = re;
            populateTable();
        }
    }

    void onDelete() {
        QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
        if (selectedItems.isEmpty()) return;

        std::vector<int> rows;
        for (auto *item : selectedItems) {
            int r = item->row();
            if (std::find(rows.begin(), rows.end(), r) == rows.end()) {
                rows.push_back(r);
            }
        }
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows) {
            m_rules.erase(m_rules.begin() + r);
        }
        populateTable();
    }

    void onMoveUp() {
        QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
        if (selectedItems.isEmpty()) return;

        std::vector<int> selectedRows;
        for (auto *item : selectedItems) {
            int r = item->row();
            if (std::find(selectedRows.begin(), selectedRows.end(), r) == selectedRows.end()) {
                selectedRows.push_back(r);
            }
        }
        std::sort(selectedRows.begin(), selectedRows.end());

        if (selectedRows.empty() || selectedRows.front() == 0) return;

        for (int r : selectedRows) {
            std::swap(m_rules[r], m_rules[r - 1]);
        }

        populateTable();

        m_table->clearSelection();
        for (int r : selectedRows) {
            m_table->selectionModel()->select(m_table->model()->index(r - 1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    void onMoveDown() {
        QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
        if (selectedItems.isEmpty()) return;

        std::vector<int> selectedRows;
        for (auto *item : selectedItems) {
            int r = item->row();
            if (std::find(selectedRows.begin(), selectedRows.end(), r) == selectedRows.end()) {
                selectedRows.push_back(r);
            }
        }
        std::sort(selectedRows.begin(), selectedRows.end());

        if (selectedRows.empty() || selectedRows.back() == static_cast<int>(m_rules.size()) - 1) return;

        for (int i = static_cast<int>(selectedRows.size()) - 1; i >= 0; --i) {
            int r = selectedRows[i];
            std::swap(m_rules[r], m_rules[r + 1]);
        }

        populateTable();

        m_table->clearSelection();
        for (int r : selectedRows) {
            m_table->selectionModel()->select(m_table->model()->index(r + 1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
};

// ─── LogFilterProxy ────────────────────────────────────────────────────


LogFilterProxy::LogFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent) {}

void LogFilterProxy::setRegexFilterActive(bool active) {
    beginFilterChange();
    m_regexActive = active;
    endFilterChange();
}

void LogFilterProxy::setTimeFilterActive(bool active) {
    beginFilterChange();
    m_timeActive = active;
    endFilterChange();
}

void LogFilterProxy::setTimeRange(const QDateTime &start, const QDateTime &end) {
    if (!m_timeActive) {
        m_timeStart = start;
        m_timeEnd = end;
        return;
    }
    beginFilterChange();
    m_timeStart = start;
    m_timeEnd = end;
    endFilterChange();
}

void LogFilterProxy::refreshFilter() {
    beginFilterChange();
    endFilterChange();
}

bool LogFilterProxy::filterAcceptsRow(int sourceRow,
                                      const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);
    auto *src = qobject_cast<LogModel*>(sourceModel());
    if (!src) return true;

    if (m_regexActive && !src->isMatch(sourceRow))
        return false;

    if (m_timeActive && m_timeStart.isValid() && m_timeEnd.isValid()) {
        QString line = src->lineText(sourceRow);
        QDateTime ts = LogModel::parseTimestampFromLine(line);
        if (ts.isValid()) {
            if (ts < m_timeStart || ts > m_timeEnd)
                return false;
        }
    }

    return true;
}

// ─── LogViewerWidget ───────────────────────────────────────────────────

LogViewerWidget::LogViewerWidget(QWidget *parent)
    : QWidget(parent), model(new LogModel(this))
{
    // ────────────────────────────────────────────────────────────────────
    // FOCUS LAYER 1: Preventive – NoFocus on the container itself.
    // No WA_NativeWindow, no WA_ShowWithoutActivating – those create
    // Wayland subsurface issues that are worse than the problem they solve.
    // ────────────────────────────────────────────────────────────────────
    setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ── Top Header ─────────────────────────────────────────────────────
    QHBoxLayout *headerLayout = new QHBoxLayout();

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("Regex search...");
    headerLayout->addWidget(searchEdit);

    btnSearchStart = new QPushButton("Search / Next", this);
    btnSearchStop  = new QPushButton("Stop", this);
    btnSearchStop->setEnabled(false);
    headerLayout->addWidget(btnSearchStart);
    headerLayout->addWidget(btnSearchStop);

    timeStart = new QDateTimeEdit(this);
    timeEnd   = new QDateTimeEdit(this);
    headerLayout->addWidget(new QLabel("From:"));
    headerLayout->addWidget(timeStart);
    headerLayout->addWidget(new QLabel("To:"));
    headerLayout->addWidget(timeEnd);

    chkFollow     = new QCheckBox("Follow", this);
    chkFilterMode = new QCheckBox("Filter", this);
    btnSettings   = new QPushButton("⚙ Settings", this);
    headerLayout->addWidget(chkFollow);
    headerLayout->addWidget(chkFilterMode);
    headerLayout->addWidget(btnSettings);

    mainLayout->addLayout(headerLayout);

    // ── Filter proxy ───────────────────────────────────────────────────
    filterProxy = new LogFilterProxy(this);
    filterProxy->setSourceModel(model);

    // ── Log viewport ───────────────────────────────────────────────────
    listView = new QListView(this);
    listView->setModel(filterProxy);
    listView->setUniformItemSizes(true);
    listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listView->setWordWrap(false);
    listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(listView);

    // Context menu (Copy)
    listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *copyAct = menu.addAction("Copy");
        connect(copyAct, &QAction::triggered, this, &LogViewerWidget::copySelectedLines);
        menu.exec(listView->viewport()->mapToGlobal(pos));
    });

    // ── Status bar ─────────────────────────────────────────────────────
    QHBoxLayout *statusLayout = new QHBoxLayout();
    progressBar = new QProgressBar(this);
    progressBar->hide();
    statusLabel = new QLabel("Ready.", this);
    statusLayout->addWidget(progressBar);
    statusLayout->addWidget(statusLabel);
    mainLayout->addLayout(statusLayout);

    // ────────────────────────────────────────────────────────────────────
    // FOCUS LAYER 2: Set Qt::NoFocus on ALL child widgets.
    // This prevents Tab traversal and click-to-focus from pulling keyboard
    // focus away from Double Commander's file panel.
    // We install ourselves as an event filter on every child to catch
    // any FocusIn events that bypass the policy (programmatic setFocus).
    // ────────────────────────────────────────────────────────────────────
    installFocusGuard();

    // ── Connections ────────────────────────────────────────────────────
    connect(btnSearchStart, &QPushButton::clicked,
            this, &LogViewerWidget::onSearchStartClicked);
    connect(btnSearchStop,  &QPushButton::clicked,
            this, &LogViewerWidget::onSearchStopClicked);
    connect(chkFollow, &QCheckBox::toggled,
            this, &LogViewerWidget::onFollowToggled);
    connect(chkFilterMode, &QCheckBox::toggled,
            this, &LogViewerWidget::onFilterModeToggled);
    connect(timeStart, &QDateTimeEdit::dateTimeChanged,
            this, &LogViewerWidget::onTimeRangeChanged);
    connect(timeEnd, &QDateTimeEdit::dateTimeChanged,
            this, &LogViewerWidget::onTimeRangeChanged);
    connect(btnSettings, &QPushButton::clicked,
            this, &LogViewerWidget::onSettingsClicked);

    connect(model, &LogModel::searchFinished,
            this, &LogViewerWidget::onSearchFinished);
    connect(model, &LogModel::timestampsDetected,
            this, &LogViewerWidget::onTimestampsDetected);
    connect(model, &LogModel::tailUpdated,
            this, &LogViewerWidget::onTailUpdated);

    // Load and apply highlight rules
    std::vector<HighlightRule> initialRules = loadHighlightRules();
    model->setHighlightRules(initialRules);
}

LogViewerWidget::~LogViewerWidget() {
    qDebug() "LogViewerWidget destroyed";
}

// ─── Focus Layer 2: installFocusGuard ──────────────────────────────────
//
// Walk the entire child tree: set NoFocus on non-input widgets.
// Input widgets (searchEdit, timeStart, timeEnd) and their internal children
// are left alone so they remain usable, but we still install our event
// filter on them for Escape/Enter handling and FocusIn interception.
//
bool LogViewerWidget::isInputWidget(QWidget *w) const {
    if (!w) return false;
    if (w == searchEdit || w == timeStart || w == timeEnd) return true;
    // Check if w is an internal child of an input widget
    if (searchEdit->isAncestorOf(w)) return true;
    if (timeStart->isAncestorOf(w)) return true;
    if (timeEnd->isAncestorOf(w)) return true;
    return false;
}

void LogViewerWidget::installFocusGuard() {
    const auto children = findChildren<QWidget*>();
    for (QWidget *child : children) {
        child->installEventFilter(this);
        // Input widgets keep their default focus policy so they remain usable
        if (!isInputWidget(child))
            child->setFocusPolicy(Qt::NoFocus);
    }
}

// ─── Focus Layer 3: save / restore ─────────────────────────────────────
//
// Call restoreFocusToDC() after any operation that may have stolen focus.
//
void LogViewerWidget::restoreFocusToDC() {
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    } else {
        // Last resort: clear focus from anything inside our subtree
        if (QWidget *fw = QApplication::focusWidget()) {
            if (fw == this || fw->isAncestorOf(this) || this->isAncestorOf(fw))
                fw->clearFocus();
        }
    }
}

// ─── Focus Layer 4: Global FocusIn interceptor ─────────────────────────
//
// If ANY child widget inside our subtree receives FocusIn, we immediately
// clear it — UNLESS the user has explicitly activated an input widget
// (searchEdit, timeStart, timeEnd) via mouse click.
//
bool LogViewerWidget::eventFilter(QObject *obj, QEvent *event) {
    auto *w = qobject_cast<QWidget*>(obj);

    // ── Layer 4: Intercept FocusIn on our children ─────────────────────
    if (event->type() == QEvent::FocusIn && w && this->isAncestorOf(w)) {
        // Allow focus on the active input widget and its internal children
        if (m_activeInput && (w == m_activeInput || m_activeInput->isAncestorOf(w)))
            return false;
        // Reject all other focus — restore to DC
        QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
        return false;
    }

    // ── Handle ChildAdded: guard dynamically-created children ──────────
    if (event->type() == QEvent::ChildAdded) {
        auto *ce = static_cast<QChildEvent*>(event);
        if (auto *childWidget = qobject_cast<QWidget*>(ce->child())) {
            if (!isInputWidget(childWidget))
                childWidget->setFocusPolicy(Qt::NoFocus);
            childWidget->installEventFilter(this);
        }
    }

    // ── KeyPress handling ──────────────────────────────────────────────
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);

        // Escape from any input widget: deactivate and restore focus to DC
        if (ke->key() == Qt::Key_Escape && m_activeInput) {
            m_activeInput = nullptr;
            restoreFocusToDC();
            return true;
        }

        // Enter in search edit: trigger search, deactivate, restore focus
        if (obj == searchEdit && (ke->key() == Qt::Key_Return ||
                                  ke->key() == Qt::Key_Enter)) {
            onSearchStartClicked();
            m_activeInput = nullptr;
            restoreFocusToDC();
            return true;
        }

        // Ctrl+C in list view: copy
        if (obj == listView && ke->matches(QKeySequence::Copy)) {
            copySelectedLines();
            return true;
        }
    }

    // ── MousePress on input widgets: activate them temporarily ─────────
    if (event->type() == QEvent::MouseButtonPress && w) {
        // Determine if click is on one of our input widgets
        QWidget *inputTarget = nullptr;
        if (w == searchEdit || searchEdit->isAncestorOf(w))
            inputTarget = searchEdit;
        else if (w == timeStart || timeStart->isAncestorOf(w))
            inputTarget = timeStart;
        else if (w == timeEnd || timeEnd->isAncestorOf(w))
            inputTarget = timeEnd;

        if (inputTarget) {
            m_activeInput = inputTarget;
            return false; // let the click through normally
        }
    }

    return QWidget::eventFilter(obj, event);
}

// ─── File loading ──────────────────────────────────────────────────────

void LogViewerWidget::loadFile(const QString& filePath) {
    qDebug() << "LogViewerWidget loading file:" << filePath;

    // FOCUS LAYER 3: Save whichever DC widget currently has focus
    m_savedFocusWidget = QApplication::focusWidget();

    currentFile = filePath;
    m_lastMatchRow = -1;
    m_lastSearchQuery.clear();
    m_activeInput = nullptr;
    statusLabel->setText(QString("Loading %1...").arg(filePath));
    model->loadFile(filePath);
    statusLabel->setText(QString("Lines: %1 | %2")
        .arg(model->lineCount()).arg(filePath));

    // FOCUS LAYER 3: Restore focus to DC after loading completes
    QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
}

// ─── External search trigger (from ListSearchText) ─────────────────────

void LogViewerWidget::triggerSearch(const QString& searchString, int) {
    searchEdit->setText(searchString);
    onSearchStartClicked();
}

// ─── Search ────────────────────────────────────────────────────────────

void LogViewerWidget::onSearchStartClicked() {
    const QString query = searchEdit->text();
    if (query.isEmpty()) return;

    if (query != m_lastSearchQuery) {
        m_lastMatchRow = -1;
        m_lastSearchQuery = query;
        btnSearchStop->setEnabled(true);
        statusLabel->setText("Searching...");
        model->startSearch(query);
        return;
    }

    // Same query — jump to next match
    if (model->matchCount() > 0) {
        int next = model->nextMatch(m_lastMatchRow);
        if (next >= 0) {
            m_lastMatchRow = next;
            scrollToSourceRow(next);
            statusLabel->setText(QString("Match at line %1 | %2 total")
                .arg(next + 1).arg(model->matchCount()));
        }
    }
}

void LogViewerWidget::onSearchStopClicked() {
    model->stopSearch();
    btnSearchStop->setEnabled(false);
    statusLabel->setText("Search interrupted");
}

void LogViewerWidget::onSearchFinished(int matchCount) {
    btnSearchStop->setEnabled(false);

    if (matchCount < 0) {
        statusLabel->setText("Invalid regex pattern");
        m_lastSearchQuery.clear();
        return;
    }

    statusLabel->setText(QString("Matches: %1 / %2 lines")
        .arg(matchCount).arg(model->lineCount()));

    if (matchCount > 0) {
        int first = model->nextMatch(-1);
        if (first >= 0) {
            m_lastMatchRow = first;
            scrollToSourceRow(first);
        }
    }

    if (chkFilterMode->isChecked())
        filterProxy->refreshFilter();
}

void LogViewerWidget::scrollToSourceRow(int sourceRow) {
    QModelIndex srcIdx = model->index(sourceRow);
    QModelIndex proxyIdx = filterProxy->mapFromSource(srcIdx);
    if (proxyIdx.isValid()) {
        listView->setCurrentIndex(proxyIdx);
        listView->scrollTo(proxyIdx, QAbstractItemView::PositionAtCenter);
    }
}

// ─── Filter mode ───────────────────────────────────────────────────────

void LogViewerWidget::onFilterModeToggled(bool checked) {
    filterProxy->setRegexFilterActive(checked);
}

// ─── Timestamps ────────────────────────────────────────────────────────

void LogViewerWidget::onTimestampsDetected(const QDateTime &first,
                                            const QDateTime &last) {
    m_timestampsLoading = true;
    if (first.isValid()) timeStart->setDateTime(first);
    if (last.isValid())  timeEnd->setDateTime(last);
    m_timestampsLoading = false;
}

void LogViewerWidget::onTimeRangeChanged() {
    if (m_timestampsLoading) return;

    QDateTime start = timeStart->dateTime();
    QDateTime end   = timeEnd->dateTime();

    if (start.isValid() && end.isValid() && start < end) {
        filterProxy->setTimeRange(start, end);
        filterProxy->setTimeFilterActive(true);
        statusLabel->setText(QString("Time filter: %1 — %2")
            .arg(start.toString("yyyy-MM-dd hh:mm:ss"))
            .arg(end.toString("yyyy-MM-dd hh:mm:ss")));
    }
}

// ─── Follow / tail ─────────────────────────────────────────────────────

void LogViewerWidget::onFollowToggled(bool checked) {
    model->setFollowEnabled(checked);
    if (checked)
        QTimer::singleShot(0, listView, &QListView::scrollToBottom);
}

void LogViewerWidget::onTailUpdated() {
    statusLabel->setText(QString("Lines: %1 | %2 (following)")
        .arg(model->lineCount()).arg(currentFile));
    if (chkFollow->isChecked())
        QTimer::singleShot(0, listView, &QListView::scrollToBottom);
}

// ─── Copy ──────────────────────────────────────────────────────────────

void LogViewerWidget::copySelectedLines() {
    QModelIndexList selected = listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    std::sort(selected.begin(), selected.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() < b.row();
              });

    QStringList lines;
    for (const QModelIndex &idx : selected)
        lines << idx.data(Qt::DisplayRole).toString();

    QApplication::clipboard()->setText(lines.join('\n'));
    statusLabel->setText(QString("Copied %1 line(s)").arg(lines.size()));
}

void LogViewerWidget::onSettingsClicked() {
    SettingsDialog dlg(model->highlightRules(), this);
    if (dlg.exec() == QDialog::Accepted) {
        auto newRules = dlg.rules();
        saveHighlightRules(newRules);
        model->setHighlightRules(newRules);
    }
}

std::vector<HighlightRule> LogViewerWidget::loadHighlightRules() {
    std::vector<HighlightRule> rules;
    QString path = g_iniPath;
    if (path.isEmpty()) {
        path = QDir::homePath() + "/.config/doublecmd/logviewer.ini";
    }

    QSettings settings(path, QSettings::IniFormat);
    bool sectionExists = settings.contains("HighlightRules/Count");

    if (!sectionExists) {
        struct DefaultRule {
            QString pat;
            QString fg;
            QString bg;
        } defaults[] = {
            { ".*TRACE.*", "#9CA3AF", "#000000" },
            { ".*DEBUG.*", "#60A5FA", "#000000" },
            { ".*INFO.*", "#4ADE80", "#000000" },
            { ".*WARN.*", "#FBBF24", "#000000" },
            { ".*ERROR.*", "#F87171", "#000000" },
            { ".*FATAL.*", "#C084FC", "#000000" }
        };
        for (const auto &d : defaults) {
            HighlightRule rule;
            rule.pattern = d.pat;
            rule.foregroundColor = QColor(d.fg);
            rule.backgroundColor = QColor(d.bg);
            auto re = std::make_shared<re2::RE2>(d.pat.toStdString());
            if (re->ok()) {
                rule.compiledRegex = re;
                rules.push_back(rule);
            }
        }
        // Save these defaults initially so the section gets created
        saveHighlightRules(rules);
    } else {
        settings.beginGroup("HighlightRules");
        int count = settings.value("Count", 0).toInt();
        for (int i = 0; i < count; ++i) {
            QString regexKey = QString("Rule%1_Regex").arg(i);
            QString fgKey = QString("Rule%1_FG").arg(i);
            QString bgKey = QString("Rule%1_BG").arg(i);

            QString regexStr = settings.value(regexKey).toString();
            if (regexStr.isEmpty()) continue;

            QString fgStr = settings.value(fgKey).toString();
            QString bgStr = settings.value(bgKey).toString();

            HighlightRule rule;
            rule.pattern = regexStr;
            rule.foregroundColor = fgStr.isEmpty() ? QColor() : QColor(fgStr);
            rule.backgroundColor = bgStr.isEmpty() ? QColor() : QColor(bgStr);

            auto re = std::make_shared<re2::RE2>(regexStr.toStdString());
            if (re->ok()) {
                rule.compiledRegex = re;
                rules.push_back(rule);
            } else {
                qWarning() << "Invalid regex from INI:" << regexStr;
            }
        }
        settings.endGroup();
    }

    return rules;
}

void LogViewerWidget::saveHighlightRules(const std::vector<HighlightRule>& rules) {
    QString path = g_iniPath;
    if (path.isEmpty()) {
        path = QDir::homePath() + "/.config/doublecmd/logviewer.ini";
    }

    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup("HighlightRules");
    settings.remove(""); // Clean up existing keys to prevent orphaned entries

    settings.setValue("Count", static_cast<int>(rules.size()));
    for (size_t i = 0; i < rules.size(); ++i) {
        settings.setValue(QString("Rule%1_Regex").arg(i), rules[i].pattern);
        settings.setValue(QString("Rule%1_FG").arg(i), rules[i].foregroundColor.name());
        settings.setValue(QString("Rule%1_BG").arg(i), rules[i].backgroundColor.name());
    }
    settings.endGroup();
    settings.sync();
}

