#pragma once

#include "crypto_cli_types.h"
#include <stddef.h>

// Parse hex string to bytes
// Supports formats: "0xAABB", "AABB", "AA BB" (with spaces)
// Returns true on success, false if invalid hex
bool crypto_parse_hex(
    const char* input,
    uint8_t* output,
    size_t max_len,
    size_t* output_len);

// Parse integer argument
// Supports decimal and hex (0x prefix)
bool crypto_parse_int(const char* input, int* output);

// Check if flag is present
bool crypto_has_flag(int argc, const char** argv, const char* flag);

// Get flag value: --slot 12 → returns "12"
// Supports both "--slot 12" and "--slot=12" formats
const char* crypto_get_flag_value(int argc, const char** argv, const char* flag);

// Validate slot number (0-15)
bool crypto_validate_slot(int slot);

// Validate file path (must start with /ext/)
bool crypto_validate_sd_path(const char* path);
