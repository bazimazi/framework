// font_stroke.c: the built-in stroke font and text layout (SPEC §12.2, task 1A-21).
// Included by bmf.c after draw2d.c. See draw2d.h for the contract and README.md for the layout.
//
// Glyphs are polylines on an integer grid, authored below as text and parsed once into a
// segment table on the first bmf_draw_text. Every segment is drawn as one GLYPH instance (a
// capsule: round caps and joints, so polylines look smooth). Nothing here allocates.
//
// Grid (y up, baseline 0): the em (line height, the font size) is 64 units; caps and ascenders
// are 44 high, the lowercase x-height is 32, descenders reach -12. The line box spans y = 48
// (its top) to y = -16. Each glyph's points lie in x = 3..advance-3 and y = -12..45, so a stroke
// of radius up to 3 units (BMF_TEXT_MAX_CONTAINED_STROKE) keeps the ink inside the line box and
// the glyph's advance cell; the 6-unit side bearings are part of the advance. Digits share one
// advance (tabular), so changing numbers do not shift the text after them.

enum {
    FONT_STROKE_EM = 64,              // grid units per font size
    FONT_STROKE_BASELINE = 48,        // grid units from the line top down to the baseline
    FONT_STROKE_GLYPH_COUNT = 96,     // ASCII 32..126, then the replacement glyph
    FONT_STROKE_REPLACEMENT = 95,
    FONT_STROKE_MAX_SEGMENTS = 1024,
};

typedef struct font_stroke_glyph_source {
    u8 advance;              // grid units
    const char* polylines;   // "x,y x,y ...|x,y ...": points joined by segments; '|' lifts the pen;
                             // a polyline of one point is a dot
} font_stroke_glyph_source;

static const font_stroke_glyph_source FONT_STROKE_GLYPHS[FONT_STROKE_GLYPH_COUNT] = {
    {  20, "" },   // 0x20 space
    {  16, "8,44 8,14|8,0" },   // 0x21 !
    {  24, "6,44 6,32|18,44 18,32" },   // 0x22 "
    {  44, "16,2 16,42|28,2 28,42|6,14 38,14|6,30 38,30" },   // 0x23 #
    {  40, "33,34 30,38 26,40 20,41 15,40 10,38 7,35 6,31 7,27 10,24 15,22 20,21 25,20 30,18 33,15 34,11 33,7 30,4 25,2 20,1 14,2 10,4 7,8|20,-5 20,45" },   // 0x24 $
    {  44, "6,0 38,44|12,44 8,42 6,37 7,32 10,28 14,28 17,32 18,37 16,42 12,44|32,16 28,14 26,9 27,4 30,0 34,0 37,4 38,9 36,14 32,16" },   // 0x25 %
    {  44, "38,0 14,28 12,34 14,41 20,44 26,41 28,36 26,30 12,20 6,12 8,4 14,0 22,0 30,6 36,16" },   // 0x26 &
    {  12, "6,44 6,32" },   // 0x27 '
    {  22, "14,43 12,39 9,35 8,30 7,25 6,19 6,13 7,7 8,2 9,-3 12,-7 14,-11" },   // 0x28 (
    {  22, "8,43 10,39 13,35 14,30 15,25 16,19 16,13 15,7 14,2 13,-3 10,-7 8,-11" },   // 0x29 )
    {  32, "16,24 16,44|7,39 25,29|7,29 25,39" },   // 0x2A *
    {  40, "20,8 20,36|6,22 34,22" },   // 0x2B +
    {  16, "10,2 6,-8" },   // 0x2C ,
    {  32, "6,18 26,18" },   // 0x2D -
    {  12, "6,0" },   // 0x2E .
    {  36, "6,-2 30,45" },   // 0x2F /
    {  40, "20,44 16,43 13,41 10,38 8,33 6,28 6,22 6,16 8,11 10,6 13,3 16,1 20,0 24,1 27,3 30,6 32,11 34,16 34,22 34,28 32,33 30,38 27,41 24,43 20,44|12,10 28,34" },   // 0x30 0
    {  40, "10,36 20,44 20,0|10,0 30,0" },   // 0x31 1
    {  40, "7,35 10,39 14,43 19,44 25,43 29,41 33,36 34,31 33,26 31,21 6,0 34,0" },   // 0x32 2
    {  40, "9,39 12,42 17,44 23,44 28,42 31,39 33,34 32,30 30,26 25,23 20,22 26,21 30,18 33,14 34,10 32,6 28,2 23,0 17,0 12,2 8,6" },   // 0x33 3
    {  40, "26,0 26,44 6,12 34,12" },   // 0x34 4
    {  40, "32,44 10,44 9,25 10,26 15,29 20,30 25,29 29,26 33,21 34,16 33,10 31,5 27,2 21,0 16,1 11,3 8,8" },   // 0x35 5
    {  40, "6,14 7,8 11,4 16,1 21,0 27,2 31,6 34,11 34,17 31,22 27,26 21,28 16,27 11,24 7,20 6,14|6,14 6,19 7,25 9,30 11,34 14,38 17,41 20,43 24,44 28,44 31,43" },   // 0x36 6
    {  40, "6,44 34,44 14,0" },   // 0x37 7
    {  40, "20,22 26,23 30,27 32,32 31,37 28,41 23,44 17,44 12,41 9,37 8,32 10,27 14,23 20,22|20,22 14,21 10,18 7,14 6,10 8,5 12,2 17,0 23,0 28,2 32,5 34,10 33,14 30,18 26,21 20,22" },   // 0x38 8
    {  40, "34,30 33,36 29,40 24,43 19,44 13,42 9,38 6,33 6,27 9,22 13,18 19,16 24,17 29,20 33,24 34,30|34,30 34,25 33,19 31,14 29,10 26,6 23,3 20,1 16,0 12,0 9,1" },   // 0x39 9
    {  12, "6,0|6,24" },   // 0x3A :
    {  16, "10,24|10,2 6,-8" },   // 0x3B ;
    {  40, "34,40 6,22 34,4" },   // 0x3C <
    {  40, "6,14 34,14|6,30 34,30" },   // 0x3D =
    {  40, "6,40 34,22 6,4" },   // 0x3E >
    {  36, "7,36 10,41 15,44 21,44 26,41 29,36 30,31 28,25 24,21 18,20 18,12|18,0" },   // 0x3F ?
    {  52, "24,30 19,28 16,24 15,19 17,13 21,10 27,10 31,13 33,19 32,24 29,28 24,30|33,30 33,14 34,10 38,9 38,14 42,15 45,17 46,20 45,26 43,31 40,36 36,39 31,41 26,42 20,41 15,39 11,35 8,30 6,25 6,19 7,13 9,8 13,4 17,0 22,-2 27,-2 33,-1 37,2 41,6" },   // 0x40 @
    {  44, "6,0 22,44 38,0|11,14 33,14" },   // 0x41 A
    {  42, "6,22 22,22|6,0 6,44 22,44 27,43 31,39 32,33 31,28 27,23 22,22 6,22|22,22 23,22 29,21 33,18 36,13 36,9 33,4 29,1 23,0 6,0" },   // 0x42 B
    {  42, "34,36 31,40 27,43 23,44 19,44 15,41 11,38 8,33 7,28 6,22 7,16 8,11 11,6 15,3 19,0 23,0 27,1 31,4 34,8" },   // 0x43 C
    {  42, "6,0 6,44 16,44 21,43 26,41 30,38 33,33 35,28 36,22 35,16 33,11 30,6 26,3 21,1 16,0 6,0" },   // 0x44 D
    {  38, "32,44 6,44 6,0 32,0|6,22 26,22" },   // 0x45 E
    {  38, "32,44 6,44 6,0|6,22 26,22" },   // 0x46 F
    {  44, "34,36 31,40 27,43 23,44 19,44 15,42 11,38 9,34 7,29 6,23 6,17 8,12 10,7 14,3 17,1 22,0 26,1 30,3 33,6 36,11 37,16 38,22 24,22" },   // 0x47 G
    {  42, "6,0 6,44|36,0 36,44|6,22 36,22" },   // 0x48 H
    {  28, "14,0 14,44|6,44 22,44|6,0 22,0" },   // 0x49 I
    {  36, "30,44 30,12 29,7 25,3 21,0 15,0 11,3 7,7 6,12" },   // 0x4A J
    {  40, "6,0 6,44|34,44 6,16|16,26 34,0" },   // 0x4B K
    {  36, "6,44 6,0 30,0" },   // 0x4C L
    {  48, "6,0 6,44 24,14 42,44 42,0" },   // 0x4D M
    {  42, "6,0 6,44 36,0 36,44" },   // 0x4E N
    {  44, "22,44 18,43 14,41 11,38 8,33 7,28 6,22 7,16 8,11 11,6 14,3 18,1 22,0 26,1 30,3 33,6 36,11 37,16 38,22 37,28 36,33 33,38 30,41 26,43 22,44" },   // 0x4F O
    {  40, "6,0 6,44 22,44 27,43 31,39 34,35 34,29 31,25 27,21 22,20 6,20" },   // 0x50 P
    {  44, "22,44 18,43 14,41 11,38 8,33 7,28 6,22 7,16 8,11 11,6 14,3 18,1 22,0 26,1 30,3 33,6 36,11 37,16 38,22 37,28 36,33 33,38 30,41 26,43 22,44|26,10 38,-4" },   // 0x51 Q
    {  40, "6,0 6,44 22,44 27,43 31,39 34,35 34,29 31,25 27,21 22,20 6,20|20,20 34,0" },   // 0x52 R
    {  40, "33,37 30,40 26,43 20,44 15,43 10,41 7,37 6,33 7,29 10,25 15,23 20,22 25,21 30,19 33,15 34,11 33,7 30,3 25,1 20,0 14,1 10,4 7,7" },   // 0x53 S
    {  44, "6,44 38,44|22,44 22,0" },   // 0x54 T
    {  42, "6,44 6,15 7,9 10,4 15,1 21,0 27,1 32,4 35,9 36,15 36,44" },   // 0x55 U
    {  44, "6,44 22,0 38,44" },   // 0x56 V
    {  52, "6,44 16,0 26,30 36,0 46,44" },   // 0x57 W
    {  42, "6,0 36,44|6,44 36,0" },   // 0x58 X
    {  44, "6,44 22,22 38,44|22,22 22,0" },   // 0x59 Y
    {  40, "6,44 34,44 6,0 34,0" },   // 0x5A Z
    {  24, "18,45 6,45 6,-9 18,-9" },   // 0x5B [
    {  36, "6,45 30,-2" },   // 0x5C backslash
    {  24, "6,45 18,45 18,-9 6,-9" },   // 0x5D ]
    {  36, "6,28 18,44 30,28" },   // 0x5E ^
    {  40, "6,-8 34,-8" },   // 0x5F _
    {  18, "6,45 12,37" },   // 0x60 `
    {  38, "19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32|32,32 32,0" },   // 0x61 a
    {  38, "6,44 6,0|19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32" },   // 0x62 b
    {  36, "28,27 24,31 20,32 15,31 11,29 8,24 6,19 6,13 8,8 11,3 15,1 20,0 24,1 28,5" },   // 0x63 c
    {  38, "32,44 32,0|19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32" },   // 0x64 d
    {  38, "7,16 32,16 31,22 29,27 25,30 20,32 16,31 11,29 8,25 6,19 6,13 8,8 11,4 15,1 20,0 24,1 28,5" },   // 0x65 e
    {  34, "12,0 12,34 13,39 17,43 22,44 27,43 31,39|6,32 24,32" },   // 0x66 f
    {  38, "19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32|32,32 32,-2 31,-6 28,-9 24,-11 19,-12 14,-11 9,-9 7,-5" },   // 0x67 g
    {  36, "6,44 6,0|6,20 7,25 11,29 15,32 21,32 25,29 29,25 30,20 30,0" },   // 0x68 h
    {  16, "8,0 8,32|8,42" },   // 0x69 i
    {  24, "16,32 16,-4 15,-9 11,-12 6,-11 3,-8|16,42" },   // 0x6A j
    {  36, "6,44 6,0|30,32 6,12|14,19 30,0" },   // 0x6B k
    {  22, "8,44 8,6 10,2 14,0 16,0" },   // 0x6C l
    {  52, "6,32 6,0|6,22 7,27 11,31 16,32 21,31 25,27 26,22 26,0|26,22 27,27 31,31 36,32 41,31 45,27 46,22 46,0" },   // 0x6D m
    {  36, "6,32 6,0|6,20 7,25 11,29 15,32 21,32 25,29 29,25 30,20 30,0" },   // 0x6E n
    {  40, "20,32 15,31 11,28 7,23 6,17 7,12 9,6 13,2 17,0 23,0 27,2 31,6 33,12 34,17 33,23 29,28 25,31 20,32" },   // 0x6F o
    {  38, "6,32 6,-12|19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32" },   // 0x70 p
    {  38, "32,32 32,-12|19,32 14,31 10,28 7,23 6,17 6,12 9,6 12,2 17,0 21,0 26,2 29,6 32,12 32,17 31,23 28,28 24,31 19,32" },   // 0x71 q
    {  32, "6,32 6,0|6,18 7,24 11,28 16,31 21,32 27,30" },   // 0x72 r
    {  36, "29,27 26,30 21,32 15,32 10,30 7,27 6,23 8,19 12,17 18,16 24,15 28,13 30,9 29,5 26,2 21,0 15,0 10,2 7,5" },   // 0x73 s
    {  32, "12,42 12,8 14,3 18,0 23,0|6,32 24,32" },   // 0x74 t
    {  36, "6,32 6,12 7,7 11,3 15,0 21,0 25,3 29,7 30,12 30,32|30,32 30,0" },   // 0x75 u
    {  40, "6,32 20,0 34,32" },   // 0x76 v
    {  48, "6,32 14,0 24,24 34,0 42,32" },   // 0x77 w
    {  38, "6,0 32,32|6,32 32,0" },   // 0x78 x
    {  40, "6,32 18,0|34,32 12,-12" },   // 0x79 y
    {  36, "6,32 30,32 6,0 30,0" },   // 0x7A z
    {  26, "20,45 16,44 14,40 14,26 12,21 6,19 12,17 14,12 14,-4 16,-8 20,-9" },   // 0x7B {
    {  12, "6,45 6,-10" },   // 0x7C |
    {  26, "6,45 10,44 12,40 12,26 14,21 20,19 14,17 12,12 12,-4 10,-8 6,-9" },   // 0x7D }
    {  40, "6,18 9,23 14,25 19,23 21,21 26,19 31,21 34,26" },   // 0x7E ~
    {  36, "6,0 30,0 30,44 6,44 6,0" },   // replacement (any other code point)
};

typedef struct font_stroke_segment { i8 x0, y0, x1, y1; } font_stroke_segment;

static struct {
    b32 built;
    i32 segment_count;
    i16 first[FONT_STROKE_GLYPH_COUNT];   // index into segments
    i16 count[FONT_STROKE_GLYPH_COUNT];
    font_stroke_segment segments[FONT_STROKE_MAX_SEGMENTS];
} font_stroke;

// Malformed glyph data is a programmer error: panic naming the glyph and byte.
static __attribute__((noreturn)) void font_stroke_bad_data(i32 glyph, i32 at, const char* what) {
    static char message[128];
    bmf_fmt_buf(message, (i32)sizeof message, "font_stroke: glyph %i byte %i: %s", glyph, at, what);
    bmf_panic(message);
}

// Parses an optionally negative decimal integer at text[*at] (-128..127) and advances *at.
static i8 font_stroke_parse_coordinate(const char* text, i32* at, i32 glyph) {
    i32 i = *at;
    b32 negative = text[i] == '-';
    if (negative) i++;
    if (text[i] < '0' || text[i] > '9') font_stroke_bad_data(glyph, i, "expected a number");
    i32 value = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        if (value > 128) font_stroke_bad_data(glyph, i, "coordinate out of range");
        i++;
    }
    if (negative) value = -value;
    if (value > 127) font_stroke_bad_data(glyph, i, "coordinate out of range");
    *at = i;
    return (i8)value;
}

// Parses every glyph's polylines into the segment table. Malformed data is a programmer error:
// it panics with the glyph index. O(table size), once.
static void font_stroke_build(void) {
    i32 total = 0;
    for (i32 glyph = 0; glyph < FONT_STROKE_GLYPH_COUNT; glyph++) {
        const char* text = FONT_STROKE_GLYPHS[glyph].polylines;
        font_stroke.first[glyph] = (i16)total;
        i32 at = 0;
        while (text[at]) {
            // One polyline: points separated by ' ', ended by '|' or the end of the text.
            i8 previous_x = 0, previous_y = 0;
            i32 points = 0;
            for (;;) {
                i8 x = font_stroke_parse_coordinate(text, &at, glyph);
                if (text[at] != ',') font_stroke_bad_data(glyph, at, "expected ','");
                at++;
                i8 y = font_stroke_parse_coordinate(text, &at, glyph);
                if (points > 0) {
                    if (total == FONT_STROKE_MAX_SEGMENTS) bmf_panic("font_stroke: more than FONT_STROKE_MAX_SEGMENTS segments");
                    font_stroke.segments[total++] = (font_stroke_segment){ previous_x, previous_y, x, y };
                }
                previous_x = x;
                previous_y = y;
                points++;
                if (text[at] != ' ') break;
                at++;
            }
            if (points == 1) {   // a dot
                if (total == FONT_STROKE_MAX_SEGMENTS) bmf_panic("font_stroke: more than FONT_STROKE_MAX_SEGMENTS segments");
                font_stroke.segments[total++] = (font_stroke_segment){ previous_x, previous_y, previous_x, previous_y };
            }
            if (text[at] == '|') at++;
            else if (text[at]) font_stroke_bad_data(glyph, at, "expected ' ', '|' or the end");
        }
        font_stroke.count[glyph] = (i16)(total - font_stroke.first[glyph]);
    }
    font_stroke.segment_count = total;
    font_stroke.built = 1;
}

// The glyph of a code point: its own for 32..126, the replacement glyph otherwise.
static inline i32 font_stroke_glyph(u32 codepoint) {
    return codepoint >= 32u && codepoint <= 126u ? (i32)codepoint - 32 : FONT_STROKE_REPLACEMENT;
}

// The advance sum of one line (no '\n' inside) in grid units; '\r' advances nothing.
static i32 font_stroke_line_units(bmf_str line) {
    i32 units = 0;
    for (i32 at = 0; at < line.len;) {
        u32 codepoint;
        at += bmf_utf8_decode(line, at, &codepoint);
        if (codepoint == '\r') continue;
        units += FONT_STROKE_GLYPHS[font_stroke_glyph(codepoint)].advance;
    }
    return units;
}

bmf_v2 bmf_text_measure(f32 size, bmf_str text) {
    BMF_ASSERT(size > 0.0f && "bmf_text_measure: size must be > 0");
    i32 widest = 0, lines = 1;
    for (i32 at = 0;;) {
        i32 end = bmf_str_find_byte(text, '\n', at);
        i32 units = font_stroke_line_units(bmf_str_slice(text, at, end < 0 ? text.len : end));
        if (units > widest) widest = units;
        if (end < 0) break;
        at = end + 1;
        lines++;
    }
    f32 unit = size * (1.0f / (f32)FONT_STROKE_EM);
    return bmf_v2_make((f32)widest * unit, (f32)lines * size);
}

void bmf_draw_text(i32 layer, bmf_v2 position, f32 size, f32 stroke_width, bmf_color color, bmf_text_align align, bmf_str text) {
    BMF_ASSERT(draw2d.frame && "bmf_draw_text: outside a frame (bmf_draw2d_begin_frame)");
    BMF_ASSERT(layer >= 0 && layer < BMF_DRAW2D_MAX_LAYERS && "bmf_draw_text: layer must be 0..255");
    BMF_ASSERT(size > 0.0f && "bmf_draw_text: size must be > 0");
    BMF_ASSERT(stroke_width >= 0.0f && "bmf_draw_text: stroke_width must be >= 0");
    BMF_ASSERT((align == BMF_TEXT_ALIGN_LEFT || align == BMF_TEXT_ALIGN_CENTER || align == BMF_TEXT_ALIGN_RIGHT) &&
               "bmf_draw_text: unknown align");
    if (!font_stroke.built) font_stroke_build();

    // Text space: x right, y down the screen, in grid units from `position`. Its axes in target
    // pixels follow from the layer's camera transform: "down the screen" is +y in a SCREEN
    // camera's space (the map to y-down pixels keeps orientation, determinant > 0) and -y in a
    // WORLD camera's space (determinant < 0).
    const draw2d_transform* t = &draw2d.layer_transforms[layer];
    bmf_m3x2 m = t->to_pixels;
    f32 unit = size * (1.0f / (f32)FONT_STROKE_EM);
    f32 determinant = bmf_m3x2_determinant(m);
    bmf_v2 right = bmf_v2_scale(m.x_axis, unit);
    bmf_v2 down = bmf_v2_scale(m.y_axis, determinant > 0.0f ? unit : -unit);
    bmf_v2 origin = bmf_m3x2_apply_point(m, position);
    f32 thickness = (stroke_width > 0.0f ? stroke_width : size * BMF_TEXT_DEFAULT_STROKE) * t->scale;
    u32 white = draw2d.white.id;

    i32 line = 0;
    for (i32 at = 0;;) {
        i32 end = bmf_str_find_byte(text, '\n', at);
        bmf_str line_text = bmf_str_slice(text, at, end < 0 ? text.len : end);
        f32 start = 0.0f;
        if (align != BMF_TEXT_ALIGN_LEFT) {
            i32 width = font_stroke_line_units(line_text);
            start = align == BMF_TEXT_ALIGN_CENTER ? (f32)width * -0.5f : -(f32)width;
        }
        f32 baseline = (f32)(line * FONT_STROKE_EM + FONT_STROKE_BASELINE);
        i32 pen = 0;
        for (i32 i = 0; i < line_text.len;) {
            u32 codepoint;
            i += bmf_utf8_decode(line_text, i, &codepoint);
            if (codepoint == '\r') continue;
            i32 glyph = font_stroke_glyph(codepoint);
            f32 pen_x = start + (f32)pen;
            const font_stroke_segment* segments = &font_stroke.segments[font_stroke.first[glyph]];
            for (i32 s = 0; s < font_stroke.count[glyph]; s++) {
                bmf_draw_instance* d = draw2d_push(layer, white);
                if (!d) continue;
                bmf_v2 a = bmf_v2_add_scaled(bmf_v2_add_scaled(origin, right, pen_x + (f32)segments[s].x0), down, baseline - (f32)segments[s].y0);
                bmf_v2 b = bmf_v2_add_scaled(bmf_v2_add_scaled(origin, right, pen_x + (f32)segments[s].x1), down, baseline - (f32)segments[s].y1);
                d->position = a;
                d->size = (bmf_v2){ 0.0f, thickness };
                d->rotation = 0.0f;
                d->color = color;
                d->extra = (bmf_v4){ b.x, b.y, 0.0f, 0.0f };
                d->kind_corner = BMF_DRAW_KIND_GLYPH;
                d->stroke_softness = 0;
            }
            pen += FONT_STROKE_GLYPHS[glyph].advance;
        }
        if (end < 0) break;
        at = end + 1;
        line++;
    }
}
