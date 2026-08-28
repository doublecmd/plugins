#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtWidgets>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <QGraphicsSvgItem>
#include <QWheelEvent>
#include <QResizeEvent>
#include <cmath>
#include <dlfcn.h>
#include <libintl.h>
#include <locale.h>

#define _(STRING) gettext(STRING)
#define GETTEXT_PACKAGE "plugins"

#include "wlxplugin.h"
#include "DiagramRenderer.h"

static DiagramRenderer::Settings g_settings;
static QString g_configPath;

// Helper to check system dark mode
static bool isSystemDark() {
	QPalette pal = QGuiApplication::palette();
	return pal.color(QPalette::Window).value() < 128;
}

class SvgDiagramViewer : public QGraphicsView {
private:
	QString m_currentFilePath;
	QFileSystemWatcher m_watcher;
	QTimer m_debounceTimer;
	QByteArray m_lastSvgData;

public:
	SvgDiagramViewer(QWidget* parent = nullptr) : QGraphicsView(parent) {
		setScene(new QGraphicsScene(this));
		setDragMode(QGraphicsView::ScrollHandDrag);
		setRenderHint(QPainter::Antialiasing);
		setRenderHint(QPainter::SmoothPixmapTransform);
		setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setFocusPolicy(Qt::NoFocus);

		m_debounceTimer.setSingleShot(true);
		m_debounceTimer.setInterval(200);

		connect(&m_debounceTimer, &QTimer::timeout, this, &SvgDiagramViewer::executeRender);

		connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
			if (g_settings.autoReloadEnabled) {
				m_debounceTimer.start();
			}
		});
	}

	void loadFile(const QString& filePath) {
		m_currentFilePath = filePath;

		if (!m_watcher.files().isEmpty()) {
			m_watcher.removePaths(m_watcher.files());
		}

		if (QFile::exists(filePath)) {
			m_watcher.addPath(filePath);
		}

		executeRender();
	}

	void loadSvgData(const QByteArray& svgData) {
		scene()->clear();

		QGraphicsSvgItem* svgItem = new QGraphicsSvgItem();
		QSvgRenderer* renderer = new QSvgRenderer(svgData, svgItem);
		if (!renderer->isValid()) {
			delete svgItem;
			return;
		}
		svgItem->setSharedRenderer(renderer);
		scene()->addItem(svgItem);

		QRectF bounds = svgItem->boundingRect();
		if (bounds.isEmpty() || !bounds.isValid()) {
			QSize defSize = renderer->defaultSize();
			if (defSize.isValid() && !defSize.isEmpty()) {
				bounds = QRectF(0, 0, defSize.width(), defSize.height());
			} else {
				bounds = renderer->viewBoxF();
			}
		}
		scene()->setSceneRect(bounds);

		if (width() > 10 && height() > 10) {
			fitInView(bounds, Qt::KeepAspectRatio);
		}
	}

	void executeRender() {
		if (m_currentFilePath.isEmpty())
			return;

		std::string svgStd;
		QFileInfo fi(m_currentFilePath);
		QString ext = fi.suffix().toLower();

		bool activeDarkMode = g_settings.useSystemDarkMode ? isSystemDark() : g_settings.darkMode;
		std::string pathStd = m_currentFilePath.toStdString();

		if (ext == "mmd" || ext == "mermaid") {
			svgStd = DiagramRenderer::renderMermaid(g_settings, pathStd, activeDarkMode);
			if (svgStd.empty()) {
				QMessageBox::critical(this, _("Diagram Viewer Error"),
					_("Failed to render Mermaid diagram.\n"
					  "Please ensure '@mermaid-js/mermaid-cli' is installed, 'npx' is available, or internet connection is active."));
				return;
			}
			svgStd = DiagramRenderer::fixMermaidSvgText(svgStd, activeDarkMode);
		} else if (ext == "puml" || ext == "plantuml") {
			svgStd = DiagramRenderer::renderPlantUml(g_settings, pathStd, activeDarkMode);
			if (svgStd.empty()) {
				QMessageBox::critical(this, _("Diagram Viewer Error"),
					_("Failed to render PlantUML diagram.\n"
					  "Please ensure Java/PlantUML is installed locally, or internet connection is active."));
				return;
			}
		} else {
			QMessageBox::critical(this, _("Diagram Viewer Error"), _("Unsupported file extension: ") + ext);
			return;
		}

		QByteArray svgData = QByteArray::fromStdString(svgStd);
		if (!svgData.isEmpty()) {
			m_lastSvgData = svgData;
			loadSvgData(svgData);
		}

		// Re-add to watcher in case editor used atomic save (delete/rename)
		if (QFile::exists(m_currentFilePath) && !m_watcher.files().contains(m_currentFilePath)) {
			m_watcher.addPath(m_currentFilePath);
		}
	}

	void saveAsSvg() {
		if (m_lastSvgData.isEmpty())
			return;
		QString filePath = QFileDialog::getSaveFileName(this, _("Save as SVG"), QString(), _("SVG Files (*.svg)"));
		if (!filePath.isEmpty()) {
			QFile file(filePath);
			if (file.open(QIODevice::WriteOnly)) {
				file.write(m_lastSvgData);
				file.close();
			} else {
				QMessageBox::warning(this, _("Error"), _("Could not open file for writing."));
			}
		}
	}

	void saveAsPng() {
		if (m_lastSvgData.isEmpty())
			return;
		QSvgRenderer renderer(m_lastSvgData);
		QSize size = renderer.defaultSize();
		if (!size.isValid() || size.isEmpty()) {
			size = QSize(800, 600);
		}
		QImage image(size, QImage::Format_ARGB32);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		renderer.render(&painter);
		painter.end();

		QString filePath = QFileDialog::getSaveFileName(this, _("Save as PNG"), QString(), _("PNG Files (*.png)"));
		if (!filePath.isEmpty()) {
			if (!image.save(filePath, "PNG")) {
				QMessageBox::warning(this, _("Error"), _("Could not save PNG file."));
			}
		}
	}

	void copyToClipboard() {
		if (m_lastSvgData.isEmpty())
			return;
		QSvgRenderer renderer(m_lastSvgData);
		QSize size = renderer.defaultSize();
		if (!size.isValid() || size.isEmpty()) {
			size = QSize(800, 600);
		}
		QImage image(size, QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		renderer.render(&painter);
		painter.end();

		QGuiApplication::clipboard()->setImage(image);
	}

protected:
	void wheelEvent(QWheelEvent* event) override {
		const double scaleFactor = 1.15;
		if (event->angleDelta().y() > 0) {
			scale(scaleFactor, scaleFactor);
		} else {
			scale(1.0 / scaleFactor, 1.0 / scaleFactor);
		}
	}

	void resizeEvent(QResizeEvent* event) override {
		QGraphicsView::resizeEvent(event);
		if (scene() && !scene()->sceneRect().isEmpty()) {
			fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
		}
	}

	void drawBackground(QPainter* painter, const QRectF& rect) override {
		bool activeDarkMode = g_settings.useSystemDarkMode ? isSystemDark() : g_settings.darkMode;
		QColor bgColor = activeDarkMode ? QColor(30, 30, 46) : QColor(248, 249, 250);
		QColor gridColor = activeDarkMode ? QColor(45, 45, 68) : QColor(226, 232, 240);

		painter->fillRect(rect, bgColor);

		painter->save();
		painter->setPen(QPen(gridColor, 1, Qt::DotLine));

		qreal gridSpacing = 20.0;
		qreal left = std::floor(rect.left() / gridSpacing) * gridSpacing;
		qreal top = std::floor(rect.top() / gridSpacing) * gridSpacing;

		for (qreal x = left; x < rect.right(); x += gridSpacing) {
			painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
		}
		for (qreal y = top; y < rect.bottom(); y += gridSpacing) {
			painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
		}
		painter->restore();
	}

	void contextMenuEvent(QContextMenuEvent* event) override {
		QMenu menu(this);

		QAction* reloadAction = menu.addAction(_("Reload Diagram"));
		connect(reloadAction, &QAction::triggered, this, &SvgDiagramViewer::executeRender);

		menu.addSeparator();

		QAction* saveAsSvgAction = menu.addAction(_("Save as SVG..."));
		connect(saveAsSvgAction, &QAction::triggered, this, &SvgDiagramViewer::saveAsSvg);

		QAction* saveAsPngAction = menu.addAction(_("Save as PNG..."));
		connect(saveAsPngAction, &QAction::triggered, this, &SvgDiagramViewer::saveAsPng);

		QAction* copyAction = menu.addAction(_("Copy Image to Clipboard"));
		connect(copyAction, &QAction::triggered, this, &SvgDiagramViewer::copyToClipboard);

		menu.addSeparator();

		QAction* toggleAutoAction = menu.addAction(_("Auto-Reload on Save"));
		toggleAutoAction->setCheckable(true);
		toggleAutoAction->setChecked(g_settings.autoReloadEnabled);
		connect(toggleAutoAction, &QAction::triggered, this, [](bool checked) {
			g_settings.autoReloadEnabled = checked;
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
		});

		QAction* toggleSystemDarkAction = menu.addAction(_("Use System Dark Mode"));
		toggleSystemDarkAction->setCheckable(true);
		toggleSystemDarkAction->setChecked(g_settings.useSystemDarkMode);
		connect(toggleSystemDarkAction, &QAction::triggered, this, [this](bool checked) {
			g_settings.useSystemDarkMode = checked;
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		QAction* toggleDarkAction = menu.addAction(_("Force Dark Mode"));
		toggleDarkAction->setCheckable(true);
		toggleDarkAction->setChecked(g_settings.darkMode);
		toggleDarkAction->setEnabled(!g_settings.useSystemDarkMode);
		connect(toggleDarkAction, &QAction::triggered, this, [this](bool checked) {
			g_settings.darkMode = checked;
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		menu.addSeparator();

		QMenu* mermaidMenu = menu.addMenu(_("Mermaid Renderer"));
		QAction* mermaidLocalAction = mermaidMenu->addAction(_("Local (mmdc/npx)"));
		mermaidLocalAction->setCheckable(true);
		mermaidLocalAction->setChecked(g_settings.mermaidRenderer == "local");
		connect(mermaidLocalAction, &QAction::triggered, this, [this]() {
			g_settings.mermaidRenderer = "local";
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		QAction* mermaidWebAction = mermaidMenu->addAction(_("Web (mermaid.ink)"));
		mermaidWebAction->setCheckable(true);
		mermaidWebAction->setChecked(g_settings.mermaidRenderer == "web");
		connect(mermaidWebAction, &QAction::triggered, this, [this]() {
			g_settings.mermaidRenderer = "web";
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		QMenu* plantumlMenu = menu.addMenu(_("PlantUML Renderer"));
		QAction* pumlLocalAction = plantumlMenu->addAction(_("Local (native/java)"));
		pumlLocalAction->setCheckable(true);
		pumlLocalAction->setChecked(g_settings.renderer == "java");
		connect(pumlLocalAction, &QAction::triggered, this, [this]() {
			g_settings.renderer = "java";
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		QAction* pumlWebAction = plantumlMenu->addAction(_("Web (plantuml.com)"));
		pumlWebAction->setCheckable(true);
		pumlWebAction->setChecked(g_settings.renderer == "web");
		connect(pumlWebAction, &QAction::triggered, this, [this]() {
			g_settings.renderer = "web";
			g_settings.save(g_configPath.toStdString(), PLUGNAME);
			executeRender();
		});

		menu.exec(event->globalPos());
	}
};

extern "C" {

HANDLE DCPCALL ListLoad(HANDLE ParentWin, char* FileToLoad, int ShowFlags)
{
	if (!QApplication::instance())
		return nullptr;

	QFileInfo fi(FileToLoad);
	QString ext = fi.suffix().toLower();
	if (ext != "mmd" && ext != "mermaid" && ext != "puml" && ext != "plantuml") {
		return nullptr;
	}

	SvgDiagramViewer* viewer = new SvgDiagramViewer((QWidget*)ParentWin);
	viewer->loadFile(QString(FileToLoad));
	viewer->show();

	return viewer;
}

void DCPCALL ListCloseWindow(HANDLE ListWin)
{
	SvgDiagramViewer* viewer = (SvgDiagramViewer*)ListWin;
	delete viewer;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
	SvgDiagramViewer* viewer = (SvgDiagramViewer*)ListWin;

	if (Command == lc_newparams) {
		viewer->executeRender();
		return LISTPLUGIN_OK;
	}

	if (Command == lc_copy) {
		viewer->copyToClipboard();
		return LISTPLUGIN_OK;
	}

	return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
	return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	snprintf(DetectString, maxlen - 1, "(EXT=\"PUML\" | EXT=\"PLANTUML\" | EXT=\"MMD\" | EXT=\"MERMAID\") & SIZE<30000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	QFileInfo defini(QString::fromStdString(dps->DefaultIniName));
	g_configPath = defini.absolutePath() + "/diagramview.ini";
	g_settings.loadOrInitDefaults(g_configPath.toStdString(), PLUGNAME);

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

} // extern "C"
