#pragma once
#include <string>

void reload_poll();
void reload_shutdown();
void reload_draw_status();
void reload_report_error(const std::string& message);
void reload_clear_error();
const std::string& reload_last_error();
bool reload_preflight();
void reload_note_success();
