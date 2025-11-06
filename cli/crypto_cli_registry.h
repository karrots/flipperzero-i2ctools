#pragma once

#include "crypto_cli_types.h"
#include "crypto_cli_formatter.h"

// Initialize command registry
void crypto_registry_init(void);

// Register a command (called by each command module)
void crypto_registry_register(const CryptoCommand* cmd);

// Find command by name
const CryptoCommand* crypto_registry_find(const char* name);

// List all commands (for help text)
void crypto_registry_list(OutputBuffer* buf);

// Get command count
size_t crypto_registry_count(void);

// Cleanup registry
void crypto_registry_cleanup(void);
