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

/**
 * Application-level I2C mutex to prevent bus contention
 *
 * This mutex protects the entire command sequence (send→delay→receive)
 * from being interrupted by other tasks accessing the I2C bus.
 *
 * Without this mutex, the following race condition can occur:
 * 1. CLI task sends command via hal_i2c_send()
 * 2. HAL releases bus mutex after transmission
 * 3. CryptoAuthLib calls atca_delay_ms() to wait for device processing
 * 4. Furi scheduler preempts CLI task during delay
 * 5. Another task (GUI, power mgmt) acquires I2C bus for its own transaction
 * 6. CLI task resumes, calls hal_i2c_receive()
 * 7. ATECC608B is confused by unexpected bus activity, returns corrupted data
 * 8. CryptoAuthLib detects CRC error → ATCA_RX_FAIL (-26)
 *
 * This mutex ensures atomicity of the entire operation across all tasks.
 */
static FuriMutex* g_crypto_i2c_mutex = NULL;

void crypto_session_manager_init(void) {
    if(g_crypto_i2c_mutex == NULL) {
        // Use recursive mutex to allow nested calls from same task
        g_crypto_i2c_mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
        furi_assert(g_crypto_i2c_mutex);
        FURI_LOG_I("SessionMgr", "I2C mutex initialized");
    }
}

void crypto_session_manager_deinit(void) {
    if(g_crypto_i2c_mutex != NULL) {
        furi_mutex_free(g_crypto_i2c_mutex);
        g_crypto_i2c_mutex = NULL;
        FURI_LOG_I("SessionMgr", "I2C mutex destroyed");
    }
}

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

    // Acquire application-level mutex BEFORE entire operation
    // This prevents other tasks from accessing I2C bus during the vulnerable
    // window between hal_i2c_send() and hal_i2c_receive()
    furi_assert(g_crypto_i2c_mutex); // Ensure init was called

    if(furi_mutex_acquire(g_crypto_i2c_mutex, FuriWaitForever) != FuriStatusOk) {
        FURI_LOG_E("SessionMgr", "Failed to acquire I2C mutex");
        if(out_status) {
            *out_status = ATCA_FUNC_FAIL;
        }
        return false;
    }

    ATCA_STATUS last_status = ATCA_GEN_FAIL;
    bool success = false;

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

        // Success - exit loop
        if(last_status == ATCA_SUCCESS) {
            success = true;
            break;
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

    // Set output status
    if(out_status) {
        *out_status = last_status;
    }

    // RELEASE mutex AFTER entire operation is complete
    // This ensures no other task can interfere with the I2C transaction
    furi_mutex_release(g_crypto_i2c_mutex);

    return success;
}
