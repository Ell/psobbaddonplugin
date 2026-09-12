#include "lua_hooks.h"
#include "luastate.h"
#include "lua_psolib.h"
#include "reload_runtime.h"
#include "luajit/lua.hpp"
#include <cstring>

bool psolua_callbacks_enabled = false;
namespace {
// A C error handler remains usable when addons replace/break Lua's debug, pso,
// logging, or error-handler globals. Preserve the stack across every callback.
int traceback(lua_State* L) {
    const char* message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "Lua error (non-string value)", 1);
    return 1;
}
bool invoke(const char* name, bool required, const char* text = nullptr, const int* key = nullptr) {
    if (!g_LuaState) return false;
    lua_State* L = g_LuaState;
    int top = lua_gettop(L);
    lua_pushcfunction(L, traceback);
    int handler = lua_gettop(L);
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        if (required) {
            reload_report_error(std::string("Missing Lua callback: ") + name);
            psolua_callbacks_enabled = false;
        }
        return !required;
    }
    int args = 0;
    if (text) { lua_pushstring(L, text); args = 1; }
    if (key) { lua_pushinteger(L, *key); args = 1; }
    int status = lua_pcall(L, args, 0, handler);
    if (status != 0) {
        const char* error = lua_tostring(L, -1);
        reload_report_error(std::string(name) + ": " + (error ? error : "Lua callback failed"));
        psolua_callbacks_enabled = false;
    }
    lua_settop(L, top);
    return status == 0;
}
}
bool psoluah_Init() { return invoke("pso_on_init", true); }
void psoluah_Present() { if (psolua_callbacks_enabled) invoke("pso_on_present", true); }
void psoluah_KeyPressed(int key) { if (psolua_callbacks_enabled) invoke("pso_on_key_pressed", true, nullptr, &key); }
void psoluah_KeyReleased(int key) { if (psolua_callbacks_enabled) invoke("pso_on_key_released", true, nullptr, &key); }
void psoluah_Log(std::string text) {
    static bool active = false;
    if (!psolua_callbacks_enabled || active) return;
    active = true;
    invoke("pso_on_log", false, text.c_str());
    active = false;
}
void psoluah_UnhandledError(std::string text) {
    static bool active = false;
    if (!psolua_callbacks_enabled || active) return;
    active = true;
    invoke("pso_on_unhandled_error", false, text.c_str());
    active = false;
}
