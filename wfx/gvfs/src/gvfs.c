/*
   Double Commander
   -------------------------------------------------------------------------
   WFX plugin for working with GVFS

   Copyright (C) 2009-2010  Koblov Alexander (Alexx2000@mail.ru)

   Based on:
     GVFS plugin for Tux Commander
     Copyright (C) 2008-2009 Tomas Bzatek <tbzatek@users.sourceforge.net>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

#include <glib.h>
#include <gio/gio.h>
#include <glib/gtypes.h>

#include "wfxplugin.h"


#define CONST_DEFAULT_QUERY_INFO_ATTRIBUTES     G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_NAME "," \
                                                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_SIZE "," \
                                                G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET "," G_FILE_ATTRIBUTE_TIME_MODIFIED "," \
                                                G_FILE_ATTRIBUTE_TIME_ACCESS "," G_FILE_ATTRIBUTE_TIME_CREATED "," \
                                                G_FILE_ATTRIBUTE_UNIX_MODE "," G_FILE_ATTRIBUTE_UNIX_UID "," \
                                                G_FILE_ATTRIBUTE_UNIX_GID
#define TUXCMD_DEFAULT_COPY_FLAGS G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS

#define GROUP_NAME "gvfs"
#define PathDelim "/"
#define cAddConnection "<Add connection>"
#define cQuickConnection "<Quick connection>"
#define cAnonymous "anonymous"
#define cDefaultIniName "gvfs.ini"
#define IS_DIR_SEP(ch) ((ch) == '/')
#define Int32x32To64(a,b) ((gint64)(a)*(gint64)(b))

typedef int TVFSResult;

typedef struct _Connection
{
  gchar *Name;
  gchar *Type;
  gchar *Host;
  gchar *UserName;
  gchar *Password;
  gchar *Path;
} TConnection, *PConnection;

struct TVFSGlobs {
  PConnection Connection;
  gchar *RemotePath;
  GFile *file;
  GFileEnumerator *enumerator;
  GFile *enumerated_file;

  GMainLoop *mount_main_loop;
  TVFSResult mount_result;
  int mount_try;
  gboolean ftp_anonymous;
};

typedef struct
{
  gchar *Path;
  gint Index;
  GList *list;
  struct TVFSGlobs *globs;
} TListRec, *PListRec;

typedef struct _ProgressInfo 
{
  gchar SourceName[MAX_PATH];
  gchar TargetName[MAX_PATH];
  GCancellable *cancellable;
} TProgressInfo, *PProgressInfo;

//---------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------

int gPluginNumber;
tProgressProc gProgressProc;
tLogProc gLogProc;
tRequestProc gRequestProc;
gchar gDefaultIniName[MAX_PATH];
GList *ActiveConnectionList;
GList *ConnectionList;
PConnection gConnection;

//---------------------------------------------------------------------

unsigned long FileTimeToUnixTime(LPFILETIME ft)
{
	gint64 ll = ft->dwHighDateTime;
	ll = (ll << 32) | ft->dwLowDateTime;
	ll = (ll - 116444736000000000) / 10000000;
	return (unsigned long)ll;
}

gboolean UnixTimeToFileTime(unsigned long mtime, LPFILETIME ft)
{
	gint64 ll = Int32x32To64(mtime, 10000000) + 116444736000000000;
	ft->dwLowDateTime = (DWORD)ll;
	ft->dwHighDateTime = ll >> 32;
	return TRUE;
}

static TVFSResult g_error_to_TVFSResult (GError *error)
{
  g_print ("g_error_to_TVFSResult: code = %d\n", error->code);
  switch (error->code) {
    case G_IO_ERROR_FAILED:
    case G_IO_ERROR_NOT_FOUND:
    case G_IO_ERROR_PERMISSION_DENIED:
      return FS_FILE_NOTFOUND;
      break;
    case G_IO_ERROR_CANCELLED:
      return FS_FILE_USERABORT;
      break;
    case G_IO_ERROR_NOT_SUPPORTED:
    case G_IO_ERROR_FILENAME_TOO_LONG:
      return FS_FILE_NOTSUPPORTED;
      break;
    case G_IO_ERROR_IS_DIRECTORY:
    case G_IO_ERROR_NOT_REGULAR_FILE:
    case G_IO_ERROR_NOT_SYMBOLIC_LINK:
    case G_IO_ERROR_NOT_MOUNTABLE_FILE:
    case G_IO_ERROR_INVALID_FILENAME:
    case G_IO_ERROR_TOO_MANY_LINKS:
    case G_IO_ERROR_INVALID_ARGUMENT:
    case G_IO_ERROR_NOT_DIRECTORY:
    case G_IO_ERROR_NOT_MOUNTED:
    case G_IO_ERROR_ALREADY_MOUNTED:
    case G_IO_ERROR_WRONG_ETAG:
    case G_IO_ERROR_TIMED_OUT:
    case G_IO_ERROR_WOULD_RECURSE:
    case G_IO_ERROR_HOST_NOT_FOUND:
      return FS_FILE_READERROR;
      break;
    case G_IO_ERROR_NO_SPACE:
    case G_IO_ERROR_EXISTS:
    case G_IO_ERROR_NOT_EMPTY:
    case G_IO_ERROR_CLOSED:
    case G_IO_ERROR_PENDING:
    case G_IO_ERROR_READ_ONLY:
    case G_IO_ERROR_CANT_CREATE_BACKUP:
    case G_IO_ERROR_BUSY:
    case G_IO_ERROR_WOULD_BLOCK:
    case G_IO_ERROR_WOULD_MERGE:
      return FS_FILE_WRITEERROR;
      break;
    case G_IO_ERROR_FAILED_HANDLED:
    default:
      return FS_FILE_NOTSUPPORTED;
  }
}

static void ask_password_cb (GMountOperation *op,
                             const char      *message,
                             const char      *default_user,
                             const char      *default_domain,
                             GAskPasswordFlags flags,
                             gpointer          user_data)
{
  struct TVFSGlobs *globs;
  char username[MAX_PATH];
  char password[MAX_PATH];
  int   anonymous;
  char  domain[MAX_PATH];
  GPasswordSave password_save;
  gboolean mount_handled = FALSE;
  
  globs = (struct TVFSGlobs*) user_data;
  g_assert (globs != NULL);
  globs->mount_try++;

  /*  First pass, look if we have a password to supply  */
  if (globs->mount_try == 1) {
    if ((flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) && globs->ftp_anonymous) {
      printf ("(WW) ask_password_cb: mount_try = %d, trying FTP anonymous login...\n", globs->mount_try);
      g_mount_operation_set_anonymous (op, TRUE);
      g_mount_operation_reply (op, G_MOUNT_OPERATION_HANDLED);
      return;
    }
    if ((flags & G_ASK_PASSWORD_NEED_USERNAME) && (globs->Connection->UserName != NULL))
    {
      printf ("(WW) ask_password_cb: mount_try = %d, trying login with saved username...\n", globs->mount_try);
      g_mount_operation_set_username (op, globs->Connection->UserName);
      mount_handled = TRUE;
    }
    if ((flags & G_ASK_PASSWORD_NEED_PASSWORD) && (globs->Connection->Password != NULL))
    {
      printf ("(WW) ask_password_cb: mount_try = %d, trying login with saved password...\n", globs->mount_try);
      g_mount_operation_set_password (op, globs->Connection->Password);
      mount_handled = TRUE;
    }
    if (mount_handled)  
    {
      g_mount_operation_reply (op, G_MOUNT_OPERATION_HANDLED);
      return;
    }  
  }

  /*  Ask user for password  */
  g_print ("(WW) ask_password_cb: mount_try = %d, message = '%s'\n", globs->mount_try, message);

  /*  Handle abort message from certain backends properly  */
  /*   - e.g. SMB backends use this to mask multiple auth callbacks from smbclient  */
  if (default_user && strcmp (default_user, "ABORT") == 0) {
    g_print ("(WW) default_user == \"ABORT\", aborting\n");
    g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
    return;
  }

  username[0] = 0;
  domain[0] = 0;
  password[0] = 0;
  anonymous = FALSE;
  password_save = G_PASSWORD_SAVE_NEVER;

  if (gRequestProc) {
    fprintf (stderr, "  (II) Spawning callback_ask_password (%p)...\n", gRequestProc);

    g_strlcpy(username, default_user, MAX_PATH);
    g_strlcpy(domain, default_domain, MAX_PATH);

    if (flags & G_ASK_PASSWORD_NEED_USERNAME)
    {
      if (gRequestProc(gPluginNumber, RT_UserName, (char *)message, NULL, username, MAX_PATH))
      {
        g_mount_operation_set_username (op, username);
      }
      else
      {
        g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
        return;
      }
    }
    if (flags & G_ASK_PASSWORD_NEED_DOMAIN)
    {
      if (gRequestProc(gPluginNumber, RT_Other, (char *)message, "Domain:", domain, MAX_PATH))
      {
        g_mount_operation_set_domain (op, domain);
      }
      else
      {
        g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
        return;
      }
    }
    if (flags & G_ASK_PASSWORD_NEED_PASSWORD)
    {
      if (gRequestProc(gPluginNumber, RT_Password, (char *)message, NULL, password, MAX_PATH))
      {
        g_mount_operation_set_password (op, password);
      }
      else
      {
        g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
        return;
      }
    }
    if (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED)
      g_mount_operation_set_anonymous (op, anonymous);
    if (flags & G_ASK_PASSWORD_SAVING_SUPPORTED)
      g_mount_operation_set_password_save (op, password_save);

    g_mount_operation_reply (op, G_MOUNT_OPERATION_HANDLED);
    return;
  }
  /*  Unhandled, abort  */
  g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
}

static void ask_question_cb (GMountOperation *op,
                             const gchar     *message,
                             const gchar     *choices[],
                             gpointer         user_data)
{
  struct TVFSGlobs *globs;
  int len;
  int choice;

  globs = (struct TVFSGlobs*) user_data;
  g_assert (globs != NULL);

  g_print ("(WW) ask_question_cb: message = '%s'\n", message);

  len = 0;
  while (choices[len] != NULL) {
    g_print ("(WW) ask_question_cb: choice[%d] = '%s'\n", len, choices[len]);
    len++;
  }

  choice = -1;
/*
  if (globs->callback_ask_question) {
    fprintf (stderr, "  (II) Spawning callback_ask_question (%p)...\n", globs->callback_ask_question);
    // At this moment, only SFTP uses ask_question and the second button is cancellation
    globs->callback_ask_question (message, choices, &choice, 1, globs->callback_data);
    fprintf (stderr, "    (II) Received choice = %d\n", choice);

    if (choice >= 0) {
      g_mount_operation_set_choice (op, choice);
      g_mount_operation_reply (op, G_MOUNT_OPERATION_HANDLED);
    }
    else {
      g_mount_operation_reply (op, G_MOUNT_OPERATION_ABORTED);
    }
    return;
  }
*/
  g_mount_operation_reply (op, G_MOUNT_OPERATION_UNHANDLED);
}

static void mount_done_cb (GObject *object,
                           GAsyncResult *res,
                           gpointer user_data)
{
  struct TVFSGlobs *globs;
  gboolean succeeded;
  GError *error = NULL;

  globs = (struct TVFSGlobs*) user_data;
  g_assert (globs != NULL);

  succeeded = g_file_mount_enclosing_volume_finish (G_FILE (object), res, &error);

  if (! succeeded) {
    g_print ("(EE) Error mounting location: %s\n", error->message);
    globs->mount_result = g_error_to_TVFSResult (error);
    g_error_free (error);
  }
  else {
    globs->mount_result = FS_FILE_OK;
    g_print ("(II) Mount successful.\n");
  }

  g_main_loop_quit (globs->mount_main_loop);
}

static TVFSResult vfs_handle_mount (struct TVFSGlobs *globs, GFile *file)
{
  GMountOperation *op;

  g_print ("(II) Mounting location...\n");

  op = g_mount_operation_new ();
  g_signal_connect (op, "ask-password", (GCallback)ask_password_cb, globs);
  g_signal_connect (op, "ask-question", (GCallback)ask_question_cb, globs);
  globs->mount_result = FS_FILE_NOTFOUND;
  globs->mount_try = 0;
  
  /*  Inspiration taken from Bastien Nocera's http://svn.gnome.org/viewvc/totem-pl-parser/trunk/plparse/totem-disc.c?view=markup  */
  globs->mount_main_loop = g_main_loop_new (NULL, FALSE);
  g_file_mount_enclosing_volume (file, G_MOUNT_MOUNT_NONE, op, NULL, mount_done_cb, globs);
  g_main_loop_run (globs->mount_main_loop);

  g_main_loop_unref (globs->mount_main_loop);
  globs->mount_main_loop = NULL;
  g_object_unref (op);

  return globs->mount_result;
}

struct TVFSGlobs * VFSNew ()
{
  struct TVFSGlobs *globs;

  globs = (struct TVFSGlobs *) malloc (sizeof (struct TVFSGlobs));
  memset (globs, 0, sizeof (struct TVFSGlobs));

  globs->file = NULL;
  globs->enumerator = NULL;
  globs->enumerated_file = NULL;

  return globs;
}

char * VFSGetServices ()
{
  GVfs *gvfs;
  const gchar* const * schemes;
  char *l = NULL;
  char *s;

  gvfs = g_vfs_get_default ();
  g_print ("(II) GVFS: is_active = %d\n", g_vfs_is_active (gvfs));

  schemes = g_vfs_get_supported_uri_schemes (gvfs);
  for (; *schemes; schemes++) {
    if (l) {
      s = g_strdup_printf ("%s;%s", l, *schemes);
      g_free (l);
      l = s;
    }
    else
      l = g_strdup (*schemes);
  }

  g_print ("(II) GVFS: supported schemes: %s\n", l);
  return l;
}

char * VFSGetPrefix (struct TVFSGlobs *globs)
{
  GFile *f;
  char *s;

  if (globs->file) {
    f = g_file_resolve_relative_path (globs->file, "/");
    s = g_file_get_uri (f);
    g_object_unref (f);
    return s;
  }
  else
    return NULL;
}

char * VFSGetPath (struct TVFSGlobs *globs)
{
  GFile *root;
  char *path, *s;

  if (globs->file) {
    root = g_file_resolve_relative_path (globs->file, "/");
    if (root == NULL)
      return NULL;
    path = g_file_get_relative_path (root, globs->file);
    if (path == NULL) {
      g_object_unref (root);
      return NULL;
    }
    if (! g_path_is_absolute (path))
      s = g_strdup_printf ("/%s", path);
    else
      s = g_strdup (path);
    g_print ("(II) VFSGetPath: '%s'\n", s);
    g_free (path);
    g_object_unref (root);
    return s;
  }
  else
    return NULL;
}

char * VFSGetPathURI (struct TVFSGlobs *globs)
{
  if (globs->file)
    return g_file_get_uri (globs->file);
  else
    return NULL;
}

guint64 VFSGetFileSystemFree (struct TVFSGlobs *globs, char *APath)
{
  GFileInfo *info;
  GError *error;
  guint64 res;

  if (globs->file == NULL)
    return 0;

  error = NULL;
  info = g_file_query_filesystem_info (globs->file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, &error);
  if (error) {
    g_print ("(EE) VFSGetFileSystemFree: %s\n", error->message);
    g_error_free (error);
    return 0;
  }
  res = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
  g_object_unref (info);
  return res;
}

guint64 VFSGetFileSystemSize (struct TVFSGlobs *globs, char *APath)
{
  GFileInfo *info;
  GError *error;
  guint64 res;

  if (globs->file == NULL)
    return 0;

  error = NULL;
  info = g_file_query_filesystem_info (globs->file, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE, NULL, &error);
  if (error) {
    g_print ("(EE) VFSGetFileSystemSize: %s\n", error->message);
    g_error_free (error);
    return 0;
  }
  res = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
  g_object_unref (info);
  return res;
}

/**************************************************************************************************************************************/
/**************************************************************************************************************************************/

TVFSResult VFSChangeDir (struct TVFSGlobs *globs, char *NewPath)
{
  GFile *f;
  GFileEnumerator *en;
  GError *error, *error_shortcut;
  TVFSResult res;
  GFileInfo *info;
  gchar *target_uri;

  g_print ("VFSChangeDir: Enter\n");

  if (globs->file == NULL) {
    g_print ("(EE) VFSChangeDir: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  g_print ("(II) VFSChangeDir: changing dir to '%s'\n", NewPath);

  f = g_file_resolve_relative_path (globs->file, NewPath);
  if (f == NULL) {
    g_print ("(EE) VFSChangeDir: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  res = FS_FILE_OK;
  while (1) {
    error = NULL;
    en = g_file_enumerate_children (f, CONST_DEFAULT_QUERY_INFO_ATTRIBUTES,
                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);

    /*  if the target is shortcut, change the URI  */
    if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY)) {
      error_shortcut = NULL;
      info = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error_shortcut);
      if (info) {
        target_uri = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI));
        g_object_unref (info);
        if (target_uri) {
          g_print ("(WW) VFSChangeDir: following shortcut, changing URI to '%s'\n", target_uri);
          g_object_unref (f);
          f = g_file_new_for_uri (target_uri);
          g_free (target_uri);
          g_error_free (error);
          continue;
        }
      }
      if (error_shortcut)
        g_error_free (error_shortcut);
    }
    /*  Mount the target  */
    if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
      g_error_free (error);
      res = vfs_handle_mount (globs, f);
      if (res != FS_FILE_OK) {
        g_object_unref (f);
        return res;
      }
      else
        continue;
    }
    /*  Any other errors --> report  */
    if (error) {
      g_print ("(EE) VFSChangeDir: g_file_enumerate_children() error: %s\n", error->message);
      res = g_error_to_TVFSResult (error);
      g_error_free (error);
      g_object_unref (f);
      return res;
    }
    /*  everything ok?  */
    break;
  }

  globs->enumerator = en;
  globs->enumerated_file = g_file_dup (f);
  g_object_unref (globs->file);
  globs->file = f;

  return res;
}

/**************************************************************************************************************************************/
/**************************************************************************************************************************************/

static void GFileInfoToWin32FindData (GFile *reference_file, GFileInfo *info, WIN32_FIND_DATAA *FindData)
{
  GFileInfo *symlink_info = NULL;
  GError *error = NULL;
  
  g_assert (info != NULL);
  g_assert (FindData != NULL);

  g_strlcpy(FindData->cFileName, g_file_info_get_name (info), MAX_PATH);
  // File size
  goffset filesize = g_file_info_get_size (info);
  FindData->nFileSizeLow = (DWORD)filesize;
  FindData->nFileSizeHigh = filesize >> 32;
  // File attributes
  FindData->dwFileAttributes |= FILE_ATTRIBUTE_UNIX_MODE;
  FindData->dwReserved0 = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE);
  // File date/time
  if (!UnixTimeToFileTime(g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED), &FindData->ftLastWriteTime))
    {
          FindData->ftLastWriteTime.dwHighDateTime = 0xFFFFFFFF;
          FindData->ftLastWriteTime.dwLowDateTime = 0xFFFFFFFE;
	}
  if (!UnixTimeToFileTime(g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_ACCESS), &FindData->ftLastAccessTime))
    {
          FindData->ftLastAccessTime.dwHighDateTime = 0xFFFFFFFF;
          FindData->ftLastAccessTime.dwLowDateTime = 0xFFFFFFFE;
	}
  if (!UnixTimeToFileTime(g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CREATED), &FindData->ftCreationTime))
    {
          FindData->ftCreationTime.dwHighDateTime = 0xFFFFFFFF;
          FindData->ftCreationTime.dwLowDateTime = 0xFFFFFFFE;
	}

//  g_print ("(II) GFileInfoToWin32FindData: type = %d\n", g_file_info_get_file_type (info));
//  g_print ("(II) GFileInfoToWin32FindData: UNIX_MODE = %d\n", FindData->dwReserved0);

  switch (g_file_info_get_file_type (info)) {
      case G_FILE_TYPE_DIRECTORY:
      case G_FILE_TYPE_SHORTCUT:   /*  Used in network:/// */
      case G_FILE_TYPE_MOUNTABLE:
        FindData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        break;
      case G_FILE_TYPE_SYMBOLIC_LINK:
        FindData->dwReserved0 |= S_IFLNK;
	
        // Check: file is symlink to directory
        symlink_info = g_file_query_info (reference_file, G_FILE_ATTRIBUTE_UNIX_MODE,
                                          G_FILE_QUERY_INFO_NONE, NULL, &error);

        if (error) {
          g_print ("(EE) GFileInfoToWin32FindData: g_file_query_info() error: %s\n", error->message);
          g_error_free (error);
        }
        
        if (symlink_info)
	{
          if (g_file_info_get_file_type (symlink_info) == G_FILE_TYPE_DIRECTORY)
	  {	  
	    FindData->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
	  }  
	  g_object_unref (symlink_info);
	}
        break;
      case G_FILE_TYPE_UNKNOWN:
      case G_FILE_TYPE_REGULAR:
      case G_FILE_TYPE_SPECIAL:
	break;
    }

  /*  fallback to default file mode if read fails  */
  if (FindData->dwReserved0 == 0) {
    if ((FindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      FindData->dwReserved0 = S_IFDIR + S_IRWXU + S_IRGRP + S_IXGRP + S_IROTH + S_IXOTH;
    else
      FindData->dwReserved0 = S_IRUSR + S_IWUSR + S_IRGRP + S_IROTH;
  }
}

/**************************************************************************************************************************************/
/**************************************************************************************************************************************/

long VFSFileExists (struct TVFSGlobs *globs, const char *FileName, const long Use_lstat)
{
  GFile *f;
  GError *error;
  GFileInfo *info;

  if (globs->file == NULL) {
    g_print ("(EE) VFSFileExists: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, FileName);
  if (f == NULL) {
    g_print ("(EE) VFSMkDir: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  error = NULL;
  info = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_NAME,
                            Use_lstat ? G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS : G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_object_unref (f);
  if (error) {
//    g_print ("(EE) VFSFileExists: g_file_query_info() error: %s\n", error->message);
    g_error_free (error);
    return FALSE;
  }
  g_object_unref (info);
  return TRUE;
}

TVFSResult VFSFileInfo (struct TVFSGlobs *globs, char *AFileName, WIN32_FIND_DATAA *FindData)
{
  GFile *f;
  GError *error;
  GFileInfo *info;
  TVFSResult res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSFileInfo: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, AFileName);
  if (f == NULL) {
    g_print ("(EE) VFSMkDir: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  error = NULL;
  info = g_file_query_info (f, CONST_DEFAULT_QUERY_INFO_ATTRIBUTES,
                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
  g_object_unref (f);
  if (error) {
    g_print ("(EE) VFSFileInfo: g_file_query_info() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    return res;
  }
  GFileInfoToWin32FindData (f, info, FindData);
  g_object_unref (info);
  g_object_unref (f);
  return FS_FILE_OK;
}

TVFSResult VFSRemove (struct TVFSGlobs *globs, const char *APath)
{
  GFile *f;
  GError *error;
  TVFSResult res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSRemove: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, APath);
  if (f == NULL) {
    g_print ("(EE) VFSRemove: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  error = NULL;
  g_file_delete (f, NULL, &error);
  g_object_unref (f);
  if (error) {
    g_print ("(EE) VFSRemove: g_file_delete() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    return res;
  }
  return FS_FILE_OK;
}

TVFSResult VFSMakeSymLink (struct TVFSGlobs *globs, const char *NewFileName, const char *PointTo)
{
  GFile *f;
  GError *error;
  TVFSResult res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSMakeSymLink: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, NewFileName);
  if (f == NULL) {
    g_print ("(EE) VFSMakeSymLink: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  error = NULL;
  g_file_make_symbolic_link (f, PointTo, NULL, &error);
  g_object_unref (f);
  if (error) {
    g_print ("(EE) VFSMakeSymLink: g_file_make_symbolic_link() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    return res;
  }
  return FS_FILE_OK;
}

TVFSResult VFSChmod (struct TVFSGlobs *globs, const char *FileName, const uint Mode)
{
  GFile *f;
  GError *error;
  TVFSResult res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSChmod: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, FileName);
  if (f == NULL) {
    g_print ("(EE) VFSChmod: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }
//  g_print ("(II) VFSChmod (%s, %d): Going to set permissions on '%s'\n", FileName, Mode, g_file_get_uri (f));

  error = NULL;
  g_file_set_attribute_uint32 (f, G_FILE_ATTRIBUTE_UNIX_MODE, Mode, G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_object_unref (f);
  if (error) {
    g_print ("(EE) VFSChmod: g_file_set_attribute_uint32() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    return res;
  }
  return FS_FILE_OK;
}

TVFSResult VFSChown (struct TVFSGlobs *globs, const char *FileName, const uint UID, const uint GID)
{
  GFile *f;
  GError *error;
  TVFSResult res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSChown: globs->file == NULL !\n");
    return FS_FILE_NOTFOUND;
  }

  f = g_file_resolve_relative_path (globs->file, FileName);
  if (f == NULL) {
    g_print ("(EE) VFSChown: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  error = NULL;
  g_file_set_attribute_uint32 (f, G_FILE_ATTRIBUTE_UNIX_UID, UID, G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error) {
    g_print ("(EE) VFSChown: g_file_set_attribute_uint32() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    g_object_unref (f);
    return res;
  }
  error = NULL;
  g_file_set_attribute_uint32 (f, G_FILE_ATTRIBUTE_UNIX_GID, GID, G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error) {
    g_print ("(EE) VFSChown: g_file_set_attribute_uint32() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    g_object_unref (f);
    return res;
  }
  g_object_unref (f);
  return FS_FILE_OK;
}

/**************************************************************************************************************************************/
/**************************************************************************************************************************************/

gboolean VFSIsOnSameFS (struct TVFSGlobs *globs, const char *Path1, const char *Path2)
{
  GFile *file1, *file2;
  GFileInfo *info1, *info2;
  GError *error;
  gboolean res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSIsOnSameFS: globs->file == NULL !\n");
    return FALSE;
  }

  file1 = g_file_resolve_relative_path (globs->file, Path1);
  file2 = g_file_resolve_relative_path (globs->file, Path2);
  if (file1 == NULL) {
    g_print ("(EE) VFSIsOnSameFS: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }
  if (file2 == NULL) {
    g_print ("(EE) VFSIsOnSameFS: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }

  error = NULL;
  info1 = g_file_query_info (file1, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
  if (error) {
    g_print ("(EE) VFSIsOnSameFS: g_file_query_info() error: %s\n", error->message);
    g_error_free (error);
    g_object_unref (file1);
    g_object_unref (file2);
    return FALSE;
  }
  info2 = g_file_query_info (file2, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
  if (error) {
    g_print ("(EE) VFSIsOnSameFS: g_file_query_info() error: %s\n", error->message);
    g_error_free (error);
    g_object_unref (info1);
    g_object_unref (file1);
    g_object_unref (file2);
    return FALSE;
  }

  g_print ("(II) VFSIsOnSameFS: '%s' vs. '%s'\n", g_file_info_get_attribute_string (info1, G_FILE_ATTRIBUTE_ID_FILESYSTEM),
                                                  g_file_info_get_attribute_string (info2, G_FILE_ATTRIBUTE_ID_FILESYSTEM));

  res = strcmp (g_file_info_get_attribute_string (info1, G_FILE_ATTRIBUTE_ID_FILESYSTEM),
                g_file_info_get_attribute_string (info2, G_FILE_ATTRIBUTE_ID_FILESYSTEM)) == 0;
  g_object_unref (file1);
  g_object_unref (file2);
  g_object_unref (info1);
  g_object_unref (info2);
  return res;
}

gboolean VFSTwoSameFiles (struct TVFSGlobs *globs, const char *Path1, const char *Path2)
{
  GFile *file1, *file2;
  gboolean res;

  if (globs->file == NULL) {
    g_print ("(EE) VFSTwoSameFiles: globs->file == NULL !\n");
    return FALSE;
  }

  file1 = g_file_resolve_relative_path (globs->file, Path1);
  file2 = g_file_resolve_relative_path (globs->file, Path2);
  if (file1 == NULL) {
    g_print ("(EE) VFSTwoSameFiles: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }
  if (file2 == NULL) {
    g_print ("(EE) VFSTwoSameFiles: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }

  /* FIXME: we should do some I/O test, we're esentially comparing strings at the moment */
  res = g_file_equal (file1, file2);
  g_object_unref (file1);
  g_object_unref (file2);
  return res;
}


/**************************************************************************************************************************************/
/**************************************************************************************************************************************/

static void vfs_copy_progress_callback (goffset current_num_bytes,
                                        goffset total_num_bytes,
                                        gpointer user_data)
{
  PProgressInfo ProgressInfo;
  int Percent;  

//  g_print ("(II) vfs_copy_progress_callback spawned: current_num_bytes = %lu, total_num_bytes = %lu\n", current_num_bytes, total_num_bytes);

  if (! user_data)
    return;
  ProgressInfo = (PProgressInfo)user_data;

  if (gProgressProc) {
    if (total_num_bytes == 0)
    {
      Percent = 0;
    }
    else
    {
      Percent = (current_num_bytes * 100) / total_num_bytes;
    }

    if (gProgressProc(gPluginNumber, ProgressInfo->SourceName, ProgressInfo->TargetName, Percent) == 1)
    {
      g_cancellable_cancel (ProgressInfo->cancellable);
    }
  }
}

//--------------------------------------------------------------------------------------------

PConnection g_list_lookup(GList *list, gchar *value)
{
  GList* l;
  PConnection Connection;
  for( l = g_list_first(list); l != NULL; l = l->next )
  {
    Connection = (PConnection) l->data;
//    g_print("g_list_lookup: Item = %s\n", Connection->Name);
    if (strcmp(value, Connection->Name) == 0)
    {
      return Connection;
    }
  }
  return NULL;
}

struct TVFSGlobs * g_list_lookup_globs(GList *list, gchar *value)
{
  GList* l;
  struct TVFSGlobs *globs;
  for( l = g_list_first(list); l != NULL; l = l->next )
  {
    globs = (struct TVFSGlobs *) l->data;
//    g_print("g_list_lookup: Item = %s\n", Connection->Name);
    if (strcmp(value, globs->Connection->Name) == 0)
    {
      return globs;
    }
  }
  return NULL;
}


gboolean g_key_file_save_to_file(GKeyFile *key_file, const gchar *file, GError **error)
{
  gchar *data = NULL;
  gsize length = 0;
  gboolean Result = FALSE;
  
  data = g_key_file_to_data(key_file, &length, error);
  if (data)
  {  
    Result = g_file_set_contents(file, data, length, error);
    g_free(data);
  }
  return Result;
}

PConnection NewConnection()
{
  PConnection Connection = (PConnection) malloc(sizeof(TConnection));
  memset (Connection, 0, sizeof (TConnection));
  return Connection;
}

void FreeConnection(PConnection Connection)
{
  if (Connection == NULL)
  {
    return;
  }
  if (Connection->Name != NULL)
  {
    free(Connection->Name);
  }
  if (Connection->Type != NULL)
  {
    free(Connection->Type);
  }
  if (Connection->Host != NULL)
  {
    free(Connection->Host);
  }
  if (Connection->UserName != NULL)
  {
    free(Connection->UserName);
  }
  if (Connection->Password != NULL)
  {
    free(Connection->Password);
  }
  if (Connection->Path != NULL)
  {
    free(Connection->Path);
  }
  free(Connection);
  Connection = NULL;
}

void ReadConnectionList()
{
  GKeyFile *KeyFile;
  GError *error = NULL;
  TConnection *Connection;
  gchar key[MAX_PATH];
  KeyFile = g_key_file_new();
  if (!g_key_file_load_from_file(KeyFile, gDefaultIniName, G_KEY_FILE_KEEP_COMMENTS, &error))
  {
     g_print(error->message);
     g_error_free(error);
  }  
  else
  { 
      int i;
      int Count = g_key_file_get_integer(KeyFile, GROUP_NAME, "ConnectionCount", NULL);
      for (i = 1; i <= Count; i++)
      {
        Connection = NewConnection();
        sprintf(key, "Connection%dName", i);
        Connection->Name = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);
        sprintf(key, "Connection%dType", i);
        Connection->Type = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);
        sprintf(key, "Connection%dHost", i);
        Connection->Host = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);
        sprintf(key, "Connection%dUserName", i);
        Connection->UserName = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);
        sprintf(key, "Connection%dPassword", i);
        Connection->Password = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);
        sprintf(key, "Connection%dPath", i);
        Connection->Path = g_key_file_get_string(KeyFile, GROUP_NAME, key, NULL);

        ConnectionList = g_list_append(ConnectionList, Connection);
      }
  }
  
  g_key_file_free(KeyFile);
}

void WriteConnectionList()
{
  GKeyFile *KeyFile;
  GError *error = NULL;
  GList* l;
  PConnection Connection;
  gchar key[MAX_PATH];
  int i = 0;

  KeyFile = g_key_file_new();

  for( l = g_list_first(ConnectionList); l != NULL; l = l->next )
  {
    i++;
    Connection = (PConnection) l->data;
    if (Connection->Name != NULL)
    {
      sprintf(key, "Connection%dName", i);
      g_print("WriteConnectionList: %s = %s\n", key, Connection->Name);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->Name);
    }
    if (Connection->Type != NULL)
    {
      sprintf(key, "Connection%dType", i);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->Type);
    }
    if (Connection->Host != NULL)
    {
      sprintf(key, "Connection%dHost", i);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->Host);
    }
    if (Connection->UserName != NULL)
    {
      sprintf(key, "Connection%dUserName", i);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->UserName);
    }
    if (Connection->Password != NULL)
    {
      sprintf(key, "Connection%dPassword", i);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->Password);
    }
    if (Connection->Path != NULL)
    {
      sprintf(key, "Connection%dPath", i);
      g_key_file_set_string(KeyFile, GROUP_NAME, key, Connection->Path);
    }
  }
  // save connection count
  g_key_file_set_integer(KeyFile, GROUP_NAME, "ConnectionCount", i);
  // save data to file
  if (!g_key_file_save_to_file(KeyFile, gDefaultIniName, &error))
  {
    if (error)
    {
      g_print ("(EE) Impossible to write config file: %s\n", error->message);      
      g_error_free (error);
    }
  }
  g_key_file_free(KeyFile);
}

struct TVFSGlobs * NetworkConnect(gchar *ConnectionName)
{
  struct TVFSGlobs *globs;
  g_print("NetworkConnect: Enter\n");
  // find in active connection list
  globs = (struct TVFSGlobs *) g_list_lookup_globs(ActiveConnectionList, ConnectionName);
  if (globs == NULL)
  {
    // find in exists connection list
    PConnection Connection = (PConnection) g_list_lookup(ConnectionList, ConnectionName);
    if (Connection != NULL)
    {
       GFile *f, *f2;
       GFileInfo *info;
       GError *error;
       TVFSResult res;

       globs = VFSNew(NULL);
       globs->file = NULL;
       globs->ftp_anonymous = FALSE;
       globs->Connection = Connection;

       g_print("NetworkConnect: Host = %s\n", Connection->Host);
       gchar *Host = Connection->Host;

       if (strcmp (Connection->UserName, cAnonymous) == 0) 
       {          
         globs->ftp_anonymous = TRUE;
       }

       g_print ("(II) NetworkConnect: opening URI '%s'\n", Host);
       f = g_file_new_for_commandline_arg (Host);

       while (1) {
         error = NULL;

         g_print ("(II) NetworkConnect: Before - g_file_query_info\n");

         info = g_file_query_info (f, CONST_DEFAULT_QUERY_INFO_ATTRIBUTES,
                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);

         g_print ("(II) NetworkConnect: After - g_file_query_info\n");

         /*  Fallback to parent directory if specified path doesn't exist  */
         if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
           g_print ("(II) NetworkConnect: Fallback to parent directory\n");
           f2 = g_file_get_parent (f);
           if (f2) {
             g_object_unref (f);
             f = f2;
             g_error_free (error);
             continue;
           }
         }

         g_print ("(II) NetworkConnect: Before - Mount the target\n");

         /*  Mount the target  */
         if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
           g_print ("(II) NetworkConnect: Mount the target\n");
           g_error_free (error);
           error = NULL;
           res = vfs_handle_mount (globs, f);
           if (res != FS_FILE_OK)
             return NULL;
           else
             continue;
         }
         /*  Any other errors --> report  */
         if (error) {
           g_print ("(EE) NetworkConnect: g_file_query_info() error: %s\n", error->message);
           res = g_error_to_TVFSResult (error);
           g_error_free (error);
           g_object_unref (f);
           return NULL;
         }
         /*  everything ok?  */
         break;
       } // while

      globs->file = f;
      ActiveConnectionList = g_list_append(ActiveConnectionList, globs);
      g_print("NetworkConnect: Exit = True\n");
      return globs;
    }
  }
  return globs;
}

gboolean AddQuickConnection(PConnection Connection)
{
  char host[MAX_PATH], home_dir[MAX_PATH], user[MAX_PATH], pwd[MAX_PATH];

  host[0] = 0;
  user[0] = 0;
  pwd[0] = 0;
  home_dir[0] = '/';
  home_dir[1] = 0;

  if (gRequestProc(gPluginNumber, RT_URL, NULL, NULL, host, MAX_PATH))
  {
    Connection->Host = strdup(host);
    if (gRequestProc(gPluginNumber, RT_TargetDir, NULL, NULL, home_dir, MAX_PATH))
    {
      Connection->Path = strdup(home_dir);
      /*  test for FTP protocol (we only enable anonymous here)  */
      if (strcasestr (host, "ftp://") == host)
      {
	if (gRequestProc(gPluginNumber, RT_MsgYesNo, NULL, "Use anonymous login?", NULL, MAX_PATH))
	{
	  Connection->UserName = strdup(cAnonymous);
	  return TRUE;
	}
      }
      if (gRequestProc(gPluginNumber, RT_UserName, NULL, NULL, user, MAX_PATH))
      {
        Connection->UserName = strdup(user);
        if (gRequestProc(gPluginNumber, RT_Password, NULL, NULL, pwd, MAX_PATH))
        {
          Connection->Password = strdup(pwd);
          return TRUE;
        }
      }
    }
  }
 return FALSE;
}

gboolean QuickConnection()
{
  g_print("QuickConnection: Enter\n");
  gboolean Result = FALSE;
  PConnection Connection = NewConnection();
  if (AddQuickConnection(Connection))
  {
    Connection->Name = strdup(cQuickConnection);
    ConnectionList = g_list_append(ConnectionList, Connection);
    Result = (NetworkConnect(Connection->Name) != NULL);
    ConnectionList = g_list_remove(ConnectionList, Connection);
  }

  if (!Result)
  {
    FreeConnection(Connection);
  }
  g_print("QuickConnection: Exit\n");
  return Result;
}

void AddConnection()
{
  g_print("AddConnection: Enter\n");
  gchar name[MAX_PATH];
  gboolean bCancel = TRUE;
  name[0] = 0;
  gConnection = NewConnection();
  if (gRequestProc(gPluginNumber, RT_Other, "Network", "Connection name:", name, MAX_PATH))
  {
    unsigned int i;
    for(i = 0; i < strlen(name); i++)
    {
      if (name[i] == '/') name[i] = '_';
    }
    gConnection->Name = strdup(name);
    if (AddQuickConnection(gConnection))
    {
      ConnectionList = g_list_append(ConnectionList, gConnection);
      bCancel = FALSE;
    }
  }

  if (bCancel)
  {
    FreeConnection(gConnection);
  }
  else
  {
    WriteConnectionList();
  }
  g_print("AddConnection: Exit\n");
}

gchar *ExtractConnectionName(gchar *Path)
{
  if (Path == NULL) return NULL;
  char *tmp;
  if (IS_DIR_SEP(*Path)) 
    tmp = strdup(Path + 1); 
  else 
    tmp = strdup(Path);
  
  char *connection_part = strchr(tmp, 0x2f);
  if (connection_part == NULL) 
  {
    return tmp;
  }

  char *connection_name = (char*)malloc(connection_part - tmp + 1);
  snprintf(connection_name, connection_part - tmp + 1, "%s", tmp);
  free(tmp);
  return connection_name;
}

gchar *ExtractRemoteFileName(gchar *FileName)
{
  if (FileName == NULL) return NULL;
  char *tmp;
  if (IS_DIR_SEP(*FileName))
    tmp = strdup(FileName + 1);
  else
    tmp = strdup(FileName);

  char *file_name_part = strchr(tmp, 0x2f);
  if (file_name_part == NULL)
  {
    free(tmp);
    return PathDelim;
  }
  char *file_name = strdup(file_name_part);
  free(tmp);
  return file_name;
}

struct TVFSGlobs * GetConnectionByPath(gchar *Path)
{
    struct TVFSGlobs *globs;
    gchar *ConnectionName = ExtractConnectionName(Path);
    g_print("GetConnectionByPath: ConnectionName = %s\n", ConnectionName);
    globs = (struct TVFSGlobs *) g_list_lookup_globs(ActiveConnectionList, ConnectionName);
    if (globs == NULL)
    {
       globs = NetworkConnect(ConnectionName);
       free(ConnectionName);
       return globs;
    }
    free(ConnectionName);
    /*
    if (globs->RemotePath != NULL)
    {
      free(globs->RemotePath);
    } 
    */
    globs->RemotePath = ExtractRemoteFileName(Path);
    g_print("GetConnectionByPath: RemotePath = %s\n", globs->RemotePath);
    return globs;
}

BOOL LocalFindNext(HANDLE Hdl, WIN32_FIND_DATAA *FindData)
{
  PListRec ListRec = (PListRec) Hdl;
  if (ListRec == NULL)
  {
    g_print("LocalFindNext: ListRec == NULL !\n");
    return FALSE;
  }
  g_print("LocalFindNext: Path == %s\n", ListRec->Path);
  switch (ListRec->Index)
  {
   case 0:
   {
     g_print("LocalFindNext: Item == %s\n", cAddConnection);
     strcpy(FindData->cFileName, cAddConnection);
     break;
   }
   case 1:
   {
     g_print("LocalFindNext: Item == %s\n", cQuickConnection);
     strcpy(FindData->cFileName, cQuickConnection);
     break;
   }
   default:
   {
     if (ListRec->list == NULL)
     {
       return FALSE;
     }
     PConnection Connection = (PConnection) ListRec->list->data;
     g_strlcpy(FindData->cFileName, Connection->Name, MAX_PATH);
     ListRec->list = g_list_next(ListRec->list);
     return TRUE;
   }
  }
  g_print("LocalFindNext: Exit\n");
  ListRec->Index++;
  return TRUE;
}

BOOL RemoteFindNext(HANDLE Hdl, WIN32_FIND_DATAA *FindData)
{
  PListRec ListRec;
  struct TVFSGlobs *globs;
  char *sDir;
  GError *error;
  GFileInfo *info;
  GFile *f;
  g_print ("(EE) RemoteFindNext: Enter\n");
  ListRec = (PListRec) Hdl;
  globs = ListRec->globs;
  sDir =  ExtractRemoteFileName(ListRec->Path);

  if (globs->file == NULL) {
    g_print ("(EE) RemoteFindNext: globs->file == NULL !\n");
    return FALSE;
  }
  if (globs->enumerator == NULL) {
    g_print ("(EE) RemoteFindNext: globs->enumerator == NULL !\n");
    return FALSE;
  }

  error = NULL;
  info = g_file_enumerator_next_file (globs->enumerator, NULL, &error);
  if (error) {
    g_print ("(EE) RemoteFindNext: g_file_enumerator_next_file() error: %s\n", error->message);
    g_error_free (error);
    return FALSE;
  }
  if (! error && ! info)
    return FALSE;
  
  f = g_file_get_child (globs->enumerated_file, g_file_info_get_name (info));
  GFileInfoToWin32FindData(f, info, FindData);
  
  g_object_unref (f);
  g_object_unref (info);
  return TRUE;
}

// Export functions--------------------------------------------------------------------------------------

int DCPCALL FsInit(int PluginNr,tProgressProc pProgressProc,
                     tLogProc pLogProc,tRequestProc pRequestProc)
{
  gProgressProc = pProgressProc;
  gLogProc = pLogProc;
  gRequestProc = pRequestProc;
  gPluginNumber = PluginNr;
  
  ActiveConnectionList = NULL;
  ConnectionList = NULL;

  g_type_init();  
  
  return 0;
}

HANDLE DCPCALL FsFindFirst(char* Path,WIN32_FIND_DATAA *FindData)
{
  PListRec ListRec = (PListRec) malloc(sizeof(TListRec));
  memset(ListRec, 0, sizeof(TListRec));
  ListRec->Path = Path;
  ListRec->Index = 0;
  HANDLE Handle = (HANDLE) ListRec;
  memset(FindData, 0, sizeof(WIN32_FIND_DATAA));
  if (strcmp(Path, PathDelim) == 0)
  {
    ListRec->list =  g_list_first(ConnectionList);
    LocalFindNext(Handle, FindData);
    return Handle;
  }
  else
  {
    ListRec->globs = GetConnectionByPath(Path);
    if (ListRec->globs == NULL)
    {
        free(ListRec);
        return (HANDLE)(-1);
    }
    VFSChangeDir(ListRec->globs, ListRec->globs->RemotePath);
    g_print("Call RemoteFindNext = %s\n", ListRec->globs->RemotePath);
    if (!RemoteFindNext(Handle, FindData))
    {
      free(ListRec);
      return (HANDLE)(-1);
    }
    return Handle;
  }
}

BOOL DCPCALL FsFindNext(HANDLE Hdl,WIN32_FIND_DATAA *FindData)
{
  PListRec ListRec = (PListRec) Hdl;
  memset(FindData, 0, sizeof(WIN32_FIND_DATAA));  
  if (strcmp(ListRec->Path, PathDelim) == 0)
  {
    return LocalFindNext(Hdl, FindData);
  }
  else
  {
    return RemoteFindNext(Hdl, FindData);
  }
}

int DCPCALL FsFindClose(HANDLE Hdl)
{
  PListRec ListRec;
  GError *error;
  TVFSResult res;

  ListRec = (PListRec) Hdl;

  if (ListRec->globs == NULL) {
    g_print ("(EE) FsFindClose: ListRec->globs == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  if (ListRec->globs->file == NULL) {
    g_print ("(EE) FsFindClose: ListRec->globs->file == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  if (ListRec->globs->enumerator == NULL) {
    g_print ("(EE) FsFindClose: ListRec->globs->enumerator == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  g_print ("(II) FsFindClose\n");

  error = NULL;
  g_file_enumerator_close (ListRec->globs->enumerator, NULL, &error);
  g_object_unref (ListRec->globs->enumerator);
  ListRec->globs->enumerator = NULL;
  g_object_unref (ListRec->globs->enumerated_file);
  ListRec->globs->enumerated_file = NULL;
  if (error) {
    g_print ("(EE) FsFindClose: g_file_enumerator_close() error: %s\n", error->message);
    res = g_error_to_TVFSResult (error);
    g_error_free (error);
    return res;
  }
  return FS_FILE_OK;
}

BOOL DCPCALL FsMkDir(char* Path)
{
  struct TVFSGlobs *globs;
  GFile *f;
  GError *error;

  globs = GetConnectionByPath(Path);

  if (globs == NULL) {
    g_print ("(EE) FsMkDir: globs == NULL !\n");
    return FALSE;
  }
  if (globs->file == NULL) {
    g_print ("(EE) FsMkDir: globs->file == NULL !\n");
    return FALSE;
  }

  f = g_file_resolve_relative_path (globs->file, globs->RemotePath);
  if (f == NULL) {
    g_print ("(EE) FsMkDir: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }

  error = NULL;
  g_file_make_directory (f, NULL, &error);
  g_object_unref (f);
  if (error) {
    g_print ("(EE) FsMkDir: g_file_make_directory() error: %s\n", error->message);
    g_error_free (error);
    return FALSE;
  }
  return TRUE;
}

BOOL DCPCALL FsRemoveDir(char* RemoteName)
{
  struct TVFSGlobs *globs;
  globs = GetConnectionByPath(RemoteName);
  return (VFSRemove(globs, globs->RemotePath) == FS_FILE_OK);
}

int DCPCALL FsRenMovFile(char* OldName,char* NewName,BOOL Move,
                           BOOL OverWrite,RemoteInfoStruct* ri)
{
  struct TVFSGlobs *globs;
  GFile *src, *dst;
  GError *error;
  TVFSResult res;
  char *sSrcName;
  char *sDstName;

  globs = GetConnectionByPath(OldName);

  if (globs == NULL) {
    g_print ("(EE) FsRenMovFile: globs == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  if (globs->file == NULL) {
    g_print ("(EE) FsRenMovFile: globs->file == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }

  sSrcName = globs->RemotePath;
  sDstName = ExtractRemoteFileName(NewName);

  src = g_file_resolve_relative_path (globs->file, sSrcName);
  if (src == NULL) {
    g_print ("(EE) FsRenMovFile: g_file_resolve_relative_path() failed.\n");
    return FS_FILE_NOTFOUND;
  }

  g_print ("FsRenMovFile: '%s' --> '%s'\n", sSrcName, sDstName);

  error = NULL;
  g_file_set_display_name (src, sDstName, NULL, &error);
  if (error) {
    g_print ("(WW) FsRenMovFile: g_file_set_display_name() failed (\"%s\"), using fallback g_file_move()\n", error->message);
    g_error_free (error);

    dst = g_file_resolve_relative_path (src, sDstName);
    if (dst == NULL) {
      g_print ("(EE) FsRenMovFile: g_file_resolve_relative_path() failed.\n");
      g_object_unref (src);
      return FS_FILE_NOTFOUND;
    }
    error = NULL;
    g_file_move (src, dst, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, NULL, NULL, NULL, &error);
    if (error) {
      g_print ("(EE) FsRenMovFile: g_file_move() error: %s\n", error->message);
      res = g_error_to_TVFSResult (error);
      g_error_free (error);
      g_object_unref (src);
      g_object_unref (dst);
      return res;
    }
    g_object_unref (dst);
  }

  g_object_unref (src);
  return FS_FILE_OK;
}

int DCPCALL FsGetFile(char* RemoteName,char* LocalName,int CopyFlags,
                        RemoteInfoStruct* ri)
{
  struct TVFSGlobs *globs;
  GFile *src, *dst;
  GError *error;
  TVFSResult res;
  PProgressInfo ProgressInfo;

  globs = GetConnectionByPath(RemoteName);

  if (globs == NULL) {
    g_print ("(EE) FsGetFile: globs == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  if (globs->file == NULL) {
    g_print ("(EE) FsGetFile: globs->file == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }

  ProgressInfo = (PProgressInfo) g_malloc(sizeof(TProgressInfo));
  g_strlcpy(ProgressInfo->SourceName, globs->RemotePath, MAX_PATH);
  g_strlcpy(ProgressInfo->TargetName, LocalName, MAX_PATH);

  g_print ("(II) FsGetFile: '%s' --> '%s'\n", ProgressInfo->SourceName, ProgressInfo->TargetName);

  src = g_file_resolve_relative_path (globs->file, ProgressInfo->SourceName);
  if (src == NULL) {
    g_print ("(EE) FsGetFile: g_file_resolve_relative_path() failed.\n");
    g_free(ProgressInfo);
    return FS_FILE_NOTFOUND;
  }
  dst = g_file_new_for_path (ProgressInfo->TargetName);
  if (dst == NULL) {
    g_print ("(EE) FsGetFile: g_file_resolve_relative_path() failed.\n");
    g_free(ProgressInfo);
    return FS_FILE_NOTFOUND;
  }

  ProgressInfo->cancellable = g_cancellable_new ();

  res = FS_FILE_OK;
  error = NULL;
  g_file_copy (src, dst, TUXCMD_DEFAULT_COPY_FLAGS, ProgressInfo->cancellable, vfs_copy_progress_callback, ProgressInfo, &error);
  if (error) {
    g_print ("(EE) FsGetFile: g_file_copy() error: %s\n", error->message);
//    res = g_error_to_TVFSResult (error);
    if (error->code == G_IO_ERROR_CANCELLED)
      res = FS_FILE_USERABORT;
    else
      res = FS_FILE_READERROR;
    g_error_free (error);
  }

  g_object_unref (ProgressInfo->cancellable);
  g_free(ProgressInfo);  
  g_object_unref (src);
  g_object_unref (dst);
  return res;
}

int DCPCALL FsPutFile(char* LocalName,char* RemoteName,int CopyFlags)
{
  struct TVFSGlobs *globs;
  GFile *src, *dst;
  GError *error;
  TVFSResult res;
  PProgressInfo ProgressInfo;

  globs = GetConnectionByPath(RemoteName);

  if (globs == NULL) {
    g_print ("(EE) FsPutFile: globs == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }
  if (globs->file == NULL) {
    g_print ("(EE) FsPutFile: globs->file == NULL !\n");
    return FS_FILE_NOTSUPPORTED;
  }

  ProgressInfo = (PProgressInfo) g_malloc(sizeof(TProgressInfo));
  g_strlcpy(ProgressInfo->SourceName, LocalName, MAX_PATH);
  g_strlcpy(ProgressInfo->TargetName, globs->RemotePath, MAX_PATH);

  g_print ("(II) FsPutFile: '%s' --> '%s'\n", ProgressInfo->SourceName, ProgressInfo->TargetName);

  src = g_file_new_for_path (ProgressInfo->SourceName);
  if (src == NULL) {
    g_print ("(EE) FsPutFile: g_file_resolve_relative_path() failed.\n");
    g_free(ProgressInfo);
    return FS_FILE_NOTFOUND;
  }
  dst = g_file_resolve_relative_path (globs->file, ProgressInfo->TargetName);
  if (dst == NULL) {
    g_print ("(EE) FsPutFile: g_file_resolve_relative_path() failed.\n");
    g_free(ProgressInfo);    
    return FS_FILE_NOTFOUND;
  }

  ProgressInfo->cancellable = g_cancellable_new ();

  res = FS_FILE_OK;
  error = NULL;
  /* FIXME: Appending not supported */
  g_file_copy (src, dst, TUXCMD_DEFAULT_COPY_FLAGS, ProgressInfo->cancellable, vfs_copy_progress_callback, ProgressInfo, &error);
  if (error) {
    g_print ("(EE) FsPutFile: g_file_copy() error: %s\n", error->message);
//    res = g_error_to_TVFSResult (error);
    if (error->code == G_IO_ERROR_CANCELLED)
      res = FS_FILE_USERABORT;
    else
      res = FS_FILE_WRITEERROR;
    g_error_free (error);
  }

  g_object_unref (ProgressInfo->cancellable);
  g_free(ProgressInfo);    
  g_object_unref (src);
  g_object_unref (dst);
  return res;
}

int DCPCALL FsExecuteFile(HWND MainWin,char* RemoteName,char* Verb)
{
   g_print("FsExecuteFile: Item = %s, Verb = %s\n", RemoteName + 1, Verb);
   if (strcmp(Verb,"open") == 0)
   {
     if (strrchr(RemoteName, 0x2f) == RemoteName) // root path
     {
       if (strchr(RemoteName, 0x3c) == NULL) // connection
       {
         struct TVFSGlobs *globs = NetworkConnect(RemoteName + 1);
	 if (globs != NULL)
	 {
	   // go to Connection->Path
	   g_strlcat(RemoteName, globs->Connection->Path, MAX_PATH);
	   return FS_EXEC_SYMLINK;
	 }                   
         return FS_EXEC_ERROR;
       }
       else  // special item
       {
         if (strcmp(RemoteName + 1, cAddConnection) == 0)
         {
           AddConnection();
         }
         else if (strcmp(RemoteName + 1, cQuickConnection) == 0)
         {
           if (QuickConnection())
             return FS_EXEC_SYMLINK;
           else
             return FS_EXEC_ERROR;
         }
       }
     } // root path

     return FS_EXEC_OK;
   }
   else if (strcmp(Verb,"properties") == 0)
   {
     return FS_EXEC_OK;
   }
   else if (strstr(Verb, "chmod ") != NULL)
   {
     struct TVFSGlobs *globs;
     uint Mode = (uint) strtol(strchr(Verb, 0x20) + 1, NULL, 8);
     globs = GetConnectionByPath(RemoteName);
     if (VFSChmod(globs, globs->RemotePath, Mode) != FS_FILE_OK)
     {
       return FS_EXEC_ERROR;
     }
   }
   return FS_EXEC_OK;
}

BOOL DCPCALL FsDeleteFile(char* RemoteName)
{
  if (strrchr(RemoteName, 0x2f) == RemoteName) // root path
  {
    if (strchr(RemoteName, 0x3c) == NULL) // connection
    {
       // find in exists connection list
       PConnection Connection = (PConnection) g_list_lookup(ConnectionList, RemoteName + 1);
       if (Connection != NULL)
       {
	 ConnectionList = g_list_remove(ConnectionList, Connection);
	 WriteConnectionList();
	 return TRUE;
       }
    }
    return FALSE;
  }    
  struct TVFSGlobs *globs = GetConnectionByPath(RemoteName);
  return (VFSRemove(globs, globs->RemotePath) == FS_FILE_OK);
}

BOOL DCPCALL FsSetTime(char* RemoteName,FILETIME *CreationTime,
                         FILETIME *LastAccessTime,FILETIME *LastWriteTime)
{
  struct TVFSGlobs *globs;
  GFile *f;
  GError *error;
  long mtime;
  long atime;
  long ctime;

  globs = GetConnectionByPath(RemoteName);

  if (globs == NULL) {
    g_print ("(EE) FsSetTime: globs == NULL !\n");
    return FALSE;
  }  
  if (globs->file == NULL) {
    g_print ("(EE) FsSetTime: globs->file == NULL !\n");
    return FALSE;
  }

  f = g_file_resolve_relative_path (globs->file, globs->RemotePath);
  if (f == NULL) {
    g_print ("(EE) FsSetTime: g_file_resolve_relative_path() failed.\n");
    return FALSE;
  }

  ctime = FileTimeToUnixTime(CreationTime);
  atime = FileTimeToUnixTime(LastAccessTime);
  mtime = FileTimeToUnixTime(LastWriteTime);
  
  error = NULL;
  g_file_set_attribute_uint64 (f, G_FILE_ATTRIBUTE_TIME_MODIFIED, mtime, G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error) {
    g_print ("(EE) FsSetTime: g_file_set_attribute_uint64() error: %s\n", error->message);
    g_error_free (error);
    g_object_unref (f);
    return FALSE;
  }
  error = NULL;
  g_file_set_attribute_uint64 (f, G_FILE_ATTRIBUTE_TIME_ACCESS, atime, G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error) {
    g_print ("(EE) FsSetTime: g_file_set_attribute_uint64() error: %s\n", error->message);
    g_error_free (error);
    /*  Silently drop the error, atime is not commonly supported on most systems  */
  }
  error = NULL;
  g_file_set_attribute_uint64 (f, G_FILE_ATTRIBUTE_TIME_CREATED, ctime, G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (error) {
    g_print ("(EE) FsSetTime: g_file_set_attribute_uint64() error: %s\n", error->message);
    g_error_free (error);
    /*  Silently drop the error, ctime is not commonly supported on most systems  */
  }  
  g_object_unref (f);
  return TRUE;
}

BOOL DCPCALL FsDisconnect(char *DisconnectRoot)
{
   struct TVFSGlobs *globs;
   g_print("FsDisconnect: DisconnectRoot == %s\n", DisconnectRoot);
   globs = (struct TVFSGlobs *) g_list_lookup_globs(ActiveConnectionList, DisconnectRoot);
   if (globs != NULL)
   {
     if (globs->file)
       g_object_unref (globs->file);
     globs->file = NULL;
     ActiveConnectionList = g_list_remove(ActiveConnectionList, globs);
     free (globs);
     return TRUE;
   }
   return FALSE;
}

void DCPCALL FsSetDefaultParams(FsDefaultParamStruct* dps)
{
  // use default location, but our own ini file name
  g_strlcpy(gDefaultIniName, dps->DefaultIniName, MAX_PATH - 1);
  gchar* tmp = strrchr(gDefaultIniName, 0x2f);
  if (tmp)
  {  
    tmp[1] = 0;
    g_strlcat(gDefaultIniName, cDefaultIniName, sizeof(gDefaultIniName) - 1);        
  }    
  g_print ("gDefaultIniName: %s\n", gDefaultIniName);
  ReadConnectionList();
}

void DCPCALL FsGetDefRootName(char* DefRootName,int maxlen)
{
  g_strlcpy(DefRootName, "Network", maxlen);
}

//--------------------------------------------------

