# UI font source

`Montserrat-Medium.ttf` is copied from the repository's pinned LVGL dependency at
`.pio/libdeps/esp32-s3-touch-lcd-7b/lvgl/scripts/built_in_font/Montserrat-Medium.ttf`.
It is distributed under the SIL Open Font License; see [OFL.txt](OFL.txt).

Regenerate the two checked-in LVGL C fonts with Node.js and `lv_font_conv` 1.5.3:

```sh
npm install --global lv_font_conv@1.5.3
./tools/generate_ui_fonts.sh
```

The generator selects Cyrillic (U+0400–U+04FF), Latin Extended-A
(U+0100–U+017F), and the middle-dot glyph (U+00B7), uses 4-bit glyphs, and
keeps compression and prefiltering off to match this firmware's LVGL configuration. ASCII remains in the built-in
Montserrat fallback. Do not hand-edit the generated C glyph data.
