#include "crypto_cli_formatter.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>

void output_buffer_init(OutputBuffer* buf, char* storage, size_t capacity) {
    if(!buf || !storage) {
        return;
    }

    buf->buffer = storage;
    buf->capacity = capacity;
    buf->length = 0;

    if(capacity > 0) {
        buf->buffer[0] = '\0';
    }
}

void output_buffer_printf(OutputBuffer* buf, const char* fmt, ...) {
    if(!buf || !buf->buffer || !fmt) {
        return;
    }

    if(buf->length >= buf->capacity - 1) {
        return; // Buffer full
    }

    va_list args;
    va_start(args, fmt);

    size_t remaining = buf->capacity - buf->length;
    int written = vsnprintf(buf->buffer + buf->length, remaining, fmt, args);

    va_end(args);

    if(written > 0) {
        buf->length += (size_t)written;
        if(buf->length >= buf->capacity) {
            buf->length = buf->capacity - 1;
        }
    }
}

void output_buffer_append(OutputBuffer* buf, const char* str) {
    if(!buf || !buf->buffer || !str) {
        return;
    }

    output_buffer_printf(buf, "%s", str);
}

void output_buffer_append_hex(OutputBuffer* buf, const uint8_t* data, size_t len) {
    if(!buf || !buf->buffer || !data) {
        return;
    }

    for(size_t i = 0; i < len; i++) {
        output_buffer_printf(buf, "%02X", data[i]);
    }
}

// Error code to string mapping
static const char* exit_code_to_string(CryptoExitCode code) {
    switch(code) {
    case CRYPTO_EXIT_SUCCESS:
        return "Success";
    case CRYPTO_EXIT_BAD_ARGS:
        return "Invalid arguments";
    case CRYPTO_EXIT_DEVICE_NOT_FOUND:
        return "Device not found";
    case CRYPTO_EXIT_ACCESS_DENIED:
        return "Access denied";
    case CRYPTO_EXIT_COMMS_ERROR:
        return "Communication error";
    case CRYPTO_EXIT_TIMEOUT:
        return "Timeout";
    case CRYPTO_EXIT_CHIP_ERROR:
        return "Chip error";
    case CRYPTO_EXIT_INTERNAL_ERROR:
        return "Internal error";
    default:
        return "Unknown error";
    }
}

// Get hint message for error code
static const char* exit_code_to_hint(CryptoExitCode code) {
    switch(code) {
    case CRYPTO_EXIT_BAD_ARGS:
        return "Check command syntax with 'crypto help'";
    case CRYPTO_EXIT_DEVICE_NOT_FOUND:
        return "Check I2C connections and run 'i2c scan'";
    case CRYPTO_EXIT_ACCESS_DENIED:
        return "Slot may be locked or access denied by chip config";
    case CRYPTO_EXIT_COMMS_ERROR:
        return "Check I2C bus and connections";
    case CRYPTO_EXIT_TIMEOUT:
        return "Device may be sleeping, try 'crypto session open'";
    default:
        return NULL;
    }
}

void format_text_success(
    OutputBuffer* buf,
    const char* command,
    const CryptoCommandResult* result) {
    if(!buf || !command || !result) {
        return;
    }

    if(result->message) {
        output_buffer_append(buf, result->message);
        output_buffer_append(buf, "\n");
    }

    // Command-specific data formatting is handled by the command itself
}

void format_text_error(
    OutputBuffer* buf,
    const char* command,
    CryptoExitCode exit_code,
    const char* message) {
    (void)command; // Unused for now

    if(!buf) {
        return;
    }

    output_buffer_append(buf, "Error: ");

    if(message) {
        output_buffer_append(buf, message);
    } else {
        output_buffer_append(buf, exit_code_to_string(exit_code));
    }

    output_buffer_append(buf, "\n");

    const char* hint = exit_code_to_hint(exit_code);
    if(hint) {
        output_buffer_append(buf, "Hint: ");
        output_buffer_append(buf, hint);
        output_buffer_append(buf, "\n");
    }
}

void format_json_success(
    OutputBuffer* buf,
    const char* command,
    const CryptoCommandResult* result) {
    (void)result; // Commands append their own data

    if(!buf || !command) {
        return;
    }

    output_buffer_printf(buf, "{\"cmd\":\"%s\",\"ok\":true", command);

    // Command-specific data is appended by the command itself
    // Commands should call output_buffer_printf to add their fields

    output_buffer_append(buf, "}\n");
}

void format_json_error(
    OutputBuffer* buf,
    const char* command,
    CryptoExitCode exit_code,
    const char* message,
    uint32_t chip_status) {
    if(!buf || !command) {
        return;
    }

    output_buffer_printf(buf, "{\"cmd\":\"%s\",\"ok\":false", command);
    output_buffer_printf(buf, ",\"exit_code\":%d", exit_code);

    if(message) {
        output_buffer_printf(buf, ",\"error\":\"%s\"", message);
    } else {
        output_buffer_printf(buf, ",\"error\":\"%s\"", exit_code_to_string(exit_code));
    }

    if(chip_status != 0) {
        output_buffer_printf(buf, ",\"chip_status\":\"0x%08" PRIX32 "\"", chip_status);
    }

    output_buffer_append(buf, "}\n");
}
