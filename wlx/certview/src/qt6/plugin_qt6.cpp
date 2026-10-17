#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QFileInfo>
#include <QDebug>
#include <cstdio>
#include <cstring>

#include <wlxbase_wlqt/CrashLogger.h>
#include "wlxplugin.h"
#include "CertViewWidget.h"

#define EXPORT __attribute__((visibility("default")))

extern "C" {

EXPORT void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
    (void)dps;
}

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
    FILE *log = fopen("/tmp/certview.log", "a");
    if (log) {
        fprintf(log, "[certview] ListLoad called for: %s, ParentWin=%p\n", FileToLoad ? FileToLoad : "(null)", ParentWin);
        fclose(log);
    }

    WLX_TRY {
        Q_UNUSED(ShowFlags);

        if (!FileToLoad)
            return nullptr;

        QWidget *parentWidget = reinterpret_cast<QWidget*>(ParentWin);
        auto *widget = new certview::CertViewWidget(parentWidget);

        if (!widget->loadFile(QString::fromUtf8(FileToLoad))) {
            FILE *errLog = fopen("/tmp/certview.log", "a");
            if (errLog) {
                fprintf(errLog, "[certview] loadFile returned false for: %s\n", FileToLoad);
                fclose(errLog);
            }
            delete widget;
            return nullptr;
        }

        widget->show();
        return reinterpret_cast<HWND>(widget);
    } WLX_CATCH("ListLoad");
    return nullptr;
}

EXPORT int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags)
{
    FILE *log = fopen("/tmp/certview.log", "a");
    if (log) {
        fprintf(log, "[certview] ListLoadNext called for: %s\n", FileToLoad ? FileToLoad : "(null)");
        fclose(log);
    }

    WLX_TRY {
        Q_UNUSED(ParentWin);
        Q_UNUSED(ShowFlags);

        auto *widget = qobject_cast<certview::CertViewWidget*>(reinterpret_cast<QWidget*>(PluginWin));
        if (widget && FileToLoad) {
            if (widget->loadFile(QString::fromUtf8(FileToLoad))) {
                return LISTPLUGIN_OK;
            }
        }
    } WLX_CATCH("ListLoadNext");
    return LISTPLUGIN_ERROR;
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin)
{
    WLX_TRY {
        if (!ListWin) return;
        auto *widget = reinterpret_cast<QWidget*>(ListWin);
        delete widget;
    } WLX_CATCH("ListCloseWindow");
}

EXPORT int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    WLX_TRY {
        auto *widget = reinterpret_cast<certview::CertViewWidget*>(ListWin);
        switch (Command) {
        case lc_copy:
            widget->copySummaryToClipboard();
            return LISTPLUGIN_OK;
        case lc_focus:
            if (Parameter) {
                widget->setFocus(Qt::OtherFocusReason);
            } else {
                widget->clearFocus();
            }
            return LISTPLUGIN_OK;
        default:
            return LISTPLUGIN_ERROR;
        }
    } WLX_CATCH("ListSendCommand");
    return LISTPLUGIN_ERROR;
}

EXPORT void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
    const char* detect = "EXT=\"CER\" | EXT=\"CRT\" | EXT=\"DER\" | EXT=\"PEM\" | "
                         "EXT=\"P12\" | EXT=\"PFX\" | EXT=\"CSR\" | EXT=\"REQ\" | "
                         "EXT=\"CRL\" | EXT=\"P7B\" | EXT=\"P7C\" | EXT=\"SPC\" | "
                         "EXT=\"KBX\" | EXT=\"ASC\" | EXT=\"GPG\" | EXT=\"PGP\" | "
                         "EXT=\"PUB\" | EXT=\"KEY\"";
    std::strncpy(DetectString, detect, maxlen - 1);
    DetectString[maxlen - 1] = '\0';
}

} // extern "C"
