# LCD image assets

Put source LCD artwork here, then generate LVGL C assets into `generated/`.

GammaHex uses a 320x170 ST7789 display. For a full-screen background, use:

```sh
python3 tools/lvgl_image.py main/images/gammahex_bg.png \
    --output main/images/generated/gammahex_bg.c \
    --symbol gammahex_bg \
    --size 320x170 \
    --fit cover \
    --byte-order big
```

Then add the generated C file to `main/CMakeLists.txt` as:

```cmake
"images/generated/gammahex_bg.c"
```
