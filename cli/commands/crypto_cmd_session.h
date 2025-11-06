#pragma once

#include "../crypto_cli_types.h"

/**
 * @file crypto_cmd_session.h
 * @brief Session management CLI command
 *
 * Implements the "crypto session" command with subcommands:
 * - open: Wake device and create session
 * - idle: Put device to idle mode
 * - sleep: Close session and put device to sleep
 * - state: Display current session state
 */

/**
 * Session command implementation
 */
extern const CryptoCommand crypto_command_session;

/**
 * Register session command with CLI framework
 */
void crypto_cmd_session_register(void);
