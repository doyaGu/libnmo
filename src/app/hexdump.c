#include "app/nmo_hexdump.h"

#include <stdint.h>

static const char *or_empty(const char *s) {
    return s ? s : "";
}

void nmo_hexdump_init_options(nmo_hexdump_options_t *options) {
    if (!options) {
        return;
    }

    options->bytes_per_line = 16;
    options->group_size = 8;
    options->indent_spaces = 0;
    options->show_ascii = true;
    options->show_final_offset = true;
    options->colorize = false;

    options->ansi.offset = "";
    options->ansi.hex = "";
    options->ansi.ascii = "";
    options->ansi.delim = "";
    options->ansi.reset = "";
}

static void print_indent(FILE *out, size_t spaces) {
    for (size_t i = 0; i < spaces; ++i) {
        fputc(' ', out);
    }
}

static void print_style(FILE *out, bool colorize, const char *code) {
    if (!out || !colorize) {
        return;
    }
    if (code && code[0]) {
        fputs(code, out);
    }
}

static void print_reset(FILE *out, bool colorize, const char *reset) {
    if (!out || !colorize) {
        return;
    }
    if (reset && reset[0]) {
        fputs(reset, out);
    }
}

static bool is_printable_ascii(uint8_t b) {
    return (b >= 0x20u && b <= 0x7Eu);
}

void nmo_hexdump_canonical(FILE *out,
                           const void *data,
                           size_t size,
                           const nmo_hexdump_options_t *options)
{
    if (!out) {
        return;
    }

    nmo_hexdump_options_t defaults;
    if (!options) {
        nmo_hexdump_init_options(&defaults);
        options = &defaults;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    const size_t bytes_per_line = options->bytes_per_line ? options->bytes_per_line : 16;
    const size_t group_size = options->group_size;
    const bool colorize = options->colorize;

    const char *offset_style = or_empty(options->ansi.offset);
    const char *hex_style = or_empty(options->ansi.hex);
    const char *ascii_style = or_empty(options->ansi.ascii);
    const char *delim_style = or_empty(options->ansi.delim);
    const char *reset_style = or_empty(options->ansi.reset);

    if (!bytes || size == 0) {
        if (options->show_final_offset) {
            print_style(out, colorize, offset_style);
            fprintf(out, "%08zx\n", (size_t)0);
            print_reset(out, colorize, reset_style);
        }
        return;
    }

    for (size_t offset = 0; offset < size; offset += bytes_per_line) {
        const size_t line_bytes = (offset + bytes_per_line <= size) ? bytes_per_line : (size - offset);

        print_indent(out, options->indent_spaces);

        /* Offset */
        print_style(out, colorize, offset_style);
        fprintf(out, "%08zx", offset);
        print_reset(out, colorize, reset_style);

        fputs("  ", out);

        /* Hex bytes */
        print_style(out, colorize, hex_style);
        for (size_t i = 0; i < bytes_per_line; ++i) {
            if (i < line_bytes) {
                fprintf(out, "%02x ", (unsigned int)bytes[offset + i]);
            } else {
                fputs("   ", out);
            }

            if (group_size && ((i + 1) % group_size) == 0 && (i + 1) < bytes_per_line) {
                fputc(' ', out);
            }
        }
        print_reset(out, colorize, reset_style);

        if (options->show_ascii) {
            /* Delimiter + ASCII */
            print_style(out, colorize, delim_style);
            fputs(" |", out);
            print_reset(out, colorize, reset_style);

            print_style(out, colorize, ascii_style);
            for (size_t i = 0; i < line_bytes; ++i) {
                const uint8_t b = bytes[offset + i];
                fputc(is_printable_ascii(b) ? (char)b : '.', out);
            }
            for (size_t i = line_bytes; i < bytes_per_line; ++i) {
                fputc(' ', out);
            }
            print_reset(out, colorize, reset_style);

            print_style(out, colorize, delim_style);
            fputs("|", out);
            print_reset(out, colorize, reset_style);
        }

        fputc('\n', out);
    }

    if (options->show_final_offset) {
        print_indent(out, options->indent_spaces);
        print_style(out, colorize, offset_style);
        fprintf(out, "%08zx\n", size);
        print_reset(out, colorize, reset_style);
    }
}
