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

    // Read device revision via Info command
    uint8_t info_buffer[4];
    ATCA_STATUS status = atcab_info(info_buffer);
    if(status != ATCA_SUCCESS) {
        return status;
    }

    // Read config zone for serial number
    uint8_t config_data[128];
    status = atcab_read_config_zone(config_data);
    if(status == ATCA_SUCCESS) {
        // Extract serial number from config zone (bytes 0-3 and 8-12)
        memcpy(&context->device_info->serial_number[0], &config_data[0], 4);
        memcpy(&context->device_info->serial_number[4], &config_data[8], 5);

        // Extract device revision from config zone (bytes 4-7)
        memcpy(&context->device_info->dev_rev, &config_data[4], 4);
    } else {
        // Use info buffer for partial serial
        memcpy(&context->device_info->serial_number[0], info_buffer, 4);
        context->device_info->dev_rev = 0x00006003; // ATECC608B default
    }

    // Check lock status
    bool is_locked = false;
    status = atcab_is_locked(LOCK_ZONE_CONFIG, &is_locked);
    context->device_info->config_locked = (status == ATCA_SUCCESS) ? is_locked : false;

    status = atcab_is_locked(LOCK_ZONE_DATA, &is_locked);
    context->device_info->data_locked = (status == ATCA_SUCCESS) ? is_locked : false;

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
        FURI_LOG_E("CryptoSession", "Failed to open session: %d", atca_status);
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

    printf("[DEBUG] Session opened successfully!\n");
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
