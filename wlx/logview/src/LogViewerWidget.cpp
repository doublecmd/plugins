#include "LogViewerWidget.h"
#include "LogModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
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
#include <QColorDialog>
#include <QMessageBox>
#include <QLabel>
#include <QDir>
#include <QStyle>
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
        m_fgButton->setFixedHeight(26);
        fgLayout->addWidget(m_fgButton);
        
        QVBoxLayout *bgLayout = new QVBoxLayout();
        bgLayout->addWidget(new QLabel("Background", this));
        m_bgButton = new QPushButton(this);
        m_bgButton->setFixedHeight(26);
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
        btnOk->setFixedHeight(26);
        btnCancel->setFixedHeight(26);
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
        QPushButton *btnAdd = new QPushButton(QString::fromUtf8(u8"\u271A Add"), this);
        QPushButton *btnEdit = new QPushButton(QString::fromUtf8(u8"\u270E Edit"), this);
        QPushButton *btnDelete = new QPushButton(QString::fromUtf8(u8"\u2715 Delete"), this);
        QPushButton *btnDefault = new QPushButton(QString::fromUtf8(u8"\u2295 Add Default Rules"), this);
        QPushButton *btnUp = new QPushButton(QString::fromUtf8(u8"\u25B2 Move Up"), this);
        QPushButton *btnDown = new QPushButton(QString::fromUtf8(u8"\u25BC Move Down"), this);

        for (auto* btn : {btnAdd, btnEdit, btnDelete, btnDefault, btnUp, btnDown}) {
            btn->setFixedHeight(26);
        }

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
        btnOk->setFixedHeight(26);
        btnCancel->setFixedHeight(26);
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

// beginFilterChange()/endFilterChange() are Qt 6.7+ only (the runner's
// qt6-base-dev ships 6.4.2) -- invalidateFilter() is the pre-6.7 equivalent
// for telling the proxy its filtering criteria changed.
void LogFilterProxy::setRegexFilterActive(bool active) {
    m_regexActive = active;
    invalidateFilter();
}

void LogFilterProxy::setTimeFilterActive(bool active) {
    m_timeActive = active;
    invalidateFilter();
}

void LogFilterProxy::setTimeRange(const QDateTime &start, const QDateTime &end) {
    if (!m_timeActive) {
        m_timeStart = start;
        m_timeEnd = end;
        return;
    }
    m_timeStart = start;
    m_timeEnd = end;
    invalidateFilter();
}

void LogFilterProxy::refreshFilter() {
    invalidateFilter();
}

bool LogFilterProxy::filterAcceptsRow(int sourceRow,
                                      const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);
    auto *src = qobject_cast<LogModel*>(sourceModel());
    if (!src) return true;

    // Only filter by regex if the regex filter is active AND there is an active search with matches
    if (m_regexActive && src->matchCount() > 0 && !src->isMatch(sourceRow))
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
    QWidget *headerContainer = new QWidget(this);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerContainer);
    headerLayout->setContentsMargins(2, 2, 2, 2);
    headerLayout->setSpacing(2);

    searchComboBox = new QComboBox(headerContainer);
    searchComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchComboBox->setEditable(true);
    searchComboBox->setInsertPolicy(QComboBox::NoInsert);
    searchComboBox->lineEdit()->setPlaceholderText("Regex search...");
    searchComboBox->lineEdit()->setClearButtonEnabled(true);
    searchComboBox->setToolTip("Regex string to search or filter");
    headerLayout->addWidget(searchComboBox);

    btnFilter      = new QPushButton(QString::fromUtf8(u8"\u25BD Filter"), headerContainer);
    btnFilter->setToolTip("Apply regex as a view filter");
    btnSearchNext  = new QPushButton(QString::fromUtf8(u8"\u2315 Search / Next"), headerContainer);
    btnSearchNext->setToolTip("Find the next match for the regex");
    btnSearchStop  = new QPushButton(QString::fromUtf8(u8"\u25A0 Stop"), headerContainer);
    btnSearchStop->setEnabled(false);
    btnSearchStop->setToolTip("Stop current search");
    headerLayout->addWidget(btnFilter);
    headerLayout->addWidget(btnSearchNext);
    headerLayout->addWidget(btnSearchStop);

    timeStart = new QDateTimeEdit(headerContainer);
    timeEnd   = new QDateTimeEdit(headerContainer);
    timeStart->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    timeEnd->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    int dtWidth = timeStart->fontMetrics().horizontalAdvance("0000-00-00 00:00:00") + 45; // 45px for spinners and padding
    timeStart->setFixedWidth(dtWidth);
    timeEnd->setFixedWidth(dtWidth);
    timeStart->setToolTip("Start time for filtering logs");
    timeEnd->setToolTip("End time for filtering logs");

    headerLayout->addWidget(new QLabel("From:", headerContainer));
    headerLayout->addWidget(timeStart);
    headerLayout->addWidget(new QLabel("To:", headerContainer));
    headerLayout->addWidget(timeEnd);

    chkFollow     = new QCheckBox("Follow", headerContainer);
    chkFollow->setToolTip("Automatically scroll to new lines (tail)");
    chkFilterMode = new QCheckBox("Filter", headerContainer);
    chkFilterMode->setToolTip("Toggle filter mode");
    btnClearLog   = new QPushButton(QString::fromUtf8(u8"\u239A\uFE0E Clean"), headerContainer);
    btnClearLog->setToolTip("Clean log file (delete all contents, keep the file)");
    btnExtract    = new QPushButton(QString::fromUtf8(u8"\U0001F5AB Extract"), headerContainer);
    btnExtract->setToolTip("Extract selected lines to a new file");
    btnSettings   = new QPushButton(QString::fromUtf8(u8"\u2699 Settings"), headerContainer);
    btnResetFilter = new QPushButton(QString::fromUtf8(u8"\u2716\uFE0E Reset"), headerContainer);
    btnResetFilter->setToolTip("Reset filter");
    
    headerLayout->addWidget(chkFilterMode);
    headerLayout->addWidget(btnResetFilter);
    headerLayout->addWidget(chkFollow);
    headerLayout->addWidget(btnExtract);
    headerLayout->addWidget(btnClearLog);
    headerLayout->addWidget(btnSettings);

    // Reactive search clearing
    connect(searchComboBox->lineEdit(), &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty()) {
            executeSearch(false);
        }
    });

    connect(btnResetFilter, &QPushButton::clicked, this, [this]() {
        bool b1 = searchComboBox->lineEdit()->blockSignals(true);
        searchComboBox->lineEdit()->clear();
        searchComboBox->lineEdit()->blockSignals(b1);

        chkFilterMode->setChecked(false);

        bool b2 = timeStart->blockSignals(true);
        bool b3 = timeEnd->blockSignals(true);
        if (model && model->firstTimestamp().isValid()) {
            timeStart->setDateTime(model->firstTimestamp());
            timeEnd->setDateTime(model->lastTimestamp());
        }
        timeStart->blockSignals(b2);
        timeEnd->blockSignals(b3);

        filterProxy->setTimeFilterActive(false);
        filterProxy->refreshFilter();
        executeSearch(false);
    });

    // Normalize height and vertical alignment for all widgets in the header.
    // Use a fixed height that is large enough for all symbols and matches the search box.
    const int headerHeight = 32;
    
    headerContainer->setStyleSheet(
        "QPushButton, QComboBox, QLineEdit, QDateTimeEdit { "
        "  padding: 0px 4px; "
        "  margin: 0px; "
        "  min-width: 0px; "
        "} "
        "QPushButton { text-align: center; } "
        "QLabel { padding: 0px 2px; }"
    );

    // Targeted fixes for buttons that tend to align higher due to symbol metrics
    btnSearchStop->setStyleSheet(btnSearchStop->styleSheet() + "QPushButton { padding-top: 1px; }");
    btnExtract->setStyleSheet(btnExtract->styleSheet() + "QPushButton { padding-top: 5px; }");

    for (int i = 0; i < headerLayout->count(); ++i) {
        if (QWidget *w = headerLayout->itemAt(i)->widget()) {
            w->setFixedHeight(headerHeight);
            headerLayout->setAlignment(w, Qt::AlignVCenter);
        }
    }

    mainLayout->addWidget(headerContainer);

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

        QAction *copyAct = menu.addAction(QString::fromUtf8(u8"\u29C9 Copy"));
        connect(copyAct, &QAction::triggered, this, &LogViewerWidget::copySelectedLines);

        QAction *deleteAct = menu.addAction(QString::fromUtf8(u8"\u2715 Delete Selected Lines"));
        connect(deleteAct, &QAction::triggered, this, &LogViewerWidget::deleteSelectedLines);

        menu.addSeparator();

        QAction *extractAct = menu.addAction(QString::fromUtf8(u8"\U0001F5AB Extract"));
        connect(extractAct, &QAction::triggered, this, &LogViewerWidget::extractSelectedLines);

        bool hasSelection = !listView->selectionModel()->selectedIndexes().isEmpty();
        deleteAct->setEnabled(hasSelection);
        extractAct->setEnabled(hasSelection);

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
    // FOCUS: Install event filter on qApp for geometry-based activation.
    // Uses the FocusManager pattern from wlxbase_wlqt: click inside the
    // plugin activates it (gives listView focus); click outside deactivates
    // it (returns focus to DC).  Shortcuts only fire when active.
    // ────────────────────────────────────────────────────────────────────
    setFocusProxy(listView);
    qApp->installEventFilter(this);

    // Detect focus leaving the plugin hierarchy (e.g. Alt-Tab, DC menu)
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
        if (QApplication::activeModalWidget())
            return;

        bool oldInside = old && (old == this || this->isAncestorOf(old));
        bool nowInside = now && (now == this || this->isAncestorOf(now));

        if (m_isActive && oldInside && !nowInside) {
            m_isActive = false;
            m_activeInput = nullptr;
        } else if (!m_isActive && nowInside && !oldInside) {
            if (old) {
                QTimer::singleShot(0, this, [this]() {
                    restoreFocusToDC();
                });
            } else {
                QTimer::singleShot(0, this, [this]() {
                    restoreFocusToDC();
                });
            }
        }
    });

    // ── Connections ────────────────────────────────────────────────────
    connect(btnFilter, &QPushButton::clicked, this, [this]() {
        chkFilterMode->setChecked(true);
        executeSearch(false);
    });
    connect(btnSearchNext, &QPushButton::clicked, this, [this]() {
        chkFollow->setChecked(false);
        executeSearch(true);
    });
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
    connect(btnClearLog, &QPushButton::clicked,
            this, &LogViewerWidget::clearLogFile);
    connect(btnExtract, &QPushButton::clicked,
            this, &LogViewerWidget::extractSelectedLines);
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
}

// ─── Focus helpers ─────────────────────────────────────────────────────

bool LogViewerWidget::isTextInputWidget(QWidget *w) const {
    if (!w) return false;
    if (w == searchComboBox || w == searchComboBox->lineEdit() || w == timeStart || w == timeEnd) return true;
    if (searchComboBox->isAncestorOf(w)) return true;
    if (timeStart->isAncestorOf(w)) return true;
    if (timeEnd->isAncestorOf(w)) return true;
    return false;
}

void LogViewerWidget::restoreFocusToDC() {
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    } else if (parentWidget()) {
        parentWidget()->setFocus(Qt::OtherFocusReason);
    } else {
        if (QWidget *fw = QApplication::focusWidget()) {
            if (fw == this || this->isAncestorOf(fw))
                fw->clearFocus();
        }
    }
}

// ─── Event filter (installed on qApp) ──────────────────────────────────

bool LogViewerWidget::eventFilter(QObject *obj, QEvent *event) {
    QWidget *w = qobject_cast<QWidget*>(obj);

    // ── Geometry-based click activation/deactivation ───────────────────
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const QRect gr(mapToGlobal(QPoint(0, 0)), size());

        if (m_isActive && !gr.contains(gp)) {
            // Click outside plugin → deactivate
            m_isActive = false;
            m_activeInput = nullptr;
            clearFocus();
            restoreFocusToDC();
            return false;
        } else if (!m_isActive && gr.contains(gp)) {
            // Click inside plugin → activate
            m_isActive = true;

            // Determine what was clicked
            if (w && isTextInputWidget(w)) {
                // Clicked a text input — let it gain focus naturally
                m_activeInput = w;
                // Find the top-level input widget for tracking
                if (searchComboBox->isAncestorOf(w) || w == searchComboBox || w == searchComboBox->lineEdit())
                    m_activeInput = searchComboBox->lineEdit();
                else if (timeStart->isAncestorOf(w) || w == timeStart)
                    m_activeInput = timeStart;
                else if (timeEnd->isAncestorOf(w) || w == timeEnd)
                    m_activeInput = timeEnd;
            } else {
                // Clicked somewhere else in the plugin — focus the list
                m_activeInput = nullptr;
                listView->setFocus(Qt::MouseFocusReason);
            }
            return false;
        } else if (m_isActive && gr.contains(gp)) {
            // Already active, click inside — update input tracking
            if (w && isTextInputWidget(w)) {
                if (searchComboBox->isAncestorOf(w) || w == searchComboBox || w == searchComboBox->lineEdit())
                    m_activeInput = searchComboBox->lineEdit();
                else if (timeStart->isAncestorOf(w) || w == timeStart)
                    m_activeInput = timeStart;
                else if (timeEnd->isAncestorOf(w) || w == timeEnd)
                    m_activeInput = timeEnd;
                else
                    m_activeInput = w;
            } else if (w && (w == listView || listView->isAncestorOf(w))) {
                m_activeInput = nullptr;
                listView->setFocus(Qt::MouseFocusReason);
            } else {
                m_activeInput = nullptr;
            }
            return false;
        }
    }

    // ── KeyPress handling (only when plugin is active) ─────────────────
    if (event->type() == QEvent::KeyPress && m_isActive) {
        auto *ke = static_cast<QKeyEvent*>(event);

        // Escape: deactivate text input or deactivate plugin entirely
        if (ke->key() == Qt::Key_Escape) {
            if (m_activeInput) {
                m_activeInput = nullptr;
                listView->setFocus(Qt::OtherFocusReason);
            } else {
                m_isActive = false;
                restoreFocusToDC();
            }
            return true;
        }

        // Ctrl+Q: Toggle preview pane (pass to DC main window)
        if (ke->key() == Qt::Key_Q && (ke->modifiers() & Qt::ControlModifier)) {
            m_isActive = false;
            restoreFocusToDC();
            if (QWidget *top = window()) {
                QKeyEvent *newKe = new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier);
                QCoreApplication::postEvent(top, newKe);
            }
            return true;
        }
        // Enter in search edit: trigger search, return focus to list
        if (m_activeInput == searchComboBox->lineEdit() && (ke->key() == Qt::Key_Return ||
                                             ke->key() == Qt::Key_Enter)) {
            executeSearch(true);
            restoreFocusToDC();
            return true;
        }

        // Shortcuts that only work when NOT in a text input
        if (!m_activeInput) {
            // Ctrl+C: copy selected lines
            if (ke->matches(QKeySequence::Copy)) {
                copySelectedLines();
                return true;
            }

            // Delete: delete selected lines
            if (ke->key() == Qt::Key_Delete) {
                deleteSelectedLines();
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

// ─── File loading ──────────────────────────────────────────────────────

void LogViewerWidget::loadFile(const QString& filePath) {
    // Save whichever DC widget currently has focus to restore it later.
    // Crucial: Only save it if the focused widget is outside our plugin!
    // Otherwise, a reload triggered while we have focus would trap focus inside us.
    if (QWidget *fw = QApplication::focusWidget()) {
        if (fw != this && !this->isAncestorOf(fw)) {
            m_savedFocusWidget = fw;
        }
    }

    currentFile = filePath;
    m_lastMatchRow = -1;
    m_lastSearchQuery.clear();
    m_activeInput = nullptr;
    m_isActive = false; // Ensure we start inactive
    statusLabel->setText(QString("Loading %1...").arg(filePath));
    model->loadFile(filePath);
    statusLabel->setText(QString("Lines: %1 | %2")
        .arg(model->lineCount()).arg(filePath));

    // Restore focus to DC after loading completes
    QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
}

// ─── External search trigger (from ListSearchText) ─────────────────────

void LogViewerWidget::triggerSearch(const QString& searchString, int) {
    searchComboBox->lineEdit()->setText(searchString);
    executeSearch(true);
}

// ─── Search ────────────────────────────────────────────────────────────

void LogViewerWidget::executeSearch(bool jumpToNext) {
    const QString query = searchComboBox->currentText();

    if (query != m_lastSearchQuery) {
        m_lastMatchRow = -1;
        m_lastSearchQuery = query;

        if (query.isEmpty()) {
            btnSearchStop->setEnabled(false);
            model->startSearch("");
            return;
        }

        // Add to history
        int existingIndex = searchComboBox->findText(query);
        if (existingIndex >= 0) {
            searchComboBox->removeItem(existingIndex);
        }
        searchComboBox->insertItem(0, query);
        searchComboBox->setCurrentIndex(0);

        btnSearchStop->setEnabled(true);
        statusLabel->setText("Searching...");
        model->startSearch(query);
        return;
    }

    if (query.isEmpty() || !jumpToNext) return;

    // Add to history if we select from dropdown and click filter/search
    int existingIndex = searchComboBox->findText(query);
    if (existingIndex > 0) {
        searchComboBox->removeItem(existingIndex);
        searchComboBox->insertItem(0, query);
        searchComboBox->setCurrentIndex(0);
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
    QDateTime end   = timeEnd->dateTime().addMSecs(-1);

    if (start.isValid() && end.isValid() && start < end) {
        filterProxy->setTimeRange(start, end);
        filterProxy->setTimeFilterActive(true);
        statusLabel->setText(QString("Time filter: %1 — %2")
            .arg(start.toString("yyyy-MM-dd HH:mm:ss"))
            .arg(timeEnd->dateTime().toString("yyyy-MM-dd HH:mm:ss")));
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

// ─── Delete selected lines ─────────────────────────────────────────────

void LogViewerWidget::deleteSelectedLines() {
    QModelIndexList selected = listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    // Map proxy indices to source indices
    std::vector<int> sourceRows;
    for (const QModelIndex &idx : selected) {
        QModelIndex srcIdx = filterProxy->mapToSource(idx);
        if (srcIdx.isValid())
            sourceRows.push_back(srcIdx.row());
    }

    if (sourceRows.empty()) return;

    int count = static_cast<int>(sourceRows.size());
    model->deleteRows(sourceRows);
    statusLabel->setText(QString("Deleted %1 line(s)").arg(count));
}

// ─── Clean log file ────────────────────────────────────────────────────

void LogViewerWidget::clearLogFile() {
    if (currentFile.isEmpty()) return;

    auto reply = QMessageBox::question(this, "Clean Log",
        QString("This will delete all contents of:\n%1\n\nThe file will be kept but emptied. Continue?")
            .arg(currentFile),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    model->clearFile();
    statusLabel->setText(QString("Log cleaned: %1").arg(currentFile));
}

// ─── Extract selected lines ────────────────────────────────────────────

void LogViewerWidget::extractSelectedLines() {
    QModelIndexList selected = listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        statusLabel->setText("No lines selected for extraction");
        return;
    }

    std::sort(selected.begin(), selected.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() < b.row();
              });

    QStringList lines;
    for (const QModelIndex &idx : selected)
        lines << idx.data(Qt::DisplayRole).toString();

    // Suggest the same extension as the current log file
    QFileInfo fi(currentFile);
    QString suffix = fi.suffix();
    QString filter;
    if (!suffix.isEmpty())
        filter = QString("%1 files (*.%2);;All files (*)").arg(suffix.toUpper()).arg(suffix);
    else
        filter = "Log files (*.log);;All files (*)";

    // Default to the same directory as the original file
    QString defaultPath = fi.absolutePath() + "/extracted." + (suffix.isEmpty() ? "log" : suffix);

    QString savePath = QFileDialog::getSaveFileName(this, "Extract Lines",
                                                     defaultPath, filter);
    if (savePath.isEmpty()) return;

    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(lines.join('\n').toUtf8());
        file.write("\n");
        file.close();
        statusLabel->setText(QString("Extracted %1 line(s) to %2").arg(lines.size()).arg(savePath));
    } else {
        QMessageBox::warning(this, "Extract Error",
            QString("Could not write to:\n%1").arg(savePath));
    }
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
        path = QDir::homePath() + "/.config/doublecmd/logview.ini";
    }

    QSettings settings(path, QSettings::IniFormat);
    bool sectionExists = settings.contains("HighlightRules/Count");

    if (!sectionExists) {
        struct DefaultRule {
            QString pat;
            QString fg;
            QString bg;
        } defaults[] = {
            { "(?i).*TRACE.*", "#9CA3AF", "#000000" },
            { "(?i).*DEBUG.*", "#60A5FA", "#000000" },
            { "(?i).*INFO.*", "#4ADE80", "#000000" },
            { "(?i).*WARN.*", "#FBBF24", "#000000" },
            { "(?i).*ERROR.*", "#F87171", "#000000" },
            { "(?i).*FATAL.*", "#C084FC", "#000000" }
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
        path = QDir::homePath() + "/.config/doublecmd/logview.ini";
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

