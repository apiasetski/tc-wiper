// SPDX-License-Identifier: GPL-3.0-or-later
// Subset of the Total Commander file system plugin interface (fsplugin.h)
// used by TC Wiper. Values match the official WFX SDK from ghisler.com.
#pragma once
#include <windows.h>

#define FS_FILE_OK                  0
#define FS_FILE_EXISTS              1
#define FS_FILE_NOTFOUND            2
#define FS_FILE_READERROR           3
#define FS_FILE_WRITEERROR          4
#define FS_FILE_USERABORT           5
#define FS_FILE_NOTSUPPORTED        6

#define FS_EXEC_OK                  0
#define FS_EXEC_ERROR               1
#define FS_EXEC_YOURSELF           -1

#define FS_COPYFLAGS_OVERWRITE      1
#define FS_COPYFLAGS_RESUME         2
#define FS_COPYFLAGS_MOVE           4

#define FS_STATUS_START             0
#define FS_STATUS_END               1

#define FS_STATUS_OP_PUT_SINGLE     4
#define FS_STATUS_OP_PUT_MULTI      5

typedef struct {
    DWORD SizeLow, SizeHigh;
    FILETIME LastWriteTime;
    int Attr;
} RemoteInfoStruct;

typedef int  (__stdcall *tProgressProc)(int PluginNr, char* SourceName, char* TargetName, int PercentDone);
typedef void (__stdcall *tLogProc)(int PluginNr, int MsgType, char* LogString);
typedef BOOL (__stdcall *tRequestProc)(int PluginNr, int RequestType, char* CustomTitle, char* CustomText, char* ReturnedText, int maxlen);

typedef int  (__stdcall *tProgressProcW)(int PluginNr, WCHAR* SourceName, WCHAR* TargetName, int PercentDone);
typedef void (__stdcall *tLogProcW)(int PluginNr, int MsgType, WCHAR* LogString);
typedef BOOL (__stdcall *tRequestProcW)(int PluginNr, int RequestType, WCHAR* CustomTitle, WCHAR* CustomText, WCHAR* ReturnedText, int maxlen);
