#pragma once

#include <cstdint>
#include <string>
#include <optional>

/**
 * Initialize the flutter/isolate platform channel for IsolateNameServer support.
 */
void sparrow_isolate_channel_init();

/**
 * Look up a registered SendPort ID by name from C++.
 */
std::optional<int64_t> sparrow_isolate_lookup_port(const std::string& name);

/**
 * Register a SendPort ID by name from C++.
 */
bool sparrow_isolate_register_port(const std::string& name, int64_t port_id);

/**
 * Remove a registered SendPort name mapping from C++.
 */
bool sparrow_isolate_remove_port(const std::string& name);
