#include "crypto_cmd_session.h"
#include "../../crypto/crypto_session_service.h"
#include "../crypto_cli_parser.h"
#include "../crypto_cli_formatter.h"
#include "../crypto_cli_registry.h"

#include <furi.h>
#include <string.h>
#include <stdio.h>

// Forward declarations
static CryptoCommandResult session_cmd_open(CryptoCommandContext* ctx);
static CryptoCommandResult session_cmd_idle(CryptoCommandContext* ctx);
static CryptoCommandResult session_cmd_sleep(CryptoCommandContext* ctx);
static CryptoCommandResult session_cmd_state(CryptoCommandContext* ctx);

// State name strings for output
static const char* state_names[] = {
    "CLOSED",
    "OPENING",
    "ACTIVE",
    "IDLE",
    "ERROR"
};

/**
 * Session open subcommand: crypto session open [--keep-awake <ms>]
 */
static CryptoCommandResult session_cmd_open(CryptoCommandContext* ctx) {
    // Parse --keep-awake flag
    const char* keep_awake_str = crypto_get_flag_value(ctx->argc, ctx->argv, "--keep-awake");
    uint32_t keep_awake_ms = 0;

    if(keep_awake_str) {
        int keep_awake_val = 0;
        if(!crypto_parse_int(keep_awake_str, &keep_awake_val) || keep_awake_val < 0) {
            return (CryptoCommandResult){
                .success = false,
                .exit_code = CRYPTO_EXIT_BAD_ARGS,
                .message = "Invalid --keep-awake value (must be positive integer)"
            };
        }
        keep_awake_ms = (uint32_t)keep_awake_val;
    }

    // Open session
    CryptoSessionStatus status = crypto_session_open(keep_awake_ms);

    if(status != CRYPTO_SESSION_OK) {
        const char* error_msg;
        CryptoExitCode exit_code;

        switch(status) {
            case CRYPTO_SESSION_ERR_DEVICE_NOT_FOUND:
                error_msg = "Device not found on I2C bus";
                exit_code = CRYPTO_EXIT_DEVICE_NOT_FOUND;
                break;
            case CRYPTO_SESSION_ERR_COMMS_ERROR:
                error_msg = "I2C communication error";
                exit_code = CRYPTO_EXIT_COMMS_ERROR;
                break;
            case CRYPTO_SESSION_ERR_ALREADY_OPEN:
                error_msg = "Session already open";
                exit_code = CRYPTO_EXIT_INTERNAL_ERROR;
                break;
            default:
                error_msg = "Failed to open session";
                exit_code = CRYPTO_EXIT_INTERNAL_ERROR;
                break;
        }

        return (CryptoCommandResult){
            .success = false,
            .exit_code = exit_code,
            .message = error_msg
        };
    }

    // Get device info for output
    const CryptoDeviceInfo* info = crypto_session_get_device_info();

    if(ctx->format == OUTPUT_FORMAT_JSON) {
        // JSON output
        char output[512];
        snprintf(output, sizeof(output),
            "{\"cmd\":\"session_open\",\"ok\":true,\"device\":\"ATECC608B\","
            "\"dev_rev\":\"0x%08lX\",\"serial\":\"%02X%02X%02X%02X%02X%02X%02X%02X%02X\","
            "\"config_locked\":%s,\"data_locked\":%s,\"keep_awake_ms\":%lu}",
            info->dev_rev,
            info->serial_number[0], info->serial_number[1], info->serial_number[2],
            info->serial_number[3], info->serial_number[4], info->serial_number[5],
            info->serial_number[6], info->serial_number[7], info->serial_number[8],
            info->config_locked ? "true" : "false",
            info->data_locked ? "true" : "false",
            keep_awake_ms);

        printf("%s\n", output);
    } else {
        // Text output
        printf("Session opened\n");
        printf("Device: ATECC608B (DevRev: 0x%08lX)\n", info->dev_rev);
        printf("Serial: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            info->serial_number[0], info->serial_number[1], info->serial_number[2],
            info->serial_number[3], info->serial_number[4], info->serial_number[5],
            info->serial_number[6], info->serial_number[7], info->serial_number[8]);
        printf("Config locked: %s\n", info->config_locked ? "yes" : "no");
        printf("Data locked: %s\n", info->data_locked ? "yes" : "no");
        if(keep_awake_ms > 0) {
            printf("Keep-alive: %lu ms\n", keep_awake_ms);
        }
    }

    return (CryptoCommandResult){
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = NULL
    };
}

/**
 * Session idle subcommand: crypto session idle
 */
static CryptoCommandResult session_cmd_idle(CryptoCommandContext* ctx) {
    UNUSED(ctx);

    CryptoSessionStatus status = crypto_session_idle();

    if(status != CRYPTO_SESSION_OK) {
        const char* error_msg = (status == CRYPTO_SESSION_ERR_NOT_OPEN) ?
            "Session not open" : "Failed to idle session";

        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_INTERNAL_ERROR,
            .message = error_msg
        };
    }

    if(ctx->format == OUTPUT_FORMAT_JSON) {
        printf("{\"cmd\":\"session_idle\",\"ok\":true}\n");
    } else {
        printf("Session transitioned to idle\n");
    }

    return (CryptoCommandResult){
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = NULL
    };
}

/**
 * Session sleep subcommand: crypto session sleep
 */
static CryptoCommandResult session_cmd_sleep(CryptoCommandContext* ctx) {
    UNUSED(ctx);

    CryptoSessionStatus status = crypto_session_sleep();

    if(status != CRYPTO_SESSION_OK) {
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_INTERNAL_ERROR,
            .message = "Failed to close session"
        };
    }

    if(ctx->format == OUTPUT_FORMAT_JSON) {
        printf("{\"cmd\":\"session_sleep\",\"ok\":true}\n");
    } else {
        printf("Session closed\n");
    }

    return (CryptoCommandResult){
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = NULL
    };
}

/**
 * Session state subcommand: crypto session state
 */
static CryptoCommandResult session_cmd_state(CryptoCommandContext* ctx) {
    CryptoSessionMgr* session = crypto_session_mgr_get();
    CryptoSessionState state = crypto_session_get_state();

    if(ctx->format == OUTPUT_FORMAT_JSON) {
        char output[512];
        int len = snprintf(output, sizeof(output),
            "{\"cmd\":\"session_state\",\"ok\":true,\"state\":\"%s\"",
            state_names[state]);

        // Add device info if session is active or idle
        if(state == CRYPTO_SESSION_STATE_ACTIVE || state == CRYPTO_SESSION_STATE_IDLE) {
            const CryptoDeviceInfo* info = crypto_session_get_device_info();
            if(info) {
                len += snprintf(output + len, sizeof(output) - len,
                    ",\"i2c_addr\":\"0x%02X\",\"keep_awake_ms\":%lu",
                    info->i2c_address,
                    session->keep_awake_ms);

                // Calculate time remaining if keep-alive is active
                if(session->keep_awake_ms > 0) {
                    uint32_t elapsed = furi_get_tick() - session->last_activity_ms;
                    uint32_t remaining = (elapsed < session->keep_awake_ms) ?
                        (session->keep_awake_ms - elapsed) : 0;
                    len += snprintf(output + len, sizeof(output) - len,
                        ",\"time_remaining_ms\":%lu", remaining);
                }
            }
        }

        snprintf(output + len, sizeof(output) - len, "}");
        printf("%s\n", output);
    } else {
        printf("Session state: %s\n", state_names[state]);

        if(state == CRYPTO_SESSION_STATE_ACTIVE || state == CRYPTO_SESSION_STATE_IDLE) {
            const CryptoDeviceInfo* info = crypto_session_get_device_info();
            if(info) {
                printf("I2C address: 0x%02X\n", info->i2c_address);
                if(session->keep_awake_ms > 0) {
                    printf("Keep-alive: %lu ms\n", session->keep_awake_ms);

                    uint32_t elapsed = furi_get_tick() - session->last_activity_ms;
                    uint32_t remaining = (elapsed < session->keep_awake_ms) ?
                        (session->keep_awake_ms - elapsed) : 0;
                    printf("Time remaining: %lu ms\n", remaining);
                } else {
                    printf("Keep-alive: disabled\n");
                }
            }
        }
    }

    return (CryptoCommandResult){
        .success = true,
        .exit_code = CRYPTO_EXIT_SUCCESS,
        .message = NULL
    };
}

/**
 * Main session command router
 */
static CryptoCommandResult session_execute(CryptoCommandContext* ctx) {
    if(ctx->argc == 0) {
        const char* usage =
            "Usage: atecc session <subcommand> [options]\n"
            "Subcommands:\n"
            "  open [--keep-awake <ms>]  Open session and wake device\n"
            "  idle                      Put device to idle mode\n"
            "  sleep                     Close session and sleep device\n"
            "  state                     Display current session state";

        printf("%s\n", usage);
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_BAD_ARGS,
            .message = NULL
        };
    }

    const char* subcommand = ctx->argv[0];

    // Create sub-context for subcommand
    CryptoCommandContext sub_ctx = {
        .argc = ctx->argc - 1,
        .argv = (ctx->argc > 1) ? &ctx->argv[1] : NULL,
        .format = ctx->format,
        .command_name = ctx->command_name,
        .user_data = ctx->user_data
    };

    if(strcmp(subcommand, "open") == 0) {
        return session_cmd_open(&sub_ctx);
    } else if(strcmp(subcommand, "idle") == 0) {
        return session_cmd_idle(&sub_ctx);
    } else if(strcmp(subcommand, "sleep") == 0) {
        return session_cmd_sleep(&sub_ctx);
    } else if(strcmp(subcommand, "state") == 0) {
        return session_cmd_state(&sub_ctx);
    } else {
        printf("Unknown subcommand: %s\n", subcommand);
        return (CryptoCommandResult){
            .success = false,
            .exit_code = CRYPTO_EXIT_BAD_ARGS,
            .message = "Invalid subcommand"
        };
    }
}

/**
 * Command definition
 */
const CryptoCommand crypto_command_session = {
    .name = "session",
    .description = "Manage crypto session lifecycle",
    .usage = "atecc session <open|idle|sleep|state> [options]",
    .execute = session_execute,
    .cleanup = NULL
};

/**
 * Register with CLI framework
 */
void crypto_cmd_session_register(void) {
    crypto_registry_register(&crypto_command_session);
}
