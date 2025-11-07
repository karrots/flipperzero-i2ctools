#include "crypto_cmd_mac.h"
#include "../../crypto/crypto_mac_service.h"
#include "../crypto_cli_parser.h"
#include "../crypto_cli_formatter.h"
#include "../crypto_cli_registry.h"

#include <furi.h>
#include <string.h>
#include <stdio.h>

// Forward declarations
static CryptoCommandResult mac_execute(CryptoCommandContext* ctx);
static CryptoCommandResult format_mac_success(
    const CryptoMacResult* result,
    const CryptoMacParams* params,
    OutputFormat format);
static CryptoCommandResult format_mac_error(
    CryptoMacStatus status,
    const CryptoMacResult* result,
    OutputFormat format);

/**
 * MAC command implementation
 */
const CryptoCommand crypto_command_mac = {
    .name = "mac",
    .description = "Compute MAC/HMAC authentication tag",
    .usage = "crypto mac --slot <N> [--msg <hex> | --use-tempkey] [--mode hmac] [--json]",
    .execute = mac_execute,
    .cleanup = NULL
};

/**
 * Execute MAC command
 */
static CryptoCommandResult mac_execute(CryptoCommandContext* ctx) {
    // Parse arguments
    const char* slot_str = crypto_get_flag_value(ctx->argc, ctx->argv, "--slot");
    const char* msg_str = crypto_get_flag_value(ctx->argc, ctx->argv, "--msg");
    const char* mode_str = crypto_get_flag_value(ctx->argc, ctx->argv, "--mode");
    bool use_tempkey = crypto_has_flag(ctx->argc, ctx->argv, "--use-tempkey");

    // Validate required arguments
    if(!slot_str) {
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_BAD_ARGS,
            .message = "Missing required argument: --slot"
        };
    }

    if(!msg_str && !use_tempkey) {
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_BAD_ARGS,
            .message = "Missing required argument: --msg or --use-tempkey"
        };
    }

    // Parse slot number
    int slot;
    if(!crypto_parse_int(slot_str, &slot) || !crypto_validate_slot(slot)) {
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_BAD_ARGS,
            .message = "Invalid slot number (must be 0-15)"
        };
    }

    // Parse message (if provided)
    uint8_t message[256];
    size_t message_len = 0;

    if(msg_str && !use_tempkey) {
        if(!crypto_parse_hex(msg_str, message, sizeof(message), &message_len)) {
            return (CryptoCommandResult){
                .success = false,
                .exit_code = CRYPTO_EXIT_BAD_ARGS,
                .message = "Invalid hex string in --msg"
            };
        }
    }

    // Parse mode (default to HMAC)
    CryptoMacMode mode = CRYPTO_MAC_MODE_HMAC;
    if(mode_str) {
        if(strcmp(mode_str, "hmac") == 0) {
            mode = CRYPTO_MAC_MODE_HMAC;
        } else if(strcmp(mode_str, "mac") == 0) {
            return (CryptoCommandResult){
                .success = false,
                .exit_code = CRYPTO_EXIT_BAD_ARGS,
                .message = "Legacy MAC mode not yet supported"
            };
        } else {
            return (CryptoCommandResult){
                .success = false,
                .exit_code = CRYPTO_EXIT_BAD_ARGS,
                .message = "Invalid mode (use 'hmac')"
            };
        }
    }

    // Build MAC parameters
    CryptoMacParams params = {
        .slot = (uint8_t)slot,
        .mode = mode,
        .use_tempkey = use_tempkey,
        .message = message,
        .message_len = message_len
    };

    // Compute MAC
    CryptoMacResult result;
    CryptoMacStatus status = crypto_mac_compute(&params, &result);

    // Format output
    if(status != CRYPTO_MAC_OK) {
        return format_mac_error(status, &result, ctx->format);
    }

    return format_mac_success(&result, &params, ctx->format);
}

/**
 * Format successful MAC result
 */
static CryptoCommandResult format_mac_success(
    const CryptoMacResult* result,
    const CryptoMacParams* params,
    OutputFormat format) {

    char output_buf[1024];
    OutputBuffer buf;
    output_buffer_init(&buf, output_buf, sizeof(output_buf));

    if(format == OUTPUT_FORMAT_JSON) {
        // JSON output
        output_buffer_printf(&buf, "{\"cmd\":\"mac\",\"ok\":true");
        output_buffer_printf(&buf, ",\"slot\":%d", params->slot);

        // Format tag as hex string
        output_buffer_printf(&buf, ",\"tag\":\"");
        for(size_t i = 0; i < 32; i++) {
            output_buffer_printf(&buf, "%02X", result->tag[i]);
        }
        output_buffer_printf(&buf, "\"");

        output_buffer_printf(&buf, ",\"elapsed_ms\":%lu", result->elapsed_ms);
        output_buffer_printf(&buf, "}");
    } else {
        // Text output
        output_buffer_printf(&buf, "MAC computed successfully.\n");
        output_buffer_printf(&buf, "Slot: %d\n", params->slot);
        output_buffer_printf(&buf, "Tag: ");
        for(size_t i = 0; i < 32; i++) {
            output_buffer_printf(&buf, "%02X ", result->tag[i]);
        }
        output_buffer_printf(&buf, "\n");
    }

    printf("%s", output_buf);

    return (CryptoCommandResult){
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = NULL
    };
}

/**
 * Format MAC error result
 */
static CryptoCommandResult format_mac_error(
    CryptoMacStatus status,
    const CryptoMacResult* result,
    OutputFormat format) {

    const char* error_msg;
    CryptoExitCode exit_code;

    switch(status) {
        case CRYPTO_MAC_ERR_BAD_SLOT:
            error_msg = "Invalid slot number";
            exit_code = CRYPTO_EXIT_BAD_ARGS;
            break;

        case CRYPTO_MAC_ERR_ACCESS_DENIED:
            error_msg = "Slot access denied or not configured for HMAC";
            exit_code = CRYPTO_EXIT_ACCESS_DENIED;
            break;

        case CRYPTO_MAC_ERR_COMMS_ERROR:
            error_msg = "Communication error with device\nHint: Check I2C connections and run 'crypto session open'";
            exit_code = CRYPTO_EXIT_COMMS_ERROR;
            break;

        case CRYPTO_MAC_ERR_TEMPKEY_NOT_LOADED:
            error_msg = "TempKey not loaded\nHint: Run 'crypto nonce' command first to load TempKey";
            exit_code = CRYPTO_EXIT_INTERNAL_ERROR;
            break;

        case CRYPTO_MAC_ERR_INTERNAL:
        default:
            error_msg = "Internal error during MAC computation";
            exit_code = CRYPTO_EXIT_INTERNAL_ERROR;
            break;
    }

    // Format error output
    char output_buf[512];
    OutputBuffer buf;
    output_buffer_init(&buf, output_buf, sizeof(output_buf));

    if(format == OUTPUT_FORMAT_JSON) {
        format_json_error(&buf, "mac", exit_code, error_msg, result->chip_status);
    } else {
        format_text_error(&buf, "mac", exit_code, error_msg);
        if(result->chip_status != 0) {
            output_buffer_printf(&buf, "Chip status: 0x%02lX\n", result->chip_status);
        }
    }

    printf("%s", output_buf);

    return (CryptoCommandResult){
        .success = false,
        .exit_code = exit_code,
        .message = error_msg
    };
}

/**
 * Register MAC command with CLI framework
 */
void crypto_cmd_mac_register(void) {
    crypto_registry_register(&crypto_command_mac);
}
