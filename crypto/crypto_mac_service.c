#include "crypto_mac_service.h"
#include "crypto_session_service.h"
#include "crypto_service.h"

#include <furi.h>
#include <string.h>

// CryptoAuthLib includes
#include "cryptoauthlib.h"

/**
 * Map CryptoAuthLib status to CryptoMacStatus
 */
static CryptoMacStatus map_atca_status(ATCA_STATUS atca_status) {
    switch(atca_status) {
        case ATCA_SUCCESS:
            return CRYPTO_MAC_OK;

        case ATCA_BAD_PARAM:
            return CRYPTO_MAC_ERR_BAD_SLOT;

        case ATCA_FUNC_FAIL:
        case ATCA_EXECUTION_ERROR:
            return CRYPTO_MAC_ERR_ACCESS_DENIED;

        case ATCA_COMM_FAIL:
        case ATCA_TIMEOUT:
        case ATCA_RX_FAIL:
        case ATCA_RX_NO_RESPONSE:
            return CRYPTO_MAC_ERR_COMMS_ERROR;

        default:
            return CRYPTO_MAC_ERR_INTERNAL;
    }
}

bool crypto_mac_validate_slot(uint8_t slot) {
    // ATECC608B has slots 0-15
    if(slot > 15) {
        return false;
    }

    // Check if session is open to access device info
    const CryptoDeviceInfo* info = crypto_session_get_device_info();
    if(!info) {
        // No session open yet - will be auto-opened during compute
        // Just validate range for now
        return true;
    }

    // For ATECC608B, slots 0-15 can be configured for HMAC
    // Actual slot configuration check would require reading config zone
    // For now, we assume slots are valid and let the chip reject invalid operations
    return true;
}

CryptoMacStatus crypto_mac_compute(
    const CryptoMacParams* params,
    CryptoMacResult* result) {

    // Validate input parameters
    if(!params || !result) {
        return CRYPTO_MAC_ERR_INTERNAL;
    }

    // Initialize result
    memset(result, 0, sizeof(CryptoMacResult));

    // Validate slot number
    if(!crypto_mac_validate_slot(params->slot)) {
        return CRYPTO_MAC_ERR_BAD_SLOT;
    }

    // Validate message (required unless using TempKey only)
    if(!params->use_tempkey && (!params->message || params->message_len == 0)) {
        return CRYPTO_MAC_ERR_INTERNAL;
    }

    // Ensure session is open (auto-open if needed)
    CryptoSessionStatus session_status = crypto_session_ensure_open();
    if(session_status != CRYPTO_SESSION_OK) {
        FURI_LOG_E("CryptoMAC", "Failed to ensure session open: %d", session_status);
        return CRYPTO_MAC_ERR_COMMS_ERROR;
    }

    // Start timing
    uint32_t start_time = furi_get_tick();

    ATCA_STATUS atca_status;

    if(params->use_tempkey) {
        // MAC with TempKey (for challenge-response)
        // Mode 0x00 = Use TempKey as part of message
        atca_status = atcab_mac(0x00, params->slot, NULL, result->tag);

        if(atca_status != ATCA_SUCCESS) {
            FURI_LOG_E("CryptoMAC", "atcab_mac failed: 0x%02X", atca_status);

            // Check if TempKey was not loaded
            if(atca_status == ATCA_EXECUTION_ERROR) {
                result->chip_status = atca_status;
                result->elapsed_ms = furi_get_tick() - start_time;
                return CRYPTO_MAC_ERR_TEMPKEY_NOT_LOADED;
            }
        }
    } else {
        // HMAC with external message
        // target=0 means no specific target (standard HMAC operation)
        atca_status = atcab_sha_hmac(
            params->message,
            params->message_len,
            params->slot,
            result->tag,
            0); // target

        if(atca_status != ATCA_SUCCESS) {
            FURI_LOG_E("CryptoMAC", "atcab_sha_hmac failed: 0x%02X", atca_status);
        }
    }

    // Record timing
    result->elapsed_ms = furi_get_tick() - start_time;

    // Handle result
    if(atca_status != ATCA_SUCCESS) {
        result->success = false;
        result->chip_status = atca_status;
        return map_atca_status(atca_status);
    }

    // Success
    result->success = true;
    crypto_session_record_activity(); // Reset keep-alive timer

    return CRYPTO_MAC_OK;
}
