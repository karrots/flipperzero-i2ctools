#pragma once

#include "crypto_cli_types.h"
#include <stddef.h>

// Initialize crypto CLI framework
// Should be called during app initialization
void crypto_cli_init(void);

// Cleanup crypto CLI framework
void crypto_cli_cleanup(void);

// Execute a crypto command (for programmatic use)
// Returns exit code
CryptoExitCode crypto_cli_execute(
    const char* command_line,
    char* output_buffer,
    size_t output_buffer_size);

// Main CLI handler (for Flipper CLI integration if available)
// Registered with cli_registry_add_command()
typedef struct PipeSide PipeSide;
typedef struct FuriString FuriString;
void crypto_cli_handler(PipeSide* pipe, FuriString* args, void* context);
