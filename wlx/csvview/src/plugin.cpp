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
#include <QVBoxLayout>
#include <QWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <iostream>
#include <QLineEdit>
#include <QTimer>
#include <QMenu>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QPrinter>
#include <QPrintDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QTextStream>

#include <string.h>
#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>
#include <algorithm>
#include <vector>

#include "wlxplugin.h"
#include "CsvCore.h"

// Pull in base library headers
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <wlxbase_wlqt/ScopedFindReplacePanel.h>
#include <wlxbase_wlqt/EncodingUtils.h>
#include <wlxbase_wlqt/ThemeManager.h>

#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"
#define PLUGNAME "csvview_qt6.wlx"

bool gQuoted = true;
bool gGrid = true;
bool gResize = true;
bool gEnca = true;
bool gReadAll = false;
QString gLang = "ru";

static const int WasQuotedRole = Qt::UserRole + 2;

using namespace QtWlPlugin;

// Tokenizing itself now lives in CsvCore (src/core/CsvCore.cpp), shared
// with the GTK build — this wrapper just handles the Qt-side encoding
// conversion (EncodingUtils, still Qt-typed) and QStringList marshaling.
// NOTE: preserves the original's gQuoted-gated behavior exactly, including
// that parse_line() returns an empty list when gQuoted is false (a
// pre-existing quirk, not something introduced by this refactor).
QStringList parse_line(const QByteArray &line, const char *encoding, char separator, QList<bool> *wasQuotedOut = nullptr)
{
	QStringList list;
	QByteArray utf8Line;

	if (encoding[0] != '\0')
	{
		utf8Line = EncodingUtils::toUtf8(line, QString::fromLatin1(encoding));
	}
	else
		utf8Line = line;

	if (gQuoted)
	{
		std::vector<CsvCore::Field> fields = CsvCore::parseLine(utf8Line.toStdString(), separator);
		for (const auto &field : fields)
		{
			list.append(QString::fromUtf8(field.text.c_str()));
			if (wasQuotedOut) wasQuotedOut->append(field.wasQuoted);
		}
	}

	return list;
}

class EditCellCommand : public QUndoCommand {
public:
	EditCellCommand(QTableWidget *view, int row, int col, const QString &oldText, const QString &newText, bool oldQuoted, bool newQuoted, QUndoCommand *parent = nullptr)
		: QUndoCommand(parent), m_view(view), m_row(row), m_col(col), m_oldText(oldText), m_newText(newText), m_oldQuoted(oldQuoted), m_newQuoted(newQuoted) {
		setText(QString("Edit cell (%1, %2)").arg(row).arg(col));
	}
	void undo() override {
		m_view->blockSignals(true);
		if (QTableWidgetItem *item = m_view->item(m_row, m_col)) {
			item->setText(m_oldText);
			item->setData(WasQuotedRole, m_oldQuoted);
		}
		m_view->blockSignals(false);
	}
	void redo() override {
		m_view->blockSignals(true);
		if (QTableWidgetItem *item = m_view->item(m_row, m_col)) {
			item->setText(m_newText);
			item->setData(WasQuotedRole, m_newQuoted);
		}
		m_view->blockSignals(false);
	}
private:
	QTableWidget *m_view;
	int m_row, m_col;
	QString m_oldText, m_newText;
	bool m_oldQuoted, m_newQuoted;
};

class CsvViewerWidget : public QWidget
{
public:
	explicit CsvViewerWidget(QWidget *parent = nullptr);
	~CsvViewerWidget();

	bool loadFile(const QString& filePath);
	void saveFile(const QString& filePath);

	QTableWidget* view() const { return m_view; }
	EditableGridWidget* grid() const { return m_grid; }
	FocusManager* focusManager() const { return m_fm; }

	void setActive(bool active);

private slots:
	void onUndoStackCleanChanged(bool clean);

private:
	void setupToolbar();
	void setupFindReplace();
	void onSave();
	void onSaveAs();
	void onReload();
	void onToggleTextMode(bool checked);
	void onToggleWordWrap(bool checked);
	void updateTextView();

	void doFind(bool forward);
	void doReplace();
	void doReplaceAll();
	bool cellMatches(int row, int col, const QString &query, bool matchCase, bool entireCell, bool regexFlag);

	QTableWidget *m_view;
	EditableGridWidget *m_grid;
	FocusManager *m_fm;
	PluginToolBar *m_toolbar;
	ScopedFindReplacePanel *m_findReplace;
	
	QString m_currentFile;
	char m_separator;
	char m_encoding[256];
	bool m_firstLineAsHeader;
	
	QLabel *m_dirtyIndicator;

	QStackedWidget *m_stackedWidget;
	QTextBrowser *m_textBrowser;
	QAction *m_actFindReplace;
	QAction *m_actTextMode;
	QAction *m_actWordWrap;
	bool m_isActive;
	bool m_isProgrammaticChange;
};

CsvViewerWidget::CsvViewerWidget(QWidget *parent)
	: QWidget(parent)
	, m_view(nullptr)
	, m_grid(nullptr)
	, m_fm(nullptr)
	, m_toolbar(nullptr)
	, m_findReplace(nullptr)
	, m_separator(',')
	, m_firstLineAsHeader(true)
	, m_dirtyIndicator(nullptr)
	, m_stackedWidget(nullptr)
	, m_textBrowser(nullptr)
	, m_actFindReplace(nullptr)
	, m_actTextMode(nullptr)
	, m_actWordWrap(nullptr)
	, m_isActive(false)
	, m_isProgrammaticChange(false)
{
	memset(m_encoding, 0, sizeof(m_encoding));

	setFocusPolicy(Qt::NoFocus);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_view = new QTableWidget(this);
	m_view->setFocusPolicy(Qt::ClickFocus);

	m_fm = new FocusManager(this, m_view, this);

	// Create the EditableGridWidget around m_view
	m_grid = new EditableGridWidget(m_view, GridMode::MemoryDocument, m_fm, this);
	m_fm->setFocusProxy(m_grid->view());

	setupToolbar();
	layout->addWidget(m_toolbar);

	m_stackedWidget = new QStackedWidget(this);
	m_stackedWidget->addWidget(m_grid);

	m_textBrowser = new QTextBrowser(this);
	m_textBrowser->setOpenLinks(false);
	m_textBrowser->setReadOnly(true);
	m_stackedWidget->addWidget(m_textBrowser);

	layout->addWidget(m_stackedWidget);

	setupFindReplace();
	layout->addWidget(m_findReplace);

	connect(m_grid->undoStack(), &QUndoStack::cleanChanged, this, &CsvViewerWidget::onUndoStackCleanChanged);
}

CsvViewerWidget::~CsvViewerWidget()
{
	// Neutralize FocusManager FIRST — it has a global event filter on qApp
	// and a connection to qApp::focusChanged. If these fire during child
	// destruction (when focus shifts as widgets die), they access
	// half-destroyed objects and crash.
	if (m_fm) {
		if (qApp) {
			qApp->removeEventFilter(m_fm);
			disconnect(qApp, nullptr, m_fm, nullptr);
		}
	}

	// Block signals on the view and undo stack so that Qt's arbitrary
	// child destruction order cannot trigger dataChanged/cleanChanged
	// callbacks on dead objects. This mirrors the working original csvview.
	if (m_view)
		m_view->blockSignals(true);
	if (m_grid && m_grid->undoStack())
		m_grid->undoStack()->blockSignals(true);
}

void CsvViewerWidget::setupToolbar()
{
	m_toolbar = new PluginToolBar(m_fm, this);

	m_dirtyIndicator = new QLabel("✓", this);
	m_dirtyIndicator->setContentsMargins(4, 0, 4, 0);
	m_toolbar->addWidget(m_dirtyIndicator);

	// Save
	QAction *actSave = m_toolbar->addToolAction(
		"Save",
		QKeySequence(Qt::CTRL | Qt::Key_S),
		FocusManager::Always,
		"document-save");
	connect(actSave, &QAction::triggered, this, &CsvViewerWidget::onSave);

	// Save As
	QAction *actSaveAs = m_toolbar->addToolAction(
		"Save As...",
		QKeySequence(),
		0,
		"document-save-as");
	connect(actSaveAs, &QAction::triggered, this, &CsvViewerWidget::onSaveAs);

	// Undo
	QAction *actUndo = m_toolbar->addToolAction(
		"Undo",
		QKeySequence::Undo,
		FocusManager::Always,
		"edit-undo");
	connect(actUndo, &QAction::triggered, m_grid->undoStack(), &QUndoStack::undo);
	connect(m_grid->undoStack(), &QUndoStack::canUndoChanged, actUndo, &QAction::setEnabled);
	actUndo->setEnabled(false);

	// Redo
	QAction *actRedo = m_toolbar->addToolAction(
		"Redo",
		QKeySequence::Redo,
		FocusManager::Always,
		"edit-redo");
	actRedo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
	connect(actRedo, &QAction::triggered, m_grid->undoStack(), &QUndoStack::redo);
	connect(m_grid->undoStack(), &QUndoStack::canRedoChanged, actRedo, &QAction::setEnabled);
	actRedo->setEnabled(false);

	// Print
	QAction *actPrint = m_toolbar->addToolAction(
		"Print",
		QKeySequence::Print,
		FocusManager::Always,
		"document-print");
	connect(actPrint, &QAction::triggered, this, [this]() {
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

	// Reload
	QAction *actReload = m_toolbar->addToolAction(
		"Reload",
		QKeySequence(Qt::Key_F5),
		FocusManager::Always,
		"view-refresh");
	connect(actReload, &QAction::triggered, this, &CsvViewerWidget::onReload);

	// Header Row
	QAction *actHeader = m_toolbar->addToolAction(
		"Header Row",
		QKeySequence(),
		0,
		"format-justify-fill");
	actHeader->setCheckable(true);
	actHeader->setChecked(true);
	connect(actHeader, &QAction::toggled, this, [this](bool checked) {
		m_firstLineAsHeader = checked;
		onReload();
	});

	// Find/Replace
	m_actFindReplace = m_toolbar->addToolAction(
		"Find/Replace",
		QKeySequence(),
		0,
		"edit-find");
	m_actFindReplace->setCheckable(true);

	// Show Text
	m_actTextMode = m_toolbar->addToolAction(
		"Show Text",
		QKeySequence(),
		0,
		"visibility");
	m_actTextMode->setCheckable(true);
	connect(m_actTextMode, &QAction::toggled, this, &CsvViewerWidget::onToggleTextMode);

	// Line Wrap
	m_actWordWrap = m_toolbar->addToolAction(
		"Word Wrap",
		QKeySequence(),
		0,
		"format-text-direction-ltr");
	m_actWordWrap->setCheckable(true);
	connect(m_actWordWrap, &QAction::toggled, this, &CsvViewerWidget::onToggleWordWrap);

	// Open Externally
	QAction *actEditor = m_toolbar->addToolAction(
		"Open Externally",
		QKeySequence(Qt::CTRL | Qt::Key_O),
		FocusManager::Always,
		"document-open");
	connect(actEditor, &QAction::triggered, this, [this]() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentFile));
	});
}

void CsvViewerWidget::setupFindReplace()
{
	m_findReplace = new ScopedFindReplacePanel(m_fm, this);
	m_findReplace->setScopes({"All Cells", "Selected Cells", "Current Column", "Current Row"});
	connect(m_actFindReplace, &QAction::toggled, m_findReplace, &ScopedFindReplacePanel::showPanel);
	connect(m_findReplace, &ScopedFindReplacePanel::findRequested, this, &CsvViewerWidget::doFind);
	connect(m_findReplace, &ScopedFindReplacePanel::replaceRequested, this, &CsvViewerWidget::doReplace);
	connect(m_findReplace, &ScopedFindReplacePanel::replaceAllRequested, this, &CsvViewerWidget::doReplaceAll);
	connect(m_findReplace, &ScopedFindReplacePanel::panelClosed, this, [this]() {
		m_actFindReplace->setChecked(false);
	});

	// Register shortcuts via FocusManager
	m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), FocusManager::Always, [this]() {
		m_fm->setActive(false);
		if (QWidget *top = m_fm->pluginRoot()->window()) {
			QKeyEvent *ke = new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier);
			QCoreApplication::postEvent(top, ke);
		}
		return true;
	});
	m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), FocusManager::WhenNoInput, [this]() {
		m_actFindReplace->setChecked(!m_findReplace->isPanelVisible());
		return true;
	});
	m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), FocusManager::WhenNoInput, [this]() {
		m_actFindReplace->setChecked(!m_findReplace->isPanelVisible());
		return true;
	});
}

void CsvViewerWidget::setActive(bool active)
{
	m_isActive = active;
	m_fm->setActive(active);
}

void CsvViewerWidget::onUndoStackCleanChanged(bool clean) {
	m_dirtyIndicator->setText(clean ? "✓" : "✱");
}

void CsvViewerWidget::onToggleTextMode(bool checked) {
	if (checked) {
		// Commit editor
		if (m_fm->activeInput()) {
			QModelIndex current = m_view->currentIndex();
			QAbstractItemDelegate *delegate = m_view->itemDelegateForIndex(current);
			if (delegate)
				delegate->setModelData(m_fm->activeInput(), m_view->model(), current);
			m_view->closePersistentEditor(m_view->currentItem());
		}
		m_findReplace->showPanel(false);
		updateTextView();
		m_stackedWidget->setCurrentWidget(m_textBrowser);
	} else {
		m_stackedWidget->setCurrentWidget(m_grid);
	}
}

void CsvViewerWidget::onToggleWordWrap(bool checked) {
	m_grid->setWordWrap(checked);

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

bool CsvViewerWidget::loadFile(const QString& filePath)
{
	m_isProgrammaticChange = true;
	QWidget *fw = QApplication::focusWidget();
	if (fw && fw != this && !this->isAncestorOf(fw)) {
		m_fm->saveFocusWidget(fw);
	}
	m_currentFile = filePath;

	m_view->blockSignals(true);
	m_view->clear();
	m_view->setRowCount(0);
	m_view->setColumnCount(0);

	int columns = 0, row = 0;
	QStringList header, list;
	QFile file(filePath);
	QByteArray line;

	if (!file.open(QFile::ReadOnly | QFile::Text)) {
		m_view->blockSignals(false);
		return false;
	}

	if (gEnca)
	{
		QString enc = EncodingUtils::detectFileEncoding(filePath, gLang, 4096, gReadAll);
		if (!enc.isEmpty()) {
			snprintf(m_encoding, sizeof(m_encoding), "%s", enc.toStdString().c_str());
		}
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
		m_view->blockSignals(false);
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
		QMessageBox box(QMessageBox::Warning, "Separator Mismatch", msg, QMessageBox::NoButton, nullptr);
		QPushButton *btnIgnore = box.addButton("Ignore", QMessageBox::RejectRole);
		QPushButton *btnFixSep = box.addButton("Fix Separator", QMessageBox::AcceptRole);
		QPushButton *btnRename = box.addButton("Rename Extension", QMessageBox::AcceptRole);
		box.exec();

		if (box.clickedButton() == btnFixSep) {
			char oldSep = m_separator;
			char newSep = isCsvExt ? ',' : '\t';
			m_separator = newSep;

			file.seek(0);
			QByteArray rawData = file.readAll();
			file.close();

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
	m_view->blockSignals(false);

	m_view->setShowGrid(gGrid);

	if (gResize)
		m_view->resizeColumnsToContents();

	m_grid->undoStack()->clear();
	m_isProgrammaticChange = false;

	m_findReplace->setStatusText(QString());
	return true;
}

void CsvViewerWidget::onSave()
{
	FocusManager::expectReloadFocus();
	saveFile(m_currentFile);
	m_grid->undoStack()->setClean();
}

void CsvViewerWidget::onSaveAs()
{
	QString csvFilter = "CSV - Comma Separated (*.csv)";
	QString tsvFilter = "TSV - Tab Separated (*.tsv)";
	QString selectedFilter;
	QString filter = (m_separator == '\t') ? (tsvFilter + ";;" + csvFilter) : (csvFilter + ";;" + tsvFilter);
	
	QString path = QFileDialog::getSaveFileName(this, "Save As", m_currentFile, filter, &selectedFilter);

	if (!path.isEmpty()) {
		char oldSep = m_separator;
		if (selectedFilter == csvFilter)
			m_separator = ',';
		else if (selectedFilter == tsvFilter)
			m_separator = '\t';
		saveFile(path);
		m_currentFile = path;
		m_grid->undoStack()->setClean();
		if (m_separator != oldSep)
			updateTextView();
		QTimer::singleShot(100, this, [this]() {
			m_fm->setActive(true);
			m_fm->restoreViewFocus();
		});
	}
}

void CsvViewerWidget::onReload()
{
	if (m_currentFile.isEmpty()) return;
	loadFile(m_currentFile);
}

void CsvViewerWidget::saveFile(const QString& filePath)
{
	QFile file(filePath);
	if (!file.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::warning(nullptr, "Error", "Could not open file for writing.");
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
			text = QString::fromUtf8(CsvCore::escapeField(text.toStdString(), m_separator, wasQuoted).c_str());
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
			text = QString::fromUtf8(CsvCore::escapeField(text.toStdString(), m_separator, wasQuoted).c_str());
			rowLine << text;
		}
		outText += rowLine.join(m_separator) + "\n";
	}

	QByteArray outBytes;
	if (m_encoding[0] != '\0') {
		outBytes = EncodingUtils::fromUtf8(outText, m_encoding);
	} else {
		outBytes = outText.toUtf8();
	}

	file.write(outBytes);
	file.close();
	
	m_currentFile = filePath;
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
	QString query = m_findReplace->findText();
	if (query.isEmpty()) {
		m_findReplace->setStatusText("Search query is empty.");
		return;
	}

	bool matchCase = m_findReplace->matchCase();
	bool entireCell = m_findReplace->matchEntireCell();
	bool regexFlag = m_findReplace->useRegex();
	QString scope = m_findReplace->currentScope();

	int rows = m_view->rowCount();
	int cols = m_view->columnCount();
	if (rows == 0 || cols == 0) {
		m_findReplace->setStatusText("Grid is empty.");
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
			m_findReplace->setStatusText("No cells selected.");
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
			m_view->setCurrentCell(cell.first, cell.second);
			m_view->scrollToItem(m_view->item(cell.first, cell.second));
			m_findReplace->setStatusText(QString("Found match at (%1, %2)").arg(cell.first + 1).arg(cell.second + 1));
			return;
		}
	}

	m_findReplace->setStatusText("No match found.");
}

void CsvViewerWidget::doReplace()
{
	QString query = m_findReplace->findText();
	QString replaceText = m_findReplace->replaceText();
	if (query.isEmpty()) {
		m_findReplace->setStatusText("Search query is empty.");
		return;
	}

	QModelIndex current = m_view->currentIndex();
	if (!current.isValid()) {
		doFind(true);
		return;
	}

	bool matchCase = m_findReplace->matchCase();
	bool entireCell = m_findReplace->matchEntireCell();
	bool regexFlag = m_findReplace->useRegex();

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
			bool oldQuoted = item ? item->data(WasQuotedRole).toBool() : false;
			bool newQuoted = oldQuoted || newText.contains(m_separator);
			m_grid->undoStack()->push(new EditCellCommand(m_view, row, col, oldText, newText, oldQuoted, newQuoted));
			m_findReplace->setStatusText(QString("Replaced match at (%1, %2)").arg(row + 1).arg(col + 1));
		}
		doFind(true);
	} else {
		doFind(true);
	}
}

void CsvViewerWidget::doReplaceAll()
{
	QString query = m_findReplace->findText();
	QString replaceText = m_findReplace->replaceText();
	if (query.isEmpty()) {
		m_findReplace->setStatusText("Search query is empty.");
		return;
	}

	bool matchCase = m_findReplace->matchCase();
	bool entireCell = m_findReplace->matchEntireCell();
	bool regexFlag = m_findReplace->useRegex();
	QString scope = m_findReplace->currentScope();

	int rows = m_view->rowCount();
	int cols = m_view->columnCount();
	if (rows == 0 || cols == 0) {
		m_findReplace->setStatusText("Grid is empty.");
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
			m_findReplace->setStatusText("No cells selected.");
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
		bool oldQuoted;
		bool newQuoted;
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
			m_findReplace->setStatusText("Invalid regular expression.");
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
				bool oldQuoted = item ? item->data(WasQuotedRole).toBool() : false;
				bool newQuoted = oldQuoted || newText.contains(m_separator);
				replacements.append({r, c, oldText, newText, oldQuoted, newQuoted});
			}
		}
	}

	if (!replacements.isEmpty()) {
		m_grid->undoStack()->beginMacro(QString("Replace All: %1 -> %2").arg(query).arg(replaceText));
		for (const auto &rep : replacements) {
			m_grid->undoStack()->push(new EditCellCommand(m_view, rep.row, rep.col, rep.oldText, rep.newText, rep.oldQuoted, rep.newQuoted));
		}
		m_grid->undoStack()->endMacro();
		m_findReplace->setStatusText(QString("Replaced %1 occurrences.").arg(replacements.size()));
	} else {
		m_findReplace->setStatusText("No replacements made.");
	}
}

HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	Q_UNUSED(ShowFlags);
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
		QString text = widget->grid()->getSelectionAsText('\t');
		if (text.isEmpty()) return LISTPLUGIN_ERROR;
		QApplication::clipboard()->setText(text);
		break;
	}
	case lc_selectall :
		view->selectAll();
		break;
	case lc_focus :
		if (Parameter) {
			widget->focusManager()->setActive(true);
			view->setFocus(Qt::OtherFocusReason);
		} else {
			widget->focusManager()->setActive(false);
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
	QMessageBox::information(nullptr, "", QString::asprintf(_("\"%s\" not found!"), SearchString));
	return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	snprintf(DetectString, maxlen - 1, "(EXT=\"CSV\" | EXT=\"TSV\") & SIZE<30000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	QFileInfo defini(QString::fromStdString(dps->DefaultIniName));
	QString cfgpath = defini.absolutePath() + "/csvview.ini";
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
