#!/usr/bin/env python3
"""把 Mac 上的一个窗口镜像到状态牌第 4 页（请求-响应式，串口不常占）。

设备每约 120 ms 发一行 "MIR?" 要一帧；本进程回一行 "MIR" + RLE 报文。
帧与帧之间串口空闲，所以实体按键的 KEY 命令不会被吞掉。

用法（独占串口：先停桥）
    launchctl bootout gui/$(id -u)/com.example.dsh-badge
    python3 tools/mirror/mirror_server.py --fps 4
    # 设备上按中键进镜像页；再按回主界面

协议
    "MIR\\n" + u32 报文字节数(大端) + N × { u16 颜色(大端 RGB565), u16 游程(大端) }
"""
import argparse, os, re, select, subprocess, sys, termios, time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PORT = '/dev/cu.usbmodem11401'
W, H = 284, 240
MAX_BLOB = 96 * 1024


def window_bounds(app):
    script = (f'tell application "System Events" to tell process "{app}" '
              'to get {position, size} of front window')
    out = subprocess.run(['osascript', '-e', script], capture_output=True, text=True, timeout=5).stdout.strip()
    nums = [int(float(v)) for v in re.findall(r'-?\d+', out)]
    if len(nums) != 4:
        raise RuntimeError(f'取不到窗口位置: {out!r}')
    return tuple(nums)


def grab(region, title_bar=28):
    x, y, w, h = region
    tmp = '/tmp/dshmirror-shot.png'
    subprocess.run(['screencapture', '-x', '-R', f'{x},{y},{w},{h}', tmp], check=True, timeout=5)
    im = Image.open(tmp).convert('RGB')
    body = im.crop((0, min(title_bar, im.height - 1), im.width, im.height))
    scale = min(W / body.width, H / body.height)
    tw, th = max(1, int(body.width * scale)), max(1, int(body.height * scale))
    canvas = Image.new('RGB', (W, H), (0, 0, 0))
    canvas.paste(body.resize((tw, th), Image.Resampling.BILINEAR), ((W - tw) // 2, (H - th) // 2))
    return canvas


def encode_rle(im):
    """RGB565 大端 + 游程；返回 (报文, 游程数, 画布像素求和的简易校验)。"""
    px = im.load()
    out = bytearray()
    prev, run = None, 0
    runs = 0
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            if v == prev and run < 65535:
                run += 1
                continue
            if prev is not None:
                out += bytes((prev >> 8, prev & 0xFF, run >> 8, run & 0xFF))
                runs += 1
            prev, run = v, 1
    if prev is not None:
        out += bytes((prev >> 8, prev & 0xFF, run >> 8, run & 0xFF))
        runs += 1
    return bytes(out), runs


def open_port(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    import tty
    tty.setraw(fd)
    a = termios.tcgetattr(fd); a[2] &= ~termios.HUPCL; a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def write_paced(fd, data, chunk=2048, gap=0.009):
    """限速写：设备按字节搬运只有约 260 KB/s，灌太快队列会丢；约 220 KB/s，比设备 260 KB/s 略慢。"""
    for i in range(0, len(data), chunk):
        write_all(fd, data[i:i + chunk])
        time.sleep(gap)


def write_all(fd, data, timeout=3.0):
    end = time.monotonic() + timeout
    view = memoryview(data)
    while view:
        if time.monotonic() > end:
            raise TimeoutError('serial write timeout')
        try:
            n = os.write(fd, view)
            if n <= 0:
                raise OSError('short write')
            view = view[n:]
        except BlockingIOError:
            select.select([], [fd], [], 0.02)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', default='DeepSeek Harness')
    ap.add_argument('--fps', type=float, default=4.0)
    ap.add_argument('--port', default=PORT)
    ap.add_argument('--region', default='', help='x,y,w,h；默认取窗口')
    ap.add_argument('--seconds', type=float, default=0, help='0 = 一直跑')
    ap.add_argument('--save', default='', help='把最后一帧存成 PNG')
    ap.add_argument('--enter', action='store_true', help='启动时发 KEY M 进镜像页，退出时再按一次回主界面')
    args = ap.parse_args()

    fd = open_port(args.port)
    if args.enter:
        time.sleep(0.3)
        for _ in range(4):                      # 幂等：先问当前页，不是镜像页才按键
            write_all(fd, b'PAGE?\n')
            deadline = time.monotonic() + 0.6
            seen = None
            while time.monotonic() < deadline and seen is None:
                if select.select([fd], [], [], 0.05)[0]:
                    d = os.read(fd, 4096)
                    for ln in d.split(b'\n'):
                        if ln.strip().startswith(b'PAGE'):
                            seen = ln.strip()
            print(f'  当前 {seen.decode(errors="replace") if seen else "未知"}', flush=True)
            if seen and seen.endswith(b'3'):
                break
            write_all(fd, b'KEY M\n'); time.sleep(0.45)
    min_interval = 1.0 / max(0.2, args.fps)
    buf = b''
    last_send = 0.0
    sent = 0
    t_start = time.monotonic()
    last_report = t_start
    try:
        while True:
            if args.seconds and time.monotonic() - t_start > args.seconds:
                break
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                buf += chunk
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    line = line.strip()
                    if line.startswith(b'PAGE'):
                        print(f'  {line.decode(errors="replace")}', flush=True)
                    elif line.startswith(b'#'):
                        print(f'  设备: {line.decode(errors="replace")}', flush=True)
                    elif line == b'MIR?':
                        if time.monotonic() - last_send < min_interval:
                            continue
                        t0 = time.monotonic()
                        region = tuple(int(v) for v in args.region.split(',')) if args.region else window_bounds(args.app)
                        im = grab(region)
                        blob, runs = encode_rle(im)
                        if len(blob) > MAX_BLOB:
                            print(f'  报文 {len(blob)} B 超过上限，丢弃', flush=True)
                            continue
                        write_all(fd, b'MIR\n' + len(blob).to_bytes(4, 'big'))
                        write_paced(fd, blob)
                        last_send = time.monotonic()
                        sent += 1
                        if args.save:
                            im.save(args.save)
                        now = time.monotonic()
                        if now - last_report >= 2:
                            fps = sent / (now - t_start)
                            print(f'#{sent:4d} {len(blob):6d} B  {runs} 游程  '
                                  f'{now - t0:.2f}s/帧  平均 {fps:.1f} fps', flush=True)
                            last_report = now
    except KeyboardInterrupt:
        pass
    finally:
        if args.enter:
            try:
                write_all(fd, b'KEY M\n'); time.sleep(0.3)
            except Exception:
                pass
        os.close(fd)
        print(f'共 {sent} 帧，{(time.monotonic() - t_start):.1f} s', flush=True)


if __name__ == '__main__':
    sys.exit(main())
