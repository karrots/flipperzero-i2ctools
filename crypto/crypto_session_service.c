#include "crypto_session_service.h"
#include "crypto_service.h"
#include "crypto_session_manager.h"
#include "../lib/cryptoauthlib/lib/atca_basic.h"
#include "../lib/cryptoauthlib/lib/cryptoauthlib.h"

#include <furi.h>
#include <furi_hal.h>
#include <string.h>

// Global singleton session instance
static CryptoSessionMgr g_crypto_session = {
    .state = CRYPTO_SESSION_STATE_CLOSED,
    .keep_awake_ms = 0,
    .last_activity_ms = 0,
    .retry_count = 0
};

CryptoSessionMgr* crypto_session_mgr_get(void) {
    return &g_crypto_session;
}

// Context for session open command
typedef struct {
    CryptoDeviceInfo* device_info;
} SessionOpenContext;

// Command callback to gather device info
static ATCA_STATUS session_open_command(void* ctx) {
    SessionOpenContext* context = (SessionOpenContext*)ctx;

    // WORKAROUND: Read operations (atcab_info, atcab_read_config_zone, atcab_read_zone)
    // fail from CLI with ATCA_RX_FAIL (-26), but atcab_random() works.
    //
    // Investigation shows this is Flipper Zero specific - all I2C read operations from
    // CLI context fail, while generate operations (Random) succeed. GUI works fine.
    //
    // Root cause: Unknown - possibly HAL-level I2C read issue from CLI context
    // Workaround: Verify device with Random, use default device info for session

    // Test device responsiveness with Random command (known to work from CLI)
    uint8_t random_test[32];
    ATCA_STATUS status = atcab_random(random_test);
    if(status != ATCA_SUCCESS) {
        FURI_LOG_E("CryptoSession", "Device not responding to Random command: %d", status);
        return status;
    }

    // Attempt to read device info from config zone (likely to fail on Flipper CLI)
    uint8_t config_word[4];
    status = atcab_read_zone(ATCA_ZONE_CONFIG, 0, 0, 0, config_word, 4);

    if(status == ATCA_SUCCESS) {
        // Success! Try to read more config data in 4-byte chunks
        uint8_t config_data[16];
        memcpy(&config_data[0], config_word, 4);

        for(int word = 1; word < 4; word++) {
            status = atcab_read_zone(ATCA_ZONE_CONFIG, 0, word, 0, &config_data[word * 4], 4);
            if(status != ATCA_SUCCESS) {
                break;
            }
        }

        if(status == ATCA_SUCCESS) {
            // Extract serial number (bytes 0-3 and 8-12)
            memcpy(&context->device_info->serial_number[0], &config_data[0], 4);
            memcpy(&context->device_info->serial_number[4], &config_data[8], 5);

            // Extract device revision (bytes 4-7)
            memcpy(&context->device_info->dev_rev, &config_data[4], 4);

            FURI_LOG_I("CryptoSession", "Read device info: DevRev 0x%08lX",
                       context->device_info->dev_rev);
        }
    }

    if(status != ATCA_SUCCESS) {
        // Read failed (expected on Flipper CLI) - use fallback device info
        FURI_LOG_W("CryptoSession",
                   "Config zone read failed (%d) - using default device info", status);

        memset(&context->device_info->serial_number, 0xFF, 9);
        context->device_info->dev_rev = 0x00600200; // ATECC608B standard DevRev
        context->device_info->config_locked = true;
        context->device_info->data_locked = true;
    }

    // Set I2C address
    context->device_info->i2c_address = 0x60;

    return ATCA_SUCCESS;
}

CryptoSessionStatus crypto_session_open(uint32_t keep_awake_ms) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    // Check if already open
    if(session->state == CRYPTO_SESSION_STATE_ACTIVE ||
       session->state == CRYPTO_SESSION_STATE_IDLE) {
        FURI_LOG_W("CryptoSession", "Session already open");
        return CRYPTO_SESSION_ERR_ALREADY_OPEN;
    }

    session->state = CRYPTO_SESSION_STATE_OPENING;
    session->device_info.i2c_address = 0x60; // Standard ATECC608B address

    // Use SOLID session manager with proven GUI pattern
    // The session manager handles: init, begin, command, end
    SessionOpenContext context = {
        .device_info = &session->device_info
    };

    ATCA_STATUS atca_status;
    bool success = crypto_session_execute_with_retry(
        session_open_command,
        &context,
        CryptoDeviceSleep,
        &atca_status
    );

    if(!success) {
        FURI_LOG_E("CryptoSession", "Failed to open session: ATCA_STATUS = %d (0x%X)", atca_status, atca_status);
        session->state = CRYPTO_SESSION_STATE_ERROR;

        // Map ATCA status to session status
        switch(atca_status) {
            case ATCA_COMM_FAIL:
            case ATCA_RX_FAIL:
            case ATCA_RX_NO_RESPONSE:
                return CRYPTO_SESSION_ERR_COMMS_ERROR;
            case ATCA_TIMEOUT:
                return CRYPTO_SESSION_ERR_TIMEOUT;
            default:
                return CRYPTO_SESSION_ERR_DEVICE_NOT_FOUND;
        }
    }

    // Session successfully opened - device info cached
    session->state = CRYPTO_SESSION_STATE_ACTIVE;
    session->keep_awake_ms = keep_awake_ms;
    session->last_activity_ms = furi_get_tick();

    FURI_LOG_I("CryptoSession", "Session opened successfully");
    return CRYPTO_SESSION_OK;
}

CryptoSessionStatus crypto_session_idle(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    if(session->state != CRYPTO_SESSION_STATE_ACTIVE) {
        FURI_LOG_W("CryptoSession", "Session not active");
        return CRYPTO_SESSION_ERR_NOT_OPEN;
    }

    // In the new architecture, we don't hold a persistent hardware session
    // Each command opens/closes its own session using session_manager
    // This just updates our logical state for the user
    session->state = CRYPTO_SESSION_STATE_IDLE;
    FURI_LOG_D("CryptoSession", "Session transitioned to idle");
    return CRYPTO_SESSION_OK;
}

CryptoSessionStatus crypto_session_sleep(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    if(session->state == CRYPTO_SESSION_STATE_CLOSED) {
        FURI_LOG_D("CryptoSession", "Session already closed");
        return CRYPTO_SESSION_OK;
    }

    // Clear session state - no hardware session to close in new architecture
    session->state = CRYPTO_SESSION_STATE_CLOSED;
    session->keep_awake_ms = 0;
    session->last_activity_ms = 0;
    session->retry_count = 0;

    FURI_LOG_I("CryptoSession", "Session closed");
    return CRYPTO_SESSION_OK;
}

CryptoSessionState crypto_session_get_state(void) {
    return g_crypto_session.state;
}

const CryptoDeviceInfo* crypto_session_get_device_info(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    if(session->state != CRYPTO_SESSION_STATE_ACTIVE &&
       session->state != CRYPTO_SESSION_STATE_IDLE) {
        return NULL;
    }

    return &session->device_info;
}

void crypto_session_set_keep_awake(uint32_t ms) {
    CryptoSessionMgr* session = crypto_session_mgr_get();
    session->keep_awake_ms = ms;

    if(ms > 0) {
        session->last_activity_ms = furi_get_tick();
    }
}

void crypto_session_record_activity(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();
    session->last_activity_ms = furi_get_tick();
}

bool crypto_session_is_expired(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    // No expiration if keep-alive is disabled
    if(session->keep_awake_ms == 0) {
        return false;
    }

    // Not expired if session not active
    if(session->state != CRYPTO_SESSION_STATE_ACTIVE &&
       session->state != CRYPTO_SESSION_STATE_IDLE) {
        return false;
    }

    uint32_t current_time = furi_get_tick();
    uint32_t elapsed = current_time - session->last_activity_ms;

    return elapsed >= session->keep_awake_ms;
}

void crypto_session_expire_idle(void) {
    if(crypto_session_is_expired()) {
        CryptoSessionMgr* session = crypto_session_mgr_get();

        if(session->state == CRYPTO_SESSION_STATE_ACTIVE) {
            FURI_LOG_I("CryptoSession", "Keep-alive expired, transitioning to idle");
            crypto_session_idle();
        }
    }
}

CryptoSessionStatus crypto_session_ensure_open(void) {
    CryptoSessionMgr* session = crypto_session_mgr_get();

    // If already active, just record activity and return
    if(session->state == CRYPTO_SESSION_STATE_ACTIVE) {
        crypto_session_record_activity();
        return CRYPTO_SESSION_OK;
    }

    // If idle, transition back to active
    // In new architecture, actual device wake happens per-command via session_manager
    if(session->state == CRYPTO_SESSION_STATE_IDLE) {
        session->state = CRYPTO_SESSION_STATE_ACTIVE;
        crypto_session_record_activity();
        FURI_LOG_D("CryptoSession", "Transitioned from idle to active");
        return CRYPTO_SESSION_OK;
    }

    // Auto-open with default settings
    FURI_LOG_W(
        "CryptoSession",
        "Auto-opening session (hint: use 'atecc session open' for explicit control)");

    return crypto_session_open(0); // No keep-alive by default
}
