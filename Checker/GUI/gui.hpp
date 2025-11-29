#pragma once
int checker_gui_loading(int start_deg);
void gui_set_phase(int idx, const wchar_t* resp);
void gui_mark_start();
bool gui_is_started();
bool gui_should_exit();
void gui_request_exit();
void gui_set_current_server(const wchar_t* name);
void gui_set_server_index(int idx, int total);
void gui_add_server_duration_ms(int ms);
void gui_mark_completed();
