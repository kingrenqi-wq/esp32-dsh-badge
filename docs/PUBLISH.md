# 发布清单 · 怎么把这两个库推出去

> 每一步都是「复制 → 粘贴 → 提交」，不需要写代码。

---

## 一、GitHub 仓库设置（5 分钟，最有效）

### 1.1 `esp32-dsh-badge`

**Topics**（Settings → 右上齿轮 "Topics"，逐个粘贴）：

```
esp32
esp32-s3
arduino
lovyangfx
st7789
desk-display
status-board
weather-station
system-monitor
iot
maker
```

**Description**：
```
ESP32-S3 desk status board for your Mac — weather, CPU/RAM, AI usage, spending, activity heatmaps. One serial protocol, 3 buttons, no cloud.
```

**Website**（可选）：留空，或填你发过的文章链接

### 1.2 `dsh-session-health`

已有 5 个 topics，补充这 3 个：

```
observability
debugging
developer-tools
```

**Description**：
```
Know when your AI session is about to run out of context, or when a tool keeps failing. Read-only, offline, DSH plugin.
```

---

## 二、README 首屏（本次已改）

已经做了：

- 顶部 4 个徽章（ESP32-S3 / LovyanGFX / MIT / no cloud）
- 首图从静态 PNG 换成 **6 帧动图 GIF**（191 KB，循环播放六页）
- 保留静态图链接

**为什么换动图**：README 首屏有动图的项目，点 star 概率明显更高——一眼能看到"这是什么东西在动"。

---

## 三、社区发布文案

### 3.1 V2EX（发在「分享创造」节点）

**标题**：
```
用 ESP32-S3 做了个桌面状态牌，显示 Claude/DeepSeek 的用量和 Mac 状态
```

**正文**：
```
前段时间在折腾 AI 编程，总想瞄一眼"现在跑到哪了、这个月花了多少"，
但每次切窗口打断思路，就做了个桌面小屏。

硬件：ESP32-S3 + 2.0 寸 ST7789（284×240），三个按键，一根 USB 线。
Mac 端跑个 Python 脚本，0.5 秒往串口写一行数据，板子负责渲染。

六个页面，每个键两页：
- 左键：天气 ↔ 近四周活跃热力图
- 中键：主界面（时钟/CPU/内存/余额/鲸鱼动画）↔ 今日概览
- 右键：用量趋势 ↔ 钱包（余额 + 还能撑几天）

几个自己比较满意的点：
1. 界面是 4bpp 抗锯齿子集字体，240 宽的屏也能看清中文
2. 一次传输一行 46 字段的协议，768 字节以内，串口缓冲溢出踩过坑
3. 鲸鱼会跟着 AI 状态动：思考时游、完成时停

仓库（MIT）：https://github.com/kingrenqi-wq/esp32-dsh-badge
不需要 Claude/DeepSeek，任何能往串口写 `DSH|...` 的脚本都能驱动。

第一次发硬件项目，轻拍。
```

### 3.2 Reddit r/esp32（英文）

**Title**：
```
ESP32-S3 desk board: live weather, Mac stats, and AI-agent activity on a 2" screen
```

**Body**：
```
Made a small desk display that shows what my Mac is doing — and what my AI coding
agent is doing — without me alt-tabbing.

Hardware: ESP32-S3 (16MB flash / 8MB PSRAM) + 2.0" ST7789 284×240 over SPI @40MHz,
three tactile buttons, one USB cable.

Six pages, two per button:
- Left: weather (24h curve, 5-day) ↔ 4-week activity heatmap
- Middle: main (52px clock, CPU/RAM/disk/net, balance, animated whale) ↔ today's stats
- Right: usage trend (7-day bars) ↔ wallet (balance + days left at burn rate)

Host side is a Python script that writes one `DSH|`-prefixed CSV line (46 fields,
<768 bytes) every 500ms over USB serial. Firmware does zero network I/O.

Things that were harder than expected:
- Anti-aliased subset fonts at 4bpp so Chinese is legible at 240px width
- Serial RX buffer overflow (fixed with a 32KB buffer + paced writes)
- Keeping the UI at ~10 fps by only redrawing dirty regions

MIT licensed, no cloud, no account: https://github.com/kingrenqi-wq/esp32-dsh-badge

Happy to answer anything about the protocol or the rendering.
```

### 3.3 酷安 / 小红书（短，配 GIF）

```
花两天做了个桌面状态牌 🔧

ESP32-S3 + 2.0 寸屏，三个按键六个页面
看一眼就知道：
· 现在几点、CPU 多少、内存还剩多少
· AI 在不在思考（鲸鱼会游）
· 这个月花了多少钱、还能撑几天

不联网、不上云，一根 USB 线喂数据
代码开源（MIT）→ kingrenqi-wq/esp32-dsh-badge
```

---

## 四、发布节奏建议

| 顺序 | 渠道 | 为什么 |
|---|---|---|
| 1 | **V2EX 分享创造** | 中文技术圈，转化率最高 |
| 2 | **Reddit r/esp32** | 英文，star 来源最大 |
| 3 | 酷安 / 小红书 | 非技术人群，要配动图 |
| 4 | Hacker News（Show HN） | 只在有英文博客时发，否则容易被喷 |

**间隔 1-2 天**，别同一天全发——同一批人会在多个平台看到，反而稀释。

---

## 五、不要做的事

- ❌ 买 star / 互 star —— GitHub 会识别，且毁信誉
- ❌ 标题党（"震惊！"）—— 技术社区反感
- ❌ 一次发 5 个平台 —— 显得像营销

---

生成时间：2026-11-02
