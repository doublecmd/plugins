#ifndef WINAPI_H
#define WINAPI_H

#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include "common.h"

#define stricmp strcasecmp

#define FILE_BEGIN   SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END     SEEK_END

#define FILE_SHARE_READ  0
#define FILE_SHARE_WRITE 0

#define OPEN_EXISTING         0
#define FILE_ATTRIBUTE_NORMAL 0

#define GENERIC_READ  0x80000000

#define INVALID_HANDLE_VALUE -1
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

typedef uint8_t byte;
typedef CHAR *LPCSTR;
typedef void *LPVOID;
typedef DWORD *LPDWORD;
typedef int64_t __int64;
typedef LPVOID LPOVERLAPPED;

#define strlcpy strlcpy_
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define WideCharToMultiByte(a,b,c,d,e,f,g,h) (false)

extern char* itoa(int value, char* str, int base);

extern int CreateFileT(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                       LPVOID lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, LPVOID hTemplateFile);
extern BOOL ReadFile(int hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
extern DWORD SetFilePointer(int hFile, LONG lDistanceToMove, LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod);
extern BOOL CloseHandle(int hFile);

#endif // WINAPI_H
