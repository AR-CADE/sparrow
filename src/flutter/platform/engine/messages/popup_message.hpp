#ifndef POPUP_MESSAGE_H
#define POPUP_MESSAGE_H

#include <cstdint>
class Core;
class SparrowPopup;
void send_popup_map(SparrowPopup *popup);
void send_popup_unmap(uint32_t handle);

#endif
