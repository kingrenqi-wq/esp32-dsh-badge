#!/usr/bin/env python3
"""DSH 状态桥：独立后台采样，主线程每 0.5 秒发送完整 40 字段。
三页数据：天气 / Mac 与状态 / CNY 近七天账目。
HTTP 截图和按键与串口写帧共用锁，防止数据交错。
修改后同步到 Application Support 并重启 launchd；配置不写进源码。
"""
import argparse
import fcntl
import glob
import json
import os
import sqlite3
import subprocess
import select
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from collections import deque
from contextlib import closing
import termios
import time
import math
import datetime
from urllib.parse import urlparse, parse_qs

DSH_HOME = os.path.expanduser("~/.dsh")
LEDGER = os.path.join(DSH_HOME, "dsh-bottom-info-bar", "usage-ledger.sqlite")
CONFIG = os.path.expanduser("~/.config/dsh-badge.json")

NODE = "/Applications/DeepSeek Harness.app/Contents/Resources/runtime/primary-runtime/dependencies/node/bin/node"
STATE_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dsh-state.mjs")

NOTIF_PORT = 8790
NOTIFS = deque(maxlen=20)      # 最近通知
NOTIF_UNREAD = 0

BALANCE_TTL = 60.0     # 余额刷新间隔
WEATHER_TTL = 900.0    # 天气 15 分钟
WX_LAT, WX_LON = "40.41", "115.52"   # 沙城（沿用 P4 项目配置）
WEEK = "一二三四五六日"
MAX_FRAME_BYTES = 768


def dsh_running():
    try:
        out = subprocess.run(["pgrep", "-f", "MacOS/DeepSeek"],
                             capture_output=True, text=True, timeout=3).stdout
        return bool(out.strip())
    except Exception:
        return False


def today_cost():
    """从 usage-ledger 读今日花费（人民币）。"""
    try:
        con = sqlite3.connect(f"file:{LEDGER}?mode=ro", uri=True, timeout=1)
        row = con.execute(
            "select sum(cost) from days where day=? and currency='CNY'",
            (time.strftime("%Y-%m-%d"),)).fetchone()
        con.close()
        return float(row[0] or 0.0)
    except Exception:
        return None


def month_cost():
    """本月 CNY 花费累计；读不到返回 None。"""
    try:
        first = datetime.date.today().replace(day=1).isoformat()
        with closing(sqlite3.connect(f"file:{LEDGER}?mode=ro", uri=True, timeout=1)) as con:
            row = con.execute(
                "select sum(cost) from days where currency='CNY' and day>=?",
                (first,)).fetchone()
        return float(row[0] or 0.0)
    except Exception:
        return None


def week_stats():
    """按连续七个本地自然日统计 CNY；读取失败返回缺失，不伪装成零消费。"""
    try:
        today = datetime.date.today()
        first = today - datetime.timedelta(days=6)
        with closing(sqlite3.connect(f"file:{LEDGER}?mode=ro", uri=True, timeout=1)) as con:
            rows = con.execute(
                "select day, sum(cost), sum(records) from days "
                "where currency='CNY' and day>=? and day<=? group by day",
                (first.isoformat(), today.isoformat())).fetchall()
        values = {row[0]: (row[1] or 0.0, row[2] or 0) for row in rows}
        days = [values.get((first + datetime.timedelta(days=i)).isoformat(), (0.0, 0)) for i in range(7)]
        return [round(float(v), 2) for v, _ in days], int(days[-1][1])
    except Exception:
        return None, None


DAY_FILE = os.path.expanduser('~/.config/dsh-badge-day.json')
DAY = {'date': '', 'active': 0.0, 'hours': [0] * 24, 'turns': 0}


def day_roll():
    """跨天清零。"""
    today = datetime.date.today().isoformat()
    if DAY['date'] != today:
        DAY.update({'date': today, 'active': 0.0, 'hours': [0] * 24, 'turns': 0})


def day_load():
    try:
        with open(DAY_FILE) as f:
            d = json.load(f)
        if d.get('date') == datetime.date.today().isoformat():
            DAY.update(active=float(d.get('active', 0)), hours=list(d.get('hours', [0] * 24))[:24],
                       turns=int(d.get('turns', 0)), date=d['date'])
            if len(DAY['hours']) < 24:
                DAY['hours'] = (DAY['hours'] + [0] * 24)[:24]
    except Exception:
        pass
    day_roll()


def day_save():
    try:
        with open(DAY_FILE, 'w') as f:
            json.dump(DAY, f)
    except Exception:
        pass


def sessions_today():
    """今天有改动过的会话文件数（只看 mtime，不解压）。"""
    n = 0
    today = datetime.date.today()
    for f in glob.glob(os.path.expanduser('~/.dsh/sessions/*/session-*/session.v4.jsonl.zstd')):
        try:
            if datetime.date.fromtimestamp(os.path.getmtime(f)) == today:
                n += 1
        except OSError:
            pass
    return n


CAPTURE_LOCK = threading.Lock()
SERIAL_FD = {'fd': None}


_stats = {'disk': None, 't': 0, 'cpu': None, 'mem': None, 'dn': None, 'up': None, 'rx': None, 'tx': None}


def mac_stats():
    """CPU% / 内存% / 网络速率(KB/s)，2 秒缓存。"""
    now = time.monotonic()
    if now - _stats['t'] < 2:
        return dict(_stats)
    try:
        import subprocess as sp
        cpu = 0.0
        out = sp.run(["ps", "-A", "-o", "%cpu="], capture_output=True, text=True, timeout=5).stdout
        total = sum(float(x) for x in out.split() if x)
        cores = int(sp.run(["sysctl", "-n", "hw.ncpu"], capture_output=True, text=True, timeout=5).stdout.strip())
        cpu = min(100, total / max(1, cores))
        rss = sp.run(["ps", "-A", "-o", "rss="], capture_output=True, text=True, timeout=5).stdout
        used = sum(int(x) for x in rss.split() if x) / 1024
        total_mb = int(sp.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True, timeout=5).stdout.strip()) / 1048576
        mem = min(100, used / total_mb * 100)
        dfout = sp.run(["df", "-k", "/"], capture_output=True, text=True, timeout=5).stdout.strip().splitlines()[-1].split()
        disk = int(dfout[3]) / 1048576          # 可用 GB
        net = sp.run(["netstat", "-ib"], capture_output=True, text=True, timeout=5).stdout
        rx = tx = None
        for line in net.splitlines():
            f = line.split()
            if len(f) > 9 and f[0] == "en0":
                try:
                    rx, tx = int(f[6]), int(f[9])
                except Exception:
                    pass
        dn = up = 0.0
        if _stats['rx'] is not None and rx is not None:
            dt = max(0.1, now - _stats['t'])
            dn = max(0, (rx - _stats['rx']) / dt / 1024)
            up = max(0, (tx - _stats['tx']) / dt / 1024)
        _stats.update({'disk': disk, 't': now, 'cpu': cpu, 'mem': mem, 'dn': dn, 'up': up, 'rx': rx, 'tx': tx})
    except Exception:
        return dict(cpu=None, mem=None, disk=None, dn=None, up=None)
    return dict(_stats)


def write_all(fd, payload, timeout=1.0):
    """Nonblocking USB writes can be partial. Caller holds CAPTURE_LOCK."""
    end = time.monotonic() + timeout
    pending = memoryview(payload)
    while pending:
        if time.monotonic() >= end:
            raise TimeoutError("serial write timeout")
        try:
            n = os.write(fd, pending)
            if n == 0:
                raise OSError("serial write returned zero")
            pending = pending[n:]
        except BlockingIOError:
            select.select([], [fd], [], min(.05, max(0, end-time.monotonic())))


def close_serial():
    """Caller holds CAPTURE_LOCK."""
    fd = SERIAL_FD['fd']
    SERIAL_FD['fd'] = None
    if fd is not None:
        try:
            os.close(fd)
        except OSError:
            pass


def grab_frame():
    """One serialized transaction; fd cannot close or receive a KEY mid-capture."""
    with CAPTURE_LOCK:
        fd = SERIAL_FD['fd']
        if fd is None:
            return None
        try:
            termios.tcflush(fd, termios.TCIFLUSH)
            write_all(fd, b"\nSHOT\n")
            buf = bytearray()
            deadline = time.monotonic() + 8
            need = 284 * 240 * 2
            start = -1
            while time.monotonic() < deadline:
                if select.select([fd], [], [], .2)[0]:
                    chunk = os.read(fd, 65536)
                    if not chunk:
                        raise OSError("serial disconnected")
                    buf.extend(chunk)
                    if start < 0:
                        pos = buf.find(b'DSHSHOT')
                        if pos >= 0:
                            start = pos + 7
                    if start >= 0 and len(buf) >= start + need:
                        data = bytes(buf[start:start+need])
                        break
                    if len(buf) > need + 4096:
                        raise OSError("invalid screenshot stream")
            else:
                raise TimeoutError("screenshot timeout")
        except (OSError, TimeoutError):
            close_serial()
            return None

    W, H = 284, 240
    row = (W * 3 + 3) & ~3
    size = row * H
    hdr = b'BM' + (54 + size).to_bytes(4, 'little') + b'\0\0\0\0' + (54).to_bytes(4, 'little')
    hdr += (40).to_bytes(4, 'little') + W.to_bytes(4, 'little', signed=True) + H.to_bytes(4, 'little', signed=True)
    hdr += (1).to_bytes(2, 'little') + (24).to_bytes(2, 'little') + b'\0' * 24
    out = bytearray(hdr)
    for y in range(H - 1, -1, -1):
        line = bytearray()
        for x in range(W):
            o = (y * W + x) * 2
            v = (data[o] << 8) | data[o + 1]
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            line += bytes((b * 255 // 31, g * 255 // 63, r * 255 // 31))
        line += b'\0' * (row - W * 3)
        out += line
    return bytes(out)


class NotifyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/shot.bmp'):
            bmp = grab_frame()
            if not bmp:
                self.send_response(503); self.end_headers(); return
            self.send_response(200)
            self.send_header('Content-Type', 'image/bmp')
            self.send_header('Content-Length', str(len(bmp)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(bmp)
            return
        if urlparse(self.path).path == '/key':
            k = parse_qs(urlparse(self.path).query).get('k', [''])[0].upper()
            status = 400
            if k in ('L', 'M', 'R'):
                with CAPTURE_LOCK:
                    fd = SERIAL_FD['fd']
                    status = 503
                    if fd is not None:
                        try:
                            write_all(fd, ("KEY " + k + "\n").encode())
                            status = 204
                        except (OSError, TimeoutError):
                            close_serial()
            self.send_response(status); self.end_headers()
            return
        if self.path.startswith('/mirror'):
            html = ("""<html><head><meta charset="utf-8"><style>
body{margin:0;background:#111;color:#888;font:13px -apple-system,sans-serif;text-align:center}
img{width:568px;image-rendering:pixelated;display:block;margin:0 auto}
.bar{padding:10px;display:flex;gap:10px;justify-content:center}
button{flex:1;max-width:120px;padding:14px 0;font-size:15px;border:1px solid #333;border-radius:10px;
background:#1c1c1e;color:#eee}
button:active{background:#2c6fd6}
</style></head><body>
<img id="v" src="/shot.bmp">
<div class="bar">
<button onclick="k('L')">左键</button>
<button onclick="k('M')">中键</button>
<button onclick="k('R')">右键</button>
</div>
<script>
function k(x){fetch('/key?k='+x).then(()=>setTimeout(()=>document.getElementById('v').src='/shot.bmp?t='+Date.now(),250));}
let timer; const img=document.getElementById('v');
function schedule(){clearTimeout(timer);timer=setTimeout(()=>img.src='/shot.bmp?t='+Date.now(),1500);}
img.onload=schedule;img.onerror=schedule;schedule();
</script></body></html>""")
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(html.encode())
            return
        self.send_response(404); self.end_headers()

    def do_POST(self):
        global NOTIF_UNREAD
        try:
            n = int(self.headers.get('Content-Length', 0))
            if n < 0 or n > 4096:
                self.send_response(413); self.end_headers(); return
            data = json.loads(self.rfile.read(n) or b'{}')
            text = clean_text(data.get('text', ''), 80)
            src = str(data.get('source', '小安'))
            if text:
                NOTIFS.append({'src': src, 'text': text, 'ts': time.strftime('%H:%M')})
                NOTIF_UNREAD = min(NOTIF_UNREAD + 1, 99)
                print(f"# 通知: {src} {text[:30]}", file=sys.stderr)
        except Exception as e:
            print(f"# notify error {e}", file=sys.stderr)
        self.send_response(204); self.end_headers()

    def log_message(self, *a):
        pass


def notify_server():
    try:
        srv = HTTPServer(('127.0.0.1', NOTIF_PORT), NotifyHandler)
        print(f"# 通知端口 127.0.0.1:{NOTIF_PORT}/notify", file=sys.stderr)
        srv.serve_forever()
    except Exception as e:
        print(f"# 通知端口启动失败: {e}", file=sys.stderr)


def fetch_weather():
    """Open-Meteo：返回 (weather_code, 温度整数, 预报字典)；失败返回 (None, None)"""
    try:
        url = (f"https://api.open-meteo.com/v1/forecast?latitude={WX_LAT}&longitude={WX_LON}"
               "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
               "&hourly=temperature_2m&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset"
               "&timezone=Asia%2FShanghai&forecast_days=7")
        out = subprocess.run(["curl", "-s", "-m", "10", url], capture_output=True, text=True, timeout=25).stdout
        d = json.loads(out)
        cur, dy = d["current"], d["daily"]
        extra = {
            "hum": int(cur["relative_humidity_2m"]),
            "wind": int(round(float(cur["wind_speed_10m"]))),
            "tmax": int(round(dy["temperature_2m_max"][0])),
            "tmin": int(round(dy["temperature_2m_min"][0])),
            "d2c": int(dy["weather_code"][1]), "d2max": int(round(dy["temperature_2m_max"][1])),
            "d2min": int(round(dy["temperature_2m_min"][1])),
            "d3c": int(dy["weather_code"][2]), "d3max": int(round(dy["temperature_2m_max"][2])),
            "d3min": int(round(dy["temperature_2m_min"][2])),
            "d4c": int(dy["weather_code"][3]), "d4max": int(round(dy["temperature_2m_max"][3])),
            "d4min": int(round(dy["temperature_2m_min"][3])),
            "d5c": int(dy["weather_code"][4]), "d5max": int(round(dy["temperature_2m_max"][4])),
            "d5min": int(round(dy["temperature_2m_min"][4])),
            "d6c": int(dy["weather_code"][5]), "d6max": int(round(dy["temperature_2m_max"][5])),
            "d6min": int(round(dy["temperature_2m_min"][5])),
            "sr": str(dy["sunrise"][0])[11:16], "ss": str(dy["sunset"][0])[11:16],
            "hr": ",".join(str(int(round(v))) for v in d["hourly"]["temperature_2m"][:24]),
        }
        return int(cur["weather_code"]), int(round(float(cur["temperature_2m"]))), extra
    except Exception:
        return None, None, {}


def load_key():
    try:
        with open(CONFIG) as f:
            return json.load(f).get("deepseek_api_key", "").strip()
    except Exception:
        return ""


def fetch_balance(key):
    if not key:
        return None
    try:
        out = subprocess.run(
            ["curl", "-s", "-m", "10", "-H", f"Authorization: Bearer {key}",
             "https://api.deepseek.com/user/balance"],
            capture_output=True, text=True, timeout=15).stdout
        data = json.loads(out)
        if data.get("is_available") is False:
            return 0.0
        return float(data["balance_infos"][0]["total_balance"])
    except Exception:
        return None


class NodeState:
    """常驻 node 子进程，每 500ms 吐一行 state|elapsed|turn。

    用独立线程读 stdout —— select() 在 TextIOWrapper 上会因为用户态缓冲失效
    （这就是之前"屏幕一直停在空闲"的原因）。
    """

    def __init__(self):
        self.state = "error"
        self.elapsed = 0
        self.turn = 0
        self.proc = None
        self.active = 0          # 今日专注秒数（node 从会话文件算）
        self.sessions = -1       # 今日会话数
        self.hours = [0] * 24    # 每小时活跃秒数
        self.series = [0] * 28   # 近 4 周每天活跃秒数
        self.updated_at = 0.0
        self.next_spawn = 0.0
        self._spawn()

    def _spawn(self):
        self.updated_at = time.monotonic()
        self.next_spawn = self.updated_at + 2
        try:
            self.proc = subprocess.Popen(
                [NODE, STATE_SCRIPT], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                text=True, bufsize=1)
            threading.Thread(target=self._read_loop, args=(self.proc,), daemon=True).start()
            print("# 状态源：dsh-state.mjs（node）", file=sys.stderr)
        except Exception as e:
            print(f"# 起不来 node: {e}", file=sys.stderr)
            self.proc = None

    def _read_loop(self, proc):
        for line in proc.stdout:
            parts = line.strip().split("|")
            if len(parts) >= 3 and parts[0] in ("think", "done", "idle", "error"):
                self.state = parts[0]
                self.updated_at = time.monotonic()
                try:
                    self.elapsed = int(parts[1])
                except ValueError:
                    self.elapsed = 0
                try:
                    self.turn = max(self.turn, int(parts[2]))
                except ValueError:
                    pass
                if len(parts) >= 6:
                    try:
                        self.active = int(parts[3])
                        self.sessions = int(parts[4])
                        hs = parts[5].split(',')
                        if len(hs) == 24:
                            self.hours = [int(float(x)) for x in hs]
                        if len(parts) >= 7:
                            ds = parts[6].split(',')
                            if len(ds) == 28:
                                self.series = [int(float(x)) for x in ds]
                    except ValueError:
                        pass
        if self.proc is proc:
            self.proc = None
            self.state = "error"

    def poll(self):
        now = time.monotonic()
        if self.updated_at and now - self.updated_at > 5:
            self.state = "error"
            if self.proc is not None:
                self.proc.kill()
        if self.proc is None and now >= self.next_spawn:
            self._spawn()

    def close(self):
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def fmt_money(v):
    return numeric(v, 2)


def serial_writer(port):
    """Open raw USB serial; close failed opens rather than leaking descriptors."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        import tty
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[2] &= ~termios.HUPCL
        attrs[2] |= termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        return fd
    except Exception:
        os.close(fd)
        raise


def clean_text(value, limit=80):
    text = ''.join(' ' if c in '|\r\n' or ord(c)<32 else c for c in str(value)).strip()
    return text.encode('utf-8')[:limit].decode('utf-8', 'ignore')


def numeric(value, decimals=0):
    try:
        return f"{float(value):.{decimals}f}" if math.isfinite(float(value)) and abs(float(value))<=1e12 else 'nan'
    except (TypeError, ValueError, OverflowError):
        return 'nan'


class CachedTask:
    """One sampler per source; API delays never block the serial heartbeat."""
    def __init__(self, fetch, interval, initial, retry=None):
        self.fetch, self.interval, self.initial, self.retry = fetch, interval, initial, retry
        self.value, self.updated = initial, 0.0
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        while not self.stop.is_set():
            try:
                value = self.fetch()
            except Exception:
                value = self.initial
            with self.lock:
                self.value, self.updated = value, time.monotonic()
            wait = self.retry if self.retry and value == self.initial else self.interval
            self.stop.wait(wait)

    def get(self):
        with self.lock:
            if self.updated and time.monotonic()-self.updated > self.interval+30:
                return self.initial
            return self.value


def make_frame(state, elapsed, balance, cost, weather, weekly, stats, tm=None, notification='', sessions=-1, today=None, month=None):
    tm = tm or time.localtime()
    if today is None:
        today = (0, -1, 0, [0] * 24, [0] * 28)
    wc, wt, ext = weather
    wk, count = weekly
    fields = ['DSH', state, str(max(0,int(elapsed))), fmt_money(balance), fmt_money(cost),
              time.strftime('%H:%M',tm), str(-1 if wc is None else wc), str(-999 if wt is None else wt),
              f'{tm.tm_mon}月{tm.tm_mday}日 周{WEEK[tm.tm_wday]}', str(NOTIF_UNREAD), clean_text(notification)]
    for key in ('hum','wind','tmax','tmin','d2c','d2max','d2min','d3c','d3max','d3min','d4c','d4max','d4min'):
        fields.append(numeric(ext.get(key)))
    fields += [ext.get('sr','--:--'), ext.get('ss','--:--')]
    for key in ('d5c','d5max','d5min','d6c','d6max','d6min'):
        fields.append(numeric(ext.get(key)))
    fields += [ext.get('hr',''), ','.join(numeric(v,2) for v in wk) if wk is not None else '', numeric(count)]
    fields += [numeric(stats.get('cpu')), numeric(stats.get('mem')),numeric(stats.get('dn'),1),numeric(stats.get('up'),1),numeric(stats.get('disk'))]
    # 今日页：40 专注秒数 / 41 会话数 / 42 轮次 / 43 每小时活跃秒数（24 项）
    fields += [str(int(today[0])), str(today[1]), str(today[2]),
               ','.join(str(int(v)) for v in today[3]),
               ','.join(str(int(v)) for v in today[4])]
    fields += [fmt_money(month)]
    assert len(fields) == 46
    payload = '|'.join(fields)+'\n'
    if len(payload.encode())-1 > MAX_FRAME_BYTES:
        fields[10] = ''
        payload = '|'.join(fields)+'\n'
    if len(payload.encode())-1 > MAX_FRAME_BYTES:
        raise ValueError('frame exceeds serial buffer')
    return payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-p','--port')
    ap.add_argument('--dry-run',action='store_true')
    ap.add_argument('-i','--interval',type=float,default=.5)
    args = ap.parse_args()
    if args.interval <= 0:
        ap.error('--interval must be positive')
    os.makedirs(os.path.dirname(CONFIG),exist_ok=True)
    lock_fd = open(os.path.expanduser('~/.config/dsh-badge.lock'),'w')
    try:
        fcntl.flock(lock_fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError:
        sys.exit('已经有 dsh-badge 运行；请先停 launchd 服务')
    lock_fd.write(str(os.getpid()));lock_fd.flush()
    day_load()
    key = load_key()
    tasks = [CachedTask(lambda:fetch_balance(key),60,None),
             CachedTask(today_cost,30,None),
             CachedTask(fetch_weather,900,(None,None,{}),retry=60),
             CachedTask(week_stats,30,(None,None)),
             CachedTask(month_cost,30,None),
             CachedTask(mac_stats,2,{}),
             CachedTask(dsh_running,2,False)]
    ns = NodeState()
    if not args.dry_run:
        threading.Thread(target=notify_server,daemon=True).start()
    last_line = ''
    last_tick = time.monotonic()
    last_save = 0.0
    sess_cache, sess_at = sessions_today(), 0.0
    try:
        while True:
            started = time.monotonic()
            ns.poll()
            state = ns.state if tasks[6].get() else 'error'
            now = time.monotonic()
            last_tick = now
            day_roll()
            if ns.turn > DAY['turns']:
                DAY['turns'] = ns.turn
            if now - last_save > 30:
                day_save(); last_save = now
            line = make_frame(state,ns.elapsed,tasks[0].get(),tasks[1].get(),tasks[2].get(),tasks[3].get(),tasks[5].get(),
                              notification=NOTIFS[-1]['text'] if NOTIFS else '',
                              today=(ns.active, ns.sessions, DAY['turns'], ns.hours, ns.series),
                              month=tasks[4].get())
            if not args.dry_run:
                with CAPTURE_LOCK:
                    if SERIAL_FD['fd'] is None:
                        port = args.port or next(iter(sorted(glob.glob('/dev/cu.usbmodem*'))),None)
                        if port:
                            try:
                                SERIAL_FD['fd'] = serial_writer(port)
                                print(f'# 串口 {port}',file=sys.stderr)
                            except OSError:
                                pass
                    if SERIAL_FD['fd'] is not None:
                        try:
                            write_all(SERIAL_FD['fd'],line.encode())
                        except (OSError,TimeoutError):
                            close_serial()
            if line != last_line:
                print(line.strip(),flush=True)
            last_line = line
            time.sleep(max(.01,args.interval-(time.monotonic()-started)))
    except KeyboardInterrupt:
        pass
    finally:
        day_save()
        for task in tasks:
            task.stop.set()
        ns.close()
        with CAPTURE_LOCK:
            close_serial()
        lock_fd.close()


if __name__ == '__main__':
    main()
