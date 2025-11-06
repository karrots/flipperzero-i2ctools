#include "crypto_cli_registry.h"
#include <string.h>
#include <stddef.h>

#define MAX_COMMANDS 32

// Static array of command pointers (simple, no dynamic allocation)
static const CryptoCommand* g_commands[MAX_COMMANDS];
static size_t g_command_count = 0;

void crypto_registry_init(void) {
    g_command_count = 0;
    memset(g_commands, 0, sizeof(g_commands));
}

void crypto_registry_register(const CryptoCommand* cmd) {
    if(!cmd || !cmd->name) {
        return;
    }

    if(g_command_count >= MAX_COMMANDS) {
        return; // Registry full
    }

    // Check for duplicate names
    for(size_t i = 0; i < g_command_count; i++) {
        if(strcmp(g_commands[i]->name, cmd->name) == 0) {
            return; // Already registered
        }
    }

    g_commands[g_command_count++] = cmd;
}

const CryptoCommand* crypto_registry_find(const char* name) {
    if(!name) {
        return NULL;
    }

    for(size_t i = 0; i < g_command_count; i++) {
        if(strcmp(g_commands[i]->name, name) == 0) {
            return g_commands[i];
        }
    }

    return NULL;
}

void crypto_registry_list(OutputBuffer* buf) {
    if(!buf) {
        return;
    }

    if(g_command_count == 0) {
        output_buffer_append(buf, "No commands registered.\n");
        return;
    }

    output_buffer_append(buf, "Available commands:\n");

    for(size_t i = 0; i < g_command_count; i++) {
        const CryptoCommand* cmd = g_commands[i];
        output_buffer_printf(buf, "  %-12s %s\n", cmd->name, cmd->description);
    }

    output_buffer_append(buf, "\nUse 'atecc <command> --help' for more information.\n");
}

size_t crypto_registry_count(void) {
    return g_command_count;
}

void crypto_registry_cleanup(void) {
    g_command_count = 0;
    memset(g_commands, 0, sizeof(g_commands));
}
