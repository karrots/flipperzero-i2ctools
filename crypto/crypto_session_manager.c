#include "crypto_session_manager.h"
#include <furi.h>

/**
 * Maximum retry attempts for transient I2C errors
 */
#define MAX_RETRY_ATTEMPTS 3

/**
 * Delay between retry attempts (milliseconds)
 * Device needs time to fully enter sleep state before wake attempt
 */
#define RETRY_DELAY_MS 100

/**
 * Hardware-specific stabilization delay after wake (milliseconds)
 * The ATECC608B needs additional time beyond the 10ms in crypto_service.c
 * GUI works without this because of inherent user interaction delays
 */
#define POST_WAKE_DELAY_MS 50

bool crypto_session_execute_command(
    CryptoCommandCallback command,
    void* context,
    CryptoDevicePowerState end_state) {

    if(!command) {
        return false;
    }

    // Encapsulate proven GUI pattern (views/crypto_view.c:182-196)
    CryptoSession session;
    crypto_session_init(&session);

    // Wake device - crypto_service.c already includes:
    // - atcab_init()
    // - atcab_wakeup()
    // - 10ms stabilization delay (line 61)
    if(!crypto_session_begin(&session)) {
        FURI_LOG_E("SessionMgr", "Failed to wake device");
        return false;
    }

    // Hardware-specific: ATECC608B needs additional settling time for immediate commands
    // GUI doesn't need this explicitly due to user interaction delays
    furi_delay_ms(POST_WAKE_DELAY_MS);

    // Execute command
    ATCA_STATUS status = command(context);

    // Always end session to release resources
    crypto_session_end(&session, end_state);

    if(status != ATCA_SUCCESS) {
        FURI_LOG_E("SessionMgr", "Command failed: %d", status);
        return false;
    }

    return true;
}

bool crypto_session_execute_with_retry(
    CryptoCommandCallback command,
    void* context,
    CryptoDevicePowerState end_state,
    ATCA_STATUS* out_status) {

    if(!command) {
        if(out_status) {
            *out_status = ATCA_BAD_PARAM;
        }
        return false;
    }

    ATCA_STATUS last_status = ATCA_GEN_FAIL;

    for(int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
        if(attempt > 0) {
            FURI_LOG_W("SessionMgr", "Retry attempt %d/%d", attempt + 1, MAX_RETRY_ATTEMPTS);
            furi_delay_ms(RETRY_DELAY_MS);
        }

        CryptoSession session;
        crypto_session_init(&session);

        if(!crypto_session_begin(&session)) {
            last_status = ATCA_COMM_FAIL;
            FURI_LOG_E("SessionMgr", "Failed to wake device (attempt %d)", attempt + 1);
            continue; // Try again
        }

        // Hardware-specific stabilization delay (same as execute_command)
        furi_delay_ms(POST_WAKE_DELAY_MS);

        last_status = command(context);

        crypto_session_end(&session, end_state);

        // Success - return immediately
        if(last_status == ATCA_SUCCESS) {
            if(out_status) {
                *out_status = last_status;
            }
            return true;
        }

        // Retry on transient errors
        if(last_status == ATCA_RX_FAIL ||
           last_status == ATCA_RX_NO_RESPONSE ||
           last_status == ATCA_COMM_FAIL) {
            FURI_LOG_W("SessionMgr", "Transient error %d, retrying...", last_status);
            continue;
        }

        // Non-transient error - fail immediately
        FURI_LOG_E("SessionMgr", "Non-transient error %d, aborting", last_status);
        break;
    }

    // All retries exhausted or non-transient error
    if(out_status) {
        *out_status = last_status;
    }
    return false;
}
