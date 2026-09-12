#include "DebugViewBuffer.h"

#include "log.h"

// OutputDebugString uses a Windows debug-print exception even without an
// attached debugger. Keep that diagnostic exception inside the logger instead
// of allowing it to reach LuaJIT's exception machinery during error recovery.
static void debug_output(const char* text)
{
    __try { OutputDebugStringA(text); }
    __except (GetExceptionCode() == DBG_PRINTEXCEPTION_C ?
              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
}

DebugViewBuffer::~DebugViewBuffer()
{
    sync(); // can be avoided
}

int DebugViewBuffer::sync()
{
    debug_output(str().c_str());
    g_lualog.AddLog("%s", str().c_str());
    str("");

    return 0;
}
