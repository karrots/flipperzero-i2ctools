#pragma once

#include <stdint.h>
#include <stdbool.h>

// Exit codes (aligned with crypto_cli.md specification)
typedef enum {
    CRYPTO_EXIT_SUCCESS = 0,
    CRYPTO_EXIT_BAD_ARGS = 1,
    CRYPTO_EXIT_DEVICE_NOT_FOUND = 2,
    CRYPTO_EXIT_ACCESS_DENIED = 3,
    CRYPTO_EXIT_COMMS_ERROR = 4,
    CRYPTO_EXIT_TIMEOUT = 5,
    CRYPTO_EXIT_CHIP_ERROR = 6,
    CRYPTO_EXIT_INTERNAL_ERROR = 7
} CryptoExitCode;

// Output format selection
typedef enum {
    OUTPUT_FORMAT_TEXT,
    OUTPUT_FORMAT_JSON
} OutputFormat;

// Command context passed to all commands
typedef struct {
    int argc;
    const char** argv;
    OutputFormat format;
    const char* command_name;
    void* user_data; // For dependency injection
} CryptoCommandContext;

// Command result
typedef struct {
    bool success;
    CryptoExitCode exit_code;
    const char* message;
    void* data; // Command-specific result data
} CryptoCommandResult;

// Command interface (all commands implement this)
typedef struct {
    const char* name;
    const char* description;
    const char* usage; // e.g., "crypto mac --slot <N> --msg <hex>"

    CryptoCommandResult (*execute)(CryptoCommandContext* ctx);
    void (*cleanup)(void* data);
} CryptoCommand;
