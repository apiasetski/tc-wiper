// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the test build of the plugin and calls it the way Total Commander does.
// Usage: test_harness.exe <tcwiper_test.dll> <work folder>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/wfx_api.h"

typedef int    (__stdcall *tFsInitW)(int, tProgressProcW, tLogProcW, tRequestProcW);
typedef HANDLE (__stdcall *tFsFindFirstW)(WCHAR*, WIN32_FIND_DATAW*);
typedef void   (__stdcall *tFsStatusInfoW)(WCHAR*, int, int);
typedef int    (__stdcall *tFsPutFileW)(WCHAR*, WCHAR*, int);
typedef BOOL   (__stdcall *tFsMkDirW)(WCHAR*);
typedef BOOL   (__stdcall *tFsFindNextW)(HANDLE, WIN32_FIND_DATAW*);
typedef int    (__stdcall *tFsFindClose)(HANDLE);
typedef int    (__stdcall *tFsExecuteFileW)(HWND, WCHAR*, WCHAR*);
typedef int    (__stdcall *tFsGetFileW)(WCHAR*, WCHAR*, int, RemoteInfoStruct*);
typedef const wchar_t* (*tText)();
typedef void   (*tSetCancel)(BOOL);
typedef int    (__stdcall *tWiperGo)();
typedef int    (*tWipeItems)(const wchar_t*, const wchar_t*);
typedef int    (*tWipeList)(const wchar_t*);

static tFsFindFirstW  pFindFirst;
static tFsStatusInfoW pStatus;
static tFsPutFileW    pPut;
static tFsMkDirW      pMkDir;
static tFsFindNextW   pFindNext;
static tFsFindClose   pFindClose;
static tFsExecuteFileW pExecute;
static tFsGetFileW    pGet;
static tText          pConfirm, pSummary;
static tSetCancel     pSetCancel;
static tWiperGo       pWiperGo;
static tWipeItems     pWipeItems;
static tWipeList      pWipeList;

static int g_failed;

static void Check(bool ok, const std::wstring& what)
{
    wprintf(L"  [%s] %s\n", ok ? L"PASS" : L"FAIL", what.c_str());
    if (!ok) ++g_failed;
}

static int __stdcall Progress(int, WCHAR*, WCHAR*, int)
{
    return 0;
}

static bool Exists(const std::wstring& p)
{
    std::wstring lp = p.rfind(L"\\\\?\\", 0) == 0 ? p : L"\\\\?\\" + p;
    return GetFileAttributesW(lp.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool Contains(const std::wstring& text, const std::wstring& part)
{
    return text.find(part) != std::wstring::npos;
}

static void MakeFile(const std::wstring& p, size_t size, char fill = 'A')
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string data(size, fill);
    DWORD w = 0;
    if (size) WriteFile(h, data.data(), (DWORD)size, &w, nullptr);
    CloseHandle(h);
}

static std::string ReadAll(const std::wstring& p)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    char buf[4096];
    DWORD r = 0;
    ReadFile(h, buf, sizeof(buf), &r, nullptr);
    CloseHandle(h);
    return std::string(buf, r);
}

static void DeleteTree(const std::wstring& dir)
{
    std::wstring base = dir.rfind(L"\\\\?\\", 0) == 0 ? dir : L"\\\\?\\" + dir;
    WIN32_FIND_DATAW f;
    HANDLE find = FindFirstFileW((base + L"\\*").c_str(), &f);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = f.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring p = base + L"\\" + name;
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteTree(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(find, &f));
        FindClose(find);
    }
    RemoveDirectoryW(base.c_str());
}

// Simulates TC handing over one file from source folder `base`.
static int Put(const std::wstring& base, const std::wstring& rel, int flags = FS_COPYFLAGS_MOVE)
{
    std::wstring local = base + rel, remote = L"\\" + rel;
    return pPut(&local[0], &remote[0], flags);
}

static void MkDir(const std::wstring& rel)
{
    std::wstring remote = L"\\" + rel;
    pMkDir(&remote[0]);
}

static void Start() { pStatus(nullptr, FS_STATUS_START, FS_STATUS_OP_PUT_MULTI); }
static void End()   { pStatus(nullptr, FS_STATUS_END, FS_STATUS_OP_PUT_MULTI); }

// Writes a list file like TC's %WL (UTF-16, `bom` optional) or %UL (UTF-8).
static void WriteList(const std::wstring& p, const std::vector<std::wstring>& lines, bool utf16, bool bom)
{
    std::wstring text;
    for (const auto& l : lines) text += l + L"\r\n";
    std::string data;
    if (utf16) {
        if (bom) data = "\xFF\xFE";
        data.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    } else {
        int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        data.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &data[0], n, nullptr, nullptr);
    }
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr);
    CloseHandle(h);
}

static size_t Count(const std::wstring& text, const std::wstring& part)
{
    size_t n = 0;
    for (size_t pos = text.find(part); pos != std::wstring::npos; pos = text.find(part, pos + 1)) ++n;
    return n;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        wprintf(L"usage: test_harness <dll> <work folder>\n");
        return 2;
    }
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        wprintf(L"cannot load %s\n", argv[1]);
        return 2;
    }
    auto pInit = (tFsInitW)GetProcAddress(dll, "FsInitW");
    pFindFirst = (tFsFindFirstW)GetProcAddress(dll, "FsFindFirstW");
    pStatus    = (tFsStatusInfoW)GetProcAddress(dll, "FsStatusInfoW");
    pPut       = (tFsPutFileW)GetProcAddress(dll, "FsPutFileW");
    pMkDir     = (tFsMkDirW)GetProcAddress(dll, "FsMkDirW");
    pFindNext  = (tFsFindNextW)GetProcAddress(dll, "FsFindNextW");
    pFindClose = (tFsFindClose)GetProcAddress(dll, "FsFindClose");
    pExecute   = (tFsExecuteFileW)GetProcAddress(dll, "FsExecuteFileW");
    pGet       = (tFsGetFileW)GetProcAddress(dll, "FsGetFileW");
    pConfirm   = (tText)GetProcAddress(dll, "TcwTestConfirmText");
    pSummary   = (tText)GetProcAddress(dll, "TcwTestSummaryText");
    pSetCancel = (tSetCancel)GetProcAddress(dll, "TcwTestSetCancel");
    pWiperGo   = (tWiperGo)GetProcAddress(dll, "WiperGo");
    pWipeItems = (tWipeItems)GetProcAddress(dll, "TcwTestWipeItems");
    pWipeList  = (tWipeList)GetProcAddress(dll, "TcwTestWipeList");
    const char* required[] = { "FsInit", "FsFindFirst", "FsFindNext", "FsFindClose", "FsGetDefRootName",
                               "FsPutFile", "FsMkDir", "FsStatusInfo", "FsExecuteFile", "FsGetFile" };
    bool allExports = pInit && pFindFirst && pStatus && pPut && pMkDir && pFindNext && pFindClose && pExecute && pGet && pConfirm && pSummary && pSetCancel && pWiperGo && pWipeItems && pWipeList;
    for (const char* name : required) allExports = allExports && GetProcAddress(dll, name);
    Check(allExports, L"all WFX functions exported with undecorated names");
    if (!allExports) return 1;
    pInit(1, Progress, nullptr, nullptr);

    std::wstring work = argv[2];
    DeleteTree(work);
    CreateDirectoryW(work.c_str(), nullptr);
    std::wstring w = work + L"\\";

    wprintf(L"Listing\n");
    WIN32_FIND_DATAW fd;
    WCHAR root[] = L"\\", sub[] = L"\\sub";
    HANDLE h = pFindFirst(root, &fd);
    Check(h != INVALID_HANDLE_VALUE && wcscmp(fd.cFileName, L"Move here to securely wipe") == 0,
          L"root shows the placeholder entry");
    Check(pFindNext(h, &fd) && wcscmp(fd.cFileName, L"Readme.txt") == 0 && fd.nFileSizeLow > 10000,
          L"second entry is Readme.txt with its size");
    Check(pFindNext(h, &fd) && wcscmp(fd.cFileName, L"Readme_ru.txt") == 0 && fd.nFileSizeLow > 10000,
          L"third entry is Readme_ru.txt");
    Check(!pFindNext(h, &fd), L"no further entries");
    {
        WCHAR ru[] = L"\\Readme_ru.txt", ruOpen[] = L"open";
        std::wstring ruCopy = w + L"readme_ru_copy.txt";
        Check(pExecute(nullptr, ru, ruOpen) == FS_EXEC_YOURSELF &&
              pGet(ru, &ruCopy[0], 0, nullptr) == FS_FILE_OK &&
              ReadAll(ruCopy).find("\xD0\x91\xD0\x95\xD0\xA2\xD0\x90") != std::string::npos,  // "БЕТА"
              L"Readme_ru.txt opens and contains the Russian text");
        DeleteFileW(ruCopy.c_str());
    }
    pFindClose(h);
    Check(pFindFirst(sub, &fd) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_NO_MORE_FILES,
          L"subfolders are empty");

    wprintf(L"Readme\n");
    WCHAR readme[] = L"\\Readme.txt", open[] = L"open", placeholder[] = L"\\Move here to securely wipe";
    Check(pExecute(nullptr, readme, open) == FS_EXEC_YOURSELF, L"Enter on Readme.txt lets TC open it");
    Check(pExecute(nullptr, placeholder, open) == FS_EXEC_OK, L"Enter on placeholder shows help");
    std::wstring copy = w + L"readme_copy.txt";
    Check(pGet(readme, &copy[0], 0, nullptr) == FS_FILE_OK, L"Readme.txt can be copied out");
    std::string text = ReadAll(copy);
    Check(text.compare(0, 3, "\xEF\xBB\xBF") == 0 && text.find("SSD") != std::string::npos,
          L"copied file is the UTF-8 readme text");
    Check(pGet(readme, &copy[0], 0, nullptr) == FS_FILE_EXISTS, L"existing target reported");
    Check(pGet(readme, &copy[0], FS_COPYFLAGS_OVERWRITE, nullptr) == FS_FILE_OK, L"overwrite allowed");
    Check(pGet(readme, &copy[0], FS_COPYFLAGS_MOVE | FS_COPYFLAGS_OVERWRITE, nullptr) == FS_FILE_NOTSUPPORTED,
          L"Readme.txt cannot be moved out");
    WCHAR other[] = L"\\other.txt";
    Check(pGet(other, &copy[0], FS_COPYFLAGS_OVERWRITE, nullptr) == FS_FILE_NOTFOUND, L"other names not found");
    DeleteFileW(copy.c_str());

    wprintf(L"Copy (F5) is refused\n");
    MakeFile(w + L"copy.txt", 100);
    Start();
    Check(Put(w, L"copy.txt", 0) == FS_FILE_USERABORT, L"F5 returns user abort");
    End();
    Check(Exists(w + L"copy.txt"), L"file kept after F5");

    wprintf(L"Confirmation with counts, names and folders\n");
    std::wstring src = w + L"src\\";
    CreateDirectoryW(src.c_str(), nullptr);
    CreateDirectoryW((src + L"Folder A").c_str(), nullptr);
    CreateDirectoryW((src + L"Folder A\\Sub").c_str(), nullptr);
    CreateDirectoryW((src + L"Folder A\\Empty").c_str(), nullptr);
    MakeFile(src + L"Folder A\\a1.txt", 1000);
    MakeFile(src + L"Folder A\\Sub\\a2.txt", 2000);
    const wchar_t* loose[] = { L"b.txt", L"c.txt", L"d.txt", L"e.txt", L"f.txt" };
    for (auto n : loose) MakeFile(src + n, 300);

    Start();
    MkDir(L"Folder A");
    MkDir(L"Folder A\\Sub");
    MkDir(L"Folder A\\Empty");
    bool allOk = Put(src, L"Folder A\\a1.txt") == FS_FILE_OK && Put(src, L"Folder A\\Sub\\a2.txt") == FS_FILE_OK;
    for (auto n : loose) allOk = allOk && Put(src, n) == FS_FILE_OK;
    Check(allOk, L"files are accepted");
    Check(Exists(src + L"Folder A\\a1.txt") && Exists(src + L"f.txt"), L"nothing is touched before confirmation");
    End();
    std::wstring confirm = pConfirm();
    wprintf(L"    --- confirmation text ---\n%s\n    -------------------------\n", confirm.c_str());
    Check(Contains(confirm, L"You are about to erase 7 files in 3 folders forever. "
                            L"The operation cannot be reverted. Please confirm."),
          L"message shows 7 files in 3 folders");
    Check(Count(confirm, L"• ") == 6 && Contains(confirm, L"• Folder A") &&
          Contains(confirm, L"• e.txt") && !Contains(confirm, L"f.txt") && Contains(confirm, L"• ..."),
          L"5 selected names listed, then ...");
    Check(!Exists(src + L"Folder A") && !Exists(src + L"b.txt") && !Exists(src + L"f.txt"),
          L"files and folders erased after confirmation");
    Check(Contains(pSummary(), L"Securely erased: 7 files and 3 folders."), L"summary counts files and folders");

    wprintf(L"Short selection has no ...\n");
    MakeFile(w + L"one.txt", 10);
    Start();
    Put(w, L"one.txt");
    End();
    confirm = pConfirm();
    Check(Contains(confirm, L"erase 1 file forever.") && Contains(confirm, L"• one.txt") &&
          !Contains(confirm, L"..."), L"single file: singular wording, no ...");

    wprintf(L"Special files\n");
    MakeFile(w + L"normal.txt", 10000);
    MakeFile(w + L"tiny.txt", 5);
    MakeFile(w + L"empty.txt", 0);
    MakeFile(w + L"readonly.txt", 300);
    SetFileAttributesW((w + L"readonly.txt").c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
    std::wstring uni = L"žluťoučký кот 猫.txt";
    MakeFile(w + uni, 500);
    MakeFile(w + L"big.bin", 5 * 1024 * 1024 + 123);
    MakeFile(w + L"ads.txt", 200);
    MakeFile(w + L"ads.txt:secret", 3000, 'S');
    std::wstring deepRel;
    for (int i = 0; i < 6; ++i) {
        deepRel += L"a_rather_long_folder_name_to_exceed_max_path_" + std::to_wstring(i) + L"\\";
        CreateDirectoryW((L"\\\\?\\" + w + deepRel).c_str(), nullptr);
    }
    MakeFile(L"\\\\?\\" + w + deepRel + L"long.txt", 400);
    MakeFile(w + L"locked.txt", 100);
    HANDLE lock = CreateFileW((w + L"locked.txt").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    MakeFile(w + L"link1.txt", 50, 'L');
    CreateHardLinkW((w + L"link2.txt").c_str(), (w + L"link1.txt").c_str(), nullptr);
    MakeFile(w + L"target.txt", 60, 'T');
    bool symlink = CreateSymbolicLinkW((w + L"symlink.txt").c_str(), (w + L"target.txt").c_str(),
                                       SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
    wchar_t win[MAX_PATH];
    GetSystemWindowsDirectoryW(win, MAX_PATH);

    Start();
    const wchar_t* good[] = { L"normal.txt", L"tiny.txt", L"empty.txt", L"readonly.txt", L"big.bin", L"ads.txt" };
    for (auto n : good) Put(w, n);
    Put(w, uni);
    Put(w, deepRel + L"long.txt");
    Put(w, L"locked.txt");
    Put(w, L"link1.txt");
    Put(w, L"missing.txt");
    Put(std::wstring(win) + L"\\", L"tcwiper_not_here.txt");
    if (symlink) Put(w, L"symlink.txt");
    End();
    CloseHandle(lock);
    std::wstring summary = pSummary();
    bool goodGone = !Exists(w + uni) && !Exists(w + deepRel + L"long.txt");
    for (auto n : good) goodGone = goodGone && !Exists(w + n);
    Check(goodGone, L"normal, tiny, empty, read-only, big, ADS, Unicode and long-path files erased");
    Check(Exists(w + L"locked.txt"), L"file in use left alone");
    Check(ReadAll(w + L"link2.txt") == std::string(50, 'L'), L"hard-linked file refused, data intact");
    Check(Contains(summary, L"Not erased: 4 files") && Contains(summary, L"protected") &&
          Contains(summary, L"in use") && Contains(summary, L"hard links"),
          L"summary lists the 4 refused files with reasons");
    if (symlink)
        Check(!Exists(w + L"symlink.txt") && ReadAll(w + L"target.txt") == std::string(60, 'T'),
              L"symbolic link removed, target untouched");
    else
        wprintf(L"  [SKIP] symbolic link test (needs Developer Mode or admin)\n");

    wprintf(L"Cancel\n");
    MakeFile(w + L"cancel1.bin", 1024 * 1024);
    MakeFile(w + L"cancel2.txt", 10);
    Start();
    Put(w, L"cancel1.bin");
    Put(w, L"cancel2.txt");
    pSetCancel(TRUE);
    End();
    pSetCancel(FALSE);
    Check(Exists(w + L"cancel1.bin") && Exists(w + L"cancel2.txt") &&
          Contains(pSummary(), L"Cancelled: 2 files were left untouched."),
          L"cancel leaves remaining files untouched");

    wprintf(L"Files already gone\n");
    Start();
    Put(w, L"gone1.txt");
    Put(w, L"gone2.txt");
    End();
    Check(Contains(pSummary(), L"NOT securely wiped"), L"warns when TC removed the files itself");

    wprintf(L"File outside a START/END bracket\n");
    MakeFile(w + L"single.txt", 100);
    Check(Put(w, L"single.txt") == FS_FILE_OK && !Exists(w + L"single.txt"), L"wiped immediately after confirmation");

    wprintf(L"WiperGo (selection of the active panel)\n");
    Check(pWiperGo() == -1 && Contains(pSummary(), L"only inside Total Commander"),
          L"refuses to run outside Total Commander");
    std::wstring go = w + L"go\\";
    CreateDirectoryW(go.c_str(), nullptr);
    CreateDirectoryW((go + L"Docs").c_str(), nullptr);
    CreateDirectoryW((go + L"Docs\\Deep").c_str(), nullptr);
    CreateDirectoryW((go + L"Empty dir").c_str(), nullptr);
    MakeFile(go + L"Docs\\d1.txt", 700);
    MakeFile(go + L"Docs\\Deep\\d2.txt", 800);
    MakeFile(go + L"файл.txt", 100);
    MakeFile(go + L"keep.txt", 100, 'K');
    Check(pWipeItems(go.c_str(), L"Docs|Empty dir|файл.txt") == 0, L"returns 0 after wiping");
    confirm = pConfirm();
    Check(Contains(confirm, L"erase 3 files in 3 folders forever.") && Contains(confirm, L"• Docs") &&
          Contains(confirm, L"• Empty dir") && Contains(confirm, L"• файл.txt"),
          L"confirmation counts files inside selected folders and lists the selection");
    Check(!Exists(go + L"Docs") && !Exists(go + L"Empty dir") && !Exists(go + L"файл.txt"),
          L"selected folders (also empty ones) and files erased");
    Check(ReadAll(go + L"keep.txt") == std::string(100, 'K'), L"unselected file untouched");
    Check(Contains(pSummary(), L"Securely erased: 3 files and 3 folders."), L"summary after WiperGo");
    Check(pWipeItems(go.c_str(), L"") == 2, L"returns 2 when nothing is selected");
    MakeFile(go + L"busy.txt", 100);
    HANDLE busy = CreateFileW((go + L"busy.txt").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    MakeFile(go + L"ok.txt", 100);
    Check(pWipeItems(go.c_str(), L"busy.txt|ok.txt") == -1 && Exists(go + L"busy.txt") &&
          !Exists(go + L"ok.txt") && Contains(pSummary(), L"in use"),
          L"returns -1 when a file in use was not erased");
    CloseHandle(busy);
    MakeFile(go + L"hl1.txt", 50);
    CreateHardLinkW((go + L"hl2.txt").c_str(), (go + L"hl1.txt").c_str(), nullptr);
    Check(pWipeItems(go.c_str(), L"hl1.txt") == -1 && Contains(pSummary(), L"hard links"),
          L"returns -1 when a hard-linked file was refused");
    MakeFile(go + L"c1.bin", 1024 * 1024);
    pSetCancel(TRUE);
    Check(pWipeItems(go.c_str(), L"c1.bin") == 1 && Exists(go + L"c1.bin"),
          L"returns 1 when cancelled during the wipe");
    pSetCancel(FALSE);
    DeleteTree(go);

    wprintf(L"WipeList (list file from a TC user command)\n");
    std::wstring lst = w + L"lst\\", listFile = work + L"\\list.tmp";
    CreateDirectoryW(lst.c_str(), nullptr);
    CreateDirectoryW((lst + L"A").c_str(), nullptr);
    CreateDirectoryW((lst + L"A\\Sub").c_str(), nullptr);
    MakeFile(lst + L"A\\a.txt", 100);
    MakeFile(lst + L"A\\Sub\\b.txt", 100);
    MakeFile(lst + L"c.txt", 100);
    MakeFile(lst + L"кириллица.txt", 100);
    MakeFile(lst + L"keep.txt", 100, 'K');
    // Folders end with \; A\a.txt is inside the listed folder A (branch view).
    WriteList(listFile, { lst + L"A\\", lst + L"c.txt", lst + L"A\\a.txt", lst + L"кириллица.txt" }, true, false);
    Check(pWipeList((L"\"" + listFile + L"\"").c_str()) == 0, L"returns 0 after wiping (UTF-16 list, quoted path)");
    confirm = pConfirm();
    Check(Contains(confirm, L"erase 4 files in 2 folders forever.") && Contains(confirm, L"• A\n") &&
          Contains(confirm, L"• c.txt") && Contains(confirm, L"• кириллица.txt") && !Contains(confirm, L"a.txt"),
          L"confirmation lists the selection; items inside a listed folder are not repeated");
    Check(!Exists(lst + L"A") && !Exists(lst + L"c.txt") && !Exists(lst + L"кириллица.txt"),
          L"listed folder and files erased");
    Check(ReadAll(lst + L"keep.txt") == std::string(100, 'K'), L"unlisted file untouched");
    CreateDirectoryW((lst + L"X").c_str(), nullptr);
    CreateDirectoryW((lst + L"Y").c_str(), nullptr);
    MakeFile(lst + L"X\\x.txt", 100);
    MakeFile(lst + L"Y\\y.txt", 100);
    WriteList(listFile, { lst + L"X\\x.txt", lst + L"Y\\y.txt" }, false, false);
    Check(pWipeList(listFile.c_str()) == 0 && !Exists(lst + L"X\\x.txt") && !Exists(lst + L"Y\\y.txt") &&
          Exists(lst + L"X") && Exists(lst + L"Y") && Contains(pConfirm(), L"• X\\x.txt"),
          L"files from different folders (UTF-8 list): files erased, their folders kept");
    MakeFile(lst + L"d.txt", 100);
    WriteList(listFile, { lst + L"d.txt", lst + L"nothere.txt" }, true, true);
    Check(pWipeList(listFile.c_str()) == -1 && Exists(lst + L"d.txt") && Contains(pSummary(), L"could not find"),
          L"missing item: nothing erased, returns -1");
    WriteList(listFile, { lst + L"d.txt", L"Q:\\other drive.txt" }, true, true);
    Check(pWipeList(listFile.c_str()) == -1 && Exists(lst + L"d.txt") && Contains(pSummary(), L"same one"),
          L"items on different drives refused");
    WriteList(listFile, {}, true, false);
    Check(pWipeList(listFile.c_str()) == 2 && Contains(pSummary(), L"Nothing to erase"), L"empty list returns 2");
    Check(pWipeList((work + L"\\no such list.tmp").c_str()) == -1 && Contains(pSummary(), L"%WL"),
          L"unreadable list file returns -1");
    DeleteFileW(listFile.c_str());
    DeleteTree(lst);

    // Only the files that were meant to survive may be left; no renamed leftovers.
    int count = 0;
    WIN32_FIND_DATAW f;
    HANDLE find = FindFirstFileW((w + L"*").c_str(), &f);
    std::wstring left;
    do {
        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ++count;
        left += std::wstring(L" ") + f.cFileName;
    } while (FindNextFileW(find, &f));
    FindClose(find);
    Check(count == 7, L"no leftover files (found:" + left + L")");

    DeleteTree(work);
    wprintf(g_failed ? L"\n%d test(s) FAILED\n" : L"\nAll tests passed\n", g_failed);
    return g_failed ? 1 : 0;
}
