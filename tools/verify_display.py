#!/usr/bin/env python3
"""Explicit on-device visual acceptance. Stops/restores launchd; optionally uploads first.
Run with bundled Python + Pillow; never run concurrently with another serial client.
"""
import os, sys, time, select, termios, tty, subprocess, re
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[sys.argv.index('--out')+1]).resolve() if '--out' in sys.argv else ROOT / 'docs/display-review'
OUT.mkdir(parents=True,exist_ok=True)
SERVICE = f'gui/{os.getuid()}/com.example.dsh-badge'
PLIST = Path.home() / 'Library/LaunchAgents/com.example.dsh-badge.plist'
PORT = '/dev/cu.usbmodem11401'
FQBN = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc'
def shot(fd, name):
    os.write(fd,b'\nSHOT\n')
    data = b''; deadline=time.monotonic()+10
    while b'DSHSHOT' not in data:
        if time.monotonic()>deadline: raise RuntimeError('shot header timeout')
        if select.select([fd],[],[],0.5)[0]: data+=os.read(fd,65536)
    data=data.split(b'DSHSHOT',1)[1]
    while len(data)<284*240*2:
        if time.monotonic()>deadline: raise RuntimeError('shot data timeout')
        if select.select([fd],[],[],0.5)[0]: data+=os.read(fd,65536)
    rgb=bytearray()
    for i in range(0,284*240*2,2):
        v=(data[i]<<8)|data[i+1]
        rgb.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
    im=Image.frombytes('RGB',(284,240),bytes(rgb))
    if name:
        im.resize((852,720),Image.Resampling.NEAREST).save(OUT/f'{name}.png')
        print('captured',name,flush=True)
    return im

def frame(state='idle', secs=0, money='48.82', clock='09:48', code=0,temp=6,cpu=13,mem=83,dn=5,up=75,disk=115):
    return f'DSH|{state}|{secs}|{money}|nan|{clock}|{code}|{temp}|10月28日 周三|0||{cpu}|{mem}|{dn}|{up}|{disk}\n'.encode()

def send(fd,data):
    os.write(fd,data); time.sleep(0.35)

def extended_frame(case='normal'):
    fields=frame(code=3,temp=14,clock='15:29').decode().rstrip('\n').split('|')
    extras=['21','12','15','-1','0','15','1','0','15','0','3','10','1','06:42','17:20','3','13','0','0','13','-1',
            '2,1,0,-1,-1,-1,-1,-1,3,5,7,9,11,13,14,15,14,12,10,8,8,6,6,5',
            '0,0,0,0,5.80,10.38,10.55','476']
    if case=='extreme':
        fields[7]='-99';fields[10]='x'*120
        extras[0]='100';extras[1]='118'
        for i in [2,3,5,6,8,9,11,12,16,17,19,20]:extras[i]='-99'
        extras[22]='0,0,0,0,0,0,999999999999.99';extras[23]='1000000000'
    elif case=='missing':
        fields[6]='-1';fields[7]='nan';fields[3]=fields[4]='nan'
        extras=['nan']*24;extras[13]=extras[14]='--:--';extras[21]=extras[22]=''
    out='|'.join(fields[:11]+extras+fields[11:])+'\n'
    assert len(out.rstrip('\n').split('|'))==40 and len(out.encode())-1<=768
    return out.encode()

def main():
    running=subprocess.run(['launchctl','print',SERVICE],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
    fd=None
    try:
        if running: subprocess.run(['launchctl','bootout',SERVICE],check=True)
        if '--upload' in sys.argv:
            subprocess.run([str(Path.home()/'bin/arduino-cli'),'upload','-p',PORT,'--fqbn',FQBN,str(ROOT/'dsh_badge')],check=True)
            time.sleep(2)
        fd=os.open(PORT,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        tty.setraw(fd)
        a=termios.tcgetattr(fd); a[2]&=~termios.HUPCL; a[4]=a[5]=termios.B115200
        termios.tcsetattr(fd,termios.TCSANOW,a)
        cases=[
          ('test-idle',frame()),
          ('test-partly',frame(code=1)),
          ('test-partly-negative',frame(code=2,temp=-99,clock='23:59')),
          ('test-extremes',frame('think',359999,'12345678.90','23:59',95,-20,100,100,998.9,102400,12345)),
          ('test-wide-balance',frame('idle',0,'123456789012.34')),
          ('test-error',frame('error',16,'1000.00','00:00',85,-99,100,100,0,9999999,999999)),
          ('test-done',frame('done',83,'49.32','11:11',51,45,0,0,1024,1024000,0)),
          ('test-missing',frame('idle',0,'nan','00:00',-1,-999,'nan','nan','nan','nan','nan'))]
        for name,data in cases:
            send(fd,data); shot(fd,name)
        for case in ['normal','extreme','missing']:
            send(fd,extended_frame(case))
            for key in 'LMR':
                send(fd,('KEY '+key+'\n').encode());shot(fd,'three-'+case+'-'+key)
        send(fd,b'KEY M\n')
        baseline=frame('think',16,clock='12:34')
        send(fd,baseline); before=shot(fd,'test-valid')
        invalid=[baseline.replace(b'DSH|',b'BAD|'),baseline.replace(b'12:34',b'99:99'),b'DSH|idle|0|0|0|12:34\n',b'x'*800+baseline,baseline.replace(b'think',b'unknown')]
        for bad in invalid:
            send(fd,bad)
            after=shot(fd,None)
            assert before.crop((0,0,284,198)).tobytes()==after.crop((0,0,284,198)).tobytes(), 'invalid frame changed display'
        print('PASS: bad prefix/time/state, truncated and overlong frames rejected',flush=True)
        # Repeated screenshots must not count as live data.
        for _ in range(4): time.sleep(4); shot(fd,None)
        offline=shot(fd,'test-offline')
        # Offline dot must be gray rather than the blue working state.
        dot=offline.getpixel((15,24))
        gray=int(re.search(r'C_DIM\s*=\s*(0x[0-9A-Fa-f]+)',(ROOT/'dsh_badge/dsh_badge.ino').read_text())[1],16)
        expected=(((gray>>11)&31)*255//31,((gray>>5)&63)*255//63,(gray&31)*255//31)
        assert dot==expected, ('offline dot',dot,expected)
        assert offline.crop((0,0,284,198)).tobytes()!=before.crop((0,0,284,198)).tobytes()
        for key in 'LR':
            send(fd,('KEY '+key+'\n').encode());shot(fd,'three-offline-'+key)
        send(fd,b'KEY M\n')
        send(fd,baseline); recovered=shot(fd,'test-recovered')
        assert recovered.crop((0,0,284,198)).tobytes()==before.crop((0,0,284,198)).tobytes()
        print('PASS: 15-second offline with active SHOT polling; valid frame restores state',flush=True)
    finally:
        if fd is not None: os.close(fd)
        if running: subprocess.run(['launchctl','bootstrap',f'gui/{os.getuid()}',str(PLIST)],check=True)

if __name__=='__main__':
    main()
