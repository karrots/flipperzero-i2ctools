#pragma once

#include "crypto_cli_types.h"
#include <stddef.h>

// Output buffer for formatted text
typedef struct {
    char* buffer;
    size_t capacity;
    size_t length;
} OutputBuffer;

// Initialize output buffer
void output_buffer_init(OutputBuffer* buf, char* storage, size_t capacity);

// Append formatted text (like printf)
void output_buffer_printf(OutputBuffer* buf, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Append raw string
void output_buffer_append(OutputBuffer* buf, const char* str);

// Append hex byte array as string
void output_buffer_append_hex(OutputBuffer* buf, const uint8_t* data, size_t len);

// Format success result as text
void format_text_success(
    OutputBuffer* buf,
    const char* command,
    const CryptoCommandResult* result);

// Format error as text
void format_text_error(
    OutputBuffer* buf,
    const char* command,
    CryptoExitCode exit_code,
    const char* message);

// Format success result as JSON
void format_json_success(
    OutputBuffer* buf,
    const char* command,
    const CryptoCommandResult* result);

// Format error as JSON
void format_json_error(
    OutputBuffer* buf,
    const char* command,
    CryptoExitCode exit_code,
    const char* message,
    uint32_t chip_status); // 0 if N/A
