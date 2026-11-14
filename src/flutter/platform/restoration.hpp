#ifndef SPARROW_RESTORATION_HPP
#define SPARROW_RESTORATION_HPP

#include <cstdint>
#include <vector>

void sparrow_restoration_init();
void sparrow_restoration_reset();
void sparrow_restoration_flush_to_disk();

// Removes all persisted Flutter/shell state left over from a previous
// compositor process (session_state.bin + HydratedBloc shell_storage).
// Must only be called on cold boot (Core::init), never on an in-process
// soft GPU reset, so that state restoration is scoped to a single process.
void sparrow_restoration_clear_persisted_state();
const std::vector<uint8_t>&sparrow_restoration_get_data();
void sparrow_restoration_set_data(const std::vector<uint8_t> & data);

#endif // SPARROW_RESTORATION_HPP
