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
typedef const wchar_t* (*tText)();
typedef void   (*tSetCancel)(BOOL);

static tFsFindFirstW  pFindFirst;
static tFsStatusInfoW pStatus;
static tFsPutFileW    pPut;
static tFsMkDirW      pMkDir;
static tText          pConfirm, pSummary;
static tSetCancel     pSetCancel;

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
    pConfirm   = (tText)GetProcAddress(dll, "TcwTestConfirmText");
    pSummary   = (tText)GetProcAddress(dll, "TcwTestSummaryText");
    pSetCancel = (tSetCancel)GetProcAddress(dll, "TcwTestSetCancel");
    const char* required[] = { "FsInit", "FsFindFirst", "FsFindNext", "FsFindClose", "FsGetDefRootName",
                               "FsPutFile", "FsMkDir", "FsStatusInfo", "FsExecuteFile" };
    bool allExports = pInit && pFindFirst && pStatus && pPut && pMkDir && pConfirm && pSummary && pSetCancel;
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
    Check(pFindFirst(sub, &fd) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_NO_MORE_FILES,
          L"subfolders are empty");

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
