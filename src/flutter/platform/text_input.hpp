#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <xkbcommon/xkbcommon.h>

class Core;

void sparrow_text_input_init();
bool sparrow_text_input_is_active();

void sparrow_text_input_handle_key(
    xkb_keysym_t keysym,
    uint32_t unicode,
    bool pressed,
    bool ctrl_active  = false,
    bool shift_active = false);

void sparrow_text_input_start_repeat(
    uint32_t keycode,
    xkb_keysym_t sym,
    uint32_t unicode,
    bool ctrl_active  = false,
    bool shift_active = false,
    int32_t rate  = 25,
    int32_t delay = 300);

void sparrow_text_input_stop_repeat(uint32_t keycode = 0);

#endif
