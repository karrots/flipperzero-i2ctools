#include "crypto_cli.h"
#include "crypto_cli_parser.h"
#include "crypto_cli_formatter.h"
#include "crypto_cli_registry.h"
#include "commands/crypto_cmd_session.h"
#include "../crypto/crypto_session_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <furi.h>

#define MAX_ARGS 32
#define OUTPUT_BUFFER_SIZE 4096

// Helper command for displaying help
static CryptoCommandResult help_command_execute(CryptoCommandContext* ctx);

static const CryptoCommand help_command = {
    .name = "help",
    .description = "Show available commands",
    .usage = "atecc help",
    .execute = help_command_execute,
    .cleanup = NULL,
};

static CryptoCommandResult help_command_execute(CryptoCommandContext* ctx) {
    (void)ctx; // Unused

    char buffer[2048];
    OutputBuffer buf;
    output_buffer_init(&buf, buffer, sizeof(buffer));

    crypto_registry_list(&buf);

    CryptoCommandResult result = {
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = buffer,
        .data = NULL,
    };

    return result;
}

void crypto_cli_init(void) {
    // Initialize session manager (creates I2C mutex for bus contention prevention)
    crypto_session_manager_init();

    crypto_registry_init();

    // Register built-in commands
    crypto_registry_register(&help_command);

    // Register session management command
    crypto_cmd_session_register();
}

void crypto_cli_cleanup(void) {
    crypto_registry_cleanup();

    // Cleanup session manager (destroys I2C mutex)
    crypto_session_manager_deinit();
}

// Parse command line into argc/argv
static int parse_command_line(const char* command_line, const char** argv, int max_args) {
    if(!command_line || !argv) {
        return 0;
    }

    int argc = 0;
    const char* ptr = command_line;

    // Skip leading whitespace
    while(*ptr == ' ' || *ptr == '\t') {
        ptr++;
    }

    while(*ptr && argc < max_args) {
        // Skip whitespace
        while(*ptr == ' ' || *ptr == '\t') {
            ptr++;
        }

        if(*ptr == '\0') {
            break;
        }

        // Mark start of argument
        argv[argc++] = ptr;

        // Find end of argument
        while(*ptr && *ptr != ' ' && *ptr != '\t') {
            ptr++;
        }

        // Null-terminate if not at end
        if(*ptr) {
            // We need to modify the string, so we cast away const
            // This is safe because command_line will be a copy
            char* mutable_ptr = (char*)ptr;
            *mutable_ptr = '\0';
            ptr++;
        }
    }

    return argc;
}

CryptoExitCode crypto_cli_execute(
    const char* command_line,
    char* output_buffer,
    size_t output_buffer_size) {
    if(!command_line || !output_buffer || output_buffer_size == 0) {
        return CRYPTO_EXIT_BAD_ARGS;
    }

    // Initialize output
    output_buffer[0] = '\0';
    OutputBuffer output;
    output_buffer_init(&output, output_buffer, output_buffer_size);

    // Make a mutable copy of command line for parsing
    char* cmd_copy = malloc(strlen(command_line) + 1);
    if(!cmd_copy) {
        return CRYPTO_EXIT_INTERNAL_ERROR;
    }
    strcpy(cmd_copy, command_line);

    // Parse into argc/argv
    const char* argv[MAX_ARGS];
    int argc = parse_command_line(cmd_copy, argv, MAX_ARGS);

    if(argc == 0) {
        format_text_error(&output, "crypto", CRYPTO_EXIT_BAD_ARGS, "No command specified");
        free(cmd_copy);
        return CRYPTO_EXIT_BAD_ARGS;
    }

    // Check for --json flag
    OutputFormat format = OUTPUT_FORMAT_TEXT;
    if(crypto_has_flag(argc, argv, "--json")) {
        format = OUTPUT_FORMAT_JSON;
    }

    // First argument is the subcommand
    const char* subcommand = argv[0];
    const CryptoCommand* cmd = crypto_registry_find(subcommand);

    if(!cmd) {
        if(format == OUTPUT_FORMAT_JSON) {
            format_json_error(&output, subcommand, CRYPTO_EXIT_BAD_ARGS, "Unknown command", 0);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Unknown command: %s", subcommand);
            format_text_error(&output, "crypto", CRYPTO_EXIT_BAD_ARGS, msg);
        }
        free(cmd_copy);
        return CRYPTO_EXIT_BAD_ARGS;
    }

    // Build context
    CryptoCommandContext ctx = {
        .argc = argc - 1,
        .argv = (argc > 1) ? &argv[1] : NULL,
        .format = format,
        .command_name = subcommand,
        .user_data = NULL,
    };

    // Execute command
    CryptoCommandResult result = cmd->execute(&ctx);

    // Format output
    if(result.success) {
        if(format == OUTPUT_FORMAT_JSON) {
            format_json_success(&output, subcommand, &result);
        } else {
            format_text_success(&output, subcommand, &result);
        }
    } else {
        if(format == OUTPUT_FORMAT_JSON) {
            format_json_error(&output, subcommand, result.exit_code, result.message, 0);
        } else {
            format_text_error(&output, subcommand, result.exit_code, result.message);
        }
    }

    // Cleanup
    if(cmd->cleanup && result.data) {
        cmd->cleanup(result.data);
    }

    free(cmd_copy);
    return result.exit_code;
}

// Flipper CLI handler - registered via cli_registry_add_command
void crypto_cli_handler(PipeSide* pipe, FuriString* args, void* context) {
    (void)pipe; // Unused - printf works directly
    (void)context; // Unused

    // Use dynamic allocation to avoid stack overflow
    // CLI threads may have limited stack space (1-2KB typical)
    char* output_buffer = malloc(OUTPUT_BUFFER_SIZE);
    char* command_copy = malloc(256);

    if(!output_buffer || !command_copy) {
        printf("Error: Out of memory\n");
        if(output_buffer) free(output_buffer);
        if(command_copy) free(command_copy);
        return;
    }

    // Extract command line from FuriString and make a mutable copy
    // IMPORTANT: crypto_cli_execute modifies the string during parsing,
    // so we MUST copy it first to avoid modifying const memory
    const char* command_line = furi_string_get_cstr(args);
    strncpy(command_copy, command_line, 255);
    command_copy[255] = '\0';

    // Execute command through our CLI framework
    CryptoExitCode exit_code = crypto_cli_execute(command_copy, output_buffer, OUTPUT_BUFFER_SIZE);

    // Print output to CLI
    printf("%s", output_buffer);

    // Cleanup
    free(output_buffer);
    free(command_copy);

    // Exit code is returned via printf output, not as function return
    // Flipper CLI commands are void, not int-returning
    (void)exit_code;
}
