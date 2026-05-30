#include <QFile>
#include <QTableWidget>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QSettings>
#include <QMimeData>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QEvent>
#include <QKeyEvent>
#include <QChildEvent>
#include <QTimer>
#include <QDebug>
#include <QPointer>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

#include <glib.h>
#include <enca.h>

#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>
#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"

#include "wlxplugin.h"

static bool gEnca = true;
static bool gResize = false;
static bool gReadAll = false;
static bool gQuoted = true;
static bool gGrid = false;
static QString gLang;

static QStringList parse_line(QByteArray line, char *enc, char separator)
{
	QStringList list, rawlist;

	if (enc[0] != '\0')
	{
		gsize len;
		gchar *converted = g_convert_with_fallback(line.data(), (gsize)line.size(), "UTF-8", enc, NULL, NULL, &len, NULL);

		if (converted)
			rawlist = QString(converted).split(QLatin1Char(separator));

		g_free(converted);
	}
	else
		rawlist = QString(line).split(QLatin1Char(separator));

	if (rawlist.isEmpty())
		return rawlist;

	if (!rawlist.last().isEmpty() && rawlist.last().back() == '\n')
		rawlist.last().remove(-1, 1);

	if (!gQuoted || separator == '\t')
		list = rawlist;
	else
	{
		for (int c = 0; c < rawlist.size(); ++c)
		{
			const QString itm = rawlist.at(c);

			if (!itm.isEmpty() && itm.front() == '"')
			{
				QString temp(itm.trimmed());

				if (itm.back() == '"' && itm.count(QLatin1Char('"')) > 3 && itm.count(QLatin1Char('"')) % 2 == 0)
					temp = QString(itm).remove(0, 1).remove(-1, 1);
				else
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
			}
			else
				list.append(rawlist.at(c).trimmed());

			list.last().replace("\"\"", "\"");
		}
	}

	return list;
}

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

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private:
	void onSave();
	void onSaveAs();
	void onReload();
	void showContextMenu(const QPoint &pos);

	void installFocusGuard();
	bool isInputWidget(QWidget *w) const;
	void restoreFocusToDC();

	QTableWidget *m_view;
	QToolBar *m_toolbar;
	QString m_currentFile;
	
	char m_separator;
	char m_encoding[256];
	bool m_firstLineAsHeader;
	
	QPointer<QWidget> m_savedFocusWidget;
	QPointer<QWidget> m_activeInput;
};

CsvViewerWidget::CsvViewerWidget(QWidget *parent)
	: QWidget(parent), m_savedFocusWidget(nullptr), m_activeInput(nullptr), m_separator(','), m_firstLineAsHeader(true)
{
	memset(m_encoding, 0, sizeof(m_encoding));

	setFocusPolicy(Qt::NoFocus);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_toolbar = new QToolBar(this);
	m_toolbar->setFocusPolicy(Qt::NoFocus);
	
	QAction *actSave = m_toolbar->addAction("Save");
	actSave->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
	
	QAction *actSaveAs = m_toolbar->addAction("Save As...");
	QAction *actReload = m_toolbar->addAction("Reload");
	QAction *actHeader = m_toolbar->addAction("Header Row");
	actHeader->setCheckable(true);
	actHeader->setChecked(true);

	addAction(actSave);

	layout->addWidget(m_toolbar);

	m_view = new QTableWidget(this);
	m_view->setFocusPolicy(Qt::NoFocus);
	m_view->setContextMenuPolicy(Qt::CustomContextMenu);
	layout->addWidget(m_view);

	// Instead of connecting to slots via moc, use QObject::connect with lambda
	QObject::connect(actSave, &QAction::triggered, this, [this]() { onSave(); });
	QObject::connect(actSaveAs, &QAction::triggered, this, [this]() { onSaveAs(); });
	QObject::connect(actReload, &QAction::triggered, this, [this]() { onReload(); });
	QObject::connect(actHeader, &QAction::toggled, this, [this](bool checked) {
		m_firstLineAsHeader = checked;
		onReload();
	});
	QObject::connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });

	installFocusGuard();
}

CsvViewerWidget::~CsvViewerWidget()
{
	if (qApp) qApp->removeEventFilter(this);
}

void CsvViewerWidget::installFocusGuard()
{
	if (qApp) qApp->installEventFilter(this);
	const auto children = findChildren<QWidget*>();
	for (QWidget *child : children) {
		if (!isInputWidget(child))
			child->setFocusPolicy(Qt::NoFocus);
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

bool CsvViewerWidget::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		auto *ke = static_cast<QKeyEvent*>(event);
		if (this->isActiveWindow()) {
			if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_S) {
				onSave();
				return true;
			}
			if (!m_activeInput) {
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
			}
		}
	}

	if (event->type() == QEvent::MouseButtonPress) {
		QWidget *w = qobject_cast<QWidget*>(obj);
		if (w) {
			if (w != this && !this->isAncestorOf(w)) {
				m_activeInput = nullptr;
				restoreFocusToDC();
			}
		}
	}

	QWidget *w = qobject_cast<QWidget*>(obj);
	if (w && (w == this || this->isAncestorOf(w))) {
		if (event->type() == QEvent::FocusIn) {
			if (isInputWidget(w)) {
				m_activeInput = w;
				return false;
			}
			QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
			return false;
		}

		if (event->type() == QEvent::ChildAdded) {
			auto *ce = static_cast<QChildEvent*>(event);
			if (auto *childWidget = qobject_cast<QWidget*>(ce->child())) {
				if (!isInputWidget(childWidget))
					childWidget->setFocusPolicy(Qt::NoFocus);
			}
		}

		if (event->type() == QEvent::KeyPress) {
			auto *ke = static_cast<QKeyEvent*>(event);
			if (ke->key() == Qt::Key_Escape && m_activeInput) {
				m_activeInput = nullptr;
				restoreFocusToDC();
				return true;
			}
			if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && isInputWidget(w)) {
				QTimer::singleShot(0, this, [this]() {
					m_activeInput = nullptr;
					restoreFocusToDC();
				});
			}
		}
	}

	return QWidget::eventFilter(obj, event);
}

bool CsvViewerWidget::loadFile(const QString& filePath)
{
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

		header = parse_line(line, m_encoding, m_separator);
		columns = header.size();

		if (columns > 1)
		{
			m_view->setColumnCount(columns);
			if (m_firstLineAsHeader)
			{
				for (int c = 0; c < columns; ++c)
					m_view->setHorizontalHeaderItem(c, new QTableWidgetItem(header.at(c).trimmed()));
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

		header = parse_line(line, m_encoding, m_separator);
		columns = header.size();
		m_view->setColumnCount(columns);
		if (m_firstLineAsHeader)
		{
			for (int c = 0; c < columns; ++c)
				m_view->setHorizontalHeaderItem(c, new QTableWidgetItem(header.at(c).trimmed()));
		}
	}

	if (columns < 1)
	{
		return false;
	}

	if (gResize)
		m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

	if (!m_firstLineAsHeader)
	{
		m_view->insertRow(row);
		for (int c = 0; c < header.size(); ++c)
		{
			QTableWidgetItem *item = new QTableWidgetItem(header.at(c).trimmed());
			item->setToolTip(header.at(c).trimmed());
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
			m_view->setItem(row, c, item);
		}
		row++;
	}

	while (!file.atEnd())
	{
		m_view->insertRow(row);
		list = parse_line(file.readLine(), m_encoding, m_separator);

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
			m_view->setItem(row, c, item);
		}

		row++;
	}

	file.close();

	m_view->setSortingEnabled(true);
	m_view->setShowGrid(gGrid);

	QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
	return true;
}

void CsvViewerWidget::onSave()
{
	if (m_activeInput) {
		// Commit any active cell edit without touching DC focus
		if (QWidget *fw = QApplication::focusWidget()) {
			if (m_view->isAncestorOf(fw))
				fw->clearFocus();
		}
		m_activeInput = nullptr;
	}
	saveFile(m_currentFile);
}

void CsvViewerWidget::onSaveAs()
{
	QString path = QFileDialog::getSaveFileName(this, "Save CSV As", m_currentFile);
	if (!path.isEmpty()) {
		saveFile(path);
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
		for (int c = 0; c < cols; ++c) {
			QString text = m_view->horizontalHeaderItem(c) ? m_view->horizontalHeaderItem(c)->text() : "";
			if (text.contains(m_separator) || text.contains('"') || text.contains('\n')) {
				text.replace("\"", "\"\"");
				text = "\"" + text + "\"";
			}
			headerLine << text;
		}
		outText += headerLine.join(m_separator) + "\n";
	}

	for (int r = 0; r < rows; ++r) {
		QStringList rowLine;
		for (int c = 0; c < cols; ++c) {
			QString text = m_view->item(r, c) ? m_view->item(r, c)->text() : "";
			if (text.contains(m_separator) || text.contains('"') || text.contains('\n')) {
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

	int minRow = m_view->rowCount();
	int maxRow = -1;
	int minCol = m_view->columnCount();
	int maxCol = -1;
	for (const QModelIndex &index : sel) {
		int r = index.row();
		int c = index.column();
		if (r < minRow) minRow = r;
		if (r > maxRow) maxRow = r;
		if (c < minCol) minCol = c;
		if (c > maxCol) maxCol = c;
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

	for (int r = minRow; r <= maxRow; ++r) {
		QStringList rowItems;
		for (int c = minCol; c <= maxCol; ++c) {
			QString cellText = "";
			QModelIndex idx = m_view->model()->index(r, c);
			if (m_view->selectionModel()->isSelected(idx)) {
				cellText = m_view->item(r, c) ? m_view->item(r, c)->text() : "";
			}
			if (cellText.contains(separator) || cellText.contains('"') || cellText.contains('\n')) {
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
	int insertRowIdx = m_view->rowCount();
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (!sel.isEmpty()) {
		int minRow = m_view->rowCount();
		for (const QModelIndex &index : sel) {
			if (index.row() < minRow) {
				minRow = index.row();
			}
		}
		insertRowIdx = minRow;
	}
	pasteSelectionAt(insertRowIdx);
}

void CsvViewerWidget::pasteSelectionAt(int atRow)
{
	int targetCols = m_view->columnCount();
	if (targetCols <= 0) return;

	QString text = QApplication::clipboard()->text();
	if (text.isEmpty()) return;

	// Split by newlines
	QStringList lines = text.split(QRegularExpression("\r?\n"));
	if (!lines.isEmpty() && lines.last().isEmpty()) {
		lines.removeLast();
	}
	if (lines.isEmpty()) return;

	// Try tab first, then current separator
	char sep = '\t';
	QStringList testList = parse_line(lines.first().toUtf8(), m_encoding, '\t');
	if (testList.size() != targetCols && m_separator != '\t') {
		testList = parse_line(lines.first().toUtf8(), m_encoding, m_separator);
		if (testList.size() == targetCols) {
			sep = m_separator;
		} else {
			return; // Column count mismatch
		}
	} else if (testList.size() != targetCols) {
		return; // Column count mismatch
	}

	// If treating first line as header, skip the clipboard's first line when it matches the header
	if (m_firstLineAsHeader && !lines.isEmpty()) {
		QStringList firstLine = parse_line(lines.first().toUtf8(), m_encoding, sep);
		bool matchesHeader = (firstLine.size() == targetCols);
		for (int c = 0; c < targetCols && matchesHeader; ++c) {
			QString headerText = m_view->horizontalHeaderItem(c) ? m_view->horizontalHeaderItem(c)->text() : "";
			if (firstLine.at(c).trimmed() != headerText)
				matchesHeader = false;
		}
		if (matchesHeader)
			lines.removeFirst();
	}
	if (lines.isEmpty()) return;

	int rowsToInsert = lines.size();
	for (int i = 0; i < rowsToInsert; ++i) {
		m_view->insertRow(atRow + i);
		QStringList list = parse_line(lines.at(i).toUtf8(), m_encoding, sep);
		for (int c = 0; c < targetCols; ++c) {
			QString cellText = c < list.size() ? list.at(c).trimmed() : "";
			QTableWidgetItem *item = new QTableWidgetItem(cellText);
			item->setToolTip(cellText);
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
			m_view->setItem(atRow + i, c, item);
		}
	}
}

void CsvViewerWidget::insertEmptyRows(int count, int atRow)
{
	int targetCols = m_view->columnCount();
	if (targetCols <= 0 || count <= 0) return;

	for (int i = 0; i < count; ++i) {
		m_view->insertRow(atRow + i);
		for (int c = 0; c < targetCols; ++c) {
			QTableWidgetItem *item = new QTableWidgetItem("");
			item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
			m_view->setItem(atRow + i, c, item);
		}
	}
}

void CsvViewerWidget::deleteSelection()
{
	QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return;

	QSet<int> rowsToDeleteSet;
	for (const QModelIndex &index : sel) {
		rowsToDeleteSet.insert(index.row());
	}

	QList<int> rowsToDelete = rowsToDeleteSet.values();
	std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());

	for (int r : rowsToDelete) {
		m_view->removeRow(r);
	}
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
	if (!res) return;

	if (res == actCopyTSV) {
		copySelection('\t');
	} else if (res == actCopyCSV) {
		copySelection(',');
	} else if (res == actDelete) {
		deleteSelection();
	} else if (res == actInsertAbove) {
		insertEmptyRows(numRows, minRow);
	} else if (res == actInsertBelow) {
		insertEmptyRows(numRows, maxRow + 1);
	} else if (res == actPasteAbove) {
		pasteSelectionAt(minRow);
	} else if (res == actPasteBelow) {
		pasteSelectionAt(maxRow + 1);
	}
}


HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	if (!QApplication::instance())
		return nullptr;

	CsvViewerWidget *widget = new CsvViewerWidget((QWidget*)ParentWin);
	if (!widget->loadFile(FileToLoad)) {
		delete widget;
		return nullptr;
	}

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
		if (text.isEmpty())
			return LISTPLUGIN_ERROR;
		QApplication::clipboard()->setText(text);
		break;
	}

	case lc_selectall :
		view->selectAll();
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

	if (SearchParameter & lcs_matchcase)
		sflags |= Qt::MatchCaseSensitive;

	QString needle(SearchString);
	QString prev = view->property("needle").value<QString>();
	view->setProperty("needle", needle);

	list = view->findItems(QString(SearchString), sflags);

	if (!list.isEmpty())
	{
		int i = view->property("findit").value<int>();

		if (needle != prev || SearchParameter & lcs_findfirst)
		{
			if (SearchParameter & lcs_backwards)
				i = list.size() - 1;
			else
				i = 0;
		}
		else if (SearchParameter & lcs_backwards)
			i--;
		else
			i++;

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

	if (!settings.contains(PLUGNAME "/resize_columns"))
		settings.setValue(PLUGNAME "/resize_columns", gResize);
	else
		gResize = settings.value(PLUGNAME "/resize_columns").toBool();

	if (!settings.contains(PLUGNAME "/enca"))
		settings.setValue(PLUGNAME "/enca", gEnca);
	else
		gEnca = settings.value(PLUGNAME "/enca").toBool();

	if (!settings.contains(PLUGNAME "/enca_lang"))
	{
		char lang[3];
		snprintf(lang, 3, "%s", setlocale(LC_ALL, ""));
		settings.setValue(PLUGNAME "/enca_lang", QString(lang));
	}
	else
		gLang = settings.value(PLUGNAME "/enca_lang").toString();

	if (!settings.contains(PLUGNAME "/enca_readall"))
		settings.setValue(PLUGNAME "/enca_readall", gReadAll);
	else
		gReadAll = settings.value(PLUGNAME "/enca_readall").toBool();

	if (!settings.contains(PLUGNAME "/doublequoted"))
		settings.setValue(PLUGNAME "/doublequoted", gQuoted);
	else
		gQuoted = settings.value(PLUGNAME "/doublequoted").toBool();

	if (!settings.contains(PLUGNAME "/draw_grid"))
		settings.setValue(PLUGNAME "/draw_grid", gGrid);
	else
		gGrid = settings.value(PLUGNAME "/draw_grid").toBool();

	Dl_info dlinfo;
	static char plg_path[PATH_MAX];
	const char* loc_dir = "langs";

	memset(&dlinfo, 0, sizeof(dlinfo));

	if (dladdr(plg_path, &dlinfo) != 0)
	{
		strncpy(plg_path, dlinfo.dli_fname, PATH_MAX);
		char *pos = strrchr(plg_path, '/');

		if (pos)
			strcpy(pos + 1, loc_dir);

		setlocale(LC_ALL, "");
		bindtextdomain(GETTEXT_PACKAGE, plg_path);
		textdomain(GETTEXT_PACKAGE);
	}
}
