#include "DebugViewBuffer.h"

#include "log.h"

DebugViewBuffer::~DebugViewBuffer()
{
    sync(); // can be avoided
}

int DebugViewBuffer::sync()
{
    // Debug output uses a first-chance Windows exception. Without a debugger,
    // avoid introducing that exception while recovering from a LuaJIT error.
    // The native ImGui log below and autoreload file log remain available.
    if (IsDebuggerPresent()) OutputDebugStringA(str().c_str());
    g_lualog.AddLog("%s", str().c_str());
    str("");

    return 0;
}
