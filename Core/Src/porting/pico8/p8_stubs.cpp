/*
 * Stubs for desktop-only features on embedded (P8_EMBEDDED)
 * Replaces p8_shell.cpp and p8_script.c
 */
#include "p8_core.h"
#include "p8_shell.h"
#include "p8_script.h"
#include "sys_api.h"

// Shell stubs
void p8_shell_init(void) {}
void p8_shell_enter(const char*) {}
void p8_shell_frame(void) {}
void p8_shell_cmd_finished(int) {}
void p8_shell_get_screen_size(int* w, int* h) { if(w)*w=128; if(h)*h=128; }
uint8_t* p8_shell_get_screen(void) { return nullptr; }
const uint32_t* p8_shell_get_palette(void) { return nullptr; }
void p8_shell_debug_update_hook(void) {}
bool p8_si_return_try_pop(int, int*) { return false; }

// Script stubs
bool p8_script_is_loaded(void) { return false; }
bool p8_script_is_recording(void) { return false; }
uint32_t p8_script_get_buttons(uint32_t) { return 0; }
void p8_script_record_frame(uint32_t, uint32_t, uint32_t) {}
bool p8_script_has_screenshot(uint32_t, char*, int) { return false; }
void p8_script_shutdown(void) {}

// Desktop-only sys_api stubs
#ifdef P8_EMBEDDED
void sys_audio_pause(bool) {}
void sys_capture_screenshot(const char*) {}
bool sys_get_key_state(int) { return false; }
bool sys_text_input_available(void) { return false; }
int sys_text_input_pop(void) { return 0; }
void sys_create_shell_window(void) {}
void sys_destroy_shell_window(void) {}
void sys_draw_shell_frame(const uint8_t*, int, int, const uint32_t*) {}
void sys_set_shell_window_size(int, int) {}
void sys_start_audio_dump(const char*) {}
void sys_stop_audio_dump(void) {}
#endif
