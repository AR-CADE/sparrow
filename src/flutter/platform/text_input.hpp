#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <xkbcommon/xkbcommon.h>

class Core;

#define TEXT_INPUT_MAX_LENGTH 8192

struct text_input_state
{
    bool active = false;
    int64_t connection_id = 0;

    char text[TEXT_INPUT_MAX_LENGTH];
    size_t text_length = 0;

    int32_t selection_base   = 0;
    int32_t selection_extent = 0;
    int32_t composing_base   = 0;
    int32_t composing_extent = 0;

    char input_action[64];
    bool multiline = false;
};

void sparrow_text_input_init();
void sparrow_text_input_handle_key(
    xkb_keysym_t keysym,
    uint32_t unicode,
    bool pressed);
#endif
