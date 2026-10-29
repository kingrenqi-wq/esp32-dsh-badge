# DSH 状态牌 · 交接文档 v4（三页·最终审查版）

更新：2026-10-28。本文件是当前统一交接入口；它取代单页版 v3 和各轮过程记录中的旧参数。先读本文，再改源码。当前固件、桥与状态引擎已部署，最新实机验收见 最终审查。

## 1. 当前功能与交付状态

| 页面 | 选择方式 | 内容 |
| --- | --- | --- |
| 天气页 page=0 | 左键 GPIO47 / `KEY L` | 沙城、当前温度与天气、当天 0–23 时温度趋势、湿度、风级、日出日落、今天起五天预报 |
| 主屏 page=1 | 中键 GPIO2 / `KEY M` | DSH 状态、计时或日期、时间、天气、CPU/内存/磁盘/网络、余额、鲸鱼 |
| 账目页 page=2 | 右键 GPIO1 / `KEY R` | 今日 CNY 花费、今日调用次数、连续七天 CNY 柱状图 |
| 钱包页 page=5 | **右键再按一次**（page2↔5 来回） / `KEY R` | 余额大字 + 还能用几天（余额/近七天日均）+ 本月 / 日均 / 今日花费 |
| 近四周页 page=4 | **左键再按一次**（page0↔4 来回） / `KEY L` | 4 周 × 7 天活跃热力日历 + 合计时长 + 有活动天数 |
| 今日页 page=3 | **中键再按一次**（page1↔3 来回） / `KEY M` | 专注时长（时:分）+ 会话数 / 轮次 / 今日花费 + 24 小时活跃热力条 |
| ~~镜像页~~ | ~~编译开关~~ | **已撤（2026-10-29）**：1.6–1.8 fps、1/4.5 缩放看不清，判定无实用价值。代码以 `#define MIRROR_PAGE 0` 保留，改成 1 可重新启用，配套 `tools/mirror/run.sh` |

开机默认主屏。物理按键按下为高，25ms 防抖；网页投屏提供三个等效切页按钮。通知字段仍保留，但屏幕不显示通知。

外观保留**黑底星群、装饰星座连线、深蓝轨道、明亮文字、暖金天气词与蓝鲸**。天气采用当前的 `wi_icons.h` 白色线条图标；旧双色多云素材已不被主程序引用，不要误回退。

- 最新编译程序 **490079 字节**，静态 RAM **25236 字节**；已实际烧录。
- 2026-10-28 修复：串口接收队列 256 → 1024 字节（`Serial.setRxBufferSize(1024)`），冬季帧 272 字节不再在重绘期间丢尾；验证见 review-20261028。
- 桥与状态引擎已同步到 Application Support 并重启，只有一个 Python 持有串口。
- 三页正常/极端/缺失/离线场景、坏帧、恢复与远程切页已验证；完成 ASan/UBSan 输入检查、桥与状态引擎离线测试。
- 本轮没有实测实体按键按压、观看距离、物理刷新率或连续多小时运行；不要把截图验收写成这些测试已完成。

![六页](six-pages.png)

截图是设备画布输出，不是实物照片，也不是实时更新的页面。

## 2. 硬件

| 项目 | 值 |
| --- | --- |
| 主控 | ESP32-S3，QFN56 rev v0.2，16 MB Flash / 8 MB OPI PSRAM |
| MAC | `7c:4f:ad:1c:bc:98` |
| 屏幕 | ST7789，物理 240×284，`setRotation(3)` → 284×240，RGB565 |
| 引脚 | SCK=15，MOSI=16，CS=7，DC=6，RST=5，BL=4 |
| SPI / 面板 | SPI2_HOST，mode 0，**40 MHz**；invert=true，rgb_order=false，offset_x/y=0 |
| 按键 | 左=47、中=2、右=1，模块下拉，`INPUT_PULLDOWN` |
| 串口 | 原生 USB-Serial/JTAG，`/dev/cu.usbmodem11401`，115200；重插可能改名 |

禁止占用 GPIO19/20（USB）与 GPIO33–37（OPI PSRAM）。旧点亮测试的 10MHz 不代表当前状态牌配置。

## 3. 文件与部署位置

工程：`~/Documents/codex/esp32-screen-bringup/`。

```text
AI_IMPLEMENTATION_GUIDE.md       当前统一交接
README.md                       项目入口与硬件历史
dsh_badge/
  dsh_badge.ino                 三页、按键、协议、截图、离线、绘制
  ui_fonts.h                    当前八档 4bpp 字库
  wi_icons.h                    当前 8 个 48×48 4bpp 天气图标
  star_background.h             星空背景水平游程
  deepy.h                       42×30 彩色蓝鲸，4 组 17 帧
tools/
  build_ui_fonts.py             当前字体生成器
  build_star_background.py      当前背景生成器
  host-preview/                实际 .ino 的宿主绘图适配器、预览及审查测试
  verify_display.py             上板三页回归，可选上传，自动停/恢复桥
  audit_bridge_test.py           桥的离线测试
  audit_state_test.mjs           状态引擎离线测试
docs/final-audit/               最新审查、截图、日志、备份路径
backups/                       历史快照
```

旧 `cjk24.h`、`cjk48.h`、`digits.h`、`icons.h`、`weather_partly.h`、`whale.h`、`whale2.h`、`bg.h` 及对应旧生成器保留作历史资产，**当前主程序不引用**。`docs/design/star-clock/` 的旧 PNG 也不是当前背景。

`wi_icons.h` 标注来自 lmarzen/esp32-weather-epd 的 weather-icons；当前仓库没有该图标转换器，维护时保留已有头文件，不能假设运行旧多云生成器能重建它。

| 桥接项 | 路径 |
| --- | --- |
| 源码 | `~/Documents/deepseek-harness/default-workspace/dsh-badge.py` |
| 状态引擎 / 旧直连截图 | 同目录 `dsh-state.mjs` / `dsh-shot.py` |
| 实际运行副本 | `~/Library/Application Support/dsh-badge/` |
| 常驻配置 | `~/Library/LaunchAgents/com.example.dsh-badge.plist` |
| 日志 | `~/Library/Logs/dsh-badge.log` / `.err` |
| 配置 | `~/.config/dsh-badge.json`，含 API key，保持 600 权限，不输出内容 |
| 单实例锁 | `~/.config/dsh-badge.lock`，flock |

launchd 读取 Documents 曾受 TCC 限制，继续使用 Application Support。修改桥必须先改源码，再同步运行副本。

## 4. 当前串口协议：40 字段

每行一帧，索引从 0 开始。**当前 46 字段**（40/44/45 字段旧帧仍兼容）；**接收上限 768 字节，不含换行**；解析缓冲 769 字节，字段数组 48 个槽；**接收队列 32768 字节**（`setup()` 中 `setRxBufferSize`，默认 256 会丢长帧尾）。超过上限丢弃到行末，不把残片当新帧。

| 索引 | 字段 |
| --- | --- |
| 0 | 固定 DSH |
| 1 / 2 | think、done、idle、error / 本轮秒数（0…UINT32_MAX） |
| 3 / 4 | 余额 / 今日花费，double；缺失为 nan |
| 5 | HH:MM，严格 24 小时时间 |
| 6 / 7 | 当前 WMO 天气码 / 温度 |
| 8 | UTF-8 日期，如 `10月28日 周三`，少于 32 字节 |
| 9 / 10 | 通知计数 / 正文，固件忽略；桥正文去掉分隔符和换行，最多 80 UTF-8 字节 |
| 11 / 12 | 湿度百分比 / 风速 km/h（页面换算风级） |
| 13 / 14 | 今日最高 / 最低温度 |
| 15–17 | 明天天气码 / 高温 / 低温 |
| 18–20 | 后天天气码 / 高温 / 低温 |
| 21–23 | 第 4 天天气码 / 高温 / 低温 |
| 24 / 25 | 日出 / 日落 HH:MM |
| 26–28 | 第 5 天天气码 / 高温 / 低温 |
| 29–31 | 第 6 天天气码 / 高温 / 低温（目前预报行只显示前五天） |
| 32 | 当天 24 个整点温度，逗号分隔，恰好 24 项 |
| 33 | 连续七天 CNY 花费，逗号分隔，恰好 7 项，六天前→今天 |
| 34 | 今日调用次数 |
| 35 / 36 | CPU / 内存百分比 |
| 37 / 38 | 下行 / 上行 KB/s，1K=1024 字节 |
| 39 | 磁盘可用，1024³ 字节口径，界面标记 GB |
| 40 | 今日专注秒数（桥按 state∈{think,done} 每 0.5 s 累计，落盘 `~/.config/dsh-badge-day.json`） |
| 41 | 今日会话数（mtime 在今天的会话文件个数），缺失 -1 |
| 42 | 今日轮次（会话里出现过的最大 turn） |
| 43 | 今日每小时活跃秒数，逗号分隔恰好 24 项，0…3600 |
| 44 | 近 4 周每天活跃秒数，逗号分隔恰好 28 项（周一开头，末格=本周日），0…86400 |
| 45 | 本月 CNY 花费累计（读账本，缺失为 nan） |

兼容旧帧：16、26、31、34、37、38 字段。16 是基础 11 字段加 Mac 5 字段；26 是基础加天气扩展、没有 Mac 指标；31/34/37/38 分别再包含不同数量预报/小时数据和末尾 Mac 5 字段。**缺少的扩展数据每帧清空**，不会沿用上一次 40 字段数据。

- 前缀、状态、秒数、时钟、日期 UTF-8 不合规时整帧拒绝，不刷新心跳。
- 湿度 0…100、风速 0…999、温度 -99…99；缺失温度内部为 -999，不把合法 -99 当缺失。
- CSV 项数、数字格式、范围必须有效，否则对应曲线/账目数据不可用；不进行宽松拼数字。
- CPU/内存 0…100，磁盘 0…999999；无效显示 `--`。
- 7 日金额非负且不超过 1e12；失败不伪装成零消费。账目页可在没有七日数据时显示独立的今日花费字段。

镜像页协议（`MIRROR_PAGE 1` 时才有；请求-响应，串口不常占）：
`MIR?\n` 设备每 40 ms 要一帧 → 电脑回 `MIR\n` + u32 报文字节数(大端) + N × { u16 颜色(大端 RGB565), u16 游程(大端) }，
设备把 RLE 解到画布缓冲后直接推屏。原始帧 136 KB 灌不进来（USB-CDC 单线程搬运约 260 KB/s，队列会丢），RLE 后约 40 KB。
`PAGE?\n` 回 `PAGE n\n`；切页时设备主动 `PAGE n\n`。实测 1.6–1.8 fps，逐像素与源图 100% 一致，但**缩放到 1/4.5 后文字不可读**，故默认关闭。

其他串口命令：`SHOT\n` 抓图；`KEY L\n` / `KEY M\n` / `KEY R\n` 切页；`MIR` / `PAGE?` 见上。KEY 恰好 5 字符。SHOT 在需要时先重绘，避免切页后抓到上一页。

## 5. 三页显示与断线

最后有效 DSH 帧超过 **15 秒**或从未收到有效帧，三页都进入离线显示：主屏「未连接」，天气/账目页右上提示「未连接」，过期数值、曲线和柱图不再冒充当前数据。SHOT 和 KEY 均不续心跳。新有效帧到达自动恢复。

主屏保持之前的基线排版：状态 baseline=29；日期=27；时钟 x=10、baseline=91；天气图标 (168,42)、36px；温度右对齐 262、baseline=69；天气词 baseline=92；CPU/内存、磁盘、网速三行 baseline=132/160/188；分隔线 y=106/201；余额数字 (69,228)。

天气页图表区为 x=174…270，y=46…98。正温度优先 52px；三字符负温度缩至 28px，单位随之调整，避免与趋势图重叠。预测温度组合超过 52px 时用 NUM12 单行显示；正常组合仍用 UI14。

账目页今日金额 baseline=88，按宽度在 52/28/24/20px 之间选择；调用次数独立一行 baseline=108。分隔线 y=117，柱图底 y=198，星期 baseline=222。零消费不画假高度；缺失数据不画虚构柱图。

| 用途 | RGB565 |
| --- | --- |
| 背景 / 数值近白 | 0x0000 / 0xF7DF |
| 标签 / 单位、缺失值、空闲、离线 | 0xD71E / 0xC6BD |
| 思考 / 完成 / 错误 | 0x4DFF / 0x5ECA / 0xFAAA |
| 天气词 / 网络箭头 | 0xFEAB / 0x665E |

鲸鱼保留素材原彩色，不随状态整只染色。idle/think/done/error 分别 3/6/4/4 帧，每 180ms 切换；普通余额时 x=172…230、y=202，往返约 9.7 秒（2026-10-28 左极限由 214 放宽到 172，多游一个身位）。金额过长时缩字、让出鲸鱼区域或隐藏鲸鱼。

刷新间隔配置：主屏 120ms、天气 1000ms、账目 3000ms；有效数据帧还会触发立即重绘，所以实际刷新并不严格等于这些间隔。没有实测物理 fps。

## 6. 字体与背景维护

```bash
DSH_ROOT="$HOME/Documents/codex/esp32-screen-bringup"
DSH_PY="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
cd "$DSH_ROOT"
"$DSH_PY" tools/build_ui_fonts.py
"$DSH_PY" tools/build_star_background.py
```

默认系统 python3 可能没有 Pillow。字体生成依赖本机 SFNS.ttf 与 Hiragino Sans GB.ttc：拉丁/数字 Medium，中文 14px W3、16/18px W6。UI14/UI16/UI18 各 98 字；NUM12 为紧凑预测数字，另有 NUM20/24/28 与 CLOCK52。数量以生成器输出为准。

4 倍采样后压成 4bpp，高半字节在前；保存 glyph 宽高、advance、基线偏移。数字等宽且居中。缺字按半角 7 / 全角 14 推进，测量和绘制一致；加文案仍必须补字符集，不能依靠缺字占位。

字体坐标为 baseline，名义字号不等于可见墨迹高度。主时钟 52px 的实际数字约 39px 高。字边与天气图标的 alpha 都与实际背景 RGB565 混合。

背景由固定种子点、线、弧生成：1305 个非黑水平游程，10440 字节。修改布局时同步调整 `build_star_background.py` 的 protected 区域。不要切回旧 sprite pushImage 路径，也不要重新铺暗色连续渐变或规则抖动。

## 7. 桥、状态引擎和数据口径

桥主线程约每 **0.5 秒**发送完整帧，数据源独立后台采样；慢 API 不再阻塞普通串口心跳。每个来源单独一个工作线程，不无限创建请求。HTTP 截图最多占用串口锁约 8 秒，会短暂停止帧发送。

| 来源 | 采样 / 缓存 |
| --- | --- |
| 余额 | DeepSeek `/user/balance`，60 秒 |
| 今日花费 / 七日账目 | usage-ledger.sqlite，分别 30 秒；七日只统计 CNY 与连续七个本地自然日 |
| 天气 | Open-Meteo，沙城 40.41/115.52，900 秒；失败 60 秒重试 |
| Mac 指标 | 2 秒 |
| DSH 进程检查 | 2 秒 |
| Node 会话判定 | 500ms，Python 独立线程读取 stdout；无更新超 5 秒报错并重启子进程 |

Node 从 `~/.dsh/sessions/<workspace>/session-*/session.v4.jsonl.zstd` 选最近会话。新会话在首条 assistant 输出前也可判定思考；近期工具错误显示 error；最近事件超过 120 秒转 idle。状态仍是基于会话事件的启发式判断，不是 DSH 官方进度 API。

Mac 指标的既有口径未改：CPU 为 ps 进程 CPU 占比总和/逻辑核数；内存为 RSS 总和/物理内存，封顶 100%；网络固定 en0；磁盘取 df -k /。因此内存并非活动监视器的“内存压力/精确已用”，网速不自动切换 VPN 或其他网卡。采样失败现在显示缺失，不再伪装成零或长期旧值。

桥和 HTTP 的写帧、KEY、SHOT 共用串口锁；非阻塞写入会补齐短写。捕获/写入失败会关闭并清空 fd，等待主循环重新打开。仍应在烧录后重启桥，不能仅靠帧日志推断旧 USB 句柄有效。

只在修改桥后同步：

```bash
DSH_RUN="$HOME/Library/Application Support/dsh-badge"
cp "$HOME/Documents/deepseek-harness/default-workspace/dsh-badge.py" "$DSH_RUN/"
cp "$HOME/Documents/deepseek-harness/default-workspace/dsh-state.mjs" "$DSH_RUN/"
launchctl kickstart -k "gui/$(id -u)/com.example.dsh-badge"
```

## 8. 备份、编译、烧录

已使用工具链：arduino-cli 1.5.1、ESP32 core 3.3.12、LovyanGFX 1.2.31。

```bash
DSH_ROOT="$HOME/Documents/codex/esp32-screen-bringup"
DSH_FQBN='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc'
cp -R "$DSH_ROOT/dsh_badge" "$DSH_ROOT/backups/dsh_badge-$(date +%Y%m%d-%H%M%S)"
"$HOME/bin/arduino-cli" compile --fqbn "$DSH_FQBN" "$DSH_ROOT/dsh_badge"
```

编译时不必停桥。失败先修复，不能上传旧构建。成功后先用 `~/bin/arduino-cli board list` 核对端口，再在同一终端执行：

```bash
(
  set -e
  DSH_SERVICE="gui/$(id -u)/com.example.dsh-badge"
  DSH_PLIST="$HOME/Library/LaunchAgents/com.example.dsh-badge.plist"
  DSH_RESTORE=0
  trap 'if [ "$DSH_RESTORE" -eq 1 ]; then launchctl bootstrap "gui/$(id -u)" "$DSH_PLIST"; fi' EXIT
  if launchctl print "$DSH_SERVICE" >/dev/null 2>&1; then
    launchctl bootout "$DSH_SERVICE"
    DSH_RESTORE=1
  fi
  "$HOME/bin/arduino-cli" upload -p /dev/cu.usbmodem11401 \
    --fqbn "$DSH_FQBN" "$DSH_ROOT/dsh_badge"
)
launchctl print "gui/$(id -u)/com.example.dsh-badge"
lsof -n /dev/cu.usbmodem11401
```

只允许一个串口持有者。设备无法枚举时用按住 BOOT 插线进入下载模式，不要改 USB 引脚。

## 9. 截图与测试

[网页投屏与切页](http://127.0.0.1:8790/mirror)。运行桥时优先走 HTTP，不要另开直连脚本抢串口：

```bash
curl -fsS --max-time 15 http://127.0.0.1:8790/shot.bmp -o /tmp/dsh-shot.bmp
curl -fsS 'http://127.0.0.1:8790/key?k=M'
```

固件截图返回 `DSHSHOT` 7 字节头 + 136320 字节大端 RGB565；桥转 BMP。旧 `dsh-shot.py` 必须停桥后使用，它所谓“实拍”实际是帧缓冲。

离线测试（不触碰设备）：

```bash
cd "$HOME/Documents/codex/esp32-screen-bringup"
python3 tools/audit_bridge_test.py
"/Applications/DeepSeek Harness.app/Contents/Resources/runtime/primary-runtime/dependencies/node/bin/node" tools/audit_state_test.mjs
clang++ -std=c++17 -fsanitize=address,undefined -g \
  -I tools/host-preview tools/host-preview/audit.cpp -o /tmp/dsh-audit
mkdir -p /tmp/dsh-audit-images
/tmp/dsh-audit /tmp/dsh-audit-images
```

`host-preview/render.cpp` 可生成普通主屏预览；测试适配器执行真实 .ino，不验证硬件电气或实际刷新率。需要扩展页面测试时使用 audit.cpp，而不是另一份网页重写。

上板测试：**先编译**，脚本 `--upload` 只上传、不编译。它会停桥、显示测试数据、抓三页正常/极端/缺失/离线图，并在 finally 恢复之前加载的桥：

```bash
DSH_PY="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
"$DSH_PY" tools/verify_display.py --upload --out "docs/verification-$(date +%Y%m%d-%H%M%S)"
```

端口目前写在脚本 PORT 常量，重插后核对。指定新的 --out，避免覆盖历史截图。

## 10. 回退、证据和已知限制

本轮完整审查前快照位置见 backup.txt，包含 **固件、工具、桥、状态引擎与旧截图脚本**。回退要同步恢复对应协议的固件和桥，而不是只复制一个 .ino。恢复前先另存当前版本。

更早的 `dsh_badge-*-star-depth` 等目录是各轮修改前的单页快照，不代表当前三页最终版。`AI_IMPLEMENTATION_GUIDE.v3-single-page.md` 和 `.v2-archive.md` 仅供历史追溯。

| 情况 | 检查方向 |
| --- | --- |
| 编译后画面不变 | 确认烧录当前构建、桥已重启，并用 HTTP 读设备真实画布 |
| 串口有帧但离线 | 核对字段数、UTF-8、768 字节限制；不要把旧 319 字节缓冲改回来 |
| 帧被截断/串扰 | 排除第二个串口持有者，保留统一锁与 write_all 短写处理 |
| 背景彩虹条/暗部色带 | 保留游程绘制，不换回旧 pushImage 与连续渐变 |
| 新文案缺字 | 改 build_ui_fonts.py 生成当前 ui_fonts.h，不补旧 cjk24 |
| 天气/账目读数缺失 | 对应后台采样失败时显示 -- 是正确行为，不应填假 0 |
| 内存/网速与系统工具不同 | 核对 RSS 求和与 en0 口径，尚未改成系统内存压力或自动网卡选择 |

最新证据：最终审查报告、设备测试、C++ 检查、桥测试、状态测试、真实切页记录。

handoff-v4.sha256 标记交接时的源码、生成资源与工具文件；它不替代设备固件读回校验。
