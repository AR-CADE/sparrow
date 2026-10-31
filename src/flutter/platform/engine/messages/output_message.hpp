#ifndef OUTPUT_MESSAGE_H
#define OUTPUT_MESSAGE_H

#include <cstdint>
class Output;

// Send all existing outputs to Flutter (called after engine initialization)
void sparrow_send_all_outputs();
void send_output_added(Output *output);
void send_output_removed(uint32_t output_id);
void send_output_changed(Output *output);

#endif
