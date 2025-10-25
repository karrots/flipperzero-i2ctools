#include "sender_view.h"

void draw_sender_view(Canvas* canvas, i2cSender* i2c_sender) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 3);

    if(!i2c_sender->scanner->scanned) {
        scan_i2c_bus(i2c_sender->scanner);
    }

    canvas_set_font(canvas, FontSecondary);
    if(i2c_sender->scanner->nb_found <= 0) {
        canvas_draw_str_aligned(canvas, 20, 5, AlignLeft, AlignTop, "No peripherals found");
        return;
    }

    // Send Button
    canvas_draw_rbox(canvas, 45, 48, 45, 13, 3);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_icon(canvas, 50, 50, &I_Ok_btn_9x9);
    canvas_draw_str_aligned(canvas, 62, 51, AlignLeft, AlignTop, "Send");
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str_aligned(canvas, 3, 58, AlignLeft, AlignTop, "Hold OK: change");

    const char* focus_hint = NULL;
    switch(i2c_sender->focus) {
    case I2C_Sender_FocusValue:
        focus_hint = "Up/Down: value";
        break;
    case I2C_Sender_FocusLength:
        focus_hint = "Up/Down: length";
        break;
    case I2C_Sender_FocusResult:
    default:
        focus_hint = "Up/Down: scroll";
        break;
    }

    canvas_draw_str_aligned(canvas, 78, 58, AlignLeft, AlignTop, focus_hint);

    // Addr
    canvas_draw_str_aligned(canvas, 3, 5, AlignLeft, AlignTop, "Addr: ");
    canvas_draw_icon(canvas, 33, 5, &I_ButtonLeft_4x7);
    canvas_draw_icon(canvas, 68, 5, &I_ButtonRight_4x7);
    char addr_text[16];
    snprintf(
        addr_text,
        sizeof(addr_text),
        "0x%02x",
        (int)i2c_sender->scanner->addresses[i2c_sender->address_idx]);
    canvas_draw_str_aligned(canvas, 43, 5, AlignLeft, AlignTop, addr_text);

    // Value
    canvas_draw_str_aligned(canvas, 3, 15, AlignLeft, AlignTop, "Value: ");
    if(i2c_sender->focus == I2C_Sender_FocusValue) {
        canvas_draw_icon(canvas, 33, 17, &I_ButtonUp_7x4);
        canvas_draw_icon(canvas, 68, 17, &I_ButtonDown_7x4);
    }
    snprintf(addr_text, sizeof(addr_text), "0x%02x", (int)i2c_sender->value);
    canvas_draw_str_aligned(canvas, 43, 15, AlignLeft, AlignTop, addr_text);

    // Read length
    canvas_draw_str_aligned(canvas, 3, 25, AlignLeft, AlignTop, "Read: ");
    if(i2c_sender->focus == I2C_Sender_FocusLength) {
        canvas_draw_icon(canvas, 33, 27, &I_ButtonUp_7x4);
        canvas_draw_icon(canvas, 68, 27, &I_ButtonDown_7x4);
    }
    snprintf(addr_text, sizeof(addr_text), "%u", (unsigned)i2c_sender->requested_len);
    canvas_draw_str_aligned(canvas, 43, 25, AlignLeft, AlignTop, addr_text);
    canvas_draw_str_aligned(canvas, 78, 25, AlignLeft, AlignTop, "bytes");

    if(i2c_sender->must_send) {
        i2c_send(i2c_sender);
    }

    // Result
    canvas_draw_str_aligned(canvas, 3, 35, AlignLeft, AlignTop, "Result: ");
    if(i2c_sender->sended) {
        if(i2c_sender->error) {
            canvas_draw_str_aligned(canvas, 43, 35, AlignLeft, AlignTop, "I2C error");
            return;
        }

        if(i2c_sender->recv_len == 0) {
            canvas_draw_str_aligned(canvas, 43, 35, AlignLeft, AlignTop, "No data");
            return;
        }

        snprintf(addr_text, sizeof(addr_text), "%uB", (unsigned)i2c_sender->recv_len);
        canvas_draw_str_aligned(canvas, 78, 35, AlignLeft, AlignTop, addr_text);

        const uint8_t bytes_per_row = 8;
        const uint8_t column_width = 13;
        const uint8_t row_height = 9;
        const uint8_t x_min = 3;
        const uint8_t y_min = 35;
        const uint8_t visible_rows = 4;

        uint8_t total_rows = (i2c_sender->recv_len + (bytes_per_row - 1)) / bytes_per_row;
        uint8_t max_row_offset = 0;
        if(total_rows > visible_rows) {
            max_row_offset = total_rows - visible_rows;
        }
        if(i2c_sender->result_row_offset > max_row_offset) {
            i2c_sender->result_row_offset = max_row_offset;
        }

        uint8_t start_index = i2c_sender->result_row_offset * bytes_per_row;
        uint8_t end_index = i2c_sender->recv_len;
        uint8_t current_index = start_index;

        for(uint8_t row = 0; row < visible_rows && current_index < end_index; row++) {
            for(uint8_t column = 0; column < bytes_per_row && current_index < end_index; column++) {
                uint8_t x_pos = x_min + column * column_width;
                uint8_t y_pos = y_min + row * row_height;
                snprintf(addr_text, sizeof(addr_text), "%02X", i2c_sender->recv[current_index]);
                canvas_draw_str_aligned(canvas, x_pos, y_pos, AlignLeft, AlignTop, addr_text);
                current_index++;
            }
        }

        if(total_rows > visible_rows) {
            uint8_t first_row = i2c_sender->result_row_offset + 1;
            uint8_t last_row = i2c_sender->result_row_offset + visible_rows;
            if(last_row > total_rows) {
                last_row = total_rows;
            }
            snprintf(addr_text, sizeof(addr_text), "%u-%u/%u", first_row, last_row, total_rows);
            canvas_draw_str_aligned(canvas, 100, 35, AlignLeft, AlignTop, addr_text);
            if(i2c_sender->focus == I2C_Sender_FocusResult) {
                canvas_draw_icon(canvas, 112, 46, &I_ButtonUp_7x4);
                canvas_draw_icon(canvas, 112, 56, &I_ButtonDown_7x4);
            }
        }
    }
}
