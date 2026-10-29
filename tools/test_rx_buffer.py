#!/usr/bin/env python3
"""串口接收队列容量验证：长帧是否会被重绘打断。

做法（不依赖 OCR）：
  主屏左上角状态点 (15,24) 的颜色就是 state 字段的直接映射——
  think = C_THINK（蓝），idle = C_DIM（灰）。
  一次探测 = 先发一帧 think（触发一次重绘），5 ms 后再发一帧 idle。
  第二帧正好落在重绘窗口里：
    · 接收队列装得下整帧 → 灰（第二帧被接收）
    · 装不下（帧 290 B > 队列 256 B）→ 尾部被丢，整帧被拒 → 仍是蓝

用法（会临时停/恢复 launchd，独占串口）：
    python3 tools/test_rx_buffer.py --out docs/review-20261028/rx-buffer
退出码：0 = 全部被接收；1 = 存在被丢弃的帧。
"""
import argparse, os, re, select, subprocess, sys, termios, time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PORT = '/dev/cu.usbmodem11401'
SERVICE = f'gui/{os.getuid()}/com.example.dsh-badge'
PLIST = Path.home() / 'Library/LaunchAgents/com.example.dsh-badge.plist'
W, H = 284, 240


def rgb565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def const(name):
    src = (ROOT / 'dsh_badge/dsh_badge.ino').read_text()
    return int(re.search(rf'{name}\s*=\s*(0x[0-9A-Fa-f]+)', src).group(1), 16)


C_THINK, C_DIM = const('C_THINK'), const('C_DIM')


def frame(state, cold=False):
    """cold=True 是冬季帧（24 个整点温度全是 -12，每个 3 字节），比今天长 ~30 字节。"""
    if cold:
        hours = ','.join(['-12'] * 24)
        tmax, tmin, d2h, d2l, d3h, d3l, d4h, d4l, d5h, d5l, d6h, d6l = (
            '-1', '-12', '-2', '-13', '-3', '-14', '-4', '-15', '-5', '-16', '-6', '-17')
    else:
        hours = ','.join(str(v) for v in [2, 1, 0, -1, -1, -1, -1, -1, 3, 5, 7, 9, 11, 13, 14, 15, 14, 12, 10, 8, 8, 6, 6, 5])
        tmax, tmin, d2h, d2l, d3h, d3l, d4h, d4l, d5h, d5l, d6h, d6l = (
            '15', '-1', '15', '1', '15', '0', '10', '1', '13', '0', '13', '-1')
    f = ['DSH', state, '16', '42.25', '12.30', '15:29', '3', '-5', '10月28日 周三', '0', '',
         '88', '12', tmax, tmin, '3', d2h, d2l, '3', d3h, d3l, '45', d4h, d4l, '06:42', '17:20',
         '3', d5h, d5l, '61', d6h, d6l, hours,
         '0,0,0,0,5.80,10.38,15.58', '476', '9', '83', '11.9', '76.7', '115']
    assert len(f) == 40
    return ('|'.join(f) + '\n').encode()


def write_all(fd, data, timeout=2.0):
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


def shot(fd):
    write_all(fd, b'\nSHOT\n')
    data = b''
    deadline = time.monotonic() + 10
    while b'DSHSHOT' not in data:
        if time.monotonic() > deadline:
            raise RuntimeError('shot header timeout')
        if select.select([fd], [], [], 0.5)[0]:
            data += os.read(fd, 65536)
    data = data.split(b'DSHSHOT', 1)[1]
    need = W * H * 2
    while len(data) < need:
        if time.monotonic() > deadline:
            raise RuntimeError('shot data timeout')
        if select.select([fd], [], [], 0.5)[0]:
            data += os.read(fd, 65536)
    rgb = bytearray()
    for i in range(0, need, 2):
        rgb.extend(rgb565((data[i] << 8) | data[i + 1]))
    return Image.frombytes('RGB', (W, H), bytes(rgb))


def dot(img):
    return img.getpixel((15, 24))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='docs/review-20261028/rx-buffer')
    ap.add_argument('--probes', type=int, default=10)
    args = ap.parse_args()
    out = (ROOT / args.out).resolve(); out.mkdir(parents=True, exist_ok=True)

    long_think, long_idle = frame('think', cold=True), frame('idle', cold=True)
    short_think, short_idle = frame('think'), frame('idle')
    print(f'冬季帧 = {len(long_think) - 1} 字节；对照帧 = {len(short_think) - 1} 字节', flush=True)

    running = subprocess.run(['launchctl', 'print', SERVICE],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    fd = None
    got = 0
    try:
        if running:
            subprocess.run(['launchctl', 'bootout', SERVICE], check=True)
        fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        import tty
        tty.setraw(fd)
        a = termios.tcgetattr(fd); a[2] &= ~termios.HUPCL; a[4] = a[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, a)
        write_all(fd, b'KEY M\n'); time.sleep(0.3)

        # 对照：两帧之间留足时间，必须 100% 被接收，否则是测试本身有问题
        write_all(fd, short_idle); time.sleep(0.4)
        c1 = dot(shot(fd)) == rgb565(C_DIM)
        write_all(fd, short_think); time.sleep(0.4)
        c2 = dot(shot(fd)) == rgb565(C_THINK)
        print(f'对照 1（idle→灰）: {"OK" if c1 else "FAIL"}   对照 2（think→蓝）: {"OK" if c2 else "FAIL"}', flush=True)
        if not (c1 and c2):
            print('对照失败，测试环境异常，终止', flush=True); return 1

        for i in range(args.probes):
            # 先把状态确定性地置成蓝（单独发一帧 think，留足时间让它生效）
            write_all(fd, short_think); time.sleep(0.15)
            if dot(shot(fd)) != rgb565(C_THINK):
                print(f'  探测 {i + 1:2d}: 前置 think 未被接收，跳过', flush=True)
                continue
            write_all(fd, long_think)      # 冬季帧：触发一次重绘
            time.sleep(0.005)
            write_all(fd, long_idle)       # 冬季帧：落在重绘窗口里，看它能否整帧进入
            time.sleep(0.35)
            img = shot(fd)
            ok = dot(img) == rgb565(C_DIM)
            got += ok
            print(f'  探测 {i + 1:2d}: {"接收" if ok else "丢失"}', flush=True)
            if i == 0:
                img.resize((W * 2, H * 2), Image.Resampling.NEAREST).save(out / 'probe-1.png')
        print(f'结果：{got}/{args.probes} 帧被完整接收', flush=True)
        return 0 if got == args.probes else 1
    finally:
        if fd is not None:
            os.close(fd)
        if running:
            subprocess.run(['launchctl', 'bootstrap', f'gui/{os.getuid()}', str(PLIST)], check=True)


if __name__ == '__main__':
    sys.exit(main())
