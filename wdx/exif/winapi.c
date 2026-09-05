#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include "winapi.h"

char* itoa(int value, char* str, int base)
{
    if (base == 10)
    {
        sprintf(str, "%d", value);
    }
    else if (base == 16)
    {
        sprintf(str, "%x", value);
    }
    return str;
}

int CreateFileT(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                LPVOID lpSecurityAttributes, DWORD dwCreationDisposition,
                DWORD dwFlagsAndAttributes, LPVOID hTemplateFile)
{
    int res;
    int oflag = 0;
    (void)dwShareMode;
    (void)hTemplateFile;
    (void)dwFlagsAndAttributes;
    (void)lpSecurityAttributes;
    (void)dwCreationDisposition;

    if (dwDesiredAccess == GENERIC_READ)
    {
        oflag = O_RDONLY;
    }

    do
    {
        res = open(lpFileName, oflag);
    }
    while ((res == -1L) && (errno == EINTR));

    return res;
}

DWORD SetFilePointer(int hFile, LONG lDistanceToMove, LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
    __off_t res;
    (void)lpDistanceToMoveHigh;
    res = lseek(hFile, (__off_t)lDistanceToMove, (int)dwMoveMethod);
    return (res < 0) ? INVALID_SET_FILE_POINTER : (DWORD)res;
}

BOOL ReadFile(int hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    ssize_t res;
    (void)lpOverlapped;

    do
    {
        res = read(hFile, lpBuffer, nNumberOfBytesToRead);
    }
    while ((res == -1L) && (errno == EINTR));

    if (lpNumberOfBytesRead)
    {
        *lpNumberOfBytesRead = (res < 0) ? 0 : (DWORD)res;
    }

    return (res >= 0);
}

BOOL CloseHandle(int hFile)
{
    int res;

    do
    {
        res = close(hFile);
    }
    while ((res == -1L) && (errno == EINTR));

    return (res == 0);
}
