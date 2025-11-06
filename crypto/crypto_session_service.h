#pragma once

#include "crypto_session.h"

/**
 * @file crypto_session_service.h
 * @brief Session lifecycle management service for ATECC608B
 *
 * Provides functions for opening, closing, and managing crypto device sessions
 * with auto-open and keep-alive functionality.
 */

/**
 * Session operation status codes
 */
typedef enum {
    CRYPTO_SESSION_OK = 0,                   /**< Operation successful */
    CRYPTO_SESSION_ERR_DEVICE_NOT_FOUND = 2, /**< Device not detected on I2C */
    CRYPTO_SESSION_ERR_COMMS_ERROR = 4,      /**< I2C communication error */
    CRYPTO_SESSION_ERR_TIMEOUT = 5,          /**< Operation timeout */
    CRYPTO_SESSION_ERR_ALREADY_OPEN = 7,     /**< Session already open */
    CRYPTO_SESSION_ERR_NOT_OPEN = 8          /**< Session not open */
} CryptoSessionStatus;

/**
 * Open crypto session (wake device, detect, cache info)
 *
 * This function initializes the CryptoAuthLib interface, wakes the device,
 * and caches device information including serial number, revision, and lock status.
 *
 * @param keep_awake_ms Keep-alive duration in milliseconds (0 = no keep-alive)
 * @return CRYPTO_SESSION_OK on success, error code on failure
 */
CryptoSessionStatus crypto_session_open(uint32_t keep_awake_ms);

/**
 * Put device to idle mode (low power, quick resume)
 *
 * Transitions the device to idle state while maintaining session context.
 * Device can be quickly resumed for subsequent operations.
 *
 * @return CRYPTO_SESSION_OK on success, error code on failure
 */
CryptoSessionStatus crypto_session_idle(void);

/**
 * Put device to sleep and close session
 *
 * Sends device to sleep mode and releases all resources.
 * Session must be reopened for future operations.
 *
 * @return CRYPTO_SESSION_OK on success, error code on failure
 */
CryptoSessionStatus crypto_session_sleep(void);

/**
 * Get current session state (no I2C transaction)
 *
 * @return Current session state
 */
CryptoSessionState crypto_session_get_state(void);

/**
 * Get cached device information (only valid when session active/idle)
 *
 * @return Pointer to device info, or NULL if session not open
 */
const CryptoDeviceInfo* crypto_session_get_device_info(void);

/**
 * Update keep-alive timer duration
 *
 * @param ms Keep-alive duration in milliseconds (0 = disable)
 */
void crypto_session_set_keep_awake(uint32_t ms);

/**
 * Record activity (resets keep-alive timer)
 *
 * Should be called after successful command execution to prevent
 * automatic session timeout.
 */
void crypto_session_record_activity(void);

/**
 * Check if session has expired based on keep-alive timer
 *
 * @return true if session expired, false otherwise
 */
bool crypto_session_is_expired(void);

/**
 * Automatically expire idle sessions (called periodically)
 *
 * Checks if keep-alive timer has expired and transitions session to idle.
 * Should be called from main loop or timer handler.
 */
void crypto_session_expire_idle(void);

/**
 * Ensure session is open (auto-open if needed)
 *
 * This is the primary function used by crypto commands to guarantee
 * a valid session exists. If session is closed, it will automatically
 * open with default settings and log a warning.
 *
 * @return CRYPTO_SESSION_OK if session ready, error code on failure
 */
CryptoSessionStatus crypto_session_ensure_open(void);
