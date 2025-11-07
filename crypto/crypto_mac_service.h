#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "crypto_session.h"

/**
 * @file crypto_mac_service.h
 * @brief MAC/HMAC authentication tag computation service for ATECC608B
 *
 * Provides functions for computing HMAC-SHA256 authentication tags using
 * keys stored in chip slots, supporting both static message signing and
 * challenge-response authentication with TempKey.
 */

/**
 * MAC operation mode
 */
typedef enum {
    CRYPTO_MAC_MODE_HMAC = 0,   /**< HMAC-SHA256 (default) */
    CRYPTO_MAC_MODE_MAC = 1     /**< Legacy MAC (not implemented yet) */
} CryptoMacMode;

/**
 * MAC computation parameters
 */
typedef struct {
    uint8_t slot;               /**< Key slot (0-15) */
    CryptoMacMode mode;         /**< HMAC or MAC mode */
    bool use_tempkey;           /**< Include TempKey in computation */
    const uint8_t* message;     /**< Message to sign (NULL if using TempKey only) */
    size_t message_len;         /**< Message length in bytes */
} CryptoMacParams;

/**
 * MAC computation result
 */
typedef struct {
    uint8_t tag[32];            /**< 32-byte HMAC tag */
    uint32_t elapsed_ms;        /**< Execution time in milliseconds */
    bool success;               /**< Operation success flag */
    uint32_t chip_status;       /**< Chip status code (if error occurred) */
} CryptoMacResult;

/**
 * MAC operation status codes
 */
typedef enum {
    CRYPTO_MAC_OK = 0,                          /**< Operation successful */
    CRYPTO_MAC_ERR_BAD_SLOT = 1,                /**< Invalid slot number */
    CRYPTO_MAC_ERR_ACCESS_DENIED = 3,           /**< Slot access denied */
    CRYPTO_MAC_ERR_COMMS_ERROR = 4,             /**< I2C communication error */
    CRYPTO_MAC_ERR_TEMPKEY_NOT_LOADED = 7,      /**< TempKey not loaded */
    CRYPTO_MAC_ERR_INTERNAL = 8                 /**< Internal error */
} CryptoMacStatus;

/**
 * Compute MAC/HMAC authentication tag
 *
 * This function computes an HMAC-SHA256 tag using a key stored in the
 * specified chip slot. It supports two modes:
 * - Static message signing: HMAC(key, message)
 * - Challenge-response: MAC(key, TempKey) - requires prior Nonce command
 *
 * The session will be automatically opened if not already active.
 *
 * @param params MAC computation parameters
 * @param result MAC computation result (output)
 * @return CRYPTO_MAC_OK on success, error code on failure
 */
CryptoMacStatus crypto_mac_compute(
    const CryptoMacParams* params,
    CryptoMacResult* result);

/**
 * Validate slot configuration for MAC operations
 *
 * Checks if the specified slot is valid and accessible for MAC/HMAC
 * operations. This is a basic validation check.
 *
 * @param slot Slot number (0-15)
 * @return true if slot is valid, false otherwise
 */
bool crypto_mac_validate_slot(uint8_t slot);
