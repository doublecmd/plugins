/*Лицензия:*/
/*Эта программа является свободным программным обеспечением*/
/*Вы можете распространять и/или изменять*/
/*Его в соответствии с условиями GNU General Public License, опубликованной*/
/*Free Software Foundation, версии 2, либо (По вашему выбору) любой более поздней версии.*/
/*Эта программа распространяется в надежде, что она будет полезна,*/
/*Но БЕЗ КАКИХ-ЛИБО ГАРАНТИЙ*/


#include <stdio.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


Window ListLoad (Window ParentWin, char* FileToLoad, int ShowFlags)
{

/*запуск скрипта в дочернем процессе*/
/*в buf1 записывается вывод stdout*/
	char *buf1;
	buf1 = (char*)calloc(300000, sizeof(char));
	int fds1[2];
	pid_t pid;
	pipe(fds1);
	pid = fork();
	if (pid == (pid_t) 0)
		{
		close(fds1[0]);
		dup2(fds1[1], STDOUT_FILENO);
		close(fds1[1]);
		execlp("/home/user/.config/doublecmd/fileinfo.sh", "fileinfo.sh", FileToLoad, 0);
	}
	else
	{
		FILE *stream1;
		close(fds1[1]);
		stream1 = fdopen(fds1[0], "r");
		fread(buf1, sizeof(char) * 300000 - 1, 1, stream1);
		close(fds1[0]);
		int status;
		waitpid(pid, &status, 0);
	}
/*------------------------------------*/

	printf("%s\n", buf1);

/*создание виджета*/
	GtkWidget *gFix; /*основной контейнер*/
	GtkWidget *scroll; /*что-бы была прокрутка*/
	GtkWidget *vp; /*ViewPort*/
	GtkWidget *label; /*для основного текста (buf1)*/
	
	gFix = gtk_vbox_new(FALSE , 5);
	gtk_container_add(GTK_CONTAINER (GTK_WIDGET(ParentWin)), gFix);
	gtk_widget_show(gFix);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER(gFix), scroll);

	gtk_widget_show (scroll);
/*
	vp = gtk_viewport_new (NULL,NULL);
	gtk_container_add (GTK_CONTAINER (scroll), vp);
	gtk_widget_show (vp);
*/	label = gtk_label_new(NULL);
        gtk_label_set_selectable (GTK_LABEL(label), TRUE);
	gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);
        gtk_widget_modify_font (label, pango_font_description_from_string ("Monospace 11"));
	gtk_label_set_text (GTK_LABEL(label), buf1);
/*	gtk_container_add (GTK_CONTAINER (vp), label);
*/
        gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll), label);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
	gtk_widget_show (label);
	free(buf1);
/*--------------------------------------*/
	return (Window)gFix;

}

void ListCloseWindow(Window ListWin)
{
	gtk_widget_destroy(GTK_WIDGET(ListWin));
}


