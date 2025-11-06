#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "crypto_service.h"

/**
 * @file crypto_session.h
 * @brief Session state types and structures for ATECC608B crypto operations
 *
 * Defines the session lifecycle states and device information structures
 * used throughout the crypto CLI application.
 */

/**
 * Session lifecycle states
 */
typedef enum {
    CRYPTO_SESSION_STATE_CLOSED,  /**< No active session */
    CRYPTO_SESSION_STATE_OPENING, /**< Waking device */
    CRYPTO_SESSION_STATE_ACTIVE,  /**< Ready for commands */
    CRYPTO_SESSION_STATE_IDLE,    /**< Low power, quick wake */
    CRYPTO_SESSION_STATE_ERROR    /**< Fatal error, must reopen */
} CryptoSessionState;

/**
 * Device information cached on session open
 */
typedef struct {
    uint8_t i2c_address;           /**< I2C address (0x60 for ATECC608B) */
    uint8_t serial_number[9];      /**< 9-byte unique serial number */
    uint32_t dev_rev;              /**< Device revision (0x00006003 for ATECC608B) */
    bool config_locked;            /**< Config zone locked status */
    bool data_locked;              /**< Data zone locked status */
} CryptoDeviceInfo;

/**
 * Session context (global singleton)
 *
 * Note: In the SOLID refactored architecture, this struct maintains *logical* session state
 * (user's view of the session), not hardware session state. Actual device sessions are
 * managed per-command by crypto_session_manager using the proven GUI pattern.
 */
typedef struct {
    CryptoSessionState state;      /**< Current logical session state */
    CryptoDeviceInfo device_info;  /**< Cached device information (from last open) */
    uint32_t keep_awake_ms;        /**< Keep-alive duration (0 = disabled) */
    uint32_t last_activity_ms;     /**< Last activity timestamp for timeout */
    uint8_t retry_count;           /**< Error recovery retry counter */
} CryptoSessionMgr;

/**
 * Get global session instance (singleton pattern)
 *
 * @return Pointer to global session instance
 */
CryptoSessionMgr* crypto_session_mgr_get(void);
