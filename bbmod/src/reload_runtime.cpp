#include "reload_runtime.h"
#include "reload_watch.h"
#include "lua_psolib.h"
#include "luajit/lua.hpp"
#include "imgui/imgui.h"
#include "log.h"
#include <memory>
#include <fstream>

namespace {
std::unique_ptr<ReloadWatch> watcher;
std::string last_error;
std::string watcher_error;
unsigned generation = 0;

ReloadWatch& get_watcher() {
    if (!watcher) {
        DWORD delay = GetPrivateProfileIntW(L"AutoReload", L"DebounceMs", 350, L".\\addons\\autoreload.ini");
        watcher.reset(new ReloadWatch(L"addons", delay < 100 ? 100 : (delay > 5000 ? 5000 : delay)));
        watcher->enabled = GetPrivateProfileIntW(L"AutoReload", L"Enabled", 0, L".\\addons\\autoreload.ini") != 0;
        wchar_t buffer[4096];
        GetPrivateProfileStringW(L"AutoReload", L"Exclude", L"options.lua", buffer, 4096, L".\\addons\\autoreload.ini");
        watcher->excludes.clear();
        std::wstring entries(buffer);
        size_t start = 0;
        while (start < entries.size()) {
            auto end = entries.find(L';', start);
            auto entry = entries.substr(start, end == std::wstring::npos ? end : end - start);
            if (!entry.empty()) watcher->excludes.push_back(entry);
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    return *watcher;
}
void record(const std::string& message) {
    g_log << "[autoreload] " << message << std::endl;
    std::ofstream file("bbmod-autoreload.log", std::ios::app);
    file << "[" << GetTickCount() << "] " << message << std::endl;
}
}
void reload_report_error(const std::string& message) {
    last_error = message.substr(0, 16384);
    record(last_error);
}
void reload_clear_error() { last_error.clear(); }
const std::string& reload_last_error() { return last_error; }
void reload_note_success() { record("Lua reload completed, generation " + std::to_string(++generation)); }
void reload_shutdown() { watcher.reset(); }
void reload_poll() {
    auto& watch = get_watcher();
    if (watch.poll()) {
        record("Source files changed; scheduling Lua reload");
        psolua_initialize_on_next_frame = true;
    }
    if (watch.error() != watcher_error) {
        watcher_error = watch.error();
        if (!watcher_error.empty()) record(watcher_error);
    }
}
bool reload_preflight() {
    ReloadWatch::Sources files;
    std::string error;
    if (!get_watcher().snapshot(files, error)) { reload_report_error(error); return false; }
    lua_State* check = luaL_newstate();
    if (!check) { reload_report_error("Could not allocate Lua state for syntax checking."); return false; }
    bool ok = true;
    for (const auto& file : files) {
        std::string name = "@addons/" + reload_utf8(file.first);
        if (luaL_loadbuffer(check, file.second.data(), file.second.size(), name.c_str()) != 0) {
            const char* message = lua_tostring(check, -1);
            reload_report_error(message ? message : "Lua syntax validation failed.");
            ok = false;
            break;
        }
        lua_pop(check, 1);
    }
    lua_close(check);
    return ok;
}
void reload_draw_status() {
    if (last_error.empty() && watcher_error.empty()) return;
    ImGui::SetNextWindowSize(ImVec2(640, 300), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("Lua development - recovery")) {
        ImGui::TextUnformatted("Save a fix to retry automatically, or press F12.");
        ImGui::TextUnformatted("Syntax errors keep the previous Lua environment running.");
        ImGui::Separator();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(last_error.c_str());
        if (!watcher_error.empty()) ImGui::TextUnformatted(watcher_error.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Retry reload (F12)")) psolua_initialize_on_next_frame = true;
        ImGui::SameLine();
        if (ImGui::Button("Dismiss")) last_error.clear();
    }
    ImGui::End();
}
