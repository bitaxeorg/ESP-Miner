# Naja Duo LCD assets

The Naja Duo uses a 320x170 ST7789 display. Source artwork is kept in this
directory and converted to LVGL C descriptors in `generated/`.

Generate the full-screen stats background with:

```sh
python3 tools/lvgl_image.py main/images/BitaxeBackground.jpg \
    --output main/images/generated/bitaxe_background.c \
    --symbol bitaxe_background \
    --size 320x170 \
    --fit cover \
    --byte-order big
```

Generate the splash logo with:

```sh
python3 tools/lvgl_image.py main/images/BitaxeLogoLarge.png \
    --output main/images/generated/bitaxe_logo_large.c \
    --symbol bitaxe_logo_large \
    --size 150x65 \
    --fit none \
    --byte-order little
```

Install the converter dependency with:

```sh
python3 -m pip install -r tools/requirements-image.txt
```
