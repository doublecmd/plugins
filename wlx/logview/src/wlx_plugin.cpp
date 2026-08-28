#include "../../../sdk/wlxplugin.h"
#include <QWidget>
#include <QDebug>
#include <QTimer>
#include <exception>

#include <QString>
#include "LogViewerWidget.h"

QString g_iniPath;

// Define a visibility macro for exported functions
#define EXPORT __attribute__((visibility("default")))

extern "C" {

EXPORT void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps) {
    // DC's DefaultIniName is always "<config dir>/wlx.ini" -- a name shared
    // by every WLX plugin that doesn't override it, not something unique to
    // this one. Keep the directory DC handed us, but pick our own filename
    // so this plugin doesn't collide with (or share settings with) whatever
    // other plugin last wrote wlx.ini.
    if (dps) {
        QString iniName = QString::fromUtf8(dps->DefaultIniName);
        int slash = iniName.lastIndexOf('/');
        QString dir = slash < 0 ? QStringLiteral(".") : iniName.left(slash);
        g_iniPath = dir + QStringLiteral("/logview.ini");
    }
}


EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    try {
        
        // On Double Commander's Qt6 widgetset, ParentWin is a QWidget* pointer
        // (not an X11 window ID). Simply cast and use as the parent widget.
        QWidget *parentWidget = (QWidget*)ParentWin;
        
        LogViewerWidget *viewer = new LogViewerWidget(parentWidget);

        // Load data immediately so the model and QFileSystemWatcher are
        // set up right away. This also prevents a race where ListLoadNext
        // could be called before a deferred loadFile executes.
        viewer->loadFile(QString::fromUtf8(FileToLoad));

        // FOCUS LAYER 0 — Deferred show().
        // When DC user clicks a file, DC sends MousePress, then immediately
        // calls ListLoad. If we show() synchronously, the new widget can
        // trap DC's MouseRelease event, causing a "phantom-drag" state where
        // DC believes the mouse is still held down.
        // By deferring show() by 50ms, DC finishes its mouse event processing.
        QTimer::singleShot(50, viewer, [viewer]() {
            viewer->show();
        });
        
        return (HWND)viewer;
    } catch (const std::exception& e) {
        qCritical() << "ListLoad exception:" << e.what();
    } catch (...) {
        qCritical() << "ListLoad unknown exception";
    }
    return nullptr;
}

EXPORT int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    try {
        
        // PluginWin is the QWidget* pointer returned by ListLoad
        LogViewerWidget *viewer = qobject_cast<LogViewerWidget*>((QWidget*)PluginWin);
        if (viewer) {
            viewer->loadFile(QString::fromUtf8(FileToLoad));
            return LISTPLUGIN_OK;
        }
    } catch (const std::exception& e) {
        qCritical() << "ListLoadNext exception:" << e.what();
    } catch (...) {
        qCritical() << "ListLoadNext unknown exception";
    }
    return LISTPLUGIN_ERROR;
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin) {
    try {
        // ListWin is the QWidget* pointer returned by ListLoad
        QWidget *widget = (QWidget*)ListWin;
        if (widget) {
            delete widget; // Deterministic destruction
        }
    } catch (const std::exception& e) {
        qCritical() << "ListCloseWindow exception:" << e.what();
    } catch (...) {
        qCritical() << "ListCloseWindow unknown exception";
    }
}

EXPORT int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    try {
        // ListWin is the QWidget* pointer returned by ListLoad
        LogViewerWidget *viewer = qobject_cast<LogViewerWidget*>((QWidget*)ListWin);
        if (viewer) {
            viewer->triggerSearch(QString::fromUtf8(SearchString), SearchParameter);
            return LISTPLUGIN_OK;
        }
    } catch (const std::exception& e) {
        qCritical() << "ListSearchText exception:" << e.what();
    } catch (...) {
        qCritical() << "ListSearchText unknown exception";
    }
    return LISTPLUGIN_ERROR;
}

EXPORT void DCPCALL ListGetDetectString(char* DetectString, int maxlen) {
    // Defines default extensions Double Commander will associate with this plugin.
    // "No extension" cannot be easily expressed here, so users can still manually force it.
    const char* detect = "EXT=\"LOG\" | EXT=\"OUT\" | EXT=\"ERR\" | EXT=\"NDJSON\" | EXT=\"JSONL\" | EXT=\"1\" | EXT=\"2\" | EXT=\"OLD\"";
    strncpy(DetectString, detect, maxlen - 1);
    DetectString[maxlen - 1] = '\0';
}

} // extern "C"
