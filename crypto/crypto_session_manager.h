#pragma once

#include "crypto_service.h"
#include <stdbool.h>

/**
 * @file crypto_session_manager.h
 * @brief Shared session management following SOLID principles
 *
 * This module encapsulates the proven session lifecycle pattern from the GUI
 * and provides it as a reusable service for both GUI and CLI.
 *
 * Design Principles Applied:
 * - SRP: Single responsibility - manage session lifecycle around command execution
 * - OCP: Open for extension - new commands just implement the callback
 * - LSP: Both GUI and CLI can use this manager interchangeably
 * - ISP: Clean interface - only what's needed for command execution
 * - DIP: Callers depend on this abstraction, not concrete implementation
 */

/**
 * Command execution callback type
 *
 * Commands implement this callback to execute their specific logic
 * while the session manager handles session lifecycle.
 *
 * @param context User-provided context (command-specific data)
 * @return ATCA_SUCCESS on success, error code otherwise
 */
typedef ATCA_STATUS (*CryptoCommandCallback)(void* context);

/**
 * Execute a command within a managed session
 *
 * This function encapsulates the proven pattern:
 * 1. Initialize session
 * 2. Wake device (with proven 10ms delay from crypto_service.c)
 * 3. Execute command callback
 * 4. Put device to sleep/idle
 * 5. Release resources
 *
 * Pattern proven by GUI (views/crypto_view.c:182-196):
 * - crypto_session_init()
 * - crypto_session_begin() [includes 10ms + device settle time]
 * - <execute command>
 * - crypto_session_end()
 *
 * @param command Callback function to execute
 * @param context Context passed to callback (can be NULL)
 * @param end_state Power state after execution (Idle or Sleep)
 * @return true on success, false if session or command failed
 */
bool crypto_session_execute_command(
    CryptoCommandCallback command,
    void* context,
    CryptoDevicePowerState end_state);

/**
 * Execute a command that may fail transiently with retry logic
 *
 * Same as crypto_session_execute_command but retries up to 3 times
 * if the command returns ATCA_RX_FAIL or ATCA_RX_NO_RESPONSE.
 *
 * @param command Callback function to execute
 * @param context Context passed to callback (can be NULL)
 * @param end_state Power state after execution (Idle or Sleep)
 * @param out_status If not NULL, receives the final ATCA_STATUS
 * @return true on success, false if all retries failed
 */
bool crypto_session_execute_with_retry(
    CryptoCommandCallback command,
    void* context,
    CryptoDevicePowerState end_state,
    ATCA_STATUS* out_status);
