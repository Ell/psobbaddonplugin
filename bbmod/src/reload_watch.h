#pragma once
#include <Windows.h>
#include <map>
#include <string>
#include <vector>

// All methods run on the render thread. The OS fills an overlapped buffer;
// no worker thread calls Lua, ImGui, or plugin code.
class ReloadWatch {
public:
    using Sources = std::map<std::wstring, std::string>;
    explicit ReloadWatch(std::wstring root = L"addons", DWORD debounce = 350);
    ~ReloadWatch();
    bool poll();
    void stop();
    bool snapshot(Sources& files, std::string& error) const;
    const std::string& error() const { return error_; }
    bool enabled = true;
    std::vector<std::wstring> excludes = { L"options.lua" };
private:
    bool scan(const std::wstring& relative, Sources& files, std::string& error) const;
    bool excluded(const std::wstring& relative) const;
    bool open();
    bool arm();
    std::wstring root_;
    DWORD debounce_, changed_at_ = 0, retry_at_ = 0;
    bool dirty_ = false, pending_ = false, baseline_ = false;
    HANDLE directory_ = INVALID_HANDLE_VALUE;
    OVERLAPPED operation_ = {};
    alignas(DWORD) BYTE buffer_[32768];
    Sources previous_;
    std::string error_;
};

std::string reload_utf8(const std::wstring& text);
