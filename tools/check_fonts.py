#!/usr/bin/env python3
"""查固件里出现的每个非 ASCII 字符是否都在 ui_fonts.h 的字库子集里。
缺字不会报错，只会静默不显示 —— 改任何文案后都要跑这个。"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ino = (ROOT / 'dsh_badge/dsh_badge.ino').read_text()
chars = sorted({ch for s in re.findall(r'"((?:[^"\\]|\\.)*)"', ino) for ch in s if ord(ch) > 0x7f})
font_py = (ROOT / 'tools/build_ui_fonts.py').read_text()
have = set(re.search(r"ASCII='([^']*)'", font_py).group(1)) | set(re.search(r"CJK='([^']*)'", font_py).group(1))
missing = [c for c in chars if c not in have]
print(f'固件非 ASCII 字符 {len(chars)} 个，字库缺 {len(missing)} 个')
if missing:
    print('缺字:', ''.join(missing))
    sys.exit(1)
print('PASS: 全部字库内有')
