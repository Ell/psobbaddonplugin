#include "reload_watch.h"
#include "reload_runtime.h"
#include "lua_psolib.h"
#include "lua_hooks.h"
#include "luastate.h"
#include "luajit/lua.hpp"
#include "imgui/imgui.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS: " << message << std::endl;
}
static void write(const char* path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) throw std::runtime_error(std::string("write failed: ") + path);
}
static bool wait_change(ReloadWatch& watch, DWORD timeout = 2000) {
    DWORD start = GetTickCount();
    do { if (watch.poll()) return true; Sleep(10); } while ((DWORD)(GetTickCount() - start) < timeout);
    return false;
}
static void tick() {
    reload_poll();
    if (psolua_initialize_on_next_frame) psolua_initialize_state();
}
static void wait_runtime() {
    DWORD start = GetTickCount();
    do { tick(); Sleep(10); } while ((DWORD)(GetTickCount() - start) < 800);
}
static int integer(const char* name) {
    lua_getglobal(g_LuaState, name);
    int value = (int)lua_tointeger(g_LuaState, -1);
    lua_pop(g_LuaState, 1);
    return value;
}
static std::string bootstrap(int value) {
    return "value=" + std::to_string(value) + R"(
function pso_on_init() end
function pso_on_present() value=value+1 end
function pso_on_key_pressed(k) end
function pso_on_key_released(k) end
function pso_on_log(s) end
function pso_on_unhandled_error(s) end
)";
}
int main(int argc, char** argv) {
    try {
        char repository[MAX_PATH], temp[MAX_PATH], path[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, repository);
        if (argc > 1) strcpy_s(repository, argv[1]);
        GetTempPathA(MAX_PATH, temp);
        sprintf_s(path, "%sbbmod-recovery-%lu-%lu", temp, GetCurrentProcessId(), GetTickCount());
        CreateDirectoryA(path, nullptr);
        SetCurrentDirectoryA(path);
        CreateDirectoryA("addons", nullptr);
        CreateDirectoryA("addons/test", nullptr);
        write("addons/autoreload.ini", "[AutoReload]\nEnabled=1\nDebounceMs=100\nExclude=options.lua\n");
        write("addons/init.lua", bootstrap(1));
        {
            ReloadWatch watch(L"addons", 100);
            check(!watch.poll(), "watcher establishes baseline");
            write("addons/init.lua", bootstrap(2));
            check(wait_change(watch), "ordinary save triggers native notification");
            check(!wait_change(watch, 200), "one save does not cause a reload loop");
            write("addons/init.lua", bootstrap(2));
            check(!wait_change(watch, 300), "identical content does not reload");
            write("addons/test/options.lua", "invalid generated settings");
            check(!wait_change(watch, 300), "generated options excluded");
            write("addons/test/.init.lua.swp", "temporary");
            check(!wait_change(watch, 300), "editor temporary files excluded");
            write("addons/replacement.tmp", bootstrap(3));
            MoveFileExA("addons/replacement.tmp", "addons/init.lua", MOVEFILE_REPLACE_EXISTING);
            check(wait_change(watch), "atomic replace save detected");
            write("addons/test/new.lua", "return {}");
            check(wait_change(watch), "new addon source detected");
            DeleteFileA("addons/test/new.lua");
            check(wait_change(watch), "deleted addon source detected");
            DWORD before, after;
            GetProcessHandleCount(GetCurrentProcess(), &before);
            for (int i=0; i<20; ++i) { ReloadWatch temporary; temporary.poll(); }
            GetProcessHandleCount(GetCurrentProcess(), &after);
            check(after <= before + 1, "watcher cancellation releases handles");
        }
        DeleteFileA("addons/test/options.lua");
        psolua_initialize_state();
        check(psolua_callbacks_enabled && integer("value") == 3, "initial Lua bootstrap succeeds");
        reload_poll();
        write("addons/init.lua", bootstrap(10));
        wait_runtime();
        check(psolua_callbacks_enabled && integer("value") == 10, "native watcher reloads running Lua");
        lua_State* previous = g_LuaState;
        write("addons/init.lua", "function broken(");
        wait_runtime();
        check(g_LuaState == previous && psolua_callbacks_enabled && !reload_last_error().empty(), "syntax error preserves running environment and reports error");
        psoluah_Present();
        check(integer("value") == 11, "previous callbacks still run after rejected syntax");
        write("addons/init.lua", "error('bootstrap failure')");
        wait_runtime();
        check(!psolua_callbacks_enabled && reload_last_error().find("bootstrap failure") != std::string::npos, "bootstrap runtime error is recoverable");
        psoluah_Present();
        psoluah_KeyPressed(1);
        psoluah_Log("still alive");
        write("addons/init.lua", bootstrap(20));
        wait_runtime();
        check(psolua_callbacks_enabled && integer("value") == 20 && reload_last_error().empty(), "saving bootstrap fix recovers automatically");
        write("addons/init.lua", bootstrap(30) + "\nfunction pso_on_present() error('callback failure') end\n");
        wait_runtime();
        psoluah_Present();
        check(!psolua_callbacks_enabled && reload_last_error().find("callback failure") != std::string::npos, "core callback errors suspend callbacks");
        // Native UI must draw with Lua suspended; no graphics device needed.
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800,600);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
        reload_draw_status();
        ImGui::Render();
        check(!reload_last_error().empty(), "native error panel renders without Lua callbacks");
        write("addons/init.lua", bootstrap(40));
        wait_runtime();
        check(psolua_callbacks_enabled && integer("value") == 40, "core callback recovers on save");
        write("addons/init.lua", bootstrap(50) + "\nfunction pso_on_init() error('init failure') end\n");
        wait_runtime();
        check(!psolua_callbacks_enabled && reload_last_error().find("init failure") != std::string::npos, "core initialization callback failure is recoverable");
        write("addons/init.lua", bootstrap(60));
        psolua_push_key_pressed(VK_F12);
        psolua_process_key_events();
        check(psolua_initialize_on_next_frame, "F12 requests reload while Lua disabled");
        psolua_initialize_state();
        check(psolua_callbacks_enabled && integer("value") == 60, "native manual retry restores Lua");
        // Exercise the real shipped Lua dispatcher, including its error isolation.
        CreateDirectoryA("addons/psointernal", nullptr);
        CopyFileA((std::string(repository)+"/addons/init.lua").c_str(), "addons/init.lua", FALSE);
        CopyFileA((std::string(repository)+"/addons/psointernal/init.lua").c_str(), "addons/psointernal/init.lua", FALSE);
        write("addons/test/init.lua", "return {__addon={init=function() return {name='bad', present=function() error('addon callback failure') end} end}}");
        psolua_initialize_state();
        check(psolua_callbacks_enabled, "real addon dispatcher initializes");
        psoluah_Present();
        check(psolua_callbacks_enabled && reload_last_error().find("addon callback failure") != std::string::npos, "ordinary addon error disables addon without disabling core");
        write("addons/test/init.lua", "return {__addon={init=function() return {name='fixed',present=function() end} end}}");
        wait_runtime();
        psoluah_Present();
        check(psolua_callbacks_enabled && reload_last_error().empty(), "ordinary addon recovers on save");
        // Broken core module is distinct from a broken bootstrap file.
        write("addons/psointernal/init.lua", "error('core module failure')");
        wait_runtime();
        check(!psolua_callbacks_enabled && reload_last_error().find("core module failure") != std::string::npos, "broken required core module is recoverable");
        CopyFileA((std::string(repository)+"/addons/psointernal/init.lua").c_str(), "addons/psointernal/init.lua", FALSE);
        wait_runtime();
        check(psolua_callbacks_enabled && reload_last_error().empty(), "saving core module fix restores addons");
        reload_shutdown();
        lua_close(g_LuaState); g_LuaState = nullptr;
        std::cout << "ALL RECOVERY TESTS PASSED. Fixture logs: " << path << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
