# Diagnostic Request: ATECC608B Session Open Failure from CLI

## Problem Statement

The `atecc session open` command fails on Flipper Zero with ATCA_RX_FAIL (-26 / 0xFFFFFFE6) error when executing read operations from the CLI context. This occurs even though the same underlying CryptoAuthLib functions work perfectly from the GUI context.

## Symptoms

### ✅ Working Operations
1. **GUI Random Button**: Works perfectly - generates random numbers successfully
2. **CLI Random Command** (tested earlier): `atecc random` works from CLI
3. **I2C Bus Scan**: Device visible at address 0x60

### ❌ Failing Operations
1. **CLI `atcab_info()`**: Returns ATCA_RX_FAIL (-26)
2. **CLI `atcab_read_config_zone()`**: Returns ATCA_RX_FAIL (-26)
3. **Session Open Command**: Fails with retry attempts exhausted

### Test Output
```
>: atecc session open
DEBUG: Skipping atcab_info() - reading config zone directly...
DEBUG: atcab_read_config_zone() FAILED - status = -26 (0xFFFFFFE6)
DEBUG: Skipping atcab_info() - reading config zone directly...
DEBUG: atcab_read_config_zone() FAILED - status = -26 (0xFFFFFFE6)
DEBUG: Skipping atcab_info() - reading config zone directly...
DEBUG: atcab_read_config_zone() FAILED - status = -26 (0xFFFFFFE6)
Session open failed - ATCA_STATUS = -26 (0xFFFFFFE6)
Error: Communication error with device
Hint: Check I2C connections and run 'atecc session open'
```

## Pattern Analysis

| Operation | Type | GUI | CLI | Status Code |
|-----------|------|-----|-----|-------------|
| `atcab_random()` | GENERATE | ✅ Works | ✅ Works | ATCA_SUCCESS (0) |
| `atcab_info()` | READ | ✅ Works | ❌ Fails | ATCA_RX_FAIL (-26) |
| `atcab_read_config_zone()` | READ | ✅ Works | ❌ Fails | ATCA_RX_FAIL (-26) |

**Hypothesis**: The issue appears specific to READ operations from CLI, not GENERATE operations.

## Architecture Context

### SOLID Refactoring
We've implemented a SOLID refactoring where both GUI and CLI share the same session management code:

**Session Manager Pattern** ([crypto/crypto_session_manager.c](../crypto/crypto_session_manager.c)):
```c
bool crypto_session_execute_with_retry(
    CryptoCommandCallback command,
    void* context,
    CryptoDevicePowerState end_state,
    ATCA_STATUS* out_status) {

    for(int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
        CryptoSession session;
        crypto_session_init(&session);

        if(!crypto_session_begin(&session)) {
            last_status = ATCA_COMM_FAIL;
            continue;
        }

        // Hardware-specific stabilization delay
        furi_delay_ms(POST_WAKE_DELAY_MS);  // 50ms

        last_status = command(context);

        crypto_session_end(&session, end_state);

        if(last_status == ATCA_SUCCESS) {
            return true;
        }

        // Retry on transient errors
        if(last_status == ATCA_RX_FAIL ||
           last_status == ATCA_RX_NO_RESPONSE ||
           last_status == ATCA_COMM_FAIL) {
            furi_delay_ms(RETRY_DELAY_MS);  // 100ms
            continue;
        }

        break;  // Non-transient error
    }

    return false;
}
```

### Session Begin Implementation
The `crypto_session_begin()` function performs:
1. `atcab_init()` - Initialize CryptoAuthLib interface
2. `atcab_wakeup()` - Wake device from sleep
3. `furi_delay_ms(10)` - Initial stabilization delay
4. Additional `furi_delay_ms(50)` - Extra settling time for CLI commands

**Code** ([crypto/crypto_service.c](../crypto/crypto_service.c:39-65)):
```c
bool crypto_session_begin(CryptoSession* session) {
    furi_assert(session);

    if(session->is_active) {
        return true;
    }

    crypto_populate_default_cfg(&session->iface_cfg);

    ATCA_STATUS status = atcab_init(&session->iface_cfg);
    if(status != ATCA_SUCCESS) {
        return false;
    }

    status = atcab_wakeup();
    if(status != ATCA_SUCCESS) {
        atcab_release();
        return false;
    }

    // Allow device to settle after wakeup before processing commands
    // Some commands (Info, Config reads) need more time than others
    furi_delay_ms(10);

    session->is_active = true;
    return true;
}
```

### Session Open Command Implementation
**Code** ([crypto/crypto_session_service.c](../crypto/crypto_session_service.c:28-68)):
```c
static ATCA_STATUS session_open_command(void* ctx) {
    SessionOpenContext* context = (SessionOpenContext*)ctx;

    // WORKAROUND: atcab_info() fails from CLI (ATCA_RX_FAIL), but atcab_random() works.
    // Skip Info entirely - config zone contains all metadata we need anyway.
    printf("DEBUG: Skipping atcab_info() - reading config zone directly...\n");

    // Read config zone - contains serial number (bytes 0-3, 8-12) and DevRev (bytes 4-7)
    uint8_t config_data[128];
    ATCA_STATUS status = atcab_read_config_zone(config_data);
    if(status != ATCA_SUCCESS) {
        printf("DEBUG: atcab_read_config_zone() FAILED - status = %d (0x%X)\n", status, status);
        return status;
    }
    printf("DEBUG: atcab_read_config_zone() SUCCESS!\n");

    // Extract serial number from config zone (bytes 0-3 and 8-12)
    memcpy(&context->device_info->serial_number[0], &config_data[0], 4);
    memcpy(&context->device_info->serial_number[4], &config_data[8], 5);

    // Extract device revision from config zone (bytes 4-7)
    memcpy(&context->device_info->dev_rev, &config_data[4], 4);

    // Set I2C address
    context->device_info->i2c_address = 0x60; // ATECC608B standard address

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

    // ... state checks ...

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
        FURI_LOG_E("CryptoSession", "Failed to open session: ATCA_STATUS = %d (0x%X)",
                   atca_status, atca_status);
        // ... error mapping ...
        return CRYPTO_SESSION_ERR_DEVICE_NOT_FOUND;
    }

    session->state = CRYPTO_SESSION_STATE_ACTIVE;
    session->keep_awake_ms = keep_awake_ms;
    session->last_activity_ms = furi_get_tick();

    return CRYPTO_SESSION_OK;
}
```

## I2C Configuration
**Code** ([crypto/crypto_service.c](../crypto/crypto_service.c:16-31)):
```c
#define CRYPTO_DEFAULT_I2C_ADDRESS (0x60U)
#define CRYPTO_WAKE_DELAY_US       (1600U)
#define CRYPTO_RX_RETRIES          (20U)

static void crypto_populate_default_cfg(ATCAIfaceCfg* cfg) {
    furi_assert(cfg);
    memset(cfg, 0, sizeof(*cfg));

    cfg->iface_type = ATCA_I2C_IFACE;
    cfg->devtype = ATECC608;
    cfg->atcai2c.address = (uint8_t)(CRYPTO_DEFAULT_I2C_ADDRESS << 1U);
    cfg->atcai2c.bus = 0;
    cfg->atcai2c.baud = 100000U;
    cfg->wake_delay = CRYPTO_WAKE_DELAY_US;
    cfg->rx_retries = CRYPTO_RX_RETRIES;
}
```

## GUI Working Reference
The GUI Random button works perfectly using the same session manager:
**Code** ([views/crypto_view.c](../views/crypto_view.c:182-208)):
```c
static void crypto_view_on_random_press(void* context) {
    CryptoView* view = context;

    CryptoSession session;
    crypto_session_init(&session);

    if(!crypto_session_begin(&session)) {
        FURI_LOG_E(TAG, "Failed to wake device");
        return;
    }

    uint8_t random_data[32];
    ATCA_STATUS status = atcab_random(random_data);

    crypto_session_end(&session, CryptoDeviceSleep);

    if(status == ATCA_SUCCESS) {
        // Display random data
        snprintf(view->status_text, sizeof(view->status_text), "Random: OK");
        // ... format hex output ...
    } else {
        snprintf(view->status_text, sizeof(view->status_text),
                 "Random failed: %d", status);
    }

    view_port_update(view->view_port);
}
```

## Timing Characteristics
- **Session Begin Delay**: 10ms (in `crypto_session_begin`)
- **Post-Wake Delay**: 50ms (in `crypto_session_manager`)
- **Retry Delay**: 100ms between attempts
- **Retry Count**: 3 attempts maximum

**Total Delays Per Attempt**: 10ms + 50ms = 60ms before command execution

## Diagnostic Questions

### 1. Context/Threading Differences
- Is there a difference between GUI context and CLI context on Flipper Zero?
- Does CLI run in a different thread with different I2C access rights?
- Could there be a mutex/semaphore issue preventing I2C access from CLI?

### 2. Command Sequence Issues
- Does the device need a "priming" command (like Random) before read commands work?
- Is there state left over from previous operations that affects read commands?
- Should we send a wake-up sequence differently for read vs. generate commands?

### 3. Timing Issues
- Are 60ms of delays (10ms + 50ms) insufficient for read operations?
- Does `atcab_read_config_zone()` need more settling time than `atcab_random()`?
- Should delays be increased for read operations specifically?

### 4. HAL/Driver Differences
- Is the Flipper Zero HAL implementation treating read commands differently?
- Could there be a buffer size issue with I2C read vs. write operations?
- Are there different code paths in CryptoAuthLib for I2C reads vs. generates?

### 5. Device State
- Is the device in a different state when CLI commands execute?
- Could there be power management differences between GUI and CLI contexts?
- Does the device need a specific sequence to enable read operations?

## Investigation Steps

### Recommended Experiments

1. **Test Random from CLI after Recent Changes**
   ```bash
   atecc random --json
   ```
   Verify that Random still works after our latest session manager changes.

2. **Test Info with Increased Delays**
   Temporarily increase delays in session manager:
   - Change POST_WAKE_DELAY_MS from 50ms to 200ms
   - Change RETRY_DELAY_MS from 100ms to 500ms
   - Test if `atcab_read_config_zone()` succeeds

3. **Test Random → Info Sequence**
   Modify `session_open_command()` to:
   ```c
   // Generate random first to "prime" device
   uint8_t random_temp[32];
   atcab_random(random_temp);
   furi_delay_ms(50);

   // Then try read operation
   status = atcab_read_config_zone(config_data);
   ```

4. **Add Verbose I2C Logging**
   Enable CryptoAuthLib debug logging to see exact I2C transactions:
   - Wake sequence details
   - Command bytes sent
   - Response bytes received
   - Where exactly the RX_FAIL occurs

5. **Compare HAL I2C Calls**
   Instrument the I2C HAL layer to log:
   - Every I2C transaction from GUI context
   - Every I2C transaction from CLI context
   - Timing between transactions
   - Any differences in I2C transaction patterns

6. **Test with Single-Byte Reads**
   Try reading config zone one word at a time instead of full 128 bytes:
   ```c
   for(int i = 0; i < 4; i++) {
       status = atcab_read_zone(ATCA_ZONE_CONFIG, 0, i, 0, &config_data[i*32], 32);
       if(status != ATCA_SUCCESS) break;
       furi_delay_ms(10);
   }
   ```

## Files for Review

1. **Session Manager**: [crypto/crypto_session_manager.c](../crypto/crypto_session_manager.c)
2. **Session Service**: [crypto/crypto_session_service.c](../crypto/crypto_session_service.c)
3. **Crypto Service (HAL)**: [crypto/crypto_service.c](../crypto/crypto_service.c)
4. **GUI Implementation**: [views/crypto_view.c](../views/crypto_view.c)
5. **CLI Command**: [cli/commands/crypto_cmd_session.c](../cli/commands/crypto_cmd_session.c)

## ATCA_RX_FAIL Error Code

From CryptoAuthLib documentation:
- **ATCA_RX_FAIL (-26 / 0xFFFFFFE6)**: Response was received but failed CRC check or packet structure validation
- Common causes:
  - Device not ready to respond (timing issue)
  - Corrupted I2C transmission
  - Device in wrong state for command
  - Incorrect command packet format

## Expected Behavior

`atecc session open` should:
1. Wake device via `atcab_wakeup()`
2. Read config zone (128 bytes) to get serial number and DevRev
3. Check lock status for config and data zones
4. Cache device info in session manager
5. Return success

## Request for Diagnostic Agent

Please investigate why READ operations (`atcab_info()`, `atcab_read_config_zone()`) fail from CLI with ATCA_RX_FAIL while GENERATE operations (`atcab_random()`) succeed, given that:
1. Both use the identical session manager code
2. GUI context works perfectly for all operations
3. Device is visible on I2C bus at 0x60
4. Retry logic with delays doesn't help

Provide:
1. Root cause analysis
2. Specific code changes needed to fix
3. Any additional diagnostic tests to run
4. Long-term architectural recommendations

---

**Date**: 2025-11-05
**Project**: Flipper Zero ATECC608B I2C Tools
**Session**: PRD-002 Session Management Implementation
**Priority**: P0 - Blocking completion of session management module
