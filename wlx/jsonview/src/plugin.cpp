#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QMimeData>
#include <QtWidgets>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMessageBox>
#include <QEvent>
#include <QKeyEvent>
#include <QChildEvent>
#include <QTimer>
#include <QPointer>

#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>
#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"

#include "wlxplugin.h"

static int  g_width = 200;
static bool g_resize = false;
static bool g_expand = true;
static bool g_sorting = false;
static bool g_filename = true;

class ValueDelegate : public QStyledItemDelegate {
public:
	ValueDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
		if (index.column() != 1) return nullptr; // Only allow editing column 1
		return QStyledItemDelegate::createEditor(parent, option, index);
	}
};

class JsonViewerWidget : public QWidget
{
public:
	explicit JsonViewerWidget(QWidget *parent = nullptr);
	~JsonViewerWidget();

	bool loadFile(const QString& filePath);
	void saveFile(const QString& filePath);

	QTreeWidget* view() const { return m_view; }

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

	void walk_array(const QJsonArray& array, QTreeWidgetItem *item);
	void walk_object(const QJsonObject& object, QTreeWidgetItem *item);
	void check_value(const QJsonValue& value, QTreeWidgetItem *item);
	QJsonValue treeToJson(QTreeWidgetItem *item);

	QString getJsonPath(QTreeWidgetItem *item);
	QString getSubtreeText(QTreeWidgetItem *item);
	QString getValueText(QTreeWidgetItem *item);

	QTreeWidget *m_view;
	QToolBar *m_toolbar;
	QString m_currentFile;
	
	QPointer<QWidget> m_savedFocusWidget;
	QPointer<QWidget> m_activeInput;
};

JsonViewerWidget::JsonViewerWidget(QWidget *parent)
	: QWidget(parent), m_savedFocusWidget(nullptr), m_activeInput(nullptr)
{
	setFocusPolicy(Qt::NoFocus);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_toolbar = new QToolBar(this);
	m_toolbar->setFocusPolicy(Qt::NoFocus);
	
	QAction *actSave = m_toolbar->addAction("Save");
	actSave->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
	
	QAction *actSaveAs = m_toolbar->addAction("Save As...");
	QAction *actReload = m_toolbar->addAction("Reload");

	addAction(actSave);

	layout->addWidget(m_toolbar);

	m_view = new QTreeWidget(this);
	m_view->setFocusPolicy(Qt::NoFocus);
	m_view->setItemDelegate(new ValueDelegate(this));
	m_view->setContextMenuPolicy(Qt::CustomContextMenu);
	layout->addWidget(m_view);

	QObject::connect(actSave, &QAction::triggered, this, [this]() { onSave(); });
	QObject::connect(actSaveAs, &QAction::triggered, this, [this]() { onSaveAs(); });
	QObject::connect(actReload, &QAction::triggered, this, [this]() { onReload(); });
	QObject::connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });

	installFocusGuard();
}

JsonViewerWidget::~JsonViewerWidget()
{
	if (qApp) qApp->removeEventFilter(this);
}

void JsonViewerWidget::installFocusGuard()
{
	if (qApp) qApp->installEventFilter(this);
	const auto children = findChildren<QWidget*>();
	for (QWidget *child : children) {
		if (!isInputWidget(child))
			child->setFocusPolicy(Qt::NoFocus);
	}
}

bool JsonViewerWidget::isInputWidget(QWidget *w) const
{
	if (!w) return false;
	if (w != m_view && m_view->isAncestorOf(w)) return true;
	return false;
}

void JsonViewerWidget::restoreFocusToDC()
{
	if (QWidget *fw = QApplication::focusWidget()) {
		if (fw == this || fw->isAncestorOf(this) || this->isAncestorOf(fw))
			fw->clearFocus();
	}
	if (m_savedFocusWidget) {
		m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
	}
}

bool JsonViewerWidget::eventFilter(QObject *obj, QEvent *event)
{
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
			if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_S) {
				onSave();
				return true;
			}
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

void JsonViewerWidget::walk_array(const QJsonArray& array, QTreeWidgetItem *item)
{
	for (int i = 0; i < array.count(); i++)
	{
		QTreeWidgetItem *newitem = new QTreeWidgetItem(item);
		newitem->setText(0, QString("[%1]").arg(i));
		newitem->setFlags(newitem->flags() | Qt::ItemIsEditable);
		check_value(array.at(i), newitem);
	}
}

void JsonViewerWidget::walk_object(const QJsonObject& object, QTreeWidgetItem *item)
{
	QJsonObject::const_iterator iter;
	for (iter = object.constBegin(); iter != object.constEnd(); ++iter)
	{
		QTreeWidgetItem *newitem = new QTreeWidgetItem(item);
		newitem->setText(0, iter.key());
		newitem->setToolTip(0, iter.key());
		newitem->setFlags(newitem->flags() | Qt::ItemIsEditable);
		check_value(iter.value(), newitem);
	}
}

void JsonViewerWidget::check_value(const QJsonValue& value, QTreeWidgetItem *item)
{
	double d;
	switch (value.type())
	{
	case QJsonValue::Object:
		item->setText(2, _("Object"));
		walk_object(value.toObject(), item);
		break;
	case QJsonValue::Array:
		item->setText(2, _("Array"));
		walk_array(value.toArray(), item);
		break;
	case QJsonValue::String:
		item->setText(2, _("String"));
		item->setText(1, value.toString());
		item->setToolTip(1, value.toString());
		break;
	case QJsonValue::Double:
		d = value.toDouble();
		if (trunc(d) == d)
		{
			item->setText(2, _("Integer"));
			item->setText(1, QString::number(d, 'f', 0));
		}
		else
		{
			item->setText(2, _("Double"));
			item->setText(1, QString::number(d));
		}
		item->setToolTip(1, QString::number(d, 'f', 1));
		break;
	case QJsonValue::Bool:
		item->setText(2, _("Boolean"));
		if (value.toBool())
			item->setText(1, _("True"));
		else
			item->setText(1, _("False"));
		break;
	case QJsonValue::Null:
		item->setText(2, _("Null"));
		break;
	default:
		item->setText(2, _("Undefined"));
		break;
	}
}

QJsonValue JsonViewerWidget::treeToJson(QTreeWidgetItem *item)
{
	QString type = item->text(2);
	if (type == _("Object")) {
		QJsonObject obj;
		for (int i = 0; i < item->childCount(); ++i) {
			QTreeWidgetItem *child = item->child(i);
			obj.insert(child->text(0), treeToJson(child));
		}
		return obj;
	} else if (type == _("Array")) {
		QJsonArray arr;
		for (int i = 0; i < item->childCount(); ++i) {
			QTreeWidgetItem *child = item->child(i);
			arr.append(treeToJson(child));
		}
		return arr;
	} else if (type == _("String")) {
		return QJsonValue(item->text(1));
	} else if (type == _("Integer") || type == _("Double")) {
		bool ok;
		double val = item->text(1).toDouble(&ok);
		if (ok) return QJsonValue(val);
		return QJsonValue(item->text(1)); // Fallback to string
	} else if (type == _("Boolean")) {
		QString val = item->text(1).toLower();
		return QJsonValue(val == "true" || val == "1");
	} else if (type == _("Null")) {
		return QJsonValue(QJsonValue::Null);
	}
	return QJsonValue();
}

bool JsonViewerWidget::loadFile(const QString& filePath)
{
	QWidget *fw = QApplication::focusWidget();
	if (fw && fw != this && !this->isAncestorOf(fw)) {
		m_savedFocusWidget = fw;
	}
	m_currentFile = filePath;
	m_activeInput = nullptr;

	QMimeDatabase db;
	QMimeType type = db.mimeTypeForFile(filePath);

	if (type.name() != "text/plain" && type.name() != "application/json")
		return false;

	QFile file(filePath);
	if (!file.open(QFile::ReadOnly | QFile::Text))
		return false;

	QJsonParseError err;
	QJsonDocument json = QJsonDocument().fromJson(file.readAll(), &err);
	file.close();

	if (json.isNull() || json.isEmpty()) {
		if (err.error != QJsonParseError::NoError)
			QMessageBox::critical(this, PLUGNAME, err.errorString());
		return false;
	}

	m_view->clear();
	m_view->setColumnCount(3);

	QFileInfo fi(filePath);
	QTreeWidgetItem *root = new QTreeWidgetItem(m_view);

	if (g_filename)
		root->setText(0, fi.fileName());
	else
		root->setText(0, _("Root"));
	
	root->setFlags(root->flags() | Qt::ItemIsEditable);

	if (json.isObject()) {
		root->setText(2, _("Object"));
		walk_object(json.object(), root);
	} else if (json.isArray()) {
		root->setText(2, _("Array"));
		walk_array(json.array(), root);
	}

	m_view->insertTopLevelItem(0, root);

	if (g_expand)
		m_view->expandAll();

	for (int i = 0; i < 3; i++) {
		if (g_resize)
			m_view->resizeColumnToContents(i);
		else
			m_view->setColumnWidth(i, g_width);
	}

	QStringList headers;
	headers << _("Node") << _("Value") << _("Type");
	m_view->setHeaderLabels(headers);

	m_view->setSelectionMode(QAbstractItemView::SingleSelection);
	m_view->setSelectionBehavior(QAbstractItemView::SelectItems);

	if (g_sorting)
		m_view->setSortingEnabled(true);

	QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
	return true;
}

void JsonViewerWidget::onSave()
{
	if (QWidget *fw = QApplication::focusWidget()) {
		if (m_view->isAncestorOf(fw)) {
			fw->clearFocus();
		}
	}
	saveFile(m_currentFile);

	m_activeInput = nullptr;
	restoreFocusToDC();
}

void JsonViewerWidget::onSaveAs()
{
	QString path = QFileDialog::getSaveFileName(this, "Save JSON As", m_currentFile);
	if (!path.isEmpty()) {
		saveFile(path);
	}
}

void JsonViewerWidget::onReload()
{
	if (QWidget *fw = QApplication::focusWidget()) {
		if (m_view->isAncestorOf(fw)) {
			fw->clearFocus();
		}
	}
	loadFile(m_currentFile);
}

void JsonViewerWidget::saveFile(const QString& filePath)
{
	QTreeWidgetItem *root = m_view->topLevelItem(0);
	if (!root) return;

	QJsonDocument doc;
	QJsonValue rootVal = treeToJson(root);
	if (rootVal.isObject()) doc.setObject(rootVal.toObject());
	else if (rootVal.isArray()) doc.setArray(rootVal.toArray());
	
	QFile file(filePath);
	if (!file.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::warning(this, "Error", "Could not open file for writing.");
		return;
	}
	
	file.write(doc.toJson(QJsonDocument::Indented));
	file.close();
	m_currentFile = filePath;
}

QString JsonViewerWidget::getJsonPath(QTreeWidgetItem *item)
{
	QString path;
	while (item) {
		QString key = item->text(0);
		if (item->parent()) {
			if (item->parent()->text(2) == _("Array")) {
				path = key + path;
			} else {
				path = "." + key + path;
			}
		} else {
			path = key + path;
		}
		item = item->parent();
	}
	return path;
}

QString JsonViewerWidget::getSubtreeText(QTreeWidgetItem *item)
{
	QJsonValue val = treeToJson(item);
	if (item->parent() && item->parent()->text(2) == _("Object")) {
		QJsonObject wrapper;
		wrapper.insert(item->text(0), val);
		QJsonDocument doc(wrapper);
		QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
		
		if (jsonStr.startsWith("{") && jsonStr.endsWith("}")) {
			int firstNewline = jsonStr.indexOf('\n');
			int lastNewline = jsonStr.lastIndexOf('\n');
			if (firstNewline != -1 && lastNewline != -1 && firstNewline < lastNewline) {
				jsonStr = jsonStr.mid(firstNewline + 1, lastNewline - firstNewline - 1);
			} else {
				jsonStr = jsonStr.mid(1, jsonStr.length() - 2);
			}
		}
		
		QStringList lines = jsonStr.split('\n');
		for (int i = 0; i < lines.size(); ++i) {
			if (lines[i].startsWith("    ")) {
				lines[i] = lines[i].mid(4);
			}
		}
		return lines.join('\n').trimmed();
	} else if (item->parent() && item->parent()->text(2) == _("Array")) {
		QJsonArray wrapper;
		wrapper.append(val);
		QJsonDocument doc(wrapper);
		return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
	} else {
		if (val.isObject()) {
			return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Indented)).trimmed();
		} else if (val.isArray()) {
			return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Indented)).trimmed();
		} else {
			return item->text(1);
		}
	}
}

QString JsonViewerWidget::getValueText(QTreeWidgetItem *item)
{
	QJsonValue val = treeToJson(item);
	if (val.isObject()) {
		return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Indented)).trimmed();
	} else if (val.isArray()) {
		return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Indented)).trimmed();
	} else {
		return item->text(1);
	}
}

void JsonViewerWidget::showContextMenu(const QPoint &pos)
{
	QTreeWidgetItem *item = m_view->itemAt(pos);
	if (!item) return;

	QMenu menu(this);
	QAction *actCopyPath = menu.addAction("Copy JSONPath");
	
	bool isComplex = (item->text(2) == _("Object") || item->text(2) == _("Array"));
	QAction *actCopySubtree = nullptr;
	QAction *actCopyKeyValue = nullptr;
	
	if (isComplex) {
		actCopySubtree = menu.addAction("Copy Subtree");
	} else {
		actCopyKeyValue = menu.addAction("Copy Key:Value");
	}

	QAction *actCopyValue = menu.addAction("Copy Value");

	QAction *res = menu.exec(m_view->viewport()->mapToGlobal(pos));
	if (res == actCopyPath) {
		QApplication::clipboard()->setText(getJsonPath(item));
	} else if (actCopySubtree && res == actCopySubtree) {
		QApplication::clipboard()->setText(getSubtreeText(item));
	} else if (actCopyKeyValue && res == actCopyKeyValue) {
		QApplication::clipboard()->setText(getSubtreeText(item));
	} else if (res == actCopyValue) {
		QApplication::clipboard()->setText(getValueText(item));
	}
}

HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	if (!QApplication::instance())
		return nullptr;

	JsonViewerWidget *widget = new JsonViewerWidget((QWidget*)ParentWin);
	if (!widget->loadFile(FileToLoad)) {
		delete widget;
		return nullptr;
	}

	widget->show();
	return widget;
}

void DCPCALL ListCloseWindow(HANDLE ListWin)
{
	JsonViewerWidget *widget = (JsonViewerWidget*)ListWin;
	delete widget;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
	JsonViewerWidget *widget = (JsonViewerWidget*)ListWin;
	QTreeWidget *view = widget->view();

	if (Command == lc_copy)
	{
		QTreeWidgetItem *item = view->currentItem();
		if (item) {
			QString text(item->text(view->currentColumn()));
			if (!text.isEmpty())
				QApplication::clipboard()->setText(text);
		}
		return LISTPLUGIN_OK;
	}

	return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
	JsonViewerWidget *widget = (JsonViewerWidget*)ListWin;
	QTreeWidget *view = widget->view();
	QList<QTreeWidgetItem*> list;
	Qt::MatchFlags sflags = Qt::MatchContains | Qt::MatchRecursive;

	if (SearchParameter & lcs_matchcase)
		sflags |= Qt::MatchCaseSensitive;

	QString needle(SearchString);
	QString prev = view->property("needle").value<QString>();
	view->setProperty("needle", needle);

	list = view->findItems(QString(SearchString), sflags, view->currentColumn());

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
			view->setCurrentItem(list.at(i), view->currentColumn());
			view->setProperty("findit", i);
			return LISTPLUGIN_OK;
		}
	}

	QMessageBox::information(widget, "", QString::asprintf(_("\"%s\" not found!"), SearchString));
	return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	snprintf(DetectString, maxlen - 1, "SIZE<30000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	QFileInfo defini(QString::fromStdString(dps->DefaultIniName));
	QString cfgpath = defini.absolutePath() + "/j2969719.ini";
	QSettings settings(cfgpath, QSettings::IniFormat);

	if (!settings.contains(PLUGNAME "/resize_columns"))
		settings.setValue(PLUGNAME "/resize_columns", g_resize);
	else
		g_resize = settings.value(PLUGNAME "/resize_columns").toBool();

	if (!settings.contains(PLUGNAME "/tree_expand"))
		settings.setValue(PLUGNAME "/tree_expand", g_expand);
	else
		g_expand = settings.value(PLUGNAME "/tree_expand").toBool();

	if (!settings.contains(PLUGNAME "/column_width"))
		settings.setValue(PLUGNAME "/column_width", g_width);
	else
	{
		g_width = settings.value(PLUGNAME "/column_width").toInt();

		if (g_width < 10)
		{
			g_width = 10;
			settings.setValue(PLUGNAME "/column_width", 10);
		}
	}

	if (!settings.contains(PLUGNAME "/sorting"))
		settings.setValue(PLUGNAME "/sorting", g_sorting);
	else
		g_sorting = settings.value(PLUGNAME "/sorting").toBool();

	if (!settings.contains(PLUGNAME "/show_filename"))
		settings.setValue(PLUGNAME "/show_filename", g_filename);
	else
		g_filename = settings.value(PLUGNAME "/show_filename").toBool();

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
