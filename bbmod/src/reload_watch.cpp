#include "reload_watch.h"
#include <algorithm>
#include <cwctype>

std::string reload_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &result[0], size, nullptr, nullptr);
    return result;
}
static std::wstring normalize(std::wstring path) {
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) {
        return c == L'/' ? L'\\' : (wchar_t)towlower(c);
    });
    return path;
}
ReloadWatch::ReloadWatch(std::wstring root, DWORD debounce)
    : root_(std::move(root)), debounce_(debounce) {}
ReloadWatch::~ReloadWatch() { stop(); }
void ReloadWatch::stop() {
    if (directory_ != INVALID_HANDLE_VALUE) {
        if (pending_) {
            CancelIoEx(directory_, &operation_);
            DWORD ignored;
            GetOverlappedResult(directory_, &operation_, &ignored, TRUE);
        }
        CloseHandle(directory_);
        directory_ = INVALID_HANDLE_VALUE;
    }
    if (operation_.hEvent) CloseHandle(operation_.hEvent);
    operation_ = {};
    pending_ = false;
}
bool ReloadWatch::excluded(const std::wstring& relative) const {
    auto path = normalize(relative);
    auto slash = path.find_last_of(L'\\');
    auto name = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    if (name.empty() || name[0] == L'.' || name[0] == L'#') return true;
    if (path == L"fonts" || path == L"customdlls") return true;
    for (const auto& entry : excludes) {
        auto pattern = normalize(entry);
        if (path == pattern || name == pattern) return true;
    }
    return false;
}
bool ReloadWatch::scan(const std::wstring& relative, Sources& files, std::string& error) const {
    const auto path = root_ + (relative.empty() ? L"" : L"\\" + relative);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        error = "Cannot scan " + reload_utf8(path) + " (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    bool ok = true;
    do {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        const auto child = relative.empty() ? name : relative + L"\\" + name;
        if (excluded(child) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!scan(child, files, error)) { ok = false; break; }
        } else {
            auto lower = normalize(name);
            if (lower.size() < 4 || lower.substr(lower.size() - 4) != L".lua") continue;
            HANDLE file = CreateFileW((root_ + L"\\" + child).c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE) { error = "Cannot read " + reload_utf8(child); ok = false; break; }
            std::string contents;
            char block[8192];
            DWORD count;
            BOOL read;
            while ((read = ReadFile(file, block, sizeof(block), &count, nullptr)) && count)
                contents.append(block, count);
            CloseHandle(file);
            if (!read) { error = "Cannot read " + reload_utf8(child); ok = false; break; }
            files.emplace(child, std::move(contents));
        }
    } while (FindNextFileW(find, &data));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) {
        error = "Directory enumeration failed: " + reload_utf8(path);
        ok = false;
    }
    FindClose(find);
    return ok;
}
bool ReloadWatch::snapshot(Sources& files, std::string& error) const {
    files.clear();
    return scan(L"", files, error);
}
bool ReloadWatch::arm() {
    ResetEvent(operation_.hEvent);
    pending_ = ReadDirectoryChangesW(directory_, buffer_, sizeof(buffer_), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
        nullptr, &operation_, nullptr) != FALSE;
    if (!pending_) error_ = "File watching failed (Windows error " + std::to_string(GetLastError()) + "). Retrying.";
    return pending_;
}
bool ReloadWatch::open() {
    directory_ = CreateFileW(root_.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory_ == INVALID_HANDLE_VALUE) {
        error_ = "Cannot watch addons directory. Retrying.";
        return false;
    }
    operation_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation_.hEvent || !arm()) { stop(); return false; }
    error_.clear();
    // Arm before taking a baseline so saves during enumeration remain queued.
    Sources current;
    std::string error;
    if (snapshot(current, error)) {
        if (baseline_ && current != previous_) { dirty_ = true; changed_at_ = GetTickCount(); }
        else previous_ = std::move(current);
        baseline_ = true;
    } else { error_ = error; dirty_ = true; changed_at_ = GetTickCount(); }
    return true;
}
bool ReloadWatch::poll() {
    if (!enabled) { stop(); return false; }
    DWORD now = GetTickCount();
    if (directory_ == INVALID_HANDLE_VALUE) {
        if (retry_at_ && (DWORD)(now - retry_at_) < 1000) return false;
        retry_at_ = now;
        if (!open()) return false;
    }
    if (WaitForSingleObject(operation_.hEvent, 0) == WAIT_OBJECT_0) {
        DWORD bytes = 0;
        BOOL ok = GetOverlappedResult(directory_, &operation_, &bytes, FALSE);
        DWORD code = ok ? ERROR_SUCCESS : GetLastError();
        pending_ = false;
        // Enumerate after the quiet period, including on buffer overflow. Comparing
        // contents filters generated settings, duplicate notifications and temp files.
        dirty_ = true;
        changed_at_ = now;
        if ((!ok && code != ERROR_NOTIFY_ENUM_DIR) || !arm()) {
            error_ = "File watcher interrupted. Retrying.";
            stop();
            return false;
        }
    }
    if (!dirty_ || (DWORD)(now - changed_at_) < debounce_) return false;
    Sources current;
    std::string error;
    if (!snapshot(current, error)) { error_ = error; changed_at_ = now; return false; }
    dirty_ = false;
    error_.clear();
    bool changed = baseline_ && current != previous_;
    previous_ = std::move(current);
    baseline_ = true;
    return changed;
}
