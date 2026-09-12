# BB UI addons plugin

A plugin for Phantasy Star Online Blue Burst that enables graphical UI addons,
which can read the game's memory and present additional useful information.

- [Wiki](/../../wiki)
- [Releases](/../../releases)

## Installation

1. Download the latest release from the [Releases](/../../releases) page
and extract the zip contents to your PSOBB directory.
2. Install the [Visual C++ Redistributable for Visual Studio 2015](https://www.microsoft.com/en-us/download/details.aspx?id=48145).
3. Run the game.

## Building and Testing

To test the lua code, install luacheck and busted from luarocks and

    $ luacheck addons
    $ busted

To build the plugin dll, use Visual Studio 2015.

## Usage

Press the \` key to open the Main Menu of the addon.

The UI theme is configured via the `addons/theme.ini` file.

## Changelog

See CHANGELOG.md for more info

**The API is completely unstable for now, expect it to change.**

ImGui is exposed via the `imgui` module, which is in the global environment as `imgui`.

PSO specific functions are in the `pso` global table.

### API Docs

* Lua 5.1: https://www.lua.org/manual/5.1/
* LuaJit Extensions: http://bitop.luajit.org/api.html
* ImGui: https://github.com/ocornut/imgui
* ImGui API (C++ Header): https://github.com/ocornut/imgui/blob/master/imgui.h

`pso` global table:

 * read_u8, u16, u32, u64, i8, i16, i32, i64, f32, f64, -- read mem at address as type (little endian)
 * read_cstr(addr, len) -- read c_str at address with len bytes, or null terminated (0 len)
 * read_wstr(addr, len) -- read utf16 str to utf8 at address with len characters, or double null terminated (0 len)
 * read_mem(table, addr, len) -- read len bytes from addr into table. the table should be initialized as empty, i.e. `local table = {}; pso.read_mem(table, 0x00400000, 0x7fffffff-0x00400000)` (don't read the entire address space, that's silly and will probably kill the process)
 * read_mem_str(addr, len) -- read len bytes from addr into a string (not null terminated). lua treats binary files the same way it does text files so this can be used to efficiently write sections of memory directly to a file.
 * reload() -- at the end of present, re-initialize the lua state. all addons and modules will be reloaded, no state will be preserved.
 * base_address -- the base address of the PSOBB process
 * reload_custom_theme -- after the current frame, the custom theme is applied to handle changes.
 * get_tick_count -- returns the number of ticks for a timer.
 * change_global_font -- after the current frame, changes the font at runtime using the specified settings.
 * list_directory_files -- list files in the specified directory under the addons directory.
 * set_language -- used by the Settings Editor to set the internal language for addons.
 * get_language -- retrieves the language value for addons to handle translation.
 * get_version -- returns a table containing fields `version_string`, `major`, `minor`, and `patch`. The latter three are integer values corresponding to the plugin's version number. The `version_string` is a string representation.
 * require_version -- accepts three arguments specifying the version and returns true if the plugin's version is at that level or higher.

## Ell fork: Lua development and automatic reload

This fork adds native file watching and recovery from ordinary Lua errors.
`addons/autoreload.ini` enables development mode in this distribution. Save a
Lua source file to reload all addons after 350 ms without further filesystem
changes. Editor saves using rename/replace, added files, and deleted files are
supported. Identical contents do not cause another reload.

The watcher uses asynchronous `ReadDirectoryChangesW` notifications and survives
Lua failures. It never calls Lua from a background thread. Reloads run at the
existing frame boundary. Syntax is checked before discarding the current Lua
state; a syntax error keeps the previous code running. Bootstrap or core callback
runtime errors suspend Lua callbacks, show a native error panel, and leave the
game running. Ordinary addon callback failures retain the existing per-addon
isolation. Save a fix to retry automatically, use the panel's Retry button, or
press **F12**, which works even when Lua is unavailable. The backtick menu's
Reload button and `pso.reload()` still work.

Settings in `addons/autoreload.ini` are read when the DLL starts:

- `Enabled=1`: watch for saves; `0` retains manual reload and error recovery.
- `DebounceMs=350`: quiet period, clamped to 100–5000 ms.
- `Exclude=options.lua`: semicolon-separated basenames or paths relative to
  `addons`, case-insensitive, with either slash style. No wildcard expansion.
  Add any other generated `.lua` settings here to avoid reload loops.

Only `.lua` source files are considered. Hidden/editor temporary files, `fonts`,
`customdlls`, and directory reparse points are skipped. Excluded files are also
excluded from the syntax preflight; if loaded by an addon, their Lua errors are
handled during initialization. Files are scanned on notifications, not on every
frame. A watcher failure retries opening the directory once a second and shows
its status in the native panel. Changes are applied when the game renders again
if rendering pauses while unfocused.

Successful reloads reset Lua state; runtime initialization failures do not roll
back the previous environment or undo addon side effects. Recovery covers Lua
syntax and runtime errors, including broken core scripts. Native crashes,
infinite loops, and live DLL replacement are outside this feature's scope.
Diagnostics are appended to `bbmod-autoreload.log` in the game directory.

### Building and testing this fork

Use Visual Studio 2022's C++ desktop tools and a Windows SDK:

```bat
msbuild bbmod.sln /m /p:Configuration=Release /p:Platform=Win32
rem From an x86 Native Tools command prompt:
tests\run.cmd
```

The GitHub Actions Windows build runs the native watcher/recovery tests and
uploads `bbmod-native-autoreload`, containing the DLL, symbols, addon files, and
the test executable. The tests use a temporary fixture directory and exercise
real filesystem notifications, atomic saves, exclusions, callback failures,
broken bootstrap/core scripts, native retry, and recovery after another save.

Install by closing the game, backing up `dinput8.dll`, and copying the artifact's
DLL and addon files into the PSOBB directory. Preserve existing custom addons and
settings. On Wine/Proton retain the `dinput8=n,b` override. Building and replacing
the native DLL requires one game restart; subsequent Lua saves do not.
