#include "crypto_view.h"

#include "../lib/cryptoauthlib/lib/atca_basic.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <gui/modules/text_box.h>
#include <gui/view_holder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../crypto/crypto_service.h"
#include "../lib/cryptoauthlib/lib/cryptoauthlib.h"
#include "../lib/cryptoauthlib/lib/calib/calib_basic.h"
#include "../lib/cryptoauthlib/lib/calib/calib_command.h"
#include "../lib/cryptoauthlib/lib/atcacert/atcacert_client.h"
#include "../lib/cryptoauthlib/lib/atcacert/atcacert_def.h"
#include "../lib/cryptoauthlib/lib/atcacert/atcacert_der.h"
#include "../lib/cryptoauthlib/app/tng/tng_atcacert_client.h"
#include "../lib/cryptoauthlib/app/tng/tng_atca.h"

typedef enum {
    CryptoActionDetect = 0,
    CryptoActionInfo,
    CryptoActionRandom,
    CryptoActionSelfTest,
    CryptoActionSlotPeek,
    CryptoActionSleep,
    CryptoActionCount,
} CryptoViewAction;

struct CryptoView {
    CryptoViewAction selected;
    size_t slot_index;
    size_t page;
    CryptoSession session;
};

static const uint8_t crypto_safe_slots[] =
    {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};

static const char* crypto_action_labels[CryptoActionCount] = {
    "Detect Device",
    "Chip Info",
    "Random",
    "Self-Test",
    "Slot Peek",
    "Sleep Device",
};

static void crypto_view_show_dialog(const char* header, const char* text, bool left_align) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 4, AlignCenter, AlignTop);
    const Align align = left_align ? AlignLeft : AlignCenter;
    const Align valign = left_align ? AlignTop : AlignCenter;
    const uint8_t text_x = left_align ? 4U : 64U;
    const uint8_t text_y = left_align ? 16U : 32U;
    dialog_message_set_text(message, text, text_x, text_y, align, valign);
    dialog_message_set_buttons(message, NULL, NULL, NULL);
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void crypto_view_textbox_back_callback(void* context) {
    furi_assert(context);
    FuriSemaphore* semaphore = context;
    furi_semaphore_release(semaphore);
}

static void crypto_view_show_textbox(const char* header, const char* text, TextBoxFont font) {
    // Allocate TextBox
    TextBox* textbox = text_box_alloc();

    // Configure TextBox
    text_box_set_font(textbox, font);
    text_box_set_focus(textbox, TextBoxFocusStart);

    // Build header with text content
    FuriString* content = furi_string_alloc();
    if(header != NULL) {
        furi_string_cat_printf(content, "%s\n\n%s", header, text);
    } else {
        furi_string_cat_str(content, text);
    }
    text_box_set_text(textbox, furi_string_get_cstr(content));

    // Create semaphore for blocking until back button is pressed
    FuriSemaphore* semaphore = furi_semaphore_alloc(1, 0);

    // Get view and attach to GUI via ViewHolder
    ViewHolder* view_holder = view_holder_alloc();
    view_holder_set_view(view_holder, text_box_get_view(textbox));
    view_holder_set_back_callback(view_holder, crypto_view_textbox_back_callback, semaphore);

    // Attach to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_holder_attach_to_gui(view_holder, gui);
    view_holder_send_to_front(view_holder);

    // Wait for back button press
    furi_semaphore_acquire(semaphore, FuriWaitForever);

    // Free resources in proper order
    // First, detach view from holder and send to back
    view_holder_set_view(view_holder, NULL);
    view_holder_send_to_back(view_holder);

    // Now free the holder and close GUI
    view_holder_free(view_holder);
    furi_record_close(RECORD_GUI);

    // Free the TextBox and other resources
    text_box_free(textbox);
    furi_semaphore_free(semaphore);
    furi_string_free(content);
}

static void
    crypto_view_show_status_error(const char* header, const char* context, ATCA_STATUS status) {
    char buffer[96];
    if(status == ATCA_SUCCESS) {
        snprintf(buffer, sizeof(buffer), "%s", context);
    } else {
        snprintf(buffer, sizeof(buffer), "%s\nStatus 0x%02X", context, (uint8_t)status);
    }
    crypto_view_show_dialog(header, buffer, true);
}

static bool crypto_view_open_session(CryptoView* view, const char* header) {
    furi_assert(view);

    // Always close any existing session to ensure fresh device state
    if(view->session.is_active) {
        crypto_session_end(&view->session, CryptoDeviceIdle);
    }

    if(crypto_session_begin(&view->session)) {
        return true;
    }

    crypto_view_show_dialog(header, "No response from 0x60", true);
    return false;
}

static void crypto_view_idle_session(CryptoView* view) {
    if((view != NULL) && view->session.is_active) {
        crypto_session_end(&view->session, CryptoDeviceIdle);
    }
}

/**
 * @brief Retrieves device serial number from X.509 certificate in data zone
 *
 * This function implements a workaround for ATECC608B devices where the config zone
 * is locked and atcab_read_serial_number() fails.
 *
 * IMPORTANT: This ONLY works for Trust & Go (TNG) pre-provisioned devices that have
 * an X.509 certificate stored in the data zone. For non-TNG devices, there is no
 * alternative method to retrieve the serial number if config zone reads fail.
 *
 * Background:
 * - Serial number is stored in Config Zone bytes [0:3, 8:12]
 * - Standard method uses atcab_read_serial_number() which reads config zone
 * - Info command has NO mode for reading serial number (only revision, GPIO, etc.)
 * - If config zone is locked/restricted, standard method returns error 0xE6
 * - TNG devices store X.509 cert in data zone which contains serial number
 *
 * @param serial_out Buffer to receive the serial number bytes (must be at least 32 bytes)
 * @param serial_len_out Pointer to receive the actual length of the serial number
 * @return ATCA_STATUS ATCA_SUCCESS on success, error code otherwise
 */
static ATCA_STATUS crypto_view_read_serial_from_cert(uint8_t* serial_out, size_t* serial_len_out) {
    if(serial_out == NULL || serial_len_out == NULL) {
        return ATCA_BAD_PARAM;
    }

    ATCA_STATUS status;

    // Try TNG certificate reading (for Trust & Go pre-provisioned devices)
    size_t max_cert_size = 0;
    status = tng_atcacert_max_device_cert_size(&max_cert_size);

    if(status == ATCA_SUCCESS && max_cert_size > 0 && max_cert_size <= 2048) {
        // This appears to be a TNG device, try full certificate reading
        uint8_t* cert_data = malloc(max_cert_size);
        if(cert_data != NULL) {
            size_t cert_size = max_cert_size;
            status = tng_atcacert_read_device_cert(cert_data, &cert_size, NULL);

            if(status == ATCA_SUCCESS) {
                // Successfully read certificate, now extract serial number
                const atcacert_def_t* cert_def = NULL;
                status = tng_get_device_cert_def(&cert_def);

                if(status == ATCA_SUCCESS && cert_def != NULL) {
                    size_t sn_size = *serial_len_out;
                    status = atcacert_get_cert_sn(cert_def, cert_data, cert_size, serial_out, &sn_size);

                    if(status == ATCA_SUCCESS) {
                        *serial_len_out = sn_size;
                        free(cert_data);
                        return ATCA_SUCCESS;
                    }
                }
            }
            free(cert_data);
        }
    }

    // No certificate found or device is not TNG type
    // Serial number is not accessible through any standard method
    return ATCA_GEN_FAIL;
}

static void crypto_view_action_detect(CryptoView* view) {
    if(!crypto_view_open_session(view, "Detect Device")) {
        return;
    }

    char msg[256];
    int len = 0;

    // Try to get device info via Info command
    uint8_t revision[INFO_SIZE] = {0};
    ATCA_STATUS info_status = atcab_info(revision);

    // Try to read config zone
    uint8_t config_data[ATCA_BLOCK_SIZE] = {0};
    ATCA_STATUS config_status =
        atcab_read_zone(ATCA_ZONE_CONFIG, 0U, 0U, 0U, config_data, ATCA_BLOCK_SIZE);

    // Check lock status (may fail if config unreadable)
    bool config_locked = false;
    bool data_locked = false;
    ATCA_STATUS lock_cfg_status = atcab_is_config_locked(&config_locked);
    ATCA_STATUS lock_data_status = atcab_is_data_locked(&data_locked);

    // Build message based on what we can access
    if(info_status == ATCA_SUCCESS) {
        // Full access - show device info
        const char* device_text = "ATECC608";
        if(revision[2] == 0x60U) {
            device_text = (revision[3] >= 0x03U) ? "ATECC608B" : "ATECC608A";
        }
        len += snprintf(
            msg + len,
            sizeof(msg) - len,
            "%s detected\nRev: %02X%02X%02X%02X\n",
            device_text,
            revision[0],
            revision[1],
            revision[2],
            revision[3]);

        if(lock_cfg_status == ATCA_SUCCESS) {
            len += snprintf(
                msg + len,
                sizeof(msg) - len,
                "Config: %s\n",
                config_locked ? "LOCKED" : "unlocked");
        }
        if(lock_data_status == ATCA_SUCCESS) {
            len += snprintf(
                msg + len, sizeof(msg) - len, "Data: %s", data_locked ? "LOCKED" : "unlocked");
        }
    } else if(config_status == ATCA_SUCCESS) {
        // Config readable but Info failed - unusual case
        len += snprintf(
            msg + len,
            sizeof(msg) - len,
            "ATECC608 detected\nConfig zone readable\nInfo cmd blocked\n");
        len += snprintf(
            msg + len,
            sizeof(msg) - len,
            "SN bytes: %02X%02X%02X%02X",
            config_data[0],
            config_data[1],
            config_data[2],
            config_data[3]);
    } else {
        // Config zone locked/restricted - this is your chip's state
        len += snprintf(msg + len, sizeof(msg) - len, "ATECC608B detected\n\n");
        len += snprintf(msg + len, sizeof(msg) - len, "Config zone: LOCKED\n");
        len += snprintf(msg + len, sizeof(msg) - len, "Info access: BLOCKED\n");
        len += snprintf(msg + len, sizeof(msg) - len, "Available features:\n");
        len += snprintf(msg + len, sizeof(msg) - len, "- Random\n- Self-Test\n- Slot Peek");
    }

    crypto_view_idle_session(view);
    crypto_view_show_textbox("Detect Device", msg, TextBoxFontText);
}

static void crypto_view_action_info(CryptoView* view) {
    if(!crypto_view_open_session(view, "Chip Info")) {
        return;
    }

    uint8_t revision[INFO_SIZE] = {0};
    uint8_t serial[32] = {0}; // Increased size to handle variable-length cert serial
    size_t serial_len = ATCA_SERIAL_NUM_SIZE;
    bool cert_method_used = false;

    // Try standard method first
    ATCA_STATUS serial_status = atcab_read_serial_number(serial);

    // If standard method fails, try certificate-based retrieval
    if(serial_status != ATCA_SUCCESS) {
        serial_status = crypto_view_read_serial_from_cert(serial, &serial_len);
        if(serial_status == ATCA_SUCCESS) {
            cert_method_used = true;
        }
    }

    ATCA_STATUS info_status = atcab_info(revision);

    char body[256]; // Increased to accommodate cert message

    if(serial_status != ATCA_SUCCESS && info_status != ATCA_SUCCESS) {
        // Config zone locked, no certificate, no accessible serial number
        snprintf(
            body,
            sizeof(body),
            "Config zone: LOCKED\n"
            "Info access: BLOCKED\n"
            "Serial number: BLOCKED\n"
            "No further info available");
        crypto_view_idle_session(view);
        crypto_view_show_textbox("Chip Info", body, TextBoxFontText);
        return;
    }

    if(info_status == ATCA_SUCCESS && serial_status == ATCA_SUCCESS) {
        // Full access - show all info
        // Step 4: Format and Display the Serial Number
        char serial_hex[128] = {0}; // Larger buffer for variable-length serial
        size_t hex_written = 0;
        for(size_t i = 0; i < serial_len && hex_written < sizeof(serial_hex) - 3; i++) {
            hex_written += snprintf(serial_hex + hex_written, sizeof(serial_hex) - hex_written, "%02X", serial[i]);
            if(i + 1 < serial_len) {
                serial_hex[hex_written++] = ' ';
            }
        }

        const char* device_text = "Unknown";
        if(revision[2] == 0x60U) {
            device_text = (revision[3] >= 0x03U) ? "ATECC608B" : "ATECC608";
        }

        if(cert_method_used) {
            snprintf(
                body,
                sizeof(body),
                "%s\nSN (slot):\n%s\nDevRev %02X %02X %02X %02X",
                device_text,
                serial_hex,
                revision[0],
                revision[1],
                revision[2],
                revision[3]);
        } else {
            snprintf(
                body,
                sizeof(body),
                "%s\nSN:\n%s\nDevRev %02X %02X %02X %02X",
                device_text,
                serial_hex,
                revision[0],
                revision[1],
                revision[2],
                revision[3]);
        }
    } else if(info_status == ATCA_SUCCESS) {
        // Info works but serial read failed
        const char* device_text = "Unknown";
        if(revision[2] == 0x60U) {
            device_text = (revision[3] >= 0x03U) ? "ATECC608B" : "ATECC608";
        }

        snprintf(
            body,
            sizeof(body),
            "%s\nSN: unavailable\nDevRev %02X %02X %02X %02X",
            device_text,
            revision[0],
            revision[1],
            revision[2],
            revision[3]);
    } else {
        // Serial works but info failed - unusual case (cert-based chip)
        // Step 4: Format and Display the Serial Number
        char serial_hex[128] = {0};
        size_t hex_written = 0;
        for(size_t i = 0; i < serial_len && hex_written < sizeof(serial_hex) - 3; i++) {
            hex_written += snprintf(serial_hex + hex_written, sizeof(serial_hex) - hex_written, "%02X", serial[i]);
            if(i + 1 < serial_len) {
                serial_hex[hex_written++] = ' ';
            }
        }

        if(cert_method_used) {
            snprintf(
                body,
                sizeof(body),
                "ATECC608B\nSN (slot):\n%s\nDevRev: restricted",
                serial_hex);
        } else {
            snprintf(body, sizeof(body), "ATECC608\nSN:\n%s\nDevRev: unavailable", serial_hex);
        }
    }

    crypto_view_idle_session(view);
    crypto_view_show_textbox("Chip Info", body, TextBoxFontText);
}

static void crypto_view_action_random(CryptoView* view) {
    if(!crypto_view_open_session(view, "Random")) {
        return;
    }

    uint8_t random_bytes[32] = {0};
    ATCA_STATUS status = atcab_random(random_bytes);
    if(status != ATCA_SUCCESS) {
        crypto_view_idle_session(view);
        crypto_view_show_status_error("Random", "Random command failed", status);
        return;
    }

    crypto_view_idle_session(view);

    // Format random bytes with newlines after every 7 bytes
    char hex_text[128] = {0};
    size_t offset = 0;
    for(size_t i = 0; i < sizeof(random_bytes) && offset < sizeof(hex_text) - 3; i++) {
        offset += snprintf(hex_text + offset, sizeof(hex_text) - offset, "%02X", random_bytes[i]);
        if(i + 1 < sizeof(random_bytes) && offset < sizeof(hex_text) - 1) {
            // Add newline after every 7 bytes, otherwise add space
            if((i + 1) % 7 == 0) {
                hex_text[offset++] = '\n';
            } else {
                hex_text[offset++] = ' ';
            }
        }
    }
    crypto_view_show_textbox("Random", hex_text, TextBoxFontHex);
}

static void crypto_view_action_self_test(CryptoView* view) {
    if(!crypto_view_open_session(view, "Self-Test")) {
        return;
    }

    uint8_t result = 0U;
    ATCA_STATUS status = atcab_selftest(SELFTEST_MODE_ALL, 0U, &result);
    crypto_view_idle_session(view);

    if(status != ATCA_SUCCESS) {
        crypto_view_show_status_error("Self-Test", "Command failed", status);
        return;
    }

    if(result == 0U) {
        crypto_view_show_dialog("Self-Test", "All tests passed", false);
        return;
    }

    typedef struct {
        uint8_t mask;
        const char* label;
    } CryptoSelfTestResult;

    static const CryptoSelfTestResult failures[] = {
        {SELFTEST_MODE_RNG, "RNG"},
        {SELFTEST_MODE_ECDSA_SIGN_VERIFY, "ECDSA"},
        {SELFTEST_MODE_ECDH, "ECDH"},
        {SELFTEST_MODE_AES, "AES"},
        {SELFTEST_MODE_SHA, "SHA"},
    };

    char fail_text[96] = {0};
    size_t written = snprintf(fail_text, sizeof(fail_text), "Failures:\n");
    const size_t failure_count = sizeof(failures) / sizeof(failures[0]);
    for(size_t i = 0; i < failure_count; i++) {
        if((result & failures[i].mask) != 0U) {
            if(written < sizeof(fail_text)) {
                written += snprintf(
                    fail_text + written, sizeof(fail_text) - written, "%s\n", failures[i].label);
            }
        }
    }

    crypto_view_show_dialog("Self-Test", fail_text, true);
}

static void crypto_view_action_slot_peek(CryptoView* view) {
    if(!crypto_view_open_session(view, "Slot Peek")) {
        return;
    }

    const uint8_t slot = crypto_view_current_slot(view);
    uint8_t slot_data[ATCA_BLOCK_SIZE] = {0};
    const ATCA_STATUS status =
        atcab_read_zone(ATCA_ZONE_DATA, slot, 0U, 0U, slot_data, ATCA_BLOCK_SIZE);

    if(status != ATCA_SUCCESS) {
        crypto_view_idle_session(view);
        crypto_view_show_status_error("Slot Peek", "Read failed", status);
        return;
    }

    crypto_view_idle_session(view);

    // Format slot data with newlines after every 7 bytes
    char hex_text[128] = {0};
    size_t offset = 0;
    for(size_t i = 0; i < ATCA_BLOCK_SIZE && offset < sizeof(hex_text) - 3; i++) {
        offset += snprintf(hex_text + offset, sizeof(hex_text) - offset, "%02X", slot_data[i]);
        if(i + 1 < ATCA_BLOCK_SIZE && offset < sizeof(hex_text) - 1) {
            // Add newline after every 7 bytes, otherwise add space
            if((i + 1) % 7 == 0) {
                hex_text[offset++] = '\n';
            } else {
                hex_text[offset++] = ' ';
            }
        }
    }

    char header[32] = {0};
    snprintf(header, sizeof(header), "Slot %u block0", slot);
    crypto_view_show_textbox(header, hex_text, TextBoxFontHex);
}

static void crypto_view_action_sleep(CryptoView* view) {
    if(!view->session.is_active) {
        if(!crypto_session_begin(&view->session)) {
            crypto_view_show_dialog("Sleep Device", "Device not detected", true);
            return;
        }
    }

    crypto_session_end(&view->session, CryptoDeviceSleep);
    crypto_view_show_dialog("Sleep Device", "Device put to sleep", false);
}

CryptoView* crypto_view_alloc(void) {
    CryptoView* view = malloc(sizeof(CryptoView));
    furi_assert(view);
    crypto_session_init(&view->session);
    view->selected = CryptoActionDetect;
    view->slot_index = 0U;
    view->page = 0U;
    return view;
}

void crypto_view_free(CryptoView* view) {
    if(view == NULL) {
        return;
    }

    if(view->session.is_active) {
        crypto_session_end(&view->session, CryptoDeviceIdle);
    }

    free(view);
}

void crypto_view_enter(CryptoView* view) {
    furi_assert(view);
    view->selected = CryptoActionDetect;
    view->slot_index = 0U;
    view->page = 0U;
}

void crypto_view_exit(CryptoView* view) {
    furi_assert(view);
    if(view->session.is_active) {
        crypto_session_end(&view->session, CryptoDeviceIdle);
    }
}

void crypto_view_select_previous(CryptoView* view) {
    furi_assert(view);
    if(view->selected > 0) {
        view->selected = (CryptoViewAction)(view->selected - 1);
        view->page = view->selected / 4U;
    }
}

void crypto_view_select_next(CryptoView* view) {
    furi_assert(view);
    if(view->selected + 1 < CryptoActionCount) {
        view->selected = (CryptoViewAction)(view->selected + 1);
        view->page = view->selected / 4U;
    }
}

void crypto_view_adjust_slot(CryptoView* view, int8_t delta) {
    furi_assert(view);
    if(view->selected != CryptoActionSlotPeek) {
        return;
    }

    const size_t count = sizeof(crypto_safe_slots) / sizeof(crypto_safe_slots[0]);
    if(count == 0U) {
        return;
    }

    int32_t next = (int32_t)view->slot_index + delta;
    while(next < 0) {
        next += (int32_t)count;
    }
    next %= (int32_t)count;
    view->slot_index = (size_t)next;
}

uint8_t crypto_view_current_slot(const CryptoView* view) {
    furi_assert(view);
    const size_t count = sizeof(crypto_safe_slots) / sizeof(crypto_safe_slots[0]);
    if((count == 0U) || (view->slot_index >= count)) {
        return 0U;
    }
    return crypto_safe_slots[view->slot_index];
}

void crypto_view_execute_selected(CryptoView* view) {
    furi_assert(view);
    switch(view->selected) {
    case CryptoActionDetect:
        crypto_view_action_detect(view);
        break;
    case CryptoActionInfo:
        crypto_view_action_info(view);
        break;
    case CryptoActionRandom:
        crypto_view_action_random(view);
        break;
    case CryptoActionSelfTest:
        crypto_view_action_self_test(view);
        break;
    case CryptoActionSlotPeek:
        crypto_view_action_slot_peek(view);
        break;
    case CryptoActionSleep:
        crypto_view_action_sleep(view);
        break;
    default:
        break;
    }
}

void draw_crypto_view(Canvas* canvas, CryptoView* view) {
    furi_assert(canvas);
    furi_assert(view);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 6, 4, AlignLeft, AlignTop, "Crypto Actions");

    const uint8_t list_start_y = 14U;
    const uint8_t line_step = 10U;
    const uint8_t highlight_x = 4U;
    const uint8_t highlight_width = 120U;
    const uint8_t highlight_height = 11U;
    const uint8_t text_x = 8U;
    const size_t items_per_page = 4U;

    const size_t page_start = view->page * items_per_page;
    const size_t page_end = (page_start + items_per_page < CryptoActionCount) ?
                                (page_start + items_per_page) :
                                CryptoActionCount;

    for(size_t i = page_start; i < page_end; i++) {
        const size_t display_index = i - page_start;
        const uint8_t y = list_start_y + (uint8_t)(display_index * line_step);

        char slot_label[24] = {0};
        const char* label = crypto_action_labels[i];
        if(i == CryptoActionSlotPeek) {
            snprintf(
                slot_label, sizeof(slot_label), "Slot Peek (S%u)", crypto_view_current_slot(view));
            label = slot_label;
        }

        if(view->selected == i) {
            canvas_draw_rbox(canvas, highlight_x, y - 2U, highlight_width, highlight_height, 3U);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str_aligned(canvas, text_x, y, AlignLeft, AlignTop, label);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str_aligned(canvas, text_x, y, AlignLeft, AlignTop, label);
        }
    }

    // Draw page indicators if multiple pages
    const size_t total_pages = (CryptoActionCount + items_per_page - 1U) / items_per_page;
    if(total_pages > 1U) {
        char page_text[8];
        snprintf(page_text, sizeof(page_text), "%zu/%zu", view->page + 1U, total_pages);
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignTop, page_text);
    }
}
