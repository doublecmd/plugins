#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>
#include "common.h"

HWND DCPCALL ListLoad (HWND ParentWin, char* FileToLoad, int ShowFlags)
{

	GtkWidget *gFix;

	gFix = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER((GtkWidget*)(ParentWin)), gFix);
	WebKitWebView* webView = WEBKIT_WEB_VIEW(webkit_web_view_new());
	gchar* fileUri = g_filename_to_uri(FileToLoad, NULL, NULL);
	webkit_web_view_load_uri(webView, fileUri);
	gtk_widget_grab_focus(GTK_WIDGET(webView));
	gtk_container_add (GTK_CONTAINER (gFix), GTK_WIDGET(webView));

	gtk_widget_show_all (gFix);
	return gFix;
}

void ListCloseWindow(HWND ListWin)
{
	gtk_widget_destroy(ListWin);
}

void DCPCALL ListGetDetectString(char* DetectString,int maxlen)
{
	strncpy(DetectString, "(EXT=\"HTML\")|(EXT=\"HTM\")", maxlen);
}
