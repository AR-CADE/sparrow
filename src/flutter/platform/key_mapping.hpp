#ifndef SPARROW_KEY_MAPPING_HPP
#define SPARROW_KEY_MAPPING_HPP

#include <cstdint>
#include <xkbcommon/xkbcommon.h>

uint64_t sparrow_xkb_to_physical_key(uint32_t xkb_keycode);
uint64_t sparrow_keysym_to_logical_key(xkb_keysym_t sym);

#endif // SPARROW_KEY_MAPPING_HPP
