#pragma once

#include "sol.hpp"

// Called before Direct3D8 Present function is called.
void psoluah_Present(void);
// Calls all the init callbacks and stores their return values into the addons list.
bool psoluah_Init(void);
extern bool psolua_callbacks_enabled;

void psoluah_KeyPressed(int key_code);

void psoluah_KeyReleased(int key_code);

void psoluah_Log(std::string text);

void psoluah_UnhandledError(std::string msg);
