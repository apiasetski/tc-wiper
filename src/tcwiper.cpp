// SPDX-License-Identifier: GPL-3.0-or-later
// TC Wiper - Total Commander file system plugin (WFX).
// Copyright (C) 2026 Andrei Piasetski. Developed using Claude Opus 5.5 Medium.
//
// The plugin panel shows a single placeholder entry. Files moved into it
// with F6 are not stored anywhere. While Total Commander hands over the
// files one by one, the plugin only collects them. When the operation ends
// it asks for confirmation (with counts and names) and then overwrites each
// file with random data, scrambles its name and deletes it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <algorithm>
#include "wfx_api.h"
#include "version.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

static const wchar_t kTitle[]       = L"TC Wiper";
static const wchar_t kPlaceholder[] = L"Move here to securely wipe";
static const char    kRootName[]    = "Secure Wipe";

// Readme files listed in the panel after the placeholder (RCDATA in tcwiper.rc).
struct ReadmeEntry {
    const wchar_t* name;
    int resId;
};
static const ReadmeEntry kReadmes[] = {
    { L"Readme.txt",    2 },
    { L"Readme_ru.txt", 3 },
};
static const int kReadmeCount = sizeof(kReadmes) / sizeof(kReadmes[0]);

static const DWORD  kChunk        = 1024 * 1024;  // overwrite buffer size
static const int    kRenamePasses = 3;            // renames before delete
static const size_t kMaxNames     = 5;            // names shown in the confirmation
static const size_t kMaxListed    = 10;           // failures shown in the summary

static int            g_pluginNr;
static tProgressProcW g_progressW;
static tProgressProc  g_progressA;

#ifdef TCWIPER_TEST
static std::wstring g_testConfirm, g_testSummary;
static bool g_testCancel;
#endif

struct Entry {
    std::wstring local, remote;
};

// One F6 operation, between FsStatusInfo START and END.
struct Batch {
    bool active = false;
    bool copyRefused = false;
    std::vector<Entry> files;
    std::vector<std::wstring> remoteDirs;
};
static Batch g_batch;

// Results of the wipe, for the summary.
struct Result {
    int wiped = 0;
    int foldersRemoved = 0;
    int bestEffort = 0;       // compressed / encrypted / sparse files
    int skipped = 0;          // not reached because of Cancel
    std::vector<std::wstring> failures;
};

// ---------------------------------------------------------------------------
// Helpers

static std::wstring ToWide(const char* s)
{
    if (!s) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
    return w;
}

static std::string ToAnsi(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void CopyAnsi(char* dst, size_t dstLen, const std::string& src)
{
    if (!dstLen) return;
    size_t n = src.size() < dstLen - 1 ? src.size() : dstLen - 1;
    memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

static std::wstring ErrorText(DWORD code)
{
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring text = buf ? buf : L"unknown error";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text;
}

static bool SameText(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

static std::wstring Plural(size_t n, const wchar_t* word)
{
    return std::to_wstring(n) + L" " + word + (n == 1 ? L"" : L"s");
}

// Adds the \\?\ prefix so paths longer than MAX_PATH work.
static std::wstring LongPath(const std::wstring& p)
{
    if (p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}

static bool StartsWithFolder(const std::wstring& path, std::wstring folder)
{
    if (folder.empty()) return false;
    if (folder.back() != L'\\') folder += L'\\';
    if (path.size() < folder.size()) return false;
    return SameText(path.substr(0, folder.size()), folder);
}

static std::wstring EnvVar(const wchar_t* name)
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
}

// Windows and program folders are never wiped. This also keeps the 32-bit
// build safe from WOW64 redirection, which only affects paths under Windows.
static bool IsProtected(const std::wstring& path)
{
    wchar_t win[MAX_PATH];
    UINT n = GetSystemWindowsDirectoryW(win, MAX_PATH);
    std::wstring folders[] = {
        (n > 0 && n < MAX_PATH) ? std::wstring(win, n) : std::wstring(),
        EnvVar(L"ProgramW6432"),
        EnvVar(L"ProgramFiles"),
        EnvVar(L"ProgramFiles(x86)"),
    };
    for (const auto& f : folders)
        if (StartsWithFolder(path, f)) return true;
    return false;
}

static bool RandomBytes(void* buf, ULONG len)
{
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), len,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

static std::wstring RandomName(size_t len)
{
    static const wchar_t kChars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::vector<unsigned char> rnd(len ? len : 1);
    RandomBytes(rnd.data(), (ULONG)rnd.size());
    std::wstring name;
    for (unsigned char b : rnd) name += kChars[b % 36];
    return name;
}

[[maybe_unused]] static std::wstring FileNameOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// "\Folder\file.txt" -> "Folder": the item the user selected in TC.
static std::wstring TopLevelName(const std::wstring& remote)
{
    std::wstring rel = !remote.empty() && remote[0] == L'\\' ? remote.substr(1) : remote;
    return rel.substr(0, rel.find(L'\\'));
}

// The local folder TC copies from, found by matching a file's local and
// remote names: "C:\Src\Folder\a.txt" + "\Folder\a.txt" -> "C:\Src\".
// Empty if the user renamed the target in TC's dialog.
static std::wstring SourceBase(const Entry& e)
{
    if (e.remote.size() < 2 || e.remote[0] != L'\\') return std::wstring();
    std::wstring rel = e.remote.substr(1);
    if (e.local.size() <= rel.size()) return std::wstring();
    size_t cut = e.local.size() - rel.size();
    if (e.local[cut - 1] != L'\\' || !SameText(e.local.substr(cut), rel)) return std::wstring();
    return e.local.substr(0, cut);
}

static bool IsFolderEmpty(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return empty;
}

[[maybe_unused]] static HWND OwnerWindow()
{
    HWND w = GetActiveWindow();
    return w ? GetAncestor(w, GA_ROOTOWNER) : nullptr;
}

// Message boxes. The test build records the text and answers Yes.
static bool AskYesNo(const std::wstring& text)
{
#ifdef TCWIPER_TEST
    g_testConfirm = text;
    return true;
#else
    return MessageBoxW(OwnerWindow(), text.c_str(), kTitle,
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
#endif
}

static void ShowMessage(const std::wstring& text, UINT icon)
{
#ifdef TCWIPER_TEST
    (void)icon;
    g_testSummary = text;
#else
    MessageBoxW(OwnerWindow(), text.c_str(), kTitle, MB_OK | icon);
#endif
}

// ---------------------------------------------------------------------------
// Progress window shown while a batch is wiped

static const int kIdText = 100, kIdBar = 101;

struct ProgressWindow {
    HWND wnd = nullptr, owner = nullptr, text = nullptr, bar = nullptr;
    HFONT font = nullptr;
    bool cancelled = false;
};
static ProgressWindow g_ui;

[[maybe_unused]] static LRESULT CALLBACK ProgressWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if ((msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) || msg == WM_CLOSE) {
        g_ui.cancelled = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

[[maybe_unused]] static void PumpMessages()
{
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (!g_ui.wnd || !IsDialogMessageW(g_ui.wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
}

static void OpenProgressWindow()
{
    g_ui = ProgressWindow();
#ifndef TCWIPER_TEST
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = ProgressWndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"TCWiperProgress";
    RegisterClassExW(&wc);  // fails harmlessly if already registered

    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_ui.font = CreateFontIndirectW(&ncm.lfMessageFont);

    RECT rc = { 0, 0, S(440), S(110) };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    g_ui.owner = OwnerWindow();
    RECT orc;
    if (!g_ui.owner || !GetWindowRect(g_ui.owner, &orc))
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    int x = orc.left + (orc.right - orc.left - w) / 2;
    int y = orc.top + (orc.bottom - orc.top - h) / 2;

    g_ui.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"TC Wiper - erasing",
                               style, x, y, w, h, g_ui.owner, nullptr, inst, nullptr);
    if (!g_ui.wnd) return;
    g_ui.text = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                S(12), S(12), S(416), S(20), g_ui.wnd, (HMENU)(INT_PTR)kIdText, inst, nullptr);
    g_ui.bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                               S(12), S(38), S(416), S(20), g_ui.wnd, (HMENU)(INT_PTR)kIdBar, inst, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  S(340), S(72), S(88), S(26), g_ui.wnd, (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
    for (HWND c : { g_ui.text, cancel })
        SendMessageW(c, WM_SETFONT, (WPARAM)g_ui.font, TRUE);
    SendMessageW(g_ui.bar, PBM_SETRANGE32, 0, 1000);

    if (g_ui.owner) EnableWindow(g_ui.owner, FALSE);
    ShowWindow(g_ui.wnd, SW_SHOW);
    SetFocus(cancel);
    PumpMessages();
#endif
}

static void CloseProgressWindow()
{
    if (g_ui.owner) EnableWindow(g_ui.owner, TRUE);
    if (g_ui.wnd) DestroyWindow(g_ui.wnd);
    if (g_ui.font) DeleteObject(g_ui.font);
    if (g_ui.owner) SetActiveWindow(g_ui.owner);
    g_ui = ProgressWindow();
}

// ---------------------------------------------------------------------------
// Wiping

struct Progress {
    bool batch = false;            // our window, else TC's callback
    std::wstring source, target;   // current file
    size_t fileIndex = 0, fileCount = 0;
    ULONGLONG total = 0, done = 0;
    int lastPermille = -1;
    bool aborted = false;
};

// Shows progress. Returns true if the user pressed Cancel.
static bool ReportProgress(Progress& pr, bool newFile)
{
    ULONGLONG done = pr.done < pr.total ? pr.done : pr.total;
    int permille = pr.total ? (int)(done * 1000 / pr.total) : 1000;
    if (!newFile && permille == pr.lastPermille) return false;
    pr.lastPermille = permille;

    if (pr.batch) {
#ifdef TCWIPER_TEST
        return g_testCancel;
#else
        if (g_ui.wnd) {
            if (newFile) {
                std::wstring text = L"File " + std::to_wstring(pr.fileIndex) + L" of " +
                                    std::to_wstring(pr.fileCount) + L":  " + FileNameOf(pr.source);
                SetWindowTextW(g_ui.text, text.c_str());
            }
            SendMessageW(g_ui.bar, PBM_SETPOS, permille, 0);
        }
        PumpMessages();
        return g_ui.cancelled;
#endif
    }
    if (g_progressW) {
        std::wstring s = pr.source, d = pr.target;
        return g_progressW(g_pluginNr, &s[0], &d[0], permille / 10) != 0;
    }
    if (g_progressA) {
        std::string s = ToAnsi(pr.source), d = ToAnsi(pr.target);
        return g_progressA(g_pluginNr, &s[0], &d[0], permille / 10) != 0;
    }
    return false;
}

// Overwrites [from, to) of an open file or stream with random data.
static bool OverwriteRange(HANDLE h, ULONGLONG from, ULONGLONG to, std::vector<BYTE>& buf,
                           Progress& pr, std::wstring& err)
{
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)from;
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
        err = ErrorText(GetLastError());
        return false;
    }
    for (ULONGLONG left = to > from ? to - from : 0; left > 0 && !pr.aborted;) {
        DWORD n = left < kChunk ? (DWORD)left : kChunk;
        if (!RandomBytes(buf.data(), n)) {
            err = L"random number generator failed";
            return false;
        }
        DWORD written = 0;
        if (!WriteFile(h, buf.data(), n, &written, nullptr) || written != n) {
            err = ErrorText(GetLastError());
            return false;
        }
        left -= n;
        pr.done += n;
        pr.aborted = ReportProgress(pr, false);
    }
    if (!FlushFileBuffers(h)) {
        err = ErrorText(GetLastError());
        return false;
    }
    return true;
}

// Overwrites every alternate data stream (file.txt:name) of a file.
static bool WipeAlternateStreams(const std::wstring& path, std::vector<BYTE>& buf,
                                 Progress& pr, std::wstring& err)
{
    WIN32_FIND_STREAM_DATA sd;
    HANDLE find = FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &sd, 0);
    if (find == INVALID_HANDLE_VALUE) return true;  // no streams or not supported
    bool ok = true;
    do {
        if (lstrcmpiW(sd.cStreamName, L"::$DATA") == 0) continue;  // main stream
        HANDLE h = CreateFileW((path + sd.cStreamName).c_str(), GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            err = L"cannot open stream " + std::wstring(sd.cStreamName) + L": " + ErrorText(GetLastError());
            ok = false;
            break;
        }
        ok = OverwriteRange(h, 0, (ULONGLONG)sd.StreamSize.QuadPart, buf, pr, err);
        CloseHandle(h);
    } while (ok && !pr.aborted && FindNextStreamW(find, &sd));
    FindClose(find);
    return ok;
}

// Renames a file or empty folder a few times to random names, then deletes it.
static bool ScrambleNameAndDelete(std::wstring path, bool folder, std::wstring& err)
{
    size_t slash = path.find_last_of(L'\\');
    std::wstring dir = path.substr(0, slash + 1);
    size_t nameLen = path.size() - slash - 1;
    for (int i = 0; i < kRenamePasses; ++i) {
        std::wstring next = dir + RandomName(nameLen);
        if (MoveFileExW(path.c_str(), next.c_str(), 0))
            path = next;
    }
    if (!(folder ? RemoveDirectoryW(path.c_str()) : DeleteFileW(path.c_str()))) {
        err = L"cannot delete: " + ErrorText(GetLastError());
        return false;
    }
    return true;
}

// Securely wipes one file. Returns an FS_FILE_* code.
static int WipeFile(const std::wstring& localName, Progress& pr, Result& res, std::wstring& err)
{
    std::wstring plain = localName.rfind(L"\\\\?\\", 0) == 0 ? localName.substr(4) : localName;
    if (IsProtected(plain)) {
        err = L"Windows and Program Files folders are protected";
        return FS_FILE_WRITEERROR;
    }

    std::wstring path = LongPath(localName);
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        err = ErrorText(GetLastError());
        return FS_FILE_NOTFOUND;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        err = L"is a folder";
        return FS_FILE_NOTSUPPORTED;
    }

    // A symbolic link is removed without touching the file it points to.
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) {
            err = L"cannot delete link: " + ErrorText(GetLastError());
            return FS_FILE_WRITEERROR;
        }
        return FS_FILE_OK;
    }

    if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        err = code == ERROR_SHARING_VIOLATION ? L"file is in use by another program" : ErrorText(code);
        SetFileAttributesW(path.c_str(), attrs);
        return FS_FILE_WRITEERROR;
    }

    BY_HANDLE_FILE_INFORMATION info;
    FILE_STANDARD_INFO stdInfo = {};
    if (!GetFileInformationByHandle(h, &info) ||
        !GetFileInformationByHandleEx(h, FileStandardInfo, &stdInfo, sizeof(stdInfo))) {
        err = ErrorText(GetLastError());
        CloseHandle(h);
        SetFileAttributesW(path.c_str(), attrs);
        return FS_FILE_READERROR;
    }
    // Overwriting would also destroy the data seen through the other names.
    if (info.nNumberOfLinks > 1) {
        err = L"file has other hard links; not wiped";
        CloseHandle(h);
        SetFileAttributesW(path.c_str(), attrs);
        return FS_FILE_WRITEERROR;
    }
    if (attrs & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_SPARSE_FILE))
        ++res.bestEffort;

    ULONGLONG size  = (ULONGLONG)stdInfo.EndOfFile.QuadPart;
    ULONGLONG alloc = (ULONGLONG)stdInfo.AllocationSize.QuadPart;
    ULONGLONG slack = alloc > size ? alloc - size : 0;
    if (!pr.batch) {
        pr.total = size + slack;
        pr.done = 0;
    }

    std::vector<BYTE> buf(kChunk);
    // Pass 1 overwrites the data in place; pass 2 fills the unused tail of
    // the last cluster (slack space), which can hold older data.
    bool ok = OverwriteRange(h, 0, size, buf, pr, err) &&
              OverwriteRange(h, size, size + slack, buf, pr, err);

    // Truncate so no allocated data remains once the file is deleted.
    LARGE_INTEGER zero = {};
    SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
    SetEndOfFile(h);
    FlushFileBuffers(h);
    CloseHandle(h);

    if (ok && !pr.aborted)
        ok = WipeAlternateStreams(path, buf, pr, err);

    // Even after Cancel the file is already damaged, so it is removed.
    std::wstring delErr;
    if (!ScrambleNameAndDelete(path, false, delErr)) {
        err = err.empty() ? delErr : err + L"; " + delErr;
        return FS_FILE_WRITEERROR;
    }
    if (pr.aborted) return FS_FILE_USERABORT;
    return ok ? FS_FILE_OK : FS_FILE_WRITEERROR;
}

static void AddFailure(Result& res, const std::wstring& path, const std::wstring& err)
{
    res.failures.push_back(path + L" \u2014 " + err);
}

// ---------------------------------------------------------------------------
// Confirmation and summary

static std::wstring ConfirmText(const Batch& b)
{
    std::wstring text = L"You are about to erase " + Plural(b.files.size(), L"file");
    if (!b.remoteDirs.empty())
        text += L" in " + Plural(b.remoteDirs.size(), L"folder");
    text += L" forever. The operation cannot be reverted. Please confirm.\n";

    // The items the user selected, in TC's order.
    std::vector<std::wstring> names;
    auto add = [&names](const std::wstring& remote) {
        std::wstring name = TopLevelName(remote);
        if (name.empty()) return;
        for (const auto& n : names)
            if (SameText(n, name)) return;
        names.push_back(name);
    };
    for (const auto& d : b.remoteDirs) add(d);
    for (const auto& f : b.files) add(f.remote);

    for (size_t i = 0; i < names.size() && i < kMaxNames; ++i)
        text += L"\n\u2022 " + names[i];
    if (names.size() > kMaxNames)
        text += L"\n\u2022 ...";
    return text;
}

static void ShowSummary(const Result& res)
{
    std::wstring text = L"Securely erased: " + Plural(res.wiped, L"file");
    if (res.foldersRemoved)
        text += L" and " + Plural(res.foldersRemoved, L"folder");
    text += L".";
    if (res.skipped)
        text += L"\n\nCancelled: " + Plural(res.skipped, L"file") + L" were left untouched.";
    if (!res.failures.empty()) {
        text += L"\n\nNot erased: " + Plural(res.failures.size(), L"file") + L":";
        for (size_t i = 0; i < res.failures.size() && i < kMaxListed; ++i)
            text += L"\n\u2022 " + res.failures[i];
        if (res.failures.size() > kMaxListed)
            text += L"\n\u2022 ...";
    }
    if (res.bestEffort > 0)
        text += L"\n\n" + Plural(res.bestEffort, L"file") +
                L" were compressed, encrypted or sparse. Old copies of their data "
                L"may remain elsewhere on the disk.";
    ShowMessage(text, res.failures.empty() ? MB_ICONINFORMATION : MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// Batch processing

static void WipeBatch(const Batch& b)
{
    // TC should leave moved files for the plugin to remove. If they are all
    // gone already, TC deleted them itself and they were never wiped.
    size_t present = 0;
    for (const auto& f : b.files)
        if (GetFileAttributesW(LongPath(f.local).c_str()) != INVALID_FILE_ATTRIBUTES) ++present;
    if (present == 0) {
        ShowMessage(L"The files were already removed by Total Commander before TC Wiper could "
                    L"erase them, so they were NOT securely wiped.", MB_ICONERROR);
        return;
    }

    if (!AskYesNo(ConfirmText(b)))
        return;

    Progress pr;
    pr.batch = true;
    pr.fileCount = b.files.size();
    for (const auto& f : b.files) {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(LongPath(f.local).c_str(), GetFileExInfoStandard, &fa))
            pr.total += ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    }

    Result res;
    OpenProgressWindow();
    for (const auto& f : b.files) {
        if (pr.aborted) {
            ++res.skipped;
            continue;
        }
        ++pr.fileIndex;
        pr.source = f.local;
        pr.target = f.remote;
        pr.aborted = ReportProgress(pr, true);
        if (pr.aborted) {
            ++res.skipped;
            continue;
        }
        std::wstring err;
        int rc = WipeFile(f.local, pr, res, err);
        if (rc == FS_FILE_OK || rc == FS_FILE_USERABORT)
            ++res.wiped;
        else
            AddFailure(res, f.local, err);
    }

    // Remove the source folders that are now empty, deepest first.
    std::wstring base = b.files.empty() ? std::wstring() : SourceBase(b.files.front());
    if (!base.empty()) {
        std::vector<std::wstring> dirs = b.remoteDirs;
        std::sort(dirs.begin(), dirs.end(),
                  [](const std::wstring& x, const std::wstring& y) { return x.size() > y.size(); });
        for (const auto& d : dirs) {
            std::wstring local = LongPath(base + d.substr(1));
            DWORD attrs = GetFileAttributesW(local.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                !IsFolderEmpty(local))
                continue;
            if (attrs & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(local.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            std::wstring err;
            if (ScrambleNameAndDelete(local, true, err))
                ++res.foldersRemoved;
        }
    }
    CloseProgressWindow();
    ShowSummary(res);
}

// Used when TC hands over a file outside a START/END bracket.
static int WipeSingle(const std::wstring& local, const std::wstring& remote)
{
    Batch b;
    b.files.push_back({ local, remote });
    if (!AskYesNo(ConfirmText(b)))
        return FS_FILE_USERABORT;
    Progress pr;
    pr.source = local;
    pr.target = remote;
    Result res;
    std::wstring err;
    int rc = WipeFile(local, pr, res, err);
    if (rc != FS_FILE_OK && rc != FS_FILE_USERABORT) {
        AddFailure(res, local, err);
        ShowSummary(res);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// Readme entries, stored as resources inside the plugin

static bool ReadmeData(int resId, const BYTE*& data, DWORD& size)
{
    HMODULE mod = reinterpret_cast<HMODULE>(&__ImageBase);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resId), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(mod, res) : nullptr;
    data = mem ? static_cast<const BYTE*>(LockResource(mem)) : nullptr;
    size = res ? SizeofResource(mod, res) : 0;
    return data != nullptr;
}

// Returns the readme's resource id, or 0 if the name is not a readme.
static int FindReadme(const wchar_t* remote)
{
    if (!remote || remote[0] != L'\\') return 0;
    for (const auto& r : kReadmes)
        if (lstrcmpiW(remote + 1, r.name) == 0) return r.resId;
    return 0;
}

// The readme shows the plugin file's own date, so it stays the same.
static FILETIME PluginFileTime()
{
    FILETIME ft = {};
    wchar_t path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH) &&
        GetFileAttributesExW(path, GetFileExInfoStandard, &fa))
        ft = fa.ftLastWriteTime;
    return ft;
}

// ---------------------------------------------------------------------------
// Exported WFX functions (Unicode)

extern "C" {

int __stdcall FsInitW(int PluginNr, tProgressProcW pProgressProcW, tLogProcW, tRequestProcW)
{
    g_pluginNr  = PluginNr;
    g_progressW = pProgressProcW;
    return 0;
}

HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData)
{
    if (!Path || lstrcmpW(Path, L"\\") != 0) {
        SetLastError(ERROR_NO_MORE_FILES);
        return INVALID_HANDLE_VALUE;
    }
    ZeroMemory(FindData, sizeof(*FindData));
    FindData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    GetSystemTimeAsFileTime(&FindData->ftLastWriteTime);
    lstrcpynW(FindData->cFileName, kPlaceholder, MAX_PATH);
    return reinterpret_cast<HANDLE>(new int(1));  // next entry to return
}

// Following entries: the readme files.
BOOL __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData)
{
    int* next = reinterpret_cast<int*>(Hdl);
    const BYTE* data;
    DWORD size;
    if (!next || *next < 1 || *next > kReadmeCount)
        return FALSE;
    const ReadmeEntry& readme = kReadmes[*next - 1];
    ++*next;
    if (!ReadmeData(readme.resId, data, size))
        return FALSE;
    ZeroMemory(FindData, sizeof(*FindData));
    FindData->dwFileAttributes = FILE_ATTRIBUTE_READONLY;
    FindData->nFileSizeLow = size;
    FindData->ftLastWriteTime = PluginFileTime();
    lstrcpynW(FindData->cFileName, readme.name, MAX_PATH);
    return TRUE;
}

int __stdcall FsFindClose(HANDLE Hdl)
{
    delete reinterpret_cast<int*>(Hdl);
    return 0;
}

// Copies a readme out of the plugin. TC uses this for F3, F5 and for
// opening it with Enter (FS_EXEC_YOURSELF).
int __stdcall FsGetFileW(WCHAR* RemoteName, WCHAR* LocalName, int CopyFlags, RemoteInfoStruct*)
{
    const BYTE* data;
    DWORD size;
    int resId = FindReadme(RemoteName);
    if (!resId || !ReadmeData(resId, data, size))
        return FS_FILE_NOTFOUND;
    if (CopyFlags & FS_COPYFLAGS_MOVE)
        return FS_FILE_NOTSUPPORTED;
    std::wstring local = LongPath(LocalName);
    if (!(CopyFlags & FS_COPYFLAGS_OVERWRITE) && GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES)
        return FS_FILE_EXISTS;

    Progress pr;
    pr.source = RemoteName;
    pr.target = LocalName;
    pr.total = size;
    if (ReportProgress(pr, true))
        return FS_FILE_USERABORT;
    HANDLE h = CreateFileW(local.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return FS_FILE_WRITEERROR;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, nullptr) && written == size;
    CloseHandle(h);
    if (!ok)
        return FS_FILE_WRITEERROR;
    pr.done = size;
    ReportProgress(pr, false);
    return FS_FILE_OK;
}

// Folders "created" in the plugin are not stored; they are only counted.
BOOL __stdcall FsMkDirW(WCHAR* Path)
{
    if (g_batch.active && Path)
        g_batch.remoteDirs.push_back(Path);
    return TRUE;
}

void __stdcall FsStatusInfoW(WCHAR*, int InfoStartEnd, int InfoOperation)
{
    if (InfoOperation != FS_STATUS_OP_PUT_SINGLE && InfoOperation != FS_STATUS_OP_PUT_MULTI)
        return;
    if (InfoStartEnd == FS_STATUS_START) {
        g_batch = Batch();
        g_batch.active = true;
        return;
    }
    Batch b = g_batch;
    g_batch = Batch();
    if (!b.files.empty())
        WipeBatch(b);
}

// Only records the file; it is wiped when the operation ends.
int __stdcall FsPutFileW(WCHAR* LocalName, WCHAR* RemoteName, int CopyFlags)
{
    if (!(CopyFlags & FS_COPYFLAGS_MOVE)) {
        if (!g_batch.copyRefused) {
            g_batch.copyRefused = true;
            ShowMessage(L"Copy (F5) does not erase anything.\n\n"
                        L"Use Move (F6) to securely erase the selected files.", MB_ICONINFORMATION);
        }
        return FS_FILE_USERABORT;
    }
    std::wstring local = LocalName, remote = RemoteName ? RemoteName : L"";
    if (!g_batch.active)
        return WipeSingle(local, remote);
    g_batch.files.push_back({ local, remote });
    return FS_FILE_OK;
}

int __stdcall FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb)
{
    // TC copies the readme to a temporary folder and opens it with the
    // program associated with .txt files.
    if (FindReadme(RemoteName))
        return Verb && wcsncmp(Verb, L"open", 4) == 0 ? FS_EXEC_YOURSELF : FS_EXEC_ERROR;
    if (Verb && (wcsncmp(Verb, L"open", 4) == 0 || wcsncmp(Verb, L"properties", 10) == 0)) {
#ifndef TCWIPER_TEST
        MessageBoxW(MainWin,
                    L"Select files and folders in the other panel and press F6 (Move) "
                    L"to send them here.\n\n"
                    L"After you confirm, their contents are overwritten with random data, "
                    L"the names are scrambled and the files are deleted.\n\n"
                    L"Windows and Program Files folders are protected. "
                    L"Open Readme.txt for details on how secure erasing is on HDDs and SSDs.\n\n"
                    L"TC Wiper " TCW_VERSION_TEXT L"\n"
                    TCW_COPYRIGHT L"\n"
                    L"License: " TCW_LICENSE L"\n"
                    TCW_URL L"\n\n"
                    TCW_AI_NOTE L".",
                    kTitle, MB_OK | MB_ICONINFORMATION);
#else
        (void)MainWin;
#endif
        return FS_EXEC_OK;
    }
    return FS_EXEC_ERROR;
}

// ---------------------------------------------------------------------------
// Exported WFX functions (ANSI, for TC versions without Unicode support)

int __stdcall FsInit(int PluginNr, tProgressProc pProgressProc, tLogProc, tRequestProc)
{
    g_pluginNr  = PluginNr;
    g_progressA = pProgressProc;
    return 0;
}

HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData)
{
    WIN32_FIND_DATAW w;
    std::wstring path = ToWide(Path);
    HANDLE h = FsFindFirstW(&path[0], &w);
    if (h == INVALID_HANDLE_VALUE) return h;
    ZeroMemory(FindData, sizeof(*FindData));
    FindData->dwFileAttributes = w.dwFileAttributes;
    FindData->ftLastWriteTime  = w.ftLastWriteTime;
    CopyAnsi(FindData->cFileName, MAX_PATH, ToAnsi(w.cFileName));
    return h;
}

BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData)
{
    WIN32_FIND_DATAW w;
    if (!FsFindNextW(Hdl, &w)) return FALSE;
    ZeroMemory(FindData, sizeof(*FindData));
    FindData->dwFileAttributes = w.dwFileAttributes;
    FindData->nFileSizeLow     = w.nFileSizeLow;
    FindData->ftLastWriteTime  = w.ftLastWriteTime;
    CopyAnsi(FindData->cFileName, MAX_PATH, ToAnsi(w.cFileName));
    return TRUE;
}

int __stdcall FsGetFile(char* RemoteName, char* LocalName, int CopyFlags, RemoteInfoStruct* ri)
{
    std::wstring remote = ToWide(RemoteName), local = ToWide(LocalName);
    return FsGetFileW(&remote[0], &local[0], CopyFlags, ri);
}

BOOL __stdcall FsMkDir(char* Path)
{
    std::wstring path = ToWide(Path);
    return FsMkDirW(&path[0]);
}

void __stdcall FsStatusInfo(char*, int InfoStartEnd, int InfoOperation)
{
    FsStatusInfoW(nullptr, InfoStartEnd, InfoOperation);
}

int __stdcall FsPutFile(char* LocalName, char* RemoteName, int CopyFlags)
{
    std::wstring local = ToWide(LocalName), remote = ToWide(RemoteName);
    return FsPutFileW(&local[0], &remote[0], CopyFlags);
}

int __stdcall FsExecuteFile(HWND MainWin, char* RemoteName, char* Verb)
{
    std::wstring remote = ToWide(RemoteName), verb = ToWide(Verb);
    return FsExecuteFileW(MainWin, &remote[0], &verb[0]);
}

void __stdcall FsGetDefRootName(char* DefRootName, int maxlen)
{
    CopyAnsi(DefRootName, (size_t)maxlen, kRootName);
}

#ifdef TCWIPER_TEST
__declspec(dllexport) const wchar_t* TcwTestConfirmText() { return g_testConfirm.c_str(); }
__declspec(dllexport) const wchar_t* TcwTestSummaryText() { return g_testSummary.c_str(); }
__declspec(dllexport) void TcwTestSetCancel(BOOL cancel) { g_testCancel = cancel != FALSE; }
#endif

}  // extern "C"
