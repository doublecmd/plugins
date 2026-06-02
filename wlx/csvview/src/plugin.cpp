#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTimer>
#include <QMenu>
#include <QUndoStack>
#include <QUndoCommand>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QTextOption>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QPrinter>
#include <QPrintDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>

#include <string.h>
#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>
#include <glib.h>
#include <algorithm>

#include "wlxplugin.h"
#include "enca.h"

#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"

bool gQuoted = true;
bool gGrid = true;
bool gResize = true;
bool gEnca = true;
bool gReadAll = false;
QString gLang = "ru";

QStringList parse_line(const QByteArray &line, const char *encoding, char separator, QList<bool> *wasQuotedOut = nullptr)
{
	QStringList list;
	QByteArray utf8Line;

	if (encoding[0] != '\0')
	{
		gsize len;
		gchar *converted = g_convert_with_fallback(line.data(), line.size(), "UTF-8", encoding, NULL, NULL, &len, NULL);

		if (converted)
		{
			utf8Line = QByteArray(converted, len);
			g_free(converted);
		}
		else
			utf8Line = line;
	}
	else
		utf8Line = line;

	QString text = QString::fromUtf8(utf8Line);
	
	if (text.endsWith("\r\n"))
		text.chop(2);
	else if (text.endsWith("\n"))
		text.chop(1);

	QStringList rawlist = text.split(QLatin1Char(separator));
	QString temp;

	for (int c = 0; c < rawlist.size(); c++)
	{
		if (gQuoted)
		{
			if (rawlist.at(c).startsWith('"') && !rawlist.at(c).endsWith('"'))
			{
				temp = rawlist.at(c);

				if (c < rawlist.size() - 1)
				{
					for (int x = c + 1; x < rawlist.size(); x++)
					{
						const QString nitm = rawlist.at(x);

						if (!nitm.isEmpty() && nitm.back() == '"')
						{
							temp = rawlist.mid(c, x - c + 1).join(QLatin1Char(separator)).remove(0, 1).remove(-1, 1);

							if (temp.count(QLatin1Char('"')) % 2 == 0)
							{
								c = x;
								break;
							}
						}
					}
				}

				list.append(temp);
				if (wasQuotedOut) wasQuotedOut->append(true);
			}
			else
			{
				QString val = rawlist.at(c).trimmed();
				bool quoted = (val.size() >= 2 && val.startsWith('"') && val.endsWith('"'));
				if (quoted)
					val = val.mid(1, val.size() - 2);
				list.append(val);
				if (wasQuotedOut) wasQuotedOut->append(quoted);
			}

			list.last().replace("\"\"", "\"");
		}
	}

	return list;
}

static const int WasQuotedRole = Qt::UserRole + 2;

// Custom delegate that wraps text at any character (not just word boundaries)
class WrapAnywhereDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		// Let the default style draw everything (background, selection, focus rect)
		// but without the text
		QString text = opt.text;
		opt.text.clear();
		QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

		// Now draw the text ourselves with WrapAnywhere
		if (!text.isEmpty()) {
			painter->save();
			QRect textRect = QApplication::style()->subElementRect(QStyle::SE_ItemViewItemText, &opt);
			painter->setClipRect(textRect);
			painter->setFont(opt.font);

			QTextOption textOption;
			textOption.setWrapMode(m_wrap ? QTextOption::WrapAnywhere : QTextOption::NoWrap);
			textOption.setAlignment(opt.displayAlignment);

			// Use the right color depending on selection state
			if (opt.state & QStyle::State_Selected)
				painter->setPen(opt.palette.color(QPalette::HighlightedText));
			else
				painter->setPen(opt.palette.color(QPalette::Text));

			painter->drawText(textRect, text, textOption);
			painter->restore();
		}
	}
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
		if (!m_wrap) return QStyledItemDelegate::sizeHint(option, index);
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		QRect textRect = QApplication::style()->subElementRect(QStyle::SE_ItemViewItemText, &opt);
		int width = textRect.width();
		if (width <= 0) width = opt.rect.width();

		QTextDocument doc;
		doc.setDefaultFont(opt.font);
		QTextOption textOption;
		textOption.setWrapMode(QTextOption::WrapAnywhere);
		doc.setDefaultTextOption(textOption);
		doc.setTextWidth(width);
		doc.setPlainText(opt.text);
		return QSize(width, qMax((int)doc.size().height(), opt.fontMetrics.height()));
	}
	void setWrapAnywhere(bool wrap) { m_wrap = wrap; }
	bool wrapAnywhere() const { return m_wrap; }
private:
	bool m_wrap = false;
};

class EditCellCommand : public QUndoCommand {
public:
	EditCellCommand(QTableWidget *view, int row, int col, const QString &oldText, const QString &newText, QUndoCommand *parent = nullptr)
		: QUndoCommand(parent), m_view(view), m_row(row), m_col(col), m_oldText(oldText), m_newText(newText) {
		setText(QString("Edit cell (%1, %2)").arg(row).arg(col));
	}
	void undo() override {
		m_view->blockSignals(true);
		if (QTableWidgetItem *item = m_view->item(m_row, m_col)) item->setText(m_oldText);
		m_view->blockSignals(false);
	}
	void redo() override {
		m_view->blockSignals(true);
		if (QTableWidgetItem *item = m_view->item(m_row, m_col)) item->setText(m_newText);
		m_view->blockSignals(false);
	}
private:
	QTableWidget *m_view;
	int m_row, m_col;
	QString m_oldText, m_newText;
};

class RowColCommand : public QUndoCommand {
protected:
	QTableWidget *m_view;
	int m_index, m_count;
	QList<QStringList> m_data;
	bool m_isRow, m_isInsert;
public:
	RowColCommand(QTableWidget *view, int index, int count, bool isRow, bool isInsert, QUndoCommand *parent = nullptr)
		: QUndoCommand(parent), m_view(view), m_index(index), m_count(count), m_isRow(isRow), m_isInsert(isInsert) {
		setText(QString("%1 %2 %3(s)").arg(isInsert ? "Insert" : "Delete").arg(count).arg(isRow ? "row" : "col"));
		if (!isInsert) {
			for (int i = 0; i < count; ++i) {
				QStringList list;
				int limit = isRow ? view->columnCount() : view->rowCount();
				for (int j = 0; j < limit; ++j) {
					QTableWidgetItem *item = isRow ? view->item(index + i, j) : view->item(j, index + i);
					list << (item ? item->text() : "");
				}
				m_data << list;
			}
		} else {
			for (int i = 0; i < count; ++i) {
				QStringList list;
				int limit = isRow ? view->columnCount() : view->rowCount();
				for (int j = 0; j < limit; ++j) list << "";
				m_data << list;
			}
		}
	}
	void applyInsert() {
		m_view->blockSignals(true);
		for (int i = 0; i < m_count; ++i) {
			if (m_isRow) m_view->insertRow(m_index + i);
			else m_view->insertColumn(m_index + i);
			
			QStringList list = i < m_data.size() ? m_data[i] : QStringList();
			int limit = m_isRow ? m_view->columnCount() : m_view->rowCount();
			for (int j = 0; j < limit; ++j) {
				QString text = j < list.size() ? list[j] : "";
				QTableWidgetItem *item = new QTableWidgetItem(text);
				item->setToolTip(text);
				item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
				if (m_isRow) m_view->setItem(m_index + i, j, item);
				else m_view->setItem(j, m_index + i, item);
			}
		}
		m_view->blockSignals(false);
	}
	void applyDelete() {
		m_view->blockSignals(true);
		for (int i = m_count - 1; i >= 0; --i) {
			if (m_isRow) m_view->removeRow(m_index + i);
			else m_view->removeColumn(m_index + i);
		}
		m_view->blockSignals(false);
	}
	void undo() override { if (m_isInsert) applyDelete(); else applyInsert(); }
	void redo() override { if (m_isInsert) applyInsert(); else applyDelete(); }
};

// Undo command that stores a full data snapshot (used for sort)
class DataSnapshotCommand : public QUndoCommand {
public:
	DataSnapshotCommand(QTableWidget *view, const QList<QStringList> &before, const QList<QStringList> &after, const QString &text)
		: m_view(view), m_before(before), m_after(after), m_first(true) { setText(text); }
	void undo() override { restore(m_before); }
	void redo() override { if (m_first) { m_first = false; return; } restore(m_after); }
private:
	void restore(const QList<QStringList> &data) {
		m_view->blockSignals(true);
		for (int r = 0; r < data.size() && r < m_view->rowCount(); ++r)
			for (int c = 0; c < data[r].size() && c < m_view->columnCount(); ++c)
				if (QTableWidgetItem *item = m_view->item(r, c)) item->setText(data[r][c]);
		m_view->blockSignals(false);
	}
	QTableWidget *m_view;
	QList<QStringList> m_before, m_after;
	bool m_first;
};

// Undo command for section (row/column) moves - saves full visual order
class SectionMoveCommand : public QUndoCommand {
public:
	SectionMoveCommand(QHeaderView *header, const QList<int> &beforeOrder, const QList<int> &afterOrder, const QString &text)
		: m_header(header), m_before(beforeOrder), m_after(afterOrder), m_first(true) { setText(text); }
	void undo() override { restore(m_before); }
	void redo() override { if (m_first) { m_first = false; return; } restore(m_after); }
private:
	void restore(const QList<int> &order) {
		for (int target = 0; target < order.size(); ++target) {
			int logical = order[target];
			int currentVisual = m_header->visualIndex(logical);
			if (currentVisual != target)
				m_header->moveSection(currentVisual, target);
		}
	}
	QHeaderView *m_header;
	QList<int> m_before, m_after;
	bool m_first;
};

class CsvViewerWidget : public QWidget
{
public:
	explicit CsvViewerWidget(QWidget *parent = nullptr);
	~CsvViewerWidget();

	bool loadFile(const QString& filePath);
	void saveFile(const QString& filePath);

	QTableWidget* view() const { return m_view; }

	void copySelection(char separator);
	QString getSelectionAsText(char separator);
	void pasteSelection();
	void pasteSelectionAt(int atRow);
	void insertEmptyRows(int count, int atRow);
	void deleteSelection();
	
	void copyColumnSelection(char separator);
	void pasteColumnSelectionAt(int atCol);
	void insertEmptyColumns(int count, int atCol);
	void deleteColumnSelection();

	void setActive(bool active);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
	void onItemChanged(QTableWidgetItem *item);
	void onUndoStackCleanChanged(bool clean);

private:
	void onSave();
	void onSaveAs();
	void onReload();
	void onToggleTextMode(bool checked);
	void onToggleWordWrap(bool checked);
	void updateTextView();
	void showContextMenu(const QPoint &pos);
	void showColumnContextMenu(const QPoint &pos);
	void onSortByColumn(int column);

	void installFocusGuard();
	void restoreViewFocus();
	bool isInputWidget(QWidget *w) const;
	bool isSectionSelected(QHeaderView *header, int logicalIndex) const;
	void restoreFocusToDC();
	void updateRowNumbers();

	QTableWidget *m_view;
	QToolBar *m_toolbar;
	QString m_currentFile;
	
	char m_separator;
	char m_encoding[256];
	bool m_firstLineAsHeader;
	
	QPointer<QWidget> m_savedFocusWidget;
	QPointer<QWidget> m_activeInput;
	
	QUndoStack *m_undoStack;
	QLabel *m_dirtyIndicator;
	bool m_isProgrammaticChange;

	QStackedWidget *m_stackedWidget;
	QTextBrowser *m_textBrowser;
	WrapAnywhereDelegate *m_wrapDelegate;
	QAction *m_actTextMode;
	QAction *m_actWordWrap;

	int m_lastSortColumn;
	Qt::SortOrder m_lastSortOrder;

	// Drag-to-move state
	QHeaderView *m_dragHeader;
	int m_dragLogicalIndex;
	QList<int> m_dragBeforeOrder;
	QSet<int> m_dragSelectedSections;  // Saved before Qt clears selection
	bool m_isDraggingSection;
	QTimer *m_moveDebounceTimer;
	bool m_isActive;

	// Find/Replace Panel & Actions
	QWidget *m_findReplacePanel;
	QLineEdit *m_txtFind;
	QLineEdit *m_txtReplace;
	QCheckBox *m_chkMatchCase;
	QCheckBox *m_chkMatchEntire;
	QCheckBox *m_chkRegex;
	QComboBox *m_comboScope;
	QLabel *m_lblStatus;
	QAction *m_actFindReplace;

	void showFindReplacePanel(bool show);
	void doFind(bool forward);
	void doReplace();
	void doReplaceAll();
	bool cellMatches(int row, int col, const QString &query, bool matchCase, bool entireCell, bool regexFlag);
};

CsvViewerWidget::CsvViewerWidget(QWidget *parent)
	: QWidget(parent), m_savedFocusWidget(nullptr), m_activeInput(nullptr), m_separator(','), m_firstLineAsHeader(true), m_isProgrammaticChange(false),
	  m_lastSortColumn(-1), m_lastSortOrder(Qt::AscendingOrder), m_dragHeader(nullptr), m_dragLogicalIndex(-1), m_isDraggingSection(false), m_isActive(false),
	  m_findReplacePanel(nullptr), m_txtFind(nullptr), m_txtReplace(nullptr), m_chkMatchCase(nullptr), m_chkMatchEntire(nullptr), m_chkRegex(nullptr),
	  m_comboScope(nullptr), m_lblStatus(nullptr), m_actFindReplace(nullptr)
{
	memset(m_encoding, 0, sizeof(m_encoding));

	setFocusPolicy(Qt::NoFocus);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_toolbar = new QToolBar(this);
	m_toolbar->setFocusPolicy(Qt::NoFocus);
	
	m_undoStack = new QUndoStack(this);

	QAction *actUndo = new QAction("↶ Undo", this);
	actUndo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
	actUndo->setToolTip("Undo (Ctrl+Z)");
	actUndo->setEnabled(false);
	QObject::connect(actUndo, &QAction::triggered, m_undoStack, &QUndoStack::undo);
	QObject::connect(m_undoStack, &QUndoStack::canUndoChanged, actUndo, &QAction::setEnabled);
	
	QAction *actRedo = new QAction("↷ Redo", this);
	actRedo->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
	actRedo->setToolTip("Redo (Ctrl+Y)");
	actRedo->setEnabled(false);
	QObject::connect(actRedo, &QAction::triggered, m_undoStack, &QUndoStack::redo);
	QObject::connect(m_undoStack, &QUndoStack::canRedoChanged, actRedo, &QAction::setEnabled);

	m_dirtyIndicator = new QLabel("✓", this);
	m_dirtyIndicator->setContentsMargins(4, 0, 4, 0);
	m_toolbar->addWidget(m_dirtyIndicator);
	
	QAction *actSave = m_toolbar->addAction("🖫 Save");
	actSave->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
	actSave->setToolTip("Save (Ctrl+S)");
	
	QAction *actSaveAs = m_toolbar->addAction("🖪 Save As...");
	actSaveAs->setToolTip("Save As...");
	
	m_toolbar->addAction(actUndo);
	m_toolbar->addAction(actRedo);

	QAction *actPrint = m_toolbar->addAction(QString::fromUtf8("\xf0\x9f\x96\xa8\xef\xb8\x8e Print"));
	actPrint->setToolTip("Print (Ctrl+P)");
	actPrint->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
	
	QAction *actReload = m_toolbar->addAction("⟳ Reload");
	actReload->setToolTip("Reload file from disk");

	QAction *actHeader = m_toolbar->addAction("\xf0\x9f\x96\x88 Header Row");
	actHeader->setCheckable(true);
	actHeader->setChecked(true);
	actHeader->setToolTip("Toggle First Line As Header");

	m_actFindReplace = m_toolbar->addAction(QString::fromUtf8("\xf0\x9f\x94\x8d\xef\xb8\x8e Find/Replace"));
	m_actFindReplace->setToolTip("Find and Replace (Ctrl+F / Ctrl+R)");
	m_actFindReplace->setCheckable(true);

	m_actTextMode = m_toolbar->addAction(QString::fromUtf8("\xf0\x9f\x91\x81\xef\xb8\x8e Show Text"));
	m_actTextMode->setCheckable(true);
	m_actTextMode->setToolTip("Toggle text view mode");

	m_actWordWrap = m_toolbar->addAction(QString::fromUtf8("\xe2\x86\xa9\xef\xb8\x8e Line Wrap"));
	m_actWordWrap->setCheckable(true);
	m_actWordWrap->setToolTip("Toggle line wrap");

	QAction *actEditor = m_toolbar->addAction(QString::fromUtf8("\xe2\x86\x97\xef\xb8\x8e Open Externally"));
	actEditor->setToolTip("Open in default system application");

	addAction(actSave);

	layout->addWidget(m_toolbar);

	m_stackedWidget = new QStackedWidget(this);

	m_view = new QTableWidget(this);
	m_view->setFocusPolicy(Qt::ClickFocus);
	
	m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_view->horizontalHeader()->setSectionsClickable(true);
	m_view->horizontalHeader()->setHighlightSections(true);
	m_view->horizontalHeader()->setSectionsMovable(false); // We handle drag-to-select ourselves
	m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
	
	m_view->verticalHeader()->setSectionsClickable(true);
	m_view->verticalHeader()->setHighlightSections(true);
	m_view->verticalHeader()->setSectionsMovable(false); // We handle drag-to-select ourselves
	m_view->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

	m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);

	m_view->setSortingEnabled(false);

	// Install the custom delegate for character-level wrapping
	m_wrapDelegate = new WrapAnywhereDelegate(m_view);
	m_view->setItemDelegate(m_wrapDelegate);

	m_view->setContextMenuPolicy(Qt::CustomContextMenu);

	m_textBrowser = new QTextBrowser(this);
	m_textBrowser->setOpenLinks(false);
	m_textBrowser->setReadOnly(true);

	m_stackedWidget->addWidget(m_view);
	m_stackedWidget->addWidget(m_textBrowser);
	layout->addWidget(m_stackedWidget);

	// Initialize Find/Replace panel
	m_findReplacePanel = new QWidget(this);
	m_findReplacePanel->setVisible(false);
	m_findReplacePanel->setStyleSheet("QWidget { background-color: palette(window); border-top: 1px solid palette(mid); }");

	QVBoxLayout *panelLayout = new QVBoxLayout(m_findReplacePanel);
	panelLayout->setContentsMargins(6, 6, 6, 6);
	panelLayout->setSpacing(6);

	QHBoxLayout *row1 = new QHBoxLayout();
	row1->setSpacing(6);
	QHBoxLayout *row2 = new QHBoxLayout();
	row2->setSpacing(6);

	QLabel *lblFind = new QLabel("Find:", m_findReplacePanel);
	m_txtFind = new QLineEdit(m_findReplacePanel);
	m_txtFind->setPlaceholderText("Search query...");

	QLabel *lblReplace = new QLabel("Replace:", m_findReplacePanel);
	m_txtReplace = new QLineEdit(m_findReplacePanel);
	m_txtReplace->setPlaceholderText("Replacement text...");

	m_chkMatchCase = new QCheckBox("Match Case", m_findReplacePanel);
	m_chkMatchEntire = new QCheckBox("Match Entire Cell", m_findReplacePanel);
	m_chkRegex = new QCheckBox("Regular Expression", m_findReplacePanel);

	m_chkMatchCase->setFocusPolicy(Qt::NoFocus);
	m_chkMatchEntire->setFocusPolicy(Qt::NoFocus);
	m_chkRegex->setFocusPolicy(Qt::NoFocus);

	row1->addWidget(lblFind);
	row1->addWidget(m_txtFind, 1);
	row1->addWidget(lblReplace);
	row1->addWidget(m_txtReplace, 1);

	QLabel *lblScope = new QLabel("Scope:", m_findReplacePanel);
	m_comboScope = new QComboBox(m_findReplacePanel);
	m_comboScope->addItems({"All Cells", "Selected Cells", "Current Column", "Current Row"});
	m_comboScope->setFocusPolicy(Qt::NoFocus);

	QPushButton *btnFindPrev = new QPushButton("Find Previous", m_findReplacePanel);
	QPushButton *btnFindNext = new QPushButton("Find Next", m_findReplacePanel);
	QPushButton *btnReplace = new QPushButton("Replace", m_findReplacePanel);
	QPushButton *btnReplaceAll = new QPushButton("Replace All", m_findReplacePanel);

	btnFindPrev->setFocusPolicy(Qt::NoFocus);
	btnFindNext->setFocusPolicy(Qt::NoFocus);
	btnReplace->setFocusPolicy(Qt::NoFocus);
	btnReplaceAll->setFocusPolicy(Qt::NoFocus);

	m_lblStatus = new QLabel(m_findReplacePanel);
	m_lblStatus->setStyleSheet("color: palette(link); font-weight: bold;");

	QPushButton *btnClose = new QPushButton("✕", m_findReplacePanel);
	btnClose->setFixedWidth(30);
	btnClose->setFlat(true);
	btnClose->setFocusPolicy(Qt::NoFocus);

	row2->addWidget(lblScope);
	row2->addWidget(m_comboScope);
	row2->addWidget(m_chkMatchCase);
	row2->addWidget(m_chkMatchEntire);
	row2->addWidget(m_chkRegex);
	row2->addWidget(btnFindPrev);
	row2->addWidget(btnFindNext);
	row2->addWidget(btnReplace);
	row2->addWidget(btnReplaceAll);
	row2->addWidget(m_lblStatus, 1);
	row2->addWidget(btnClose);

	panelLayout->addLayout(row1);
	panelLayout->addLayout(row2);

	layout->addWidget(m_findReplacePanel);

	// Connect Find/Replace signals
	QObject::connect(m_actFindReplace, &QAction::toggled, this, [this](bool checked) {
		showFindReplacePanel(checked);
	});
	QObject::connect(btnFindNext, &QPushButton::clicked, this, [this]() { doFind(true); });
	QObject::connect(btnFindPrev, &QPushButton::clicked, this, [this]() { doFind(false); });
	QObject::connect(btnReplace, &QPushButton::clicked, this, &CsvViewerWidget::doReplace);
	QObject::connect(btnReplaceAll, &QPushButton::clicked, this, &CsvViewerWidget::doReplaceAll);
	QObject::connect(btnClose, &QPushButton::clicked, this, [this]() { showFindReplacePanel(false); });

	QObject::connect(m_txtFind, &QLineEdit::returnPressed, this, [this]() { doFind(true); });
	QObject::connect(m_txtReplace, &QLineEdit::returnPressed, this, &CsvViewerWidget::doReplace);

	QObject::connect(actSave, &QAction::triggered, this, [this]() { onSave(); });
	QObject::connect(actSaveAs, &QAction::triggered, this, [this]() { onSaveAs(); });
	QObject::connect(actReload, &QAction::triggered, this, [this]() { onReload(); });
	QObject::connect(actHeader, &QAction::toggled, this, [this](bool checked) {
		m_firstLineAsHeader = checked;
		onReload();
	});
	QObject::connect(actEditor, &QAction::triggered, this, [this]() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentFile));
	});
	QObject::connect(actPrint, &QAction::triggered, this, [this]() {
		QPrinter printer(QPrinter::HighResolution);
		QPrintDialog dlg(&printer, this);
		if (dlg.exec() != QDialog::Accepted) return;

		int rows = m_view->rowCount();
		int cols = m_view->columnCount();
		QString html = "<table border='1' cellspacing='0' cellpadding='4' style='border-collapse:collapse;'>";
		if (m_firstLineAsHeader) {
			html += "<tr>";
			for (int vc = 0; vc < cols; ++vc) {
				int c = m_view->horizontalHeader()->logicalIndex(vc);
				QString text = m_view->horizontalHeaderItem(c) ? m_view->horizontalHeaderItem(c)->text().toHtmlEscaped() : "";
				html += QString("<th style='background:#eee;'>%1</th>").arg(text);
			}
			html += "</tr>";
		}
		for (int vr = 0; vr < rows; ++vr) {
			int r = m_view->verticalHeader()->logicalIndex(vr);
			html += "<tr>";
			for (int vc = 0; vc < cols; ++vc) {
				int c = m_view->horizontalHeader()->logicalIndex(vc);
				QString text = m_view->item(r, c) ? m_view->item(r, c)->text().toHtmlEscaped() : "";
				html += QString("<td>%1</td>").arg(text);
			}
			html += "</tr>";
		}
		html += "</table>";

		QTextDocument doc;
		doc.setHtml(html);
		doc.print(&printer);
	});
	QObject::connect(m_actTextMode, &QAction::toggled, this, &CsvViewerWidget::onToggleTextMode);
	QObject::connect(m_actWordWrap, &QAction::toggled, this, &CsvViewerWidget::onToggleWordWrap);

	QObject::connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });
	QObject::connect(m_view->verticalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });
	QObject::connect(m_view->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showColumnContextMenu(pos); });

	// Sort by clicking column header
	QObject::connect(m_view->horizontalHeader(), &QHeaderView::sectionClicked, this, &CsvViewerWidget::onSortByColumn);

	QObject::connect(m_undoStack, &QUndoStack::cleanChanged, this, &CsvViewerWidget::onUndoStackCleanChanged);
	QObject::connect(m_undoStack, &QUndoStack::indexChanged, this, [this]() { updateRowNumbers(); });
	QObject::connect(m_view, &QTableWidget::itemChanged, this, &CsvViewerWidget::onItemChanged);

	// Focus management: detect when focus leaves or enters our widget hierarchy
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
		bool oldInside = old && (old == this || this->isAncestorOf(old));
		bool nowInside = now && (now == this || this->isAncestorOf(now));

		if (m_isActive) {
			if (oldInside && !nowInside) {
				// Focus left our plugin
				setActive(false);
			}
		} else {
			if (nowInside && !oldInside) {
				// Focus is entering the plugin programmatically while inactive.
				// Restore focus to the widget that had it (old), or fallback to DC.
				if (old) {
					QPointer<QWidget> pOld(old);
					QTimer::singleShot(0, this, [this, pOld]() {
						if (pOld) {
							QWidget *currentFocus = QApplication::focusWidget();
							if (currentFocus && (currentFocus == this || this->isAncestorOf(currentFocus))) {
								pOld->setFocus(Qt::OtherFocusReason);
							}
						}
					});
				} else {
					QTimer::singleShot(0, this, [this]() {
						QWidget *currentFocus = QApplication::focusWidget();
						if (currentFocus && (currentFocus == this || this->isAncestorOf(currentFocus))) {
							restoreFocusToDC();
						}
					});
				}
			}
		}
	});

	// Debounce timer for section moves (sectionMoved fires many times during drag)
	m_moveDebounceTimer = new QTimer(this);
	m_moveDebounceTimer->setSingleShot(true);
	m_moveDebounceTimer->setInterval(0);
	QObject::connect(m_moveDebounceTimer, &QTimer::timeout, this, [this]() {
		if (!m_isDraggingSection || !m_dragHeader) return;

		int newVisual = m_dragHeader->visualIndex(m_dragLogicalIndex);
		bool anyMoved = (newVisual != m_dragBeforeOrder.indexOf(m_dragLogicalIndex));

		if (anyMoved) {
			bool isHorizontal = (m_dragHeader == m_view->horizontalHeader());

			// Current visual order (after Qt moved the dragged section)
			QList<int> currentOrder;
			for (int v = 0; v < m_dragHeader->count(); ++v)
				currentOrder.append(m_dragHeader->logicalIndex(v));

			// Non-selected in current visual order
			QList<int> nonSelected;
			for (int li : currentOrder) {
				if (!m_dragSelectedSections.contains(li))
					nonSelected.append(li);
			}

			// Selected in original visual order (preserving relative order)
			QList<int> selectedInOrder;
			for (int li : m_dragBeforeOrder) {
				if (m_dragSelectedSections.contains(li))
					selectedInOrder.append(li);
			}

			// First selected item goes to the drop position
			int insertIdx = qBound(0, newVisual, nonSelected.size());

			// Build target order: nonSelected with selectedInOrder inserted at insertIdx
			QList<int> targetOrder;
			for (int i = 0; i < insertIdx; ++i)
				targetOrder.append(nonSelected[i]);
			for (int li : selectedInOrder)
				targetOrder.append(li);
			for (int i = insertIdx; i < nonSelected.size(); ++i)
				targetOrder.append(nonSelected[i]);

			// Apply target order via moveSection
			for (int v = 0; v < targetOrder.size(); ++v) {
				int logical = targetOrder[v];
				int curVisual = m_dragHeader->visualIndex(logical);
				if (curVisual != v)
					m_dragHeader->moveSection(curVisual, v);
			}

			QList<int> afterOrder;
			for (int v = 0; v < m_dragHeader->count(); ++v)
				afterOrder.append(m_dragHeader->logicalIndex(v));
			m_undoStack->push(new SectionMoveCommand(m_dragHeader, m_dragBeforeOrder, afterOrder,
				isHorizontal ? "Move columns" : "Move rows"));
			updateRowNumbers();
		}

		m_isDraggingSection = false;
		m_dragHeader->setSectionsMovable(false);
		m_dragHeader = nullptr;
	});

	// Connect sectionMoved to restart the debounce timer
	auto connectMoveDebounce = [this](QHeaderView *header) {
		QObject::connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) {
			if (m_isDraggingSection)
				m_moveDebounceTimer->start();
		});
	};
	connectMoveDebounce(m_view->horizontalHeader());
	connectMoveDebounce(m_view->verticalHeader());

	installFocusGuard();

	for (QAction *action : m_toolbar->actions()) {
		QWidget *w = m_toolbar->widgetForAction(action);
		if (w) {
			w->setFocusPolicy(Qt::NoFocus);
		}
		QObject::connect(action, &QAction::triggered, this, [this]() {
			QTimer::singleShot(0, this, &CsvViewerWidget::restoreViewFocus);
		});
	}
}

CsvViewerWidget::~CsvViewerWidget()
{
	m_moveDebounceTimer->stop();
	m_view->blockSignals(true);
	m_undoStack->blockSignals(true);
	if (qApp) qApp->removeEventFilter(this);
}

void CsvViewerWidget::installFocusGuard()
{
	if (qApp) qApp->installEventFilter(this);
	setFocusProxy(m_view);
}

void CsvViewerWidget::setActive(bool active)
{
	m_isActive = active;
	if (!active) {
		m_activeInput = nullptr;
		clearFocus();
		if (parentWidget()) {
			parentWidget()->setFocus(Qt::OtherFocusReason);
		}
	}
}

bool CsvViewerWidget::isInputWidget(QWidget *w) const
{
	if (!w) return false;
	if (w != m_view && m_view->isAncestorOf(w)) return true;
	return false;
}

void CsvViewerWidget::restoreFocusToDC()
{
	if (m_savedFocusWidget) {
		m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
	} else {
		if (QWidget *fw = QApplication::focusWidget()) {
			if (fw == this || fw->isAncestorOf(this) || this->isAncestorOf(fw))
				fw->clearFocus();
		}
	}
}

void CsvViewerWidget::restoreViewFocus()
{
	if (m_stackedWidget->currentWidget() == m_view) {
		m_view->setFocus(Qt::OtherFocusReason);
	} else {
		m_textBrowser->setFocus(Qt::OtherFocusReason);
	}
}

void CsvViewerWidget::updateRowNumbers()
{
	QHeaderView *vh = m_view->verticalHeader();
	m_view->blockSignals(true);
	for (int v = 0; v < m_view->rowCount(); ++v) {
		int logical = vh->logicalIndex(v);
		QTableWidgetItem *item = m_view->verticalHeaderItem(logical);
		if (!item) {
			item = new QTableWidgetItem();
			m_view->setVerticalHeaderItem(logical, item);
		}
		item->setText(QString::number(v + 1));
	}
	m_view->blockSignals(false);
}

bool CsvViewerWidget::isSectionSelected(QHeaderView *header, int logicalIndex) const
{
	QItemSelectionModel *sel = m_view->selectionModel();
	if (!sel) return false;
	bool isHorizontal = (header == m_view->horizontalHeader());
	if (isHorizontal) {
		// Check if ALL rows in this column are selected
		for (int r = 0; r < m_view->rowCount(); ++r) {
			if (!sel->isSelected(m_view->model()->index(r, logicalIndex)))
				return false;
		}
		return m_view->rowCount() > 0;
	} else {
		// Check if ALL columns in this row are selected
		for (int c = 0; c < m_view->columnCount(); ++c) {
			if (!sel->isSelected(m_view->model()->index(logicalIndex, c)))
				return false;
		}
		return m_view->columnCount() > 0;
	}
}

void CsvViewerWidget::onUndoStackCleanChanged(bool clean) {
	m_dirtyIndicator->setText(clean ? "✓" : "✱");
}

void CsvViewerWidget::onItemChanged(QTableWidgetItem *item) {
	if (m_isProgrammaticChange) return;
	if (!item) return;

	// Check if we have old text stashed in UserRole
	QVariant oldData = item->data(Qt::UserRole);
	if (!oldData.isValid()) return;

	QString oldText = oldData.toString();
	QString newText = item->text();

	// Clear the stashed value so we don't re-trigger
	m_isProgrammaticChange = true;
	item->setData(Qt::UserRole, QVariant());
	m_isProgrammaticChange = false;

	if (oldText != newText) {
		// Auto-quote if user introduced separator in a previously unquoted cell
		if (!item->data(WasQuotedRole).toBool() && newText.contains(m_separator)) {
			m_isProgrammaticChange = true;
			item->setData(WasQuotedRole, true);
			m_isProgrammaticChange = false;
		}
		m_isProgrammaticChange = true;
		item->setText(oldText); // Revert so the undo command applies the new text
		m_isProgrammaticChange = false;
		m_undoStack->push(new EditCellCommand(m_view, item->row(), item->column(), oldText, newText));
	}
}

void CsvViewerWidget::onToggleTextMode(bool checked) {
	if (checked) {
		// Commit any active cell editor before switching to text view
		if (m_activeInput) {
			QModelIndex current = m_view->currentIndex();
			QAbstractItemDelegate *delegate = m_view->itemDelegateForIndex(current);
			if (delegate)
				delegate->setModelData(m_activeInput, m_view->model(), current);
			m_view->closePersistentEditor(m_view->currentItem());
			m_activeInput = nullptr;
		}
		showFindReplacePanel(false);
		updateTextView();
		m_stackedWidget->setCurrentWidget(m_textBrowser);
	} else {
		m_stackedWidget->setCurrentWidget(m_view);
	}
}

void CsvViewerWidget::onToggleWordWrap(bool checked) {
	// For grid mode: use the custom delegate
	m_wrapDelegate->setWrapAnywhere(checked);
	m_view->setWordWrap(checked);
	if (checked) {
		m_view->resizeRowsToContents();
	} else {
		m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 8);
		m_view->resizeRowsToContents();
	}

	// For text mode
	QTextOption opt;
	opt.setWrapMode(checked ? QTextOption::WrapAnywhere : QTextOption::NoWrap);
	m_textBrowser->document()->setDefaultTextOption(opt);
	m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
}

void CsvViewerWidget::updateTextView() {
	static const char *colors[] = {
		"#9CA3AF", "#60A5FA", "#4ADE80", "#FBBF24",
		"#CE9178", "#F87171", "#F44747", "#C084FC"
	};
	static const int numColors = 8;

	int rows = m_view->rowCount();
	int cols = m_view->columnCount();
	bool useColors = (rows <= 10000);
	QString sepStr = useColors ? QString(QChar(m_separator)).toHtmlEscaped() : QString(QChar(m_separator));

	if (!useColors) {
		// Plain text mode for large files
		QString plain;
		if (m_firstLineAsHeader) {
			for (int vc = 0; vc < cols; ++vc) {
				int c = m_view->horizontalHeader()->logicalIndex(vc);
				if (vc > 0) plain += sepStr;
				QTableWidgetItem *hItem = m_view->horizontalHeaderItem(c);
				QString text = hItem ? hItem->text() : "";
				if (hItem && hItem->data(WasQuotedRole).toBool()) {
					text.replace("\"", "\"\"");
					text = "\"" + text + "\"";
				}
				plain += text;
			}
			plain += "\n";
		}
		for (int vr = 0; vr < rows; ++vr) {
			int r = m_view->verticalHeader()->logicalIndex(vr);
			for (int vc = 0; vc < cols; ++vc) {
				int c = m_view->horizontalHeader()->logicalIndex(vc);
				if (vc > 0) plain += sepStr;
				QTableWidgetItem *item = m_view->item(r, c);
				QString text = item ? item->text() : "";
				if (item && item->data(WasQuotedRole).toBool()) {
					text.replace("\"", "\"\"");
					text = "\"" + text + "\"";
				}
				plain += text;
			}
			plain += "\n";
		}
		m_textBrowser->setPlainText(plain);
		return;
	}

	QString html = "<pre style=\"font-family: monospace;\">";

	if (m_firstLineAsHeader) {
		for (int vc = 0; vc < cols; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			if (vc > 0) html += QString("<span style=\"color:%1;\">%2</span>").arg(colors[vc % numColors]).arg(sepStr);
			QTableWidgetItem *hItem = m_view->horizontalHeaderItem(c);
			QString text = hItem ? hItem->text() : "";
			if (hItem && hItem->data(WasQuotedRole).toBool()) {
				text.replace("\"", "\"\"");
				text = "\"" + text + "\"";
			}
			html += QString("<span style=\"color:%1;\">%2</span>").arg(colors[vc % numColors]).arg(text.toHtmlEscaped());
		}
		html += "\n";
	}

	for (int vr = 0; vr < rows; ++vr) {
		int r = m_view->verticalHeader()->logicalIndex(vr);
		for (int vc = 0; vc < cols; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			if (vc > 0) html += QString("<span style=\"color:%1;\">%2</span>").arg(colors[vc % numColors]).arg(sepStr);
			QTableWidgetItem *item = m_view->item(r, c);
			QString text = item ? item->text() : "";
			if (item && item->data(WasQuotedRole).toBool()) {
				text.replace("\"", "\"\"");
				text = "\"" + text + "\"";
			}
			html += QString("<span style=\"color:%1;\">%2</span>").arg(colors[vc % numColors]).arg(text.toHtmlEscaped());
		}
		html += "\n";
	}
	html += "</pre>";
	m_textBrowser->setHtml(html);
}

void CsvViewerWidget::onSortByColumn(int column) {
	if (column != m_lastSortColumn) {
		// First click on a new column: just remember it, don't sort
		m_lastSortColumn = column;
		m_lastSortOrder = Qt::AscendingOrder;
		m_view->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
		return;
	}

	// Second+ click on same column: sort
	QList<QStringList> beforeData;
	for (int r = 0; r < m_view->rowCount(); ++r) {
		QStringList row;
		for (int c = 0; c < m_view->columnCount(); ++c)
			row << (m_view->item(r, c) ? m_view->item(r, c)->text() : "");
		beforeData << row;
	}

	Qt::SortOrder order = m_lastSortOrder;

	m_view->blockSignals(true);
	m_view->sortItems(column, order);
	m_view->blockSignals(false);

	QList<QStringList> afterData;
	for (int r = 0; r < m_view->rowCount(); ++r) {
		QStringList row;
		for (int c = 0; c < m_view->columnCount(); ++c)
			row << (m_view->item(r, c) ? m_view->item(r, c)->text() : "");
		afterData << row;
	}

	m_undoStack->push(new DataSnapshotCommand(m_view, beforeData, afterData, "Sort"));
	m_view->horizontalHeader()->setSortIndicatorShown(true);
	m_view->horizontalHeader()->setSortIndicator(column, order);

	// Toggle for next click
	m_lastSortOrder = (order == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
}

bool CsvViewerWidget::eventFilter(QObject *obj, QEvent *event)
{
	QWidget *w = qobject_cast<QWidget*>(obj);
	// --- Determine if our plugin has focus ---
	bool pluginHasFocus = m_isActive;

	// --- Geometry-based click detection ---
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent*>(event);
		const QPoint gp = me->globalPosition().toPoint();
		const QRect gr(mapToGlobal(QPoint(0, 0)), size());
		if (m_isActive && !gr.contains(gp)) {
			// Click outside our plugin — deactivate
			setActive(false);
			return false; // let the click through to DC
		} else if (!m_isActive && gr.contains(gp)) {
			// Click inside our plugin — activate
			m_isActive = true;
			if (m_stackedWidget->currentWidget() == m_view)
				m_view->setFocus(Qt::MouseFocusReason);
			else
				m_textBrowser->setFocus(Qt::MouseFocusReason);
		}
	}

	// --- Track FocusIn on our children ---
	if (event->type() == QEvent::FocusIn) {
		if (w && (w == this || this->isAncestorOf(w))) {
			QFocusEvent *fe = static_cast<QFocusEvent*>(event);
			if (!m_isActive && fe->reason() == Qt::OtherFocusReason) {
				// Focus bounce or programmatic focus entry while inactive.
				// Do not activate the plugin, let the event filter propagate,
				// and let QApplication::focusChanged handle focus restoration.
				return false;
			}
			m_isActive = true;
			if (isInputWidget(w)) {
				m_activeInput = w;
				if (QTableWidgetItem *item = m_view->currentItem()) {
					if (!item->data(Qt::UserRole).isValid()) {
						m_isProgrammaticChange = true;
						item->setData(Qt::UserRole, item->text());
						m_isProgrammaticChange = false;
					}
				}
			}
		}
	}

	// --- Top-level key handling (only when plugin is active) ---
	if (event->type() == QEvent::KeyPress && pluginHasFocus) {
		auto *ke = static_cast<QKeyEvent*>(event);
		bool isFindReplaceEvent = m_findReplacePanel && w && (w == m_findReplacePanel || m_findReplacePanel->isAncestorOf(w));

		// Ctrl+F or Ctrl+R: Find/Replace (only in table view)
		if (m_stackedWidget->currentWidget() == m_view) {
			if ((ke->modifiers() & Qt::ControlModifier) && (ke->key() == Qt::Key_F || ke->key() == Qt::Key_R)) {
				showFindReplacePanel(!m_findReplacePanel->isVisible());
				return true;
			}
		}
		// Ctrl+S: Save
		if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_S) {
			onSave();
			return true;
		}
		// Ctrl+Z: Undo (only when not editing and not in Find/Replace inputs)
		if (!m_activeInput && !isFindReplaceEvent && (ke->modifiers() & Qt::ControlModifier) && !(ke->modifiers() & Qt::ShiftModifier) && ke->key() == Qt::Key_Z) {
			if (m_undoStack->canUndo()) {
				m_undoStack->undo();
				return true;
			}
		}
		// Ctrl+Shift+Z: Redo
		if (!m_activeInput && !isFindReplaceEvent && (ke->modifiers() & Qt::ControlModifier) && (ke->modifiers() & Qt::ShiftModifier) && ke->key() == Qt::Key_Z) {
			if (m_undoStack->canRedo()) {
				m_undoStack->redo();
				return true;
			}
		}
		// Ctrl+Y: Redo
		if (!m_activeInput && !isFindReplaceEvent && (ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_Y) {
			if (m_undoStack->canRedo()) {
				m_undoStack->redo();
				return true;
			}
		}
		if (!m_activeInput && m_stackedWidget->currentWidget() == m_view && !isFindReplaceEvent) {
			if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_C) {
				copySelection('\t');
				return true;
			}
			if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_V) {
				pasteSelection();
				return true;
			}
			if (ke->key() == Qt::Key_Delete) {
				deleteSelection();
				return true;
			}
			// Enter on a selected cell: open editor
			if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
				if (m_view->currentItem()) {
					m_view->editItem(m_view->currentItem());
					return true;
				}
			}
			// Arrow key navigation (with right-wrap)
			if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down ||
			    ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) {
				int visualCol = m_view->horizontalHeader()->visualIndex(m_view->currentIndex().column());
				int visualRow = m_view->verticalHeader()->visualIndex(m_view->currentIndex().row());
				if (ke->key() == Qt::Key_Up) visualRow--;
				if (ke->key() == Qt::Key_Down) visualRow++;
				if (ke->key() == Qt::Key_Left) {
					visualCol--;
					if (visualCol < 0 && visualRow > 0) {
						visualCol = m_view->columnCount() - 1;
						visualRow--;
					}
				}
				if (ke->key() == Qt::Key_Right) {
					visualCol++;
					if (visualCol >= m_view->columnCount() && visualRow < m_view->rowCount() - 1) {
						visualCol = 0;
						visualRow++;
					}
				}
				visualRow = qBound(0, visualRow, m_view->rowCount() - 1);
				visualCol = qBound(0, visualCol, m_view->columnCount() - 1);
				int r = m_view->verticalHeader()->logicalIndex(visualRow);
				int c = m_view->horizontalHeader()->logicalIndex(visualCol);
				m_view->setCurrentCell(r, c);
				return true;
			}
		}
	}

	// --- Header mouse events: drag-to-move vs drag-to-select ---
	QHeaderView *hHeader = m_view->horizontalHeader();
	QHeaderView *vHeader = m_view->verticalHeader();
	if (event->type() == QEvent::MouseButtonPress) {
		if (obj == hHeader->viewport() || obj == vHeader->viewport()) {
			QHeaderView *header = (obj == hHeader->viewport()) ? hHeader : vHeader;
			auto *me = static_cast<QMouseEvent*>(event);
			int logicalIndex = header->logicalIndexAt(me->pos());
			if (logicalIndex >= 0 && isSectionSelected(header, logicalIndex)) {
				header->setSectionsMovable(true);
				m_isDraggingSection = true;
				m_dragHeader = header;
				m_dragLogicalIndex = logicalIndex;
				m_dragBeforeOrder.clear();
				for (int v = 0; v < header->count(); ++v)
					m_dragBeforeOrder.append(header->logicalIndex(v));

				// Save selected sections NOW, before Qt clears the selection
				bool isHorizontal = (header == m_view->horizontalHeader());
				m_dragSelectedSections.clear();
				QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
				for (const QModelIndex &idx : sel) {
					int li = isHorizontal ? idx.column() : idx.row();
					m_dragSelectedSections.insert(li);
				}

				// Restore the full selection after Qt's default handler clears it
				QItemSelection savedSel = m_view->selectionModel()->selection();
				QTimer::singleShot(0, this, [this, savedSel]() {
					if (m_isDraggingSection)
						m_view->selectionModel()->select(savedSel, QItemSelectionModel::ClearAndSelect);
				});
			} else {
				header->setSectionsMovable(false);
			}
		}
	}

	// --- Child widget events (header drag handling) ---
	if (w && (w == this || this->isAncestorOf(w))) {
		if (event->type() == QEvent::ChildAdded) {
			auto *ce = static_cast<QChildEvent*>(event);
			if (auto *childWidget = qobject_cast<QWidget*>(ce->child())) {
				if (!isInputWidget(childWidget))
					childWidget->setFocusPolicy(Qt::NoFocus);
			}
		}

		if (event->type() == QEvent::KeyPress && m_stackedWidget->currentWidget() == m_view) {
			auto *ke = static_cast<QKeyEvent*>(event);
			if (m_activeInput && w != m_textBrowser) {
				if (ke->key() == Qt::Key_Escape) {
					if (QTableWidgetItem *item = m_view->currentItem()) {
						QVariant oldData = item->data(Qt::UserRole);
						if (oldData.isValid()) {
							m_isProgrammaticChange = true;
							item->setText(oldData.toString());
							item->setData(Qt::UserRole, QVariant());
							m_isProgrammaticChange = false;
						}
					}
					m_activeInput = nullptr;
					restoreFocusToDC();
					return true;
				}
				if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
					QModelIndex current = m_view->currentIndex();
					int r = current.row(), c = current.column();
					if (ke->key() == Qt::Key_Up) r--;
					if (ke->key() == Qt::Key_Down) r++;
					r = qBound(0, r, m_view->rowCount() - 1);
					
					if (QTableWidgetItem *item = m_view->currentItem()) {
						QVariant oldData = item->data(Qt::UserRole);
						if (oldData.isValid()) {
							m_isProgrammaticChange = true;
							item->setText(oldData.toString());
							item->setData(Qt::UserRole, QVariant());
							m_isProgrammaticChange = false;
						}
					}
					m_view->closePersistentEditor(m_view->currentItem());
					m_activeInput = nullptr;
					
					m_view->setCurrentCell(r, c);
					m_view->editItem(m_view->item(r, c));
					return true;
				}
				// Left/Right arrows: let them pass through to the cell editor for cursor movement
				if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
					QModelIndex current = m_view->currentIndex();
					int r = current.row(), c = current.column();

					// Commit the editor data via the delegate
					QAbstractItemDelegate *delegate = m_view->itemDelegateForIndex(current);
					if (delegate && m_activeInput) {
						delegate->setModelData(m_activeInput, m_view->model(), current);
					}
					m_activeInput = nullptr;

					// Navigate to next cell (right, wrap to next row)
					int visualCol = m_view->horizontalHeader()->visualIndex(c);
					int visualRow = m_view->verticalHeader()->visualIndex(r);
					visualCol++;
					if (visualCol >= m_view->columnCount()) {
						visualCol = 0;
						visualRow++;
					}
					if (visualRow < m_view->rowCount()) {
						int nr = m_view->verticalHeader()->logicalIndex(visualRow);
						int nc = m_view->horizontalHeader()->logicalIndex(visualCol);
						QTimer::singleShot(0, this, [this, nr, nc]() {
							m_view->setCurrentCell(nr, nc);
						});
					}
					return true;
				}
			} else {
				if (ke->key() == Qt::Key_Escape && m_findReplacePanel->isVisible()) {
					showFindReplacePanel(false);
					return true;
				}
			}
		}
	}

	return QWidget::eventFilter(obj, event);
}

bool CsvViewerWidget::loadFile(const QString& filePath)
{
	m_isProgrammaticChange = true;
	QWidget *fw = QApplication::focusWidget();
	if (fw && fw != this && !this->isAncestorOf(fw)) {
		m_savedFocusWidget = fw;
	}
	m_currentFile = filePath;
	m_activeInput = nullptr;

	m_view->clear();
	m_view->setRowCount(0);
	m_view->setColumnCount(0);

	int columns = 0, row = 0;
	QStringList header, list;
	QFile file(filePath);
	QByteArray line;

	if (!file.open(QFile::ReadOnly | QFile::Text))
		return false;

	if (gEnca)
	{
		if (gReadAll)
			line = file.readAll();
		else
			line = file.read(4096);

		EncaAnalyser analyser;
		EncaEncoding encoding;
		analyser = enca_analyser_alloc(gLang.toStdString().c_str());

		if (analyser)
		{
			enca_set_threshold(analyser, 1.38);
			enca_set_multibyte(analyser, 1);
			enca_set_ambiguity(analyser, 1);
			enca_set_garbage_test(analyser, 1);
			enca_set_filtering(analyser, 0);
			encoding = enca_analyse(analyser, (unsigned char*)line.data(), (size_t)line.size());

			if (encoding.charset > 0 && encoding.charset != 27)
				snprintf(m_encoding, sizeof(m_encoding), "%s", enca_charset_name(encoding.charset, ENCA_NAME_STYLE_ICONV));

			enca_analyser_free(analyser);
		}

		file.seek(0);
	}

	line = file.readLine();
	QByteArray seps(",;\t");
	bool detected = false;

	for (int i = 0; i < seps.size(); ++i)
	{
		m_separator = seps.at(i);

		QList<bool> headerQuoted;
		header = parse_line(line, m_encoding, m_separator, &headerQuoted);
		columns = header.size();

		if (columns > 1)
		{
			m_view->setColumnCount(columns);
			if (m_firstLineAsHeader)
			{
				for (int c = 0; c < columns; ++c) {
					QTableWidgetItem *hItem = new QTableWidgetItem(header.at(c).trimmed());
					hItem->setData(WasQuotedRole, c < headerQuoted.size() && headerQuoted[c]);
					m_view->setHorizontalHeaderItem(c, hItem);
				}
			}
			detected = true;
			break;
		}
	}

	if (!detected)
	{
		if (filePath.endsWith(".tsv", Qt::CaseInsensitive))
			m_separator = '\t';
		else
			m_separator = ',';

		QList<bool> headerQuoted;
		header = parse_line(line, m_encoding, m_separator, &headerQuoted);
		columns = header.size();
		m_view->setColumnCount(columns);
		if (m_firstLineAsHeader)
		{
			for (int c = 0; c < columns; ++c) {
				QTableWidgetItem *hItem = new QTableWidgetItem(header.at(c).trimmed());
				hItem->setData(WasQuotedRole, c < headerQuoted.size() && headerQuoted[c]);
				m_view->setHorizontalHeaderItem(c, hItem);
			}
		}
	}

	if (columns < 1)
	{
		m_isProgrammaticChange = false;
		return false;
	}

	// Check for extension/separator mismatch
	bool isTsvExt = filePath.endsWith(".tsv", Qt::CaseInsensitive);
	bool isCsvExt = filePath.endsWith(".csv", Qt::CaseInsensitive);
	if ((isCsvExt && m_separator == '\t') || (isTsvExt && m_separator == ',')) {
		QString msg = isCsvExt
			? "This .csv file appears to use tab separators instead of commas."
			: "This .tsv file appears to use comma separators instead of tabs.";
		QMessageBox box(QMessageBox::Warning, "Separator Mismatch", msg, QMessageBox::NoButton, this);
		QPushButton *btnIgnore = box.addButton("Ignore", QMessageBox::RejectRole);
		QPushButton *btnFixSep = box.addButton("Fix Separator", QMessageBox::AcceptRole);
		QPushButton *btnRename = box.addButton("Rename Extension", QMessageBox::AcceptRole);
		box.exec();

		if (box.clickedButton() == btnFixSep) {
			// Replace separator in the raw file, respecting quoted fields
			char oldSep = m_separator;
			char newSep = isCsvExt ? ',' : '\t';
			m_separator = newSep;

			file.seek(0);
			QByteArray rawData = file.readAll();
			file.close();

			// Walk through bytes, toggle inQuote on '"', replace oldSep with newSep only outside quotes
			bool inQuote = false;
			for (int i = 0; i < rawData.size(); ++i) {
				char ch = rawData[i];
				if (ch == '"') {
					inQuote = !inQuote;
				} else if (!inQuote && ch == oldSep) {
					rawData[i] = newSep;
				}
			}

			QFile outFile(m_currentFile);
			if (outFile.open(QFile::WriteOnly | QFile::Truncate)) {
				outFile.write(rawData);
				outFile.close();
			}
		} else if (box.clickedButton() == btnRename) {
			QString newExt = isCsvExt ? ".tsv" : ".csv";
			QString newPath = filePath;
			int dotPos = newPath.lastIndexOf('.');
			if (dotPos >= 0) newPath = newPath.left(dotPos) + newExt;
			QFile::rename(filePath, newPath);
			m_currentFile = newPath;
		}
		(void)btnIgnore;
	}

	if (!m_firstLineAsHeader)
	{
		QList<bool> headerQuoted;
		header = parse_line(line, m_encoding, m_separator, &headerQuoted);
		m_view->insertRow(row);
		for (int c = 0; c < header.size(); ++c)
		{
			QTableWidgetItem *item = new QTableWidgetItem(header.at(c).trimmed());
			item->setToolTip(header.at(c).trimmed());
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
			item->setData(WasQuotedRole, c < headerQuoted.size() && headerQuoted[c]);
			m_view->setItem(row, c, item);
		}
		row++;
	}

	while (!file.atEnd())
	{
		m_view->insertRow(row);
		QList<bool> rowQuoted;
		list = parse_line(file.readLine(), m_encoding, m_separator, &rowQuoted);

		if (list.size() > columns)
		{
			columns = list.size();
			m_view->setColumnCount(columns);
		}

		for (int c = 0; c < list.size(); ++c)
		{
			QTableWidgetItem *item = new QTableWidgetItem(list.at(c).trimmed());
			item->setToolTip(list.at(c).trimmed());
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
			item->setData(WasQuotedRole, c < rowQuoted.size() && rowQuoted[c]);
			m_view->setItem(row, c, item);
		}

		row++;
	}

	file.close();

	m_view->setShowGrid(gGrid);

	if (gResize)
		m_view->resizeColumnsToContents();

	m_lastSortColumn = -1;
	m_lastSortOrder = Qt::AscendingOrder;
	m_undoStack->clear();
	m_isProgrammaticChange = false;

	if (m_lblStatus) m_lblStatus->clear();

	if (!m_isActive) {
		QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
	}
	return true;
}

void CsvViewerWidget::onSave()
{
	if (m_activeInput) {
		if (QWidget *fw = QApplication::focusWidget()) {
			if (m_view->isAncestorOf(fw))
				fw->clearFocus();
		}
		m_activeInput = nullptr;
	}
	saveFile(m_currentFile);
	m_undoStack->setClean();
}

void CsvViewerWidget::onSaveAs()
{
	QString csvFilter = "CSV - Comma Separated (*.csv)";
	QString tsvFilter = "TSV - Tab Separated (*.tsv)";
	QString selectedFilter;
	// Put the current format first
	QString filter = (m_separator == '\t') ? (tsvFilter + ";;" + csvFilter) : (csvFilter + ";;" + tsvFilter);
	QString path = QFileDialog::getSaveFileName(this, "Save As", m_currentFile, filter, &selectedFilter);
	if (!path.isEmpty()) {
		// Convert separator if the user chose a different format
		char oldSep = m_separator;
		if (selectedFilter == csvFilter)
			m_separator = ',';
		else if (selectedFilter == tsvFilter)
			m_separator = '\t';
		saveFile(path);
		m_currentFile = path;
		m_undoStack->setClean();
		if (m_separator != oldSep)
			updateTextView();
	}
}

void CsvViewerWidget::onReload()
{
	if (m_currentFile.isEmpty()) return;
	if (QWidget *fw = QApplication::focusWidget()) {
		if (m_view->isAncestorOf(fw)) {
			fw->clearFocus();
		}
	}
	loadFile(m_currentFile);
}

void CsvViewerWidget::saveFile(const QString& filePath)
{
	QFile file(filePath);
	if (!file.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::warning(this, "Error", "Could not open file for writing.");
		return;
	}

	QString outText;
	int rows = m_view->rowCount();
	int cols = m_view->columnCount();

	QStringList headerLine;
	if (m_firstLineAsHeader) {
		for (int vc = 0; vc < cols; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			QTableWidgetItem *hItem = m_view->horizontalHeaderItem(c);
			QString text = hItem ? hItem->text() : "";
			bool wasQuoted = hItem && hItem->data(WasQuotedRole).toBool();
			if (wasQuoted || text.contains(m_separator)) {
				text.replace("\"", "\"\"");
				text = "\"" + text + "\"";
			}
			headerLine << text;
		}
		outText += headerLine.join(m_separator) + "\n";
	}

	for (int vr = 0; vr < rows; ++vr) {
		int r = m_view->verticalHeader()->logicalIndex(vr);
		QStringList rowLine;
		for (int vc = 0; vc < cols; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			QTableWidgetItem *item = m_view->item(r, c);
			QString text = item ? item->text() : "";
			bool wasQuoted = item && item->data(WasQuotedRole).toBool();
			if (wasQuoted || text.contains(m_separator)) {
				text.replace("\"", "\"\"");
				text = "\"" + text + "\"";
			}
			rowLine << text;
		}
		outText += rowLine.join(m_separator) + "\n";
	}

	QByteArray outBytes;
	if (m_encoding[0] != '\0') {
		gsize len;
		QByteArray utf8Text = outText.toUtf8();
		gchar *converted = g_convert_with_fallback(utf8Text.data(), utf8Text.size(), m_encoding, "UTF-8", NULL, NULL, &len, NULL);
		if (converted) {
			outBytes = QByteArray(converted, len);
			g_free(converted);
		} else {
			outBytes = utf8Text;
		}
	} else {
		outBytes = outText.toUtf8();
	}

	file.write(outBytes);
	file.close();
	
	m_currentFile = filePath;
}

void CsvViewerWidget::copySelection(char separator)
{
	QString text = getSelectionAsText(separator);
	if (!text.isEmpty()) {
		QApplication::clipboard()->setText(text);
	}
}

QString CsvViewerWidget::getSelectionAsText(char separator)
{
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return QString();

	// Find visual bounds of selection
	int minVRow = m_view->rowCount(), maxVRow = -1;
	int minVCol = m_view->columnCount(), maxVCol = -1;
	for (const QModelIndex &index : sel) {
		int vr = m_view->verticalHeader()->visualIndex(index.row());
		int vc = m_view->horizontalHeader()->visualIndex(index.column());
		if (vr < minVRow) minVRow = vr;
		if (vr > maxVRow) maxVRow = vr;
		if (vc < minVCol) minVCol = vc;
		if (vc > maxVCol) maxVCol = vc;
	}

	QString outText;
	if (m_firstLineAsHeader) {
		QStringList headerItems;
		for (int vc = minVCol; vc <= maxVCol; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			QTableWidgetItem *hItem = m_view->horizontalHeaderItem(c);
			QString headerText = hItem ? hItem->text() : "";
			bool wasQuoted = hItem && hItem->data(WasQuotedRole).toBool();
			if (wasQuoted || headerText.contains(separator)) {
				headerText.replace("\"", "\"\"");
				headerText = "\"" + headerText + "\"";
			}
			headerItems << headerText;
		}
		outText += headerItems.join(separator) + "\n";
	}

	for (int vr = minVRow; vr <= maxVRow; ++vr) {
		int r = m_view->verticalHeader()->logicalIndex(vr);
		QStringList rowItems;
		for (int vc = minVCol; vc <= maxVCol; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			QString cellText = "";
			QTableWidgetItem *item = nullptr;
			QModelIndex idx = m_view->model()->index(r, c);
			if (m_view->selectionModel()->isSelected(idx)) {
				item = m_view->item(r, c);
				cellText = item ? item->text() : "";
			}
			bool wasQuoted = item && item->data(WasQuotedRole).toBool();
			if (wasQuoted || cellText.contains(separator)) {
				cellText.replace("\"", "\"\"");
				cellText = "\"" + cellText + "\"";
			}
			rowItems << cellText;
		}
		outText += rowItems.join(separator) + "\n";
	}
	return outText;
}

void CsvViewerWidget::pasteSelection()
{
	// Find the target VISUAL row position for the paste
	int targetVisualRow = m_view->rowCount();
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (!sel.isEmpty()) {
		int minVRow = m_view->rowCount();
		for (const QModelIndex &index : sel) {
			int vr = m_view->verticalHeader()->visualIndex(index.row());
			if (vr < minVRow) minVRow = vr;
		}
		targetVisualRow = minVRow;
	}

	int endRow = m_view->rowCount();
	bool needsMove = (targetVisualRow < endRow);

	// Capture header order before paste (for undo of visual move)
	QList<int> beforeOrder;
	QHeaderView *vh = m_view->verticalHeader();
	if (needsMove) {
		for (int v = 0; v < vh->count(); ++v)
			beforeOrder.append(vh->logicalIndex(v));
	}

	// Insert at end, then move to the visual target
	if (needsMove) m_undoStack->beginMacro("Paste rows");

	pasteSelectionAt(endRow);

	int rowsInserted = m_view->rowCount() - endRow;
	if (rowsInserted > 0 && needsMove) {
		// Capture order after insertion (new rows at end)
		QList<int> midOrder;
		for (int v = 0; v < vh->count(); ++v)
			midOrder.append(vh->logicalIndex(v));

		// Move new rows from end to target visual position
		for (int i = 0; i < rowsInserted; ++i) {
			int logicalRow = endRow + i;
			int curVisual = vh->visualIndex(logicalRow);
			vh->moveSection(curVisual, targetVisualRow + i);
		}

		QList<int> afterOrder;
		for (int v = 0; v < vh->count(); ++v)
			afterOrder.append(vh->logicalIndex(v));

		m_undoStack->push(new SectionMoveCommand(vh, midOrder, afterOrder, "Move pasted rows"));
		updateRowNumbers();
	}

	if (needsMove) m_undoStack->endMacro();
}

void CsvViewerWidget::pasteSelectionAt(int atRow)
{
	int targetCols = m_view->columnCount();
	if (targetCols <= 0) return;

	QString text = QApplication::clipboard()->text();
	if (text.isEmpty()) return;

	QStringList lines = text.split(QRegularExpression("\r?\n"));
	if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
	if (lines.isEmpty()) return;

	char sep = '\t';
	QStringList testList = parse_line(lines.first().toUtf8(), m_encoding, '\t');
	if (testList.size() != targetCols && m_separator != '\t') {
		testList = parse_line(lines.first().toUtf8(), m_encoding, m_separator);
		if (testList.size() == targetCols) sep = m_separator;
		else return;
	} else if (testList.size() != targetCols) return;

	if (m_firstLineAsHeader && !lines.isEmpty()) {
		QStringList firstLine = parse_line(lines.first().toUtf8(), m_encoding, sep);
		bool matchesHeader = (firstLine.size() == targetCols);
		for (int c = 0; c < targetCols && matchesHeader; ++c) {
			QString headerText = m_view->horizontalHeaderItem(c) ? m_view->horizontalHeaderItem(c)->text() : "";
			if (firstLine.at(c).trimmed() != headerText) matchesHeader = false;
		}
		if (matchesHeader) lines.removeFirst();
	}
	if (lines.isEmpty()) return;

	int rowsToInsert = lines.size();
	RowColCommand *cmd = new RowColCommand(m_view, atRow, rowsToInsert, true, true);
	
	m_isProgrammaticChange = true;
	m_undoStack->push(cmd);
	m_isProgrammaticChange = false;
	
	for (int i = 0; i < rowsToInsert; ++i) {
		QList<bool> wasQuoted;
		QStringList list = parse_line(lines.at(i).toUtf8(), m_encoding, sep, &wasQuoted);
		// Map clipboard fields (in visual column order) to logical columns
		for (int vc = 0; vc < targetCols; ++vc) {
			int c = m_view->horizontalHeader()->logicalIndex(vc);
			QString cellText = vc < list.size() ? list.at(vc).trimmed() : "";
			if (QTableWidgetItem *item = m_view->item(atRow + i, c)) {
				m_isProgrammaticChange = true;
				item->setText(cellText);
				item->setData(WasQuotedRole, vc < wasQuoted.size() && wasQuoted[vc]);
				m_isProgrammaticChange = false;
			}
		}
	}
}

void CsvViewerWidget::insertEmptyRows(int count, int atRow)
{
	if (m_view->columnCount() <= 0 || count <= 0) return;
	m_undoStack->push(new RowColCommand(m_view, atRow, count, true, true));
}

void CsvViewerWidget::deleteSelection()
{
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return;
	
	QSet<int> rowsSet;
	for (const QModelIndex &index : sel) rowsSet.insert(index.row());
	QList<int> rowsToDelete = rowsSet.values();
	std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());

	m_undoStack->beginMacro("Delete rows");
	for (int r : rowsToDelete) {
		m_undoStack->push(new RowColCommand(m_view, r, 1, true, false));
	}
	m_undoStack->endMacro();
}

void CsvViewerWidget::copyColumnSelection(char separator) {
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return;
	
	int minCol = m_view->columnCount(), maxCol = -1;
	for (const QModelIndex &index : sel) {
		if (index.column() < minCol) minCol = index.column();
		if (index.column() > maxCol) maxCol = index.column();
	}
	
	QString outText;
	if (m_firstLineAsHeader) {
		QStringList headerItems;
		for (int c = minCol; c <= maxCol; ++c) {
			QString headerText = m_view->horizontalHeaderItem(c) ? m_view->horizontalHeaderItem(c)->text() : "";
			if (headerText.contains(separator) || headerText.contains('"') || headerText.contains('\n')) {
				headerText.replace("\"", "\"\"");
				headerText = "\"" + headerText + "\"";
			}
			headerItems << headerText;
		}
		outText += headerItems.join(separator) + "\n";
	}
	
	for (int r = 0; r < m_view->rowCount(); ++r) {
		QStringList rowItems;
		for (int c = minCol; c <= maxCol; ++c) {
			QString cellText = m_view->item(r, c) ? m_view->item(r, c)->text() : "";
			if (cellText.contains(separator) || cellText.contains('"') || cellText.contains('\n')) {
				cellText.replace("\"", "\"\"");
				cellText = "\"" + cellText + "\"";
			}
			rowItems << cellText;
		}
		outText += rowItems.join(separator) + "\n";
	}
	QApplication::clipboard()->setText(outText);
}

void CsvViewerWidget::pasteColumnSelectionAt(int atCol) {
	QString text = QApplication::clipboard()->text();
	if (text.isEmpty()) return;
	
	QStringList lines = text.split(QRegularExpression("\r?\n"));
	if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
	if (lines.isEmpty()) return;

	int expectedRows = m_view->rowCount();
	if (m_firstLineAsHeader) expectedRows += 1;
	
	if (lines.size() != expectedRows) {
		QMessageBox::warning(this, "Paste Error", QString("Clipboard contains %1 rows, but table requires %2.").arg(lines.size()).arg(expectedRows));
		return;
	}

	char sep = '\t';
	int colsToInsert = parse_line(lines.first().toUtf8(), m_encoding, '\t').size();
	if (colsToInsert <= 1 && m_separator != '\t') {
		sep = m_separator;
		colsToInsert = parse_line(lines.first().toUtf8(), m_encoding, m_separator).size();
	}
	
	m_isProgrammaticChange = true;
	m_undoStack->push(new RowColCommand(m_view, atCol, colsToInsert, false, true));
	m_isProgrammaticChange = false;
	
	int startIdx = 0;
	if (m_firstLineAsHeader) {
		QStringList list = parse_line(lines.at(0).toUtf8(), m_encoding, sep);
		for (int c = 0; c < colsToInsert; ++c) {
			QString cellText = c < list.size() ? list.at(c).trimmed() : "";
			if (QTableWidgetItem *item = m_view->horizontalHeaderItem(atCol + c)) item->setText(cellText);
			else m_view->setHorizontalHeaderItem(atCol + c, new QTableWidgetItem(cellText));
		}
		startIdx = 1;
	}
	
	for (int r = 0; r < m_view->rowCount(); ++r) {
		QStringList list = parse_line(lines.at(startIdx + r).toUtf8(), m_encoding, sep);
		for (int c = 0; c < colsToInsert; ++c) {
			QString cellText = c < list.size() ? list.at(c).trimmed() : "";
			if (QTableWidgetItem *item = m_view->item(r, atCol + c)) {
				m_isProgrammaticChange = true;
				item->setText(cellText);
				m_isProgrammaticChange = false;
			}
		}
	}
}

void CsvViewerWidget::insertEmptyColumns(int count, int atCol) {
	if (m_view->rowCount() <= 0 || count <= 0) return;
	m_undoStack->push(new RowColCommand(m_view, atCol, count, false, true));
}

void CsvViewerWidget::deleteColumnSelection() {
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return;
	
	QSet<int> colsSet;
	for (const QModelIndex &index : sel) colsSet.insert(index.column());
	QList<int> colsToDelete = colsSet.values();
	std::sort(colsToDelete.begin(), colsToDelete.end(), std::greater<int>());

	m_undoStack->beginMacro("Delete cols");
	for (int c : colsToDelete) {
		m_undoStack->push(new RowColCommand(m_view, c, 1, false, false));
	}
	m_undoStack->endMacro();
}

void CsvViewerWidget::showContextMenu(const QPoint &pos)
{
	QMenu menu(this);
	QAction *actCopyTSV = nullptr;
	QAction *actCopyCSV = nullptr;
	QAction *actDelete = nullptr;

	QAction *actInsertAbove = nullptr;
	QAction *actInsertBelow = nullptr;
	QAction *actPasteAbove = nullptr;
	QAction *actPasteBelow = nullptr;

	int minRow = m_view->rowCount();
	int maxRow = -1;
	int numRows = 0;

	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (!sel.isEmpty()) {
		QSet<int> rows;
		for (const QModelIndex &index : sel) {
			rows.insert(index.row());
			if (index.row() < minRow) minRow = index.row();
			if (index.row() > maxRow) maxRow = index.row();
		}
		numRows = rows.size();
		
		actCopyTSV = menu.addAction("Copy Selection as TSV");
		actCopyCSV = menu.addAction("Copy Selection as CSV");
		menu.addSeparator();
		actDelete = menu.addAction("Delete Selected Rows");
	} else {
		int clickedRow = m_view->rowAt(pos.y());
		if (clickedRow >= 0) {
			minRow = maxRow = clickedRow;
			numRows = 1;
		}
	}

	if (numRows > 0) {
		menu.addSeparator();
		QString rowStr = (numRows == 1) ? "1 row" : QString("%1 rows").arg(numRows);
		actInsertAbove = menu.addAction(QString("Insert %1 above").arg(rowStr));
		actInsertBelow = menu.addAction(QString("Insert %1 below").arg(rowStr));
		
		QString clipboardText = QApplication::clipboard()->text();
		if (!clipboardText.isEmpty()) {
			menu.addSeparator();
			actPasteAbove = menu.addAction("Insert from Clipboard above");
			actPasteBelow = menu.addAction("Insert from Clipboard below");
		}
	}

	QAction *res = menu.exec(m_view->viewport()->mapToGlobal(pos));
	QTimer::singleShot(0, this, &CsvViewerWidget::restoreViewFocus);
	if (!res) return;

	if (res == actCopyTSV) copySelection('\t');
	else if (res == actCopyCSV) copySelection(',');
	else if (res == actDelete) deleteSelection();
	else if (res == actInsertAbove) insertEmptyRows(numRows, minRow);
	else if (res == actInsertBelow) insertEmptyRows(numRows, maxRow + 1);
	else if (res == actPasteAbove) pasteSelectionAt(minRow);
	else if (res == actPasteBelow) pasteSelectionAt(maxRow + 1);
}

void CsvViewerWidget::showColumnContextMenu(const QPoint &pos)
{
	QMenu menu(this);
	QAction *actCopy = nullptr;
	QAction *actDelete = nullptr;
	QAction *actInsertLeft = nullptr;
	QAction *actInsertRight = nullptr;
	QAction *actPasteLeft = nullptr;
	QAction *actPasteRight = nullptr;

	int minCol = m_view->columnCount();
	int maxCol = -1;
	int numCols = 0;

	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (!sel.isEmpty()) {
		QSet<int> cols;
		for (const QModelIndex &index : sel) {
			cols.insert(index.column());
			if (index.column() < minCol) minCol = index.column();
			if (index.column() > maxCol) maxCol = index.column();
		}
		numCols = cols.size();
		
		actCopy = menu.addAction("Copy Columns");
		menu.addSeparator();
		actDelete = menu.addAction("Delete Selected Columns");
	} else {
		int clickedCol = m_view->columnAt(pos.x());
		if (clickedCol >= 0) {
			minCol = maxCol = clickedCol;
			numCols = 1;
		}
	}

	if (numCols > 0) {
		menu.addSeparator();
		QString colStr = (numCols == 1) ? "1 col" : QString("%1 cols").arg(numCols);
		actInsertLeft = menu.addAction(QString("Insert %1 left").arg(colStr));
		actInsertRight = menu.addAction(QString("Insert %1 right").arg(colStr));
		
		QString clipboardText = QApplication::clipboard()->text();
		if (!clipboardText.isEmpty()) {
			menu.addSeparator();
			actPasteLeft = menu.addAction("Insert from Clipboard left");
			actPasteRight = menu.addAction("Insert from Clipboard right");
		}
	}

	QAction *res = menu.exec(m_view->horizontalHeader()->viewport()->mapToGlobal(pos));
	QTimer::singleShot(0, this, &CsvViewerWidget::restoreViewFocus);
	if (!res) return;

	if (res == actCopy) copyColumnSelection('\t');
	else if (res == actDelete) deleteColumnSelection();
	else if (res == actInsertLeft) insertEmptyColumns(numCols, minCol);
	else if (res == actInsertRight) insertEmptyColumns(numCols, maxCol + 1);
	else if (res == actPasteLeft) pasteColumnSelectionAt(minCol);
	else if (res == actPasteRight) pasteColumnSelectionAt(maxCol + 1);
}


HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	if (!QApplication::instance()) return nullptr;
	CsvViewerWidget *widget = new CsvViewerWidget((QWidget*)ParentWin);
	if (!widget->loadFile(FileToLoad)) { delete widget; return nullptr; }
	widget->show();
	return widget;
}

void DCPCALL ListCloseWindow(HANDLE ListWin)
{
	CsvViewerWidget *widget = (CsvViewerWidget*)ListWin;
	delete widget;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
	CsvViewerWidget *widget = (CsvViewerWidget*)ListWin;
	QTableWidget *view = widget->view();
	switch (Command)
	{
	case lc_copy :
	{
		QString text = widget->getSelectionAsText('\t');
		if (text.isEmpty()) return LISTPLUGIN_ERROR;
		QApplication::clipboard()->setText(text);
		break;
	}
	case lc_selectall :
		view->selectAll();
		break;
	case lc_focus :
		if (Parameter) {
			widget->setActive(true);
			view->setFocus(Qt::OtherFocusReason);
		} else {
			widget->setActive(false);
			if (QWidget *fw = QApplication::focusWidget()) {
				if (fw == widget || widget->isAncestorOf(fw))
					fw->clearFocus();
			}
		}
		break;
	default :
		return LISTPLUGIN_ERROR;
	}
	return LISTPLUGIN_OK;
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
	CsvViewerWidget *widget = (CsvViewerWidget*)ListWin;
	QTableWidget *view = widget->view();
	QList<QTableWidgetItem*> list;
	Qt::MatchFlags sflags = Qt::MatchContains;
	if (SearchParameter & lcs_matchcase) sflags |= Qt::MatchCaseSensitive;

	QString needle(SearchString);
	QString prev = view->property("needle").value<QString>();
	view->setProperty("needle", needle);

	list = view->findItems(QString(SearchString), sflags);

	if (!list.isEmpty())
	{
		int i = view->property("findit").value<int>();
		if (needle != prev || SearchParameter & lcs_findfirst)
		{
			if (SearchParameter & lcs_backwards) i = list.size() - 1;
			else i = 0;
		}
		else if (SearchParameter & lcs_backwards) i--;
		else i++;

		if (i >= 0 && i < list.size() && list.at(i))
		{
			view->scrollToItem(list.at(i));
			view->setCurrentItem(list.at(i));
			view->setProperty("findit", i);
			return LISTPLUGIN_OK;
		}
	}
	QMessageBox::information(widget, "", QString::asprintf(_("\"%s\" not found!"), SearchString));
	return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	snprintf(DetectString, maxlen - 1, "(EXT=\"CSV\" | EXT=\"TSV\") & SIZE<30000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	QFileInfo defini(QString::fromStdString(dps->DefaultIniName));
	QString cfgpath = defini.absolutePath() + "/j2969719.ini";
	QSettings settings(cfgpath, QSettings::IniFormat);

	if (!settings.contains(PLUGNAME "/resize_columns")) settings.setValue(PLUGNAME "/resize_columns", gResize);
	else gResize = settings.value(PLUGNAME "/resize_columns").toBool();

	if (!settings.contains(PLUGNAME "/enca")) settings.setValue(PLUGNAME "/enca", gEnca);
	else gEnca = settings.value(PLUGNAME "/enca").toBool();

	if (!settings.contains(PLUGNAME "/enca_lang"))
	{
		char lang[3];
		snprintf(lang, 3, "%s", setlocale(LC_ALL, ""));
		settings.setValue(PLUGNAME "/enca_lang", QString(lang));
	}
	else gLang = settings.value(PLUGNAME "/enca_lang").toString();

	if (!settings.contains(PLUGNAME "/enca_readall")) settings.setValue(PLUGNAME "/enca_readall", gReadAll);
	else gReadAll = settings.value(PLUGNAME "/enca_readall").toBool();

	if (!settings.contains(PLUGNAME "/doublequoted")) settings.setValue(PLUGNAME "/doublequoted", gQuoted);
	else gQuoted = settings.value(PLUGNAME "/doublequoted").toBool();

	if (!settings.contains(PLUGNAME "/draw_grid")) settings.setValue(PLUGNAME "/draw_grid", gGrid);
	else gGrid = settings.value(PLUGNAME "/draw_grid").toBool();

	Dl_info dlinfo;
	static char plg_path[PATH_MAX];
	const char* loc_dir = "langs";

	memset(&dlinfo, 0, sizeof(dlinfo));

	if (dladdr(plg_path, &dlinfo) != 0)
	{
		strncpy(plg_path, dlinfo.dli_fname, PATH_MAX);
		char *pos = strrchr(plg_path, '/');
		if (pos) strcpy(pos + 1, loc_dir);
		setlocale(LC_ALL, "");
		bindtextdomain(GETTEXT_PACKAGE, plg_path);
		textdomain(GETTEXT_PACKAGE);
	}
}

void CsvViewerWidget::showFindReplacePanel(bool show)
{
	if (!m_findReplacePanel) return;

	m_findReplacePanel->setVisible(show);
	if (m_actFindReplace) {
		m_actFindReplace->blockSignals(true);
		m_actFindReplace->setChecked(show);
		m_actFindReplace->blockSignals(false);
	}

	if (show) {
		m_txtFind->setFocus(Qt::OtherFocusReason);
		m_txtFind->selectAll();
		m_lblStatus->clear();
	} else {
		m_lblStatus->clear();
		restoreViewFocus();
	}
}

bool CsvViewerWidget::cellMatches(int row, int col, const QString &query, bool matchCase, bool entireCell, bool regexFlag)
{
	if (query.isEmpty()) return false;

	QTableWidgetItem *item = m_view->item(row, col);
	QString text = item ? item->text() : "";

	if (regexFlag) {
		QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
		if (!matchCase) {
			options |= QRegularExpression::CaseInsensitiveOption;
		}
		QRegularExpression re(entireCell ? "^(" + query + ")$" : query, options);
		if (!re.isValid()) {
			return false;
		}
		return re.match(text).hasMatch();
	} else {
		Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
		if (entireCell) {
			return text.compare(query, cs) == 0;
		} else {
			return text.contains(query, cs);
		}
	}
}

void CsvViewerWidget::doFind(bool forward)
{
	QString query = m_txtFind->text();
	if (query.isEmpty()) {
		m_lblStatus->setText("Search query is empty.");
		return;
	}

	bool matchCase = m_chkMatchCase->isChecked();
	bool entireCell = m_chkMatchEntire->isChecked();
	bool regexFlag = m_chkRegex->isChecked();
	QString scope = m_comboScope->currentText();

	int rows = m_view->rowCount();
	int cols = m_view->columnCount();
	if (rows == 0 || cols == 0) {
		m_lblStatus->setText("Grid is empty.");
		return;
	}

	QModelIndex current = m_view->currentIndex();
	int currRow = current.isValid() ? current.row() : (forward ? 0 : rows - 1);
	int currCol = current.isValid() ? current.column() : (forward ? 0 : cols - 1);

	// Build list of cells in scope
	QList<QPair<int, int>> cells;
	if (scope == "All Cells") {
		int N = rows * cols;
		int startIdx = currRow * cols + currCol;
		for (int i = 1; i <= N; ++i) {
			int idx = forward ? (startIdx + i) % N : (startIdx - i + N) % N;
			cells.append({idx / cols, idx % cols});
		}
	} else if (scope == "Current Column") {
		int col = currCol;
		for (int i = 1; i <= rows; ++i) {
			int r = forward ? (currRow + i) % rows : (currRow - i + rows) % rows;
			cells.append({r, col});
		}
	} else if (scope == "Current Row") {
		int row = currRow;
		for (int i = 1; i <= cols; ++i) {
			int c = forward ? (currCol + i) % cols : (currCol - i + cols) % cols;
			cells.append({row, c});
		}
	} else if (scope == "Selected Cells") {
		QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
		if (sel.isEmpty()) {
			m_lblStatus->setText("No cells selected.");
			return;
		}
		std::sort(sel.begin(), sel.end(), [](const QModelIndex &a, const QModelIndex &b) {
			if (a.row() != b.row()) return a.row() < b.row();
			return a.column() < b.column();
		});

		int selIdx = -1;
		for (int i = 0; i < sel.size(); ++i) {
			if (sel[i].row() == currRow && sel[i].column() == currCol) {
				selIdx = i;
				break;
			}
		}

		int count = sel.size();
		for (int i = 1; i <= count; ++i) {
			int idx = forward ? (selIdx + i) % count : (selIdx - i + count) % count;
			cells.append({sel[idx].row(), sel[idx].column()});
		}
	}

	for (const auto &cell : cells) {
		if (cellMatches(cell.first, cell.second, query, matchCase, entireCell, regexFlag)) {
			// Found match!
			m_view->setCurrentCell(cell.first, cell.second);
			m_view->scrollToItem(m_view->item(cell.first, cell.second));
			m_lblStatus->setText(QString("Found match at (%1, %2)").arg(cell.first + 1).arg(cell.second + 1));
			return;
		}
	}

	m_lblStatus->setText("No match found.");
}

void CsvViewerWidget::doReplace()
{
	QString query = m_txtFind->text();
	QString replaceText = m_txtReplace->text();
	if (query.isEmpty()) {
		m_lblStatus->setText("Search query is empty.");
		return;
	}

	QModelIndex current = m_view->currentIndex();
	if (!current.isValid()) {
		doFind(true);
		return;
	}

	bool matchCase = m_chkMatchCase->isChecked();
	bool entireCell = m_chkMatchEntire->isChecked();
	bool regexFlag = m_chkRegex->isChecked();

	int row = current.row();
	int col = current.column();

	if (cellMatches(row, col, query, matchCase, entireCell, regexFlag)) {
		QTableWidgetItem *item = m_view->item(row, col);
		QString oldText = item ? item->text() : "";
		QString newText = oldText;

		if (regexFlag) {
			QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
			if (!matchCase) {
				options |= QRegularExpression::CaseInsensitiveOption;
			}
			QRegularExpression re(entireCell ? "^(" + query + ")$" : query, options);
			newText.replace(re, replaceText);
		} else {
			Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
			if (entireCell) {
				newText = replaceText;
			} else {
				newText.replace(query, replaceText, cs);
			}
		}

		if (newText != oldText) {
			m_undoStack->push(new EditCellCommand(m_view, row, col, oldText, newText));
			m_lblStatus->setText(QString("Replaced match at (%1, %2)").arg(row + 1).arg(col + 1));
		}
		// Move to the next match
		doFind(true);
	} else {
		// Not a match, find the next one
		doFind(true);
	}
}

void CsvViewerWidget::doReplaceAll()
{
	QString query = m_txtFind->text();
	QString replaceText = m_txtReplace->text();
	if (query.isEmpty()) {
		m_lblStatus->setText("Search query is empty.");
		return;
	}

	bool matchCase = m_chkMatchCase->isChecked();
	bool entireCell = m_chkMatchEntire->isChecked();
	bool regexFlag = m_chkRegex->isChecked();
	QString scope = m_comboScope->currentText();

	int rows = m_view->rowCount();
	int cols = m_view->columnCount();
	if (rows == 0 || cols == 0) {
		m_lblStatus->setText("Grid is empty.");
		return;
	}

	QList<QPair<int, int>> cells;
	if (scope == "All Cells") {
		for (int r = 0; r < rows; ++r) {
			for (int c = 0; c < cols; ++c) {
				cells.append({r, c});
			}
		}
	} else if (scope == "Current Column") {
		QModelIndex current = m_view->currentIndex();
		int col = current.isValid() ? current.column() : 0;
		for (int r = 0; r < rows; ++r) {
			cells.append({r, col});
		}
	} else if (scope == "Current Row") {
		QModelIndex current = m_view->currentIndex();
		int row = current.isValid() ? current.row() : 0;
		for (int c = 0; c < cols; ++c) {
			cells.append({row, c});
		}
	} else if (scope == "Selected Cells") {
		QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
		if (sel.isEmpty()) {
			m_lblStatus->setText("No cells selected.");
			return;
		}
		std::sort(sel.begin(), sel.end(), [](const QModelIndex &a, const QModelIndex &b) {
			if (a.row() != b.row()) return a.row() < b.row();
			return a.column() < b.column();
		});
		for (const auto &idx : sel) {
			cells.append({idx.row(), idx.column()});
		}
	}

	struct Replacement {
		int row;
		int col;
		QString oldText;
		QString newText;
	};
	QList<Replacement> replacements;

	QRegularExpression re;
	if (regexFlag) {
		QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
		if (!matchCase) {
			options |= QRegularExpression::CaseInsensitiveOption;
		}
		re.setPattern(entireCell ? "^(" + query + ")$" : query);
		re.setPatternOptions(options);
		if (!re.isValid()) {
			m_lblStatus->setText("Invalid regular expression.");
			return;
		}
	}

	for (const auto &cell : cells) {
		int r = cell.first;
		int c = cell.second;
		if (cellMatches(r, c, query, matchCase, entireCell, regexFlag)) {
			QTableWidgetItem *item = m_view->item(r, c);
			QString oldText = item ? item->text() : "";
			QString newText = oldText;

			if (regexFlag) {
				newText.replace(re, replaceText);
			} else {
				Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
				if (entireCell) {
					newText = replaceText;
				} else {
					newText.replace(query, replaceText, cs);
				}
			}

			if (newText != oldText) {
				replacements.append({r, c, oldText, newText});
			}
		}
	}

	if (!replacements.isEmpty()) {
		m_undoStack->beginMacro(QString("Replace All: %1 -> %2").arg(query).arg(replaceText));
		for (const auto &rep : replacements) {
			m_undoStack->push(new EditCellCommand(m_view, rep.row, rep.col, rep.oldText, rep.newText));
		}
		m_undoStack->endMacro();
		m_lblStatus->setText(QString("Replaced %1 occurrences.").arg(replacements.size()));
	} else {
		m_lblStatus->setText("No replacements made.");
	}
}
