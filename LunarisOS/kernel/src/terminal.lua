packed struct terminal_state
    framebuffer: ptr(u32)
    width: u64
    height: u64
    pitch: u64
    cursor_x: u64
    cursor_y: u64
    foreground: u64
    background: u64
    scale: u64
    glyph_width: u64
    glyph_height: u64
    line_height: u64
    tab_width: u64
end

data terminal_state: terminal_state = 0, 0, 0, 0, 0, 0, 0x00ffffff, 0x00000000, 1, 16, 32, 32, 4
data terminal_parse_index: u64 = 0

require "terminal_font"

function terminal_make_color(red: u64, green: u64, blue: u64)
    return blue + green * 256 + red * 65536
end

function terminal_set_cursor(x: u64, y: u64)
    terminal_state.cursor_x = x
    terminal_state.cursor_y = y
end

function terminal_set_rgb(red: u64, green: u64, blue: u64)
    terminal_state.foreground = terminal_make_color(red, green, blue)
end

function terminal_set_bg_rgb(red: u64, green: u64, blue: u64)
    terminal_state.background = terminal_make_color(red, green, blue)
end

function terminal_reset_style()
    terminal_state.foreground = 0x00ffffff
    terminal_state.background = 0x00000000
end

function terminal_pixel(x: u64, y: u64, color: u64)
    if x < terminal_state.width then
        if y < terminal_state.height then
            terminal_state.framebuffer[y * terminal_state.pitch + x] = color
        end
    end
end

function terminal_fill_rect(x: u64, y: u64, width: u64, height: u64, color: u64)
    local row: u64 = 0
    while row < height do
        local column: u64 = 0
        while column < width do
            terminal_pixel(x + column, y + row, color)
            column = column + 1
        end
        row = row + 1
    end
end

function terminal_clear()
    local row: u64 = 0
    while row < terminal_state.height do
        local column: u64 = 0
        while column < terminal_state.width do
            terminal_state.framebuffer[row * terminal_state.pitch + column] = terminal_state.background
            column = column + 1
        end
        row = row + 1
    end
end

function terminal_scroll()
    local row: u64 = 0
    while row + terminal_state.line_height < terminal_state.height do
        local column: u64 = 0
        while column < terminal_state.width do
            terminal_state.framebuffer[row * terminal_state.pitch + column] = terminal_state.framebuffer[(row + terminal_state.line_height) * terminal_state.pitch + column]
            column = column + 1
        end
        row = row + 1
    end

    while row < terminal_state.height do
        local column: u64 = 0
        while column < terminal_state.width do
            terminal_state.framebuffer[row * terminal_state.pitch + column] = terminal_state.background
            column = column + 1
        end
        row = row + 1
    end
end

function terminal_newline()
    terminal_state.cursor_x = 0
    terminal_state.cursor_y = terminal_state.cursor_y + terminal_state.line_height
    if terminal_state.cursor_y + terminal_state.line_height > terminal_state.height then
        terminal_scroll()
        if terminal_state.cursor_y >= terminal_state.line_height then
            terminal_state.cursor_y = terminal_state.cursor_y - terminal_state.line_height
        end
    end
end

function terminal_glyph_index(ch: u64)
    if ch < 32 then
        return 31
    end
    if ch > 126 then
        return 31
    end
    return ch - 32
end

function terminal_draw_row_mask(x: u64, y: u64, row_mask: u64, color: u64)
    local active: u64 = row_mask
    local column: u64 = 0

    if active >= 32768 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 32768
    end
    column = column + 1
    if active >= 16384 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 16384
    end
    column = column + 1
    if active >= 8192 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 8192
    end
    column = column + 1
    if active >= 4096 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 4096
    end
    column = column + 1
    if active >= 2048 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 2048
    end
    column = column + 1
    if active >= 1024 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 1024
    end
    column = column + 1
    if active >= 512 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 512
    end
    column = column + 1
    if active >= 256 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 256
    end
    column = column + 1
    if active >= 128 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 128
    end
    column = column + 1
    if active >= 64 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 64
    end
    column = column + 1
    if active >= 32 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 32
    end
    column = column + 1
    if active >= 16 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 16
    end
    column = column + 1
    if active >= 8 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 8
    end
    column = column + 1
    if active >= 4 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 4
    end
    column = column + 1
    if active >= 2 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
        active = active - 2
    end
    column = column + 1
    if active >= 1 then
        terminal_fill_rect(x + column * terminal_state.scale, y, terminal_state.scale, terminal_state.scale, color)
    end
end

function terminal_draw_glyph_chunk(x: u64, y: u64, rows: u64, color: u64)
    local row_word: u64 = rows
    local row: u64 = 0

    while row < 4 do
        local row_mask: u64 = row_word - (row_word / 65536) * 65536
        row_word = row_word / 65536
        terminal_draw_row_mask(x, y + row * terminal_state.scale, row_mask, color)
        row = row + 1
    end
end

function terminal_draw_glyph(ch: u64)
    local glyph_index: u64 = terminal_glyph_index(ch)
    local glyph_base: u64 = glyph_index * 8
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 0 * terminal_state.scale, terminal_font_word(glyph_base + 0), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 4 * terminal_state.scale, terminal_font_word(glyph_base + 1), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 8 * terminal_state.scale, terminal_font_word(glyph_base + 2), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 12 * terminal_state.scale, terminal_font_word(glyph_base + 3), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 16 * terminal_state.scale, terminal_font_word(glyph_base + 4), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 20 * terminal_state.scale, terminal_font_word(glyph_base + 5), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 24 * terminal_state.scale, terminal_font_word(glyph_base + 6), terminal_state.foreground)
    terminal_draw_glyph_chunk(terminal_state.cursor_x, terminal_state.cursor_y + 28 * terminal_state.scale, terminal_font_word(glyph_base + 7), terminal_state.foreground)
end

function terminal_write_char(ch: u64)
    if ch == 10 then
        terminal_newline()
    end
    if ch == 13 then
        terminal_state.cursor_x = 0
    end
    if ch == 9 then
        terminal_write_char(32)
        terminal_write_char(32)
        terminal_write_char(32)
        terminal_write_char(32)
    end
    if ch == 8 then
        if terminal_state.cursor_x >= terminal_state.glyph_width * terminal_state.scale then
            terminal_state.cursor_x = terminal_state.cursor_x - terminal_state.glyph_width * terminal_state.scale
        end
    end
    if ch >= 32 then
        if terminal_state.cursor_x + terminal_state.glyph_width * terminal_state.scale > terminal_state.width then
            terminal_newline()
        end
        terminal_draw_glyph(ch)
        terminal_state.cursor_x = terminal_state.cursor_x + terminal_state.glyph_width * terminal_state.scale
    end
end

function terminal_read_number(text: ptr(u8))
    local value: u64 = 0
    while text[terminal_parse_index] >= 48 do
        if text[terminal_parse_index] <= 57 then
            value = value * 10 + (text[terminal_parse_index] - 48)
            terminal_parse_index = terminal_parse_index + 1
        end
        if text[terminal_parse_index] < 48 then
            return value
        end
        if text[terminal_parse_index] > 57 then
            return value
        end
    end
    return value
end

function terminal_set_cursor_from_cells(row: u64, column: u64)
    if row == 0 then
        row = 1
    end
    if column == 0 then
        column = 1
    end
    terminal_set_cursor((column - 1) * terminal_state.glyph_width * terminal_state.scale, (row - 1) * terminal_state.line_height)
end

function terminal_consume_escape(text: ptr(u8), index: u64)
    terminal_parse_index = index + 1
    if text[terminal_parse_index] ~= 91 then
        return terminal_parse_index
    end

    terminal_parse_index = terminal_parse_index + 1
    if text[terminal_parse_index] == 109 then
        terminal_reset_style()
        return terminal_parse_index
    end
    if text[terminal_parse_index] == 72 then
        terminal_set_cursor(0, 0)
        return terminal_parse_index
    end
    if text[terminal_parse_index] == 74 then
        terminal_clear()
        terminal_set_cursor(0, 0)
        return terminal_parse_index
    end

    if text[terminal_parse_index] == 48 then
        terminal_parse_index = terminal_parse_index + 1
        if text[terminal_parse_index] == 109 then
            terminal_reset_style()
            return terminal_parse_index
        end
        if text[terminal_parse_index] == 72 then
            terminal_set_cursor(0, 0)
            return terminal_parse_index
        end
        if text[terminal_parse_index] == 74 then
            terminal_clear()
            terminal_set_cursor(0, 0)
            return terminal_parse_index
        end
    end

    if text[terminal_parse_index] == 51 then
        terminal_parse_index = terminal_parse_index + 1
        if text[terminal_parse_index] == 56 then
            terminal_parse_index = terminal_parse_index + 1
            if text[terminal_parse_index] == 59 then
                terminal_parse_index = terminal_parse_index + 1
                if text[terminal_parse_index] == 50 then
                    terminal_parse_index = terminal_parse_index + 1
                    if text[terminal_parse_index] == 59 then
                        terminal_parse_index = terminal_parse_index + 1
                        local red: u64 = terminal_read_number(text)
                        if text[terminal_parse_index] == 59 then
                            terminal_parse_index = terminal_parse_index + 1
                            local green: u64 = terminal_read_number(text)
                            if text[terminal_parse_index] == 59 then
                                terminal_parse_index = terminal_parse_index + 1
                                local blue: u64 = terminal_read_number(text)
                                if text[terminal_parse_index] == 109 then
                                    terminal_set_rgb(red, green, blue)
                                    return terminal_parse_index
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    if text[terminal_parse_index] == 52 then
        terminal_parse_index = terminal_parse_index + 1
        if text[terminal_parse_index] == 56 then
            terminal_parse_index = terminal_parse_index + 1
            if text[terminal_parse_index] == 59 then
                terminal_parse_index = terminal_parse_index + 1
                if text[terminal_parse_index] == 50 then
                    terminal_parse_index = terminal_parse_index + 1
                    if text[terminal_parse_index] == 59 then
                        terminal_parse_index = terminal_parse_index + 1
                        local red: u64 = terminal_read_number(text)
                        if text[terminal_parse_index] == 59 then
                            terminal_parse_index = terminal_parse_index + 1
                            local green: u64 = terminal_read_number(text)
                            if text[terminal_parse_index] == 59 then
                                terminal_parse_index = terminal_parse_index + 1
                                local blue: u64 = terminal_read_number(text)
                                if text[terminal_parse_index] == 109 then
                                    terminal_set_bg_rgb(red, green, blue)
                                    return terminal_parse_index
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    local row: u64 = terminal_read_number(text)
    local column: u64 = 1
    if text[terminal_parse_index] == 59 then
        terminal_parse_index = terminal_parse_index + 1
        column = terminal_read_number(text)
    end
    if text[terminal_parse_index] == 72 then
        terminal_set_cursor_from_cells(row, column)
        return terminal_parse_index
    end

    return terminal_parse_index
end

function terminal_write_string(text: ptr(u8))
    local index: u64 = 0
    while text[index] ~= 0 do
        local ch: u64 = text[index]
        if ch == 27 then
            index = terminal_consume_escape(text, index)
        end
        if ch ~= 27 then
            terminal_write_char(ch)
        end
        index = index + 1
    end
end

function terminal_next_arg(index: u64, arg0: u64, arg1: u64, arg2: u64, arg3: u64, arg4: u64)
    if index == 0 then
        return arg0
    end
    if index == 1 then
        return arg1
    end
    if index == 2 then
        return arg2
    end
    if index == 3 then
        return arg3
    end
    if index == 4 then
        return arg4
    end
    return 0
end

function terminal_write_digit(value: u64)
    terminal_write_char(48 + value)
end

function terminal_write_unsigned(value: u64)
    local quotient: u64 = value / 10
    if quotient ~= 0 then
        terminal_write_unsigned(quotient)
    end
    terminal_write_digit(value - quotient * 10)
end

function terminal_write_signed(value: i64)
    if value < 0 then
        terminal_write_char(45)
        terminal_write_unsigned(0 - value)
    end
    if value >= 0 then
        terminal_write_unsigned(value)
    end
end

function terminal_write_hex_digit(value: u64)
    if value < 10 then
        terminal_write_char(48 + value)
    end
    if value >= 10 then
        terminal_write_char(87 + value)
    end
end

function terminal_write_hex(value: u64)
    local quotient: u64 = value / 16
    if quotient ~= 0 then
        terminal_write_hex(quotient)
    end
    terminal_write_hex_digit(value - quotient * 16)
end

function terminal_write_pointer(value: u64)
    terminal_write_char(48)
    terminal_write_char(120)
    if value == 0 then
        terminal_write_char(48)
    end
    if value ~= 0 then
        terminal_write_hex(value)
    end
end

function print(text: ptr(u8))
    terminal_write_string(text)
    terminal_newline()
end

function printf(format: ptr(u8), arg0: u64, arg1: u64, arg2: u64, arg3: u64, arg4: u64)
    local index: u64 = 0
    local arg_index: u64 = 0
    while format[index] ~= 0 do
        local ch: u64 = format[index]
        if ch == 27 then
            index = terminal_consume_escape(format, index)
        end
        if ch == 37 then
            index = index + 1
            local spec: u64 = format[index]
            if spec == 37 then
                terminal_write_char(37)
            end
            if spec == 115 then
                terminal_write_string(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 100 then
                terminal_write_signed(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 117 then
                terminal_write_unsigned(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 120 then
                terminal_write_hex(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 88 then
                terminal_write_hex(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 112 then
                terminal_write_pointer(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
            if spec == 99 then
                terminal_write_char(terminal_next_arg(arg_index, arg0, arg1, arg2, arg3, arg4))
                arg_index = arg_index + 1
            end
        end
        if ch ~= 27 then
            if ch ~= 37 then
                terminal_write_char(ch)
            end
        end
        index = index + 1
    end
end

function terminal_init(framebuffer: ptr(u32), width: u64, height: u64, pitch: u64)
    terminal_state.framebuffer = framebuffer
    terminal_state.width = width
    terminal_state.height = height
    terminal_state.pitch = pitch
    terminal_state.cursor_x = 0
    terminal_state.cursor_y = 0
    terminal_state.foreground = 0x00ffffff
    terminal_state.background = 0x00000000
    terminal_state.scale = 1
    terminal_state.glyph_width = 16
    terminal_state.glyph_height = 32
    terminal_state.line_height = 32
    terminal_state.tab_width = 4
    terminal_clear()
end