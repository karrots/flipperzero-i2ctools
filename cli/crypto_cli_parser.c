#include "crypto_cli_parser.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// Helper: Check if character is valid hex
static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

// Helper: Convert hex character to value
static uint8_t hex_char_to_value(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

bool crypto_parse_hex(
    const char* input,
    uint8_t* output,
    size_t max_len,
    size_t* output_len) {
    if(!input || !output || !output_len) {
        return false;
    }

    *output_len = 0;

    // Skip "0x" or "0X" prefix
    const char* ptr = input;
    if(ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
        ptr += 2;
    }

    // Count hex characters (skip spaces)
    size_t hex_count = 0;
    for(const char* p = ptr; *p; p++) {
        if(*p == ' ' || *p == '\t') {
            continue; // Skip whitespace
        }
        if(!is_hex_char(*p)) {
            return false; // Invalid character
        }
        hex_count++;
    }

    // Must have even number of hex characters
    if(hex_count == 0 || hex_count % 2 != 0) {
        return false;
    }

    size_t byte_count = hex_count / 2;
    if(byte_count > max_len) {
        return false; // Output buffer too small
    }

    // Parse hex pairs
    size_t out_idx = 0;
    uint8_t high_nibble = 0;
    bool have_high = false;

    for(const char* p = ptr; *p; p++) {
        if(*p == ' ' || *p == '\t') {
            continue; // Skip whitespace
        }

        uint8_t nibble = hex_char_to_value(*p);

        if(!have_high) {
            high_nibble = nibble;
            have_high = true;
        } else {
            output[out_idx++] = (high_nibble << 4) | nibble;
            have_high = false;
        }
    }

    *output_len = out_idx;
    return true;
}

bool crypto_parse_int(const char* input, int* output) {
    if(!input || !output) {
        return false;
    }

    char* endptr = NULL;
    long val;

    // Check for hex prefix
    if(input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        val = strtol(input, &endptr, 16);
    } else {
        val = strtol(input, &endptr, 10);
    }

    // Check if conversion was successful
    if(endptr == input || *endptr != '\0') {
        return false;
    }

    *output = (int)val;
    return true;
}

bool crypto_has_flag(int argc, const char** argv, const char* flag) {
    if(!argv || !flag) {
        return false;
    }

    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], flag) == 0) {
            return true;
        }
    }

    return false;
}

const char* crypto_get_flag_value(int argc, const char** argv, const char* flag) {
    if(!argv || !flag) {
        return NULL;
    }

    size_t flag_len = strlen(flag);

    for(int i = 0; i < argc; i++) {
        // Check for "--flag=value" format
        if(strncmp(argv[i], flag, flag_len) == 0 && argv[i][flag_len] == '=') {
            return &argv[i][flag_len + 1];
        }

        // Check for "--flag value" format
        if(strcmp(argv[i], flag) == 0) {
            if(i + 1 < argc) {
                return argv[i + 1];
            }
        }
    }

    return NULL;
}

bool crypto_validate_slot(int slot) {
    return slot >= 0 && slot <= 15;
}

bool crypto_validate_sd_path(const char* path) {
    if(!path) {
        return false;
    }

    // Must start with /ext/
    return strncmp(path, "/ext/", 5) == 0;
}
