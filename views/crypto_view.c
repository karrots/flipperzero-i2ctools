#include "crypto_view.h"

#include "../lib/cryptoauthlib/lib/atca_basic.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../crypto/crypto_service.h"
#include "../lib/cryptoauthlib/lib/cryptoauthlib.h"
#include "../lib/cryptoauthlib/lib/calib/calib_basic.h"
#include "../lib/cryptoauthlib/lib/calib/calib_command.h"

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
    CryptoSession session;
};

static const uint8_t crypto_safe_slots[] = {8U, 9U, 10U, 11U};

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
    dialog_message_set_text(message, text, 64, 32, align, valign);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void crypto_view_show_status_error(const char* header, const char* context, ATCA_STATUS status) {
    char buffer[96];
    if(status == ATCA_SUCCESS) {
        snprintf(buffer, sizeof(buffer), "%s", context);
    } else {
        snprintf(buffer, sizeof(buffer), "%s\nStatus 0x%02X", context, (uint8_t)status);
    }
    crypto_view_show_dialog(header, buffer, true);
}

static void crypto_view_format_rows(
    char* out,
    size_t out_size,
    const uint8_t* data,
    size_t data_size,
    size_t per_row) {
    if((out == NULL) || (out_size == 0) || (data == NULL) || (data_size == 0) || (per_row == 0)) {
        return;
    }

    size_t written = 0;
    for(size_t i = 0; i < data_size; i++) {
        if(written + 3 >= out_size) {
            break;
        }

        written += snprintf(out + written, out_size - written, "%02X", data[i]);

        if(i + 1 == data_size) {
            break;
        }

        if(((i + 1U) % per_row) == 0U) {
            out[written++] = '\n';
        } else {
            out[written++] = ' ';
        }
    }

    if(written < out_size) {
        out[written] = '\0';
    } else {
        out[out_size - 1U] = '\0';
    }
}

static bool crypto_view_open_session(CryptoView* view, const char* header) {
    furi_assert(view);
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

static void crypto_view_action_detect(CryptoView* view) {
    if(!crypto_view_open_session(view, "Detect Device")) {
        return;
    }

    uint8_t revision[INFO_SIZE] = {0};
    ATCA_STATUS status = atcab_info(revision);
    if(status != ATCA_SUCCESS) {
        crypto_view_idle_session(view);
        crypto_view_show_status_error("Detect Device", "Info command failed", status);
        return;
    }

    const char* device_text = "Unknown";
    if(revision[2] == 0x60U) {
        device_text = (revision[3] >= 0x03U) ? "ATECC608B" : "ATECC608";
    }

    char body[96];
    snprintf(
        body,
        sizeof(body),
        "%s detected\nDevRev %02X %02X %02X %02X",
        device_text,
        revision[0],
        revision[1],
        revision[2],
        revision[3]);

    crypto_view_idle_session(view);
    crypto_view_show_dialog("Detect Device", body, true);
}

static void crypto_view_action_info(CryptoView* view) {
    if(!crypto_view_open_session(view, "Chip Info")) {
        return;
    }

    uint8_t revision[INFO_SIZE] = {0};
    uint8_t serial[ATCA_SERIAL_NUM_SIZE] = {0};

    ATCA_STATUS status = atcab_read_serial_number(serial);
    if(status != ATCA_SUCCESS) {
        crypto_view_idle_session(view);
        crypto_view_show_status_error("Chip Info", "Serial read failed", status);
        return;
    }

    status = atcab_info(revision);
    if(status != ATCA_SUCCESS) {
        crypto_view_idle_session(view);
        crypto_view_show_status_error("Chip Info", "Info command failed", status);
        return;
    }

    char serial_hex[3U * ATCA_SERIAL_NUM_SIZE] = {0};
    crypto_view_format_rows(serial_hex, sizeof(serial_hex), serial, ATCA_SERIAL_NUM_SIZE, ATCA_SERIAL_NUM_SIZE);

    const char* device_text = "Unknown";
    if(revision[2] == 0x60U) {
        device_text = (revision[3] >= 0x03U) ? "ATECC608B" : "ATECC608";
    }

    char body[192];
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

    crypto_view_idle_session(view);
    crypto_view_show_dialog("Chip Info", body, true);
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

    char data_text[3U * sizeof(random_bytes)] = {0};
    crypto_view_format_rows(data_text, sizeof(data_text), random_bytes, sizeof(random_bytes), 8U);

    crypto_view_idle_session(view);
    crypto_view_show_dialog("Random", data_text, true);
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
                written += snprintf(fail_text + written, sizeof(fail_text) - written, "%s\n", failures[i].label);
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

    char data_text[3U * ATCA_BLOCK_SIZE] = {0};
    crypto_view_format_rows(data_text, sizeof(data_text), slot_data, ATCA_BLOCK_SIZE, 8U);

    crypto_view_idle_session(view);

    char body[sizeof(data_text) + 32U];
    snprintf(body, sizeof(body), "Slot %u block0:\n%s", slot, data_text);
    crypto_view_show_dialog("Slot Peek", body, true);

    view->slot_index = (view->slot_index + 1U) % (sizeof(crypto_safe_slots) / sizeof(crypto_safe_slots[0]));
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
    }
}

void crypto_view_select_next(CryptoView* view) {
    furi_assert(view);
    if(view->selected + 1 < CryptoActionCount) {
        view->selected = (CryptoViewAction)(view->selected + 1);
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
    const uint8_t line_step = 8U;
    const uint8_t highlight_x = 4U;
    const uint8_t highlight_width = 120U;
    const uint8_t highlight_height = 11U;
    const uint8_t text_x = 8U;

    for(size_t i = 0; i < CryptoActionCount; i++) {
        const uint8_t y = list_start_y + (uint8_t)(i * line_step);

        char slot_label[24] = {0};
        const char* label = crypto_action_labels[i];
        if(i == CryptoActionSlotPeek) {
            snprintf(slot_label, sizeof(slot_label), "Slot Peek (S%u)", crypto_view_current_slot(view));
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
}
