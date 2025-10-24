#include "config_view.h"
#include <stdio.h>
#include <gui/elements.h>

void draw_config_view(Canvas* canvas, i2cSniffer* i2c_sniffer) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 3);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignTop, "Config");

    const char* current = i2c_sniffer_log_format_name(i2c_sniffer->log_format);
    char value_buffer[32];
    snprintf(value_buffer, sizeof(value_buffer), "< %s >", current);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 8, 26, AlignLeft, AlignTop, "Log format");

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 92, 24, AlignCenter, AlignTop, value_buffer);

    elements_button_left(canvas, "Back");
    elements_button_right(canvas, "Change");
}
