# 第三方素材与字体

本项目代码（除非另有说明）采用 MIT。下面这些素材来自别处，各自的许可在这里说清楚。

## 天气图标 `dsh_badge/wi_icons.h`

- 原始 SVG：**Weather Icons** by Erik Flowers — https://github.com/erikflowers/weather-icons
- 经由：lmarzen/esp32-weather-epd 的 `icons/svg/`（该仓库代码为 GPL-3.0，但其 `icons/svg/` 下的 svg **保留各自原始许可**，见该仓库 `icons/README`）
- 许可：**SIL Open Font License 1.1** — http://scripts.sil.org/OFL
- 本项目改动：从 SVG 转成 48×48 4bpp 位图，按 alpha 混合到实际背景。

## 蓝鲸 `dsh_badge/deepy.h`

- 出处：**ccch1mneyyy/dsh-TUI** — https://github.com/ccch1mneyyy/dsh-TUI 的 `assets/deepy`
- 许可：**MIT** — Copyright (c) 2026, chimney (ccch1mneyyy)
- 本项目改动：压成 42×30 调色板位图，4 组共 17 帧。

## 字体

`dsh_badge/ui_fonts.h` 由 `tools/build_ui_fonts.py` 渲染生成，字体为：

- **Inter** — © The Inter Project Authors，**SIL Open Font License 1.1**
- **Noto Sans SC** — © The Noto Project Authors，**SIL Open Font License 1.1**

字体文件本身不随仓库分发；需要重新生成时按 `tools/build_ui_fonts.py` 顶部注释里的命令下载。
生成的位图作为 OFL 字体的产物，同样在 OFL 下分发。

## 库

- **LovyanGFX** — MIT
- Arduino-ESP32 core — LGPL-2.1
