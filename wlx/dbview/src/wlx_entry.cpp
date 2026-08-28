#include <QApplication>
#include <QClipboard>
#include <QWidget>
#include <QTableView>
#include <QAbstractItemModel>
#include <QHash>

#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <wlxbase_wlqt/CrashLogger.h>

#include "wlxplugin.h"
#include "DbViewWidget.h"

// ---------------------------------------------------------------------------
// Widget instance tracking — prevents DC's file-change reload cycle from
// destroying the plugin widget (which causes focus loss and flicker).
// Same approach as the Kate editor WLX plugin.
// ---------------------------------------------------------------------------

static QHash<void*, DbViewWidget*> g_instances;

static void trackParentLifetime(QWidget *parent, void *key) {
    QObject::connect(parent, &QObject::destroyed, parent, [key]() {
        DbViewWidget *w = g_instances.take(key);
        delete w;
    });
}

// ---------------------------------------------------------------------------
// WLX Plugin Entry Points — all wrapped with WLX_TRY/WLX_CATCH
// ---------------------------------------------------------------------------

HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
    WLX_TRY {
        Q_UNUSED(ShowFlags);
        if (!QApplication::instance())
            return nullptr;

        auto *parentWidget = reinterpret_cast<QWidget*>(ParentWin);
        const QString path = QString::fromUtf8(FileToLoad);

        // Check for an existing widget on this parent (DC reload cycle)
        DbViewWidget *widget = g_instances.value(ParentWin, nullptr);
        if (widget && widget->parentWidget() == parentWidget) {
            // Same file → DC is reloading due to file-change detection,
            // or user navigated away and came back.
            // Discard any uncommitted edits and show the existing widget.
            if (widget->currentFilePath() == path) {
                widget->discardPendingChanges();
                widget->show();
                return reinterpret_cast<HWND>(widget);
            }
            // Different file → user selected a new file.
            // Destroy the old widget and fall through to create a new one.
            g_instances.remove(ParentWin);
            delete widget;
            widget = nullptr;
        }

        widget = new DbViewWidget(parentWidget);
        if (!widget->loadFile(path)) {
            delete widget;
            return nullptr;
        }

        g_instances.insert(ParentWin, widget);
        trackParentLifetime(parentWidget, ParentWin);
        widget->show();
        return reinterpret_cast<HWND>(widget);
    } WLX_CATCH("ListLoad");
    return nullptr;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    WLX_TRY {
        // DC calls ListCloseWindow during its internal "reload" cycle.
        // Hide instead of delete so the widget survives the cycle.
        // Cleanup happens when the parent container is destroyed.
        auto *widget = reinterpret_cast<DbViewWidget*>(ListWin);
        if (widget)
            widget->hide();
    } WLX_CATCH("ListCloseWindow");
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    WLX_TRY {
        auto *widget = reinterpret_cast<DbViewWidget*>(ListWin);

        switch (Command) {
        case lc_copy: {
            QString text = widget->getSelectionAsText('\t');
            if (text.isEmpty()) return LISTPLUGIN_ERROR;
            QApplication::clipboard()->setText(text);
            break;
        }
        case lc_selectall:
            if (widget->grid() && widget->grid()->view())
                widget->grid()->view()->selectAll();
            break;
        case lc_newparams:
            // DC sends lc_newparams when it detects file changes in the
            // viewed directory (e.g. DB engine writing LOG/WAL files).
            // Returning LISTPLUGIN_ERROR causes DC to destroy/recreate the
            // plugin. Accept and ignore to prevent the cycle.
            return LISTPLUGIN_OK;
        case lc_focus:
            if (Parameter) {
                widget->focusManager()->setActive(true);
                if (widget->grid() && widget->grid()->view())
                    widget->grid()->view()->setFocus(Qt::OtherFocusReason);
            } else {
                widget->focusManager()->setActive(false);
                if (QWidget *fw = QApplication::focusWidget()) {
                    if (fw == widget || widget->isAncestorOf(fw))
                        fw->clearFocus();
                }
            }
            break;
        default:
            return LISTPLUGIN_ERROR;
        }
        return LISTPLUGIN_OK;
    } WLX_CATCH("ListSendCommand");
    return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
    WLX_TRY {
        auto *widget = reinterpret_cast<DbViewWidget*>(ListWin);
        if (!widget->grid() || !widget->grid()->view() || !widget->grid()->view()->model())
            return LISTPLUGIN_ERROR;

        QAbstractItemModel *model = widget->grid()->view()->model();
        QString needle = QString::fromUtf8(SearchString);
        QTableView *view = widget->grid()->view();

        // Track search state
        QString prev = view->property("needle").value<QString>();
        view->setProperty("needle", needle);

        QModelIndex current = view->currentIndex();
        int startRow = 0, startCol = 0;
        if (current.isValid()) {
            startRow = current.row();
            startCol = current.column();
        }

        bool first = (needle != prev) || (SearchParameter & lcs_findfirst);
        bool backward = (SearchParameter & lcs_backwards);

        if (!first) {
            if (backward) {
                startCol--;
                if (startCol < 0) { startCol = model->columnCount() - 1; startRow--; }
            } else {
                startCol++;
                if (startCol >= model->columnCount()) { startCol = 0; startRow++; }
            }
        }

        int rows = model->rowCount();
        int cols = model->columnCount();
        int total = rows * cols;

        for (int i = 0; i < total; ++i) {
            int offset = backward ? -i : i;
            int linearStart = startRow * cols + startCol;
            int linearIdx = (linearStart + offset + total) % total;
            int r = linearIdx / cols;
            int c = linearIdx % cols;

            QModelIndex idx = model->index(r, c);
            QString cellText = model->data(idx, Qt::DisplayRole).toString();

            bool match = (SearchParameter & lcs_matchcase)
                ? cellText.contains(needle, Qt::CaseSensitive)
                : cellText.contains(needle, Qt::CaseInsensitive);

            if (match) {
                view->setCurrentIndex(idx);
                view->scrollTo(idx);
                return LISTPLUGIN_OK;
            }
        }

        return LISTPLUGIN_ERROR;
    } WLX_CATCH("ListSearchText");
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
#ifdef ENABLE_ROCKSDB_LEVELDB
    snprintf(DetectString, maxlen - 1,
        "EXT=\"DB\" | EXT=\"SQLITE\" | EXT=\"SQLITE3\" | EXT=\"DB3\" | "
        "EXT=\"DUCKDB\" | EXT=\"LDB\" | EXT=\"SST\" | EXT=\"LMDB\" | "
        "EXT=\"BDB\" | EXT=\"FDB\" | EXT=\"MDB\" | EXT=\"ACCDB\" | "
        "EXT=\"PARQUET\" | EXT=\"PQ\"");
#else
    snprintf(DetectString, maxlen - 1,
        "EXT=\"DB\" | EXT=\"SQLITE\" | EXT=\"SQLITE3\" | EXT=\"DB3\" | "
        "EXT=\"DUCKDB\" | EXT=\"LMDB\" | "
        "EXT=\"BDB\" | EXT=\"FDB\" | EXT=\"MDB\" | EXT=\"ACCDB\" | "
        "EXT=\"PARQUET\" | EXT=\"PQ\"");
#endif
}
