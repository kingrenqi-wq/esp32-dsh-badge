// DSH 状态牌：284×240，统一无衬线字体 / 基线排版 / 分级信息。
// 串口帧 40 字段（DSH|state|...），15 秒无有效数据派生为离线；SHOT 不算心跳。
// 页面：0=天气 1=主界面 2=数据；左/中/右键直接选页。（第 4 页镜像默认关闭）
#include <LovyanGFX.h>
#include <errno.h>
#include <limits.h>
#include "ui_fonts.h"
#include "star_background.h"
#include "wi_icons.h"
#include "deepy.h"

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;

public:
  Display() {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = 40000000;
    b.pin_sclk = 15;
    b.pin_mosi = 16;
    b.pin_miso = -1;
    b.pin_dc = 6;
    bus.config(b);
    panel.setBus(&bus);
    auto p = panel.config();
    p.pin_cs = 7;
    p.pin_rst = 5;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 284;
    p.offset_x = 0;
    p.offset_y = 0;
    p.invert = true;
    p.rgb_order = false;
    panel.config(p);
    setPanel(&panel);
  }
} lcd;


static const uint16_t C_BG = 0x0000, C_FG = 0xF7DF, C_DIM = 0xC6BD, C_LABEL = 0xD71E;
static const uint16_t C_THINK = 0x4DFF, C_DONE = 0x5ECA, C_ERR = 0xFAAA;
static const int W = 284, H = 240;
LGFX_Sprite canvas(&lcd);
static String rx;
static bool rxOverflow = false, hasFrame = false, needFull = true;
static int page = 1;                 // 0=天气 1=主界面 2=账目（3=镜像，默认关闭）
static const int BTN_L=47, BTN_M=2, BTN_R=1;
static bool btnPrev[3]={false,false,false};
static uint32_t btnT[3]={0,0,0};
static bool btnHeld[3]={false,false,false};
static void setPage(int p){
  if(p==page) return;
  page=p; needFull=true;
  Serial.printf("PAGE %d\n", page);   // 告诉桥现在在第几页，桥据此决定要不要推镜像
}
// 左键：天气(page0) 与 近四周(page4) 之间来回
static void leftKey(){ setPage(page == 0 ? 4 : 0); }
// 右键：账目(page2) 与 钱包(page5) 之间来回
static void rightKey(){ setPage(page == 2 ? 5 : 2); }
// 中键：主界面(page1) 与 今日(page3) 之间来回
static void togglePage13(){ setPage(page==3 ? 1 : 3); }
static char state[16] = "boot", clockStr[8] = "--:--", dateStr[32] = "";
static int cpuPct = -1, memPct = -1, diskG = -1;
static int wxHum = -1, wxWind = -1, wxTmax = -999, wxTmin = -999;
static int d2c = -1, d2max = -999, d2min = -999, d3c = -1, d3max = -999, d3min = -999;
static int d4c = -1, d4max = -999, d4min = -999;
static int d5c = -1, d5max = -999, d5min = -999;
static int d6c = -1, d6max = -999, d6min = -999;
static int hourT[24];
static double weekCost[7];
static bool weekValid = false;
static int weekRec = -1;
static uint32_t todaySec = 0;       // 今日专注秒数
static int todaySess = -1, todayTurns = -1;
static int hourAct[24];             // 今日每小时的活跃秒数
static int daySec[28];              // 近 4 周每天活跃秒数（周一开头，末格=本周日）
static double monthCost = NAN;      // 本月 CNY 花费
static bool hourActValid = false;

static int hourN = 0;
static char srTime[8] = "--:--", ssTime[8] = "--:--";
static float netDown = NAN, netUp = NAN;
static double balance = NAN, cost = NAN;
static int wxCode = -1, wxTemp = -999;
static uint32_t secs = 0, lastFrame = 0;
static const size_t MAX_LINE = 768;

// 第 4 页（Mac 窗口镜像）：实测 1.6–1.8 fps、1/4.5 缩放，字看不清，2026-10-29 撤下。
// 想再试就把它改成 1，配套用 tools/mirror/run.sh。
#define MIRROR_PAGE 0

#if MIRROR_PAGE
static uint32_t mirrorAt = 0;      // 上一帧镜像到达时间
static bool mirrorOk = false;      // 收到过完整镜像帧
#endif

static uint16_t alphaLut[256];
static void buildAlphaLut(uint16_t fg) {
  int r = (fg >> 11) & 0x1F, g = (fg >> 5) & 0x3F, b = fg & 0x1F;
  int R = r * 255 / 31, G = g * 255 / 63, B = b * 255 / 31;
  for (int a = 0; a < 256; a++)
    alphaLut[a] = (((R * a / 255) >> 3) << 11) | (((G * a / 255) >> 2) << 5) | ((B * a / 255) >> 3);
}

// Blend anti-aliased text into the actual star field, not an assumed black pixel.
static uint16_t blend565(uint16_t fg,uint16_t bg,uint8_t alpha) {
  int inv=255-alpha;
  int r=(((fg>>11)&31)*alpha+((bg>>11)&31)*inv+127)/255;
  int g=(((fg>>5)&63)*alpha+((bg>>5)&63)*inv+127)/255;
  int b=((fg&31)*alpha+(bg&31)*inv+127)/255;
  return (r<<11)|(g<<5)|b;
}

static uint32_t utf8Next(const char **pp) {
  const unsigned char *p=(const unsigned char *)*pp;
  if (!*p) return 0;
  unsigned char first=*p;
  int n=first<0x80 ? 1 : first>=0xC2 && first<=0xDF ? 2 : first>=0xE0 && first<=0xEF ? 3 : first>=0xF0 && first<=0xF4 ? 4 : 0;
  uint32_t cp=n==1 ? first : n==2 ? first&31 : n==3 ? first&15 : first&7;
  if (!n) { (*pp)++; return 0xFFFD; }
  for (int k=1;k<n;k++) {
    if (!p[k] || (p[k]&0xC0)!=0x80) { (*pp)++; return 0xFFFD; }
    cp=(cp<<6)|(p[k]&63);
  }
  if ((n==3 && cp<0x800) || (n==4 && cp<0x10000) || (cp>=0xD800 && cp<=0xDFFF) || cp>0x10FFFF) { (*pp)++; return 0xFFFD; }
  *pp+=n; return cp;
}

static bool validUtf8(const char *s) {
  while (*s) if (utf8Next(&s)==0xFFFD) return false;
  return true;
}

static uint16_t stateColor(const char *s) {
  if (!strcmp(s, "think")) return C_THINK;
  if (!strcmp(s, "done")) return C_DONE;
  if (!strcmp(s, "error")) return C_ERR;
  return C_DIM;  // idle / offline
}

static const char *stateLabel(const char *s) {
  if (!strcmp(s, "think")) return "思考中";
  if (!strcmp(s, "done")) return "已完成";
  if (!strcmp(s, "idle")) return "空闲";
  if (!strcmp(s, "error")) return "出错";
  return "未连接";
}

static int wxIconIndex(int code) {
  if (code == 0) return 0;                                   // 晴
  if (code == 1 || code == 2) return 1;                      // 少云
  if (code == 3) return 2;                                   // 阴
  if (code == 45 || code == 48) return 3;                    // 雾
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 4;  // 雨
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 5;    // 雪
  if (code >= 95) return 6;                                  // 雷
  return 1;
}

// km/h -> 蒲福风级 0..12
static int windLevel(int kmh) {
  static const int T[12] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int lv = 0;
  for (int i = 0; i < 12; i++) if (kmh >= T[i]) lv = i + 1;
  return lv;
}

static const char *wxText(int code) {
  if (code < 0) return "--";
  if (code == 0) return "晴";
  if (code <= 2) return "多云";
  if (code == 3) return "阴";
  if (code == 45 || code == 48) return "雾";
  if (code >= 51 && code <= 57) return "雨";
  if (code >= 61 && code <= 67) return "雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code == 85 || code == 86) return "阵雪";
  if (code >= 95) return "雷雨";
  return "阴";
}

static int weekdayIndex() {
  const char *w=strstr(dateStr,"周");
  if (!w || strlen(w)<6) return -1;
  const char *days="日一二三四五六";
  for (int i=0;i<7;i++) if (!memcmp(w+3,days+i*3,3)) return i;
  return -1;
}
static void splitDate(char *num,size_t n1,char *wk,size_t n2) {
  num[0]=wk[0]=0;
  const char *p=dateStr; int mo=0,dd=0;
  while (*p>='0' && *p<='9') { if (mo>12) return; mo=mo*10+(*p++-'0'); }
  while (*p && (*p<'0' || *p>'9')) p++;
  while (*p>='0' && *p<='9') { if (dd>31) return; dd=dd*10+(*p++-'0'); }
  if (mo<1 || mo>12 || dd<1 || dd>31) return;
  snprintf(num,n1,"%d/%d",mo,dd);
  int day=weekdayIndex();
  if (day>=0) snprintf(wk,n2,"周%.*s",3,&"日一二三四五六"[day*3]);
}

// lmarzen 的天气图标：48x48 4bpp，按 alpha 混到背景
static void drawWiIcon(LGFX_Sprite &g, int x, int y, int size, int idx, uint16_t col) {
  if (idx < 0 || idx >= WI_N) return;
  for (int row = 0; row < size; row++) {
    int sy = row * WI_SIZE / size;
    for (int cx = 0; cx < size; cx++) {
      int sx = cx * WI_SIZE / size;
      uint8_t v = WI_ICONS[idx][sy * (WI_SIZE / 2) + (sx >> 1)];
      uint8_t a = (sx & 1) ? (v & 0x0F) : (v >> 4);
      if (a) g.drawPixel(x + cx, y + row, blend565(col, a==15 ? 0 : g.readPixel(x+cx,y+row), a * 17));
    }
  }
}

static void drawDeepy(int anim, int frame, int x, int y, bool flip) {
  int base = 0;
  for (int i = 0; i < anim; i++) base += WH_COUNTS[i];
  if (frame >= WH_COUNTS[anim]) frame = WH_COUNTS[anim] - 1;
  const uint16_t *fr = WH_FRAMES[base + frame];
  for (int row = 0; row < WH_H; row++) {
    int runStart = -1; uint16_t runCol = 0;
    for (int xx = 0; xx <= WH_W; xx++) {
      uint16_t c = (xx < WH_W) ? fr[row * WH_W + xx] : 0;
      if (c && runStart < 0) { runStart = xx; runCol = c; }
      else if ((!c || c != runCol) && runStart >= 0) {
        int len = xx - runStart;
        int sx = flip ? (WH_W - runStart - len) : runStart;
        canvas.drawFastHLine(x + sx, y + row, len, runCol);
        runStart = (c ? xx : -1); runCol = c;
      }
    }
  }
}




// All text shares baseline metrics and one sans-serif family per script.
static const UiGlyph *uiGlyph(const UiFont &font, uint32_t cp) {
  for (int i=0; i<font.count; i++) if (font.glyphs[i].cp==cp) return &font.glyphs[i];
  return nullptr;
}
static int uiWidth(const UiFont &font, const char *s) {
  int width=0;
  while (*s) { uint32_t cp=utf8Next(&s); const UiGlyph *g=uiGlyph(font,cp); width+=g ? g->advance : (cp<0x2E80 ? 7 : 14); }
  return width;
}
static void uiText(const UiFont &font,int x,int baseline,const char *s,uint16_t col);

static void uiText(const UiFont &font,int x,int baseline,const char *s,uint16_t col) {
  buildAlphaLut(col);
  while (*s) {
    uint32_t cp = utf8Next(&s);
    const UiGlyph *g=uiGlyph(font,cp);
    if (!g) { x += (cp < 0x2E80) ? 7 : 14; continue; }  // 缺字按半角/全角推进，避免后续字符叠位
    int stride=(g->w+1)/2;
    for (int y=0; y<g->h; y++) for (int xx=0; xx<g->w; xx++) {
      uint8_t pair=font.data[g->offset+y*stride+xx/2];
      uint8_t a=(xx&1) ? (pair&15) : (pair>>4);
      if (a) {
        int px=x+g->x+xx,py=baseline+g->y+y;
        if (px < 0 || px >= W || py < 0 || py >= H) continue;
        uint16_t bg=(a==15 || py < 0) ? 0 : canvas.readPixel(px,py);
        canvas.drawPixel(px,py,bg ? blend565(col,bg,a*17) : alphaLut[a*17]);
      }
    }
    x+=g->advance;
  }
}
static void uiRight(const UiFont &font,int right,int baseline,const char *s,uint16_t col) {
  uiText(font,right-uiWidth(font,s),baseline,s,col);
}
static void metricRight(int right,int baseline,const char *value,const char *unit) {
  int uw=uiWidth(UI14,unit);
  uiRight(NUM20,right-uw-(uw ? (!strcmp(unit,"%") ? 1 : 4) : 0),baseline,value,C_FG);
  if (uw) uiRight(UI14,right,baseline,unit,C_DIM);
}
static void networkRate(int x,int baseline,float kb,bool down) {
  // Compact 6×10 arrows; scale units separately from the value.
  uint16_t c=0x665E;
  int center=x+4, top=baseline-11;
  canvas.drawFastVLine(center,top,11,c);
  int tip=down ? top+10 : top;
  int wing=down ? tip-3 : tip+3;
  canvas.drawLine(center,tip,center-3,wing,c);
  canvas.drawLine(center,tip,center+3,wing,c);
  char value[16]; const char *unit="";
  if (!isfinite(kb) || kb<0) snprintf(value,sizeof(value),"--");
  else {
    const char *units[]={"KB/s","MB/s","GB/s"};
    int idx=0;
    while (kb>=999.5f && idx<2) { kb/=1024.0f; idx++; }
    snprintf(value,sizeof(value),"%.0f",fminf(kb,999)); unit=units[idx];
  }
  uiText(NUM20,x+18,baseline,value,C_FG);
  uiText(UI14,x+18+uiWidth(NUM20,value)+4,baseline,unit,C_DIM);
}
static bool connected() { return hasFrame && (uint32_t)(millis()-lastFrame) <= 15000; }



static void drawWeatherPage() {
  canvas.fillScreen(C_BG);
  for (const auto &r:STAR_RUNS) canvas.drawFastHLine(r.x,r.y,r.n,r.color);
  bool live=connected(), weather=live && wxCode>=0;
  char b[48];
  uiText(UI18,14,28,"沙城",C_FG);
  char dn[16],wk[16]; splitDate(dn,sizeof(dn),wk,sizeof(wk));
  uiRight(UI14,270,27,live ? (*wk ? wk : "--") : "未连接",C_DIM);
  if (weather) drawWiIcon(canvas,10,34,60,wxIconIndex(wxCode),C_FG);
  else uiText(NUM24,26,78,"--",C_DIM);
  const UiFont *tempFont=&CLOCK52;
  if (live && wxTemp>-999) snprintf(b,sizeof(b),"%d",wxTemp); else snprintf(b,sizeof(b),"--");
  // Reserve x=174..270 for the chart, even for -99°C.
  if (uiWidth(*tempFont,b)+uiWidth(UI16,"°C")>92) tempFont=&NUM28;
  uiText(*tempFont,78,98,b,C_FG);
  if (live && wxTemp>-999) uiText(UI16,80+uiWidth(*tempFont,b),tempFont==&CLOCK52 ? 70 : 90,"°C",C_DIM);
  const int CX0=174,CX1=270,CY0=46,CY1=98;
  if (weather && hourN>=2) {
    int mn=hourT[0],mx=hourT[0];
    for(int i=1;i<hourN;i++){mn=min(mn,hourT[i]);mx=max(mx,hourT[i]);}
    int span=max(1,mx-mn);
    canvas.drawFastHLine(CX0,CY1+3,CX1-CX0+1,0x18E3);
    int px=CX0,py=CY1-(hourT[0]-mn)*(CY1-CY0)/span;
    for(int i=1;i<hourN;i++){
      int x=CX0+(CX1-CX0)*i/(hourN-1),y=CY1-(hourT[i]-mn)*(CY1-CY0)/span;
      canvas.drawLine(px,py,x,y,0xFEAB);px=x;py=y;
    }
  } else uiText(UI14,210,80,"--",C_DIM);
  canvas.drawFastHLine(12,110,259,0x2147);
  const char *labels[]={"湿度","风力","日出","日落"};
  char val[4][12];
  if(weather && wxHum>=0) snprintf(val[0],12,"%d%%",wxHum);else strcpy(val[0],"--");
  if(weather && wxWind>=0) snprintf(val[1],12,"%d级",windLevel(wxWind));else strcpy(val[1],"--");
  snprintf(val[2],12,"%s",weather ? srTime : "--:--");
  snprintf(val[3],12,"%s",weather ? ssTime : "--:--");
  for(int i=0;i<4;i++){
    int x=i%2==0?16:152,y=i<2?140:170;
    uiText(UI14,x,y,labels[i],C_LABEL);uiRight(UI18,i%2==0?140:272,y,val[i],C_FG);
  }
  canvas.drawFastHLine(12,178,259,0x2147);
  int day=weekdayIndex();char names[5][8];
  strcpy(names[0],"今天");strcpy(names[1],"明天");strcpy(names[2],"后天");
  for(int i=3;i<5;i++){
    if(day>=0)snprintf(names[i],8,"周%.*s",3,&"日一二三四五六"[3*((day+i)%7)]);else strcpy(names[i],"--");
  }
  const int codes[]={wxCode,d2c,d3c,d4c,d5c};
  const int hi[]={wxTmax,d2max,d3max,d4max,d5max},lo[]={wxTmin,d2min,d3min,d4min,d5min};
  for(int i=0;i<5;i++){
    int cx=28+i*56;uint16_t color=i==0?0xFEAB:C_LABEL;
    uiText(UI14,cx-uiWidth(UI14,names[i])/2,194,names[i],color);
    if(weather && codes[i]>=0)drawWiIcon(canvas,cx-10,197,20,wxIconIndex(codes[i]),C_FG);
    if(weather && hi[i]>=-99 && lo[i]>=-99)snprintf(b,sizeof(b),"%d/%d",hi[i],lo[i]);else strcpy(b,"--/--");
    // Use compact digits only when -99/-99 would exceed a 56px forecast cell.
    const UiFont &forecastFont=uiWidth(UI14,b)>52 ? NUM12 : UI14;
    uiText(forecastFont,cx-uiWidth(forecastFont,b)/2,228,b,color);
  }
  canvas.pushSprite(&lcd,0,0);
}

static void drawStatsPage() {
  canvas.fillScreen(C_BG);
  for(const auto &r:STAR_RUNS)canvas.drawFastHLine(r.x,r.y,r.n,r.color);
  bool live=connected(),valid=live && weekValid;
  char b[48];uiText(UI18,14,28,"今日",C_FG);
  if(!live)uiRight(UI14,270,27,"未连接",C_DIM);
  double today=valid ? weekCost[6] : live ? cost : NAN;
  if(isfinite(today))snprintf(b,sizeof(b),"%.2f",today);else strcpy(b,"--");
  const UiFont *font=&CLOCK52;
  for(const UiFont *candidate:{&CLOCK52,&NUM28,&NUM24,&NUM20}){
    font=candidate;if(uiWidth(*font,b)+uiWidth(UI16,"元")<=256)break;
  }
  if(uiWidth(*font,b)+uiWidth(UI16,"元")>256)strcpy(b,"--");
  uiText(*font,14,88,b,C_FG);
  if(isfinite(today))uiText(UI16,18+uiWidth(*font,b),font==&CLOCK52 ? 66 : 86,"元",C_DIM);
  if(valid && weekRec>=0)snprintf(b,sizeof(b),"调用 %d 次",weekRec);else strcpy(b,"调用 -- 次");
  uiRight(UI14,270,108,b,C_DIM);
  canvas.drawFastHLine(12,117,259,0x2147);
  uiText(UI14,14,137,"近七天",C_LABEL);
  double mx=0.01;
  if(valid)for(int i=0;i<7;i++)if(weekCost[i]>mx)mx=weekCost[i];
  const int BX=22,BY=198,BW=30,GAP=4;
  if(valid){
    for(int i=0;i<7;i++){
      int h=(int)(weekCost[i]/mx*48);
      if(weekCost[i]>0)h=max(1,h);
      if(h>0)canvas.fillRect(BX+i*(BW+GAP),BY-h,BW,h,i==6?0xFEAB:0x4A69);
    }
  }else uiText(NUM24,127,175,"--",C_DIM);
  canvas.drawFastHLine(12,200,259,0x18E3);
  int day=weekdayIndex();
  for(int i=0;i<7;i++){
    if(day>=0)snprintf(b,sizeof(b),"周%.*s",3,&"日一二三四五六"[3*((day+1+i)%7)]);else strcpy(b,"--");
    uiText(UI14,BX+i*(BW+GAP)+(BW-uiWidth(UI14,b))/2,222,b,C_LABEL);
  }
  canvas.pushSprite(&lcd,0,0);
}

#if MIRROR_PAGE
// 镜像帧（RLE）：Mac 回一行 "MIR"，紧跟
//   u32 报文长度（大端） + N × { u16 颜色(大端RGB565), u16 游程长度(大端) }
// 136 KB 的原始帧灌不进来（USB-CDC 实测约 260 KB/s，且队列会丢），RLE 后通常 20–40 KB。
static uint8_t *mirrorBuf = nullptr;       // PSRAM，放 RLE 报文
static const size_t MIRROR_BUF = 96 * 1024;
static uint32_t lastMirrorReq = 0;

static size_t gotDbg = 0;   // 诊断：上次读取实际拿到多少
static bool readBytesT(uint8_t *dst, size_t n, uint32_t timeoutMs) {
  size_t got = 0; uint32_t t0 = millis();
  while (got < n) {
    if (millis() - t0 > timeoutMs) return false;
    int a = Serial.available();
    if (a <= 0) { yield(); continue; }
    size_t chunk = (size_t)a < (n - got) ? (size_t)a : (n - got);
    size_t r = Serial.readBytes(dst + got, chunk);
    if (r) { got += r; t0 = millis(); }
  }
  gotDbg = got;
  return true;
}

static void receiveMirror() {
  if (!mirrorBuf) { Serial.print("#MIR nobuf\n"); return; }
  uint8_t h[4];
  if (!readBytesT(h, 4, 800)) { Serial.print("#MIR nohdr\n"); return; }
  uint32_t bytes = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
  if (bytes == 0 || bytes > MIRROR_BUF || (bytes & 3)) { Serial.printf("#MIR badlen %u\n", (unsigned)bytes); return; }
  if (!readBytesT(mirrorBuf, bytes, 1500)) { Serial.printf("#MIR short %u/%u\n", (unsigned)gotDbg, (unsigned)bytes); return; }
  uint8_t *px = (uint8_t *)canvas.getBuffer();
  uint32_t at = 0;
  for (uint32_t o = 0; o + 4 <= bytes; o += 4) {
    uint8_t hi = mirrorBuf[o], lo = mirrorBuf[o + 1];
    uint16_t len = ((uint16_t)mirrorBuf[o + 2] << 8) | mirrorBuf[o + 3];
    if (len == 0 || at + len > (uint32_t)W * H) { Serial.printf("#MIR overrun %u\n", (unsigned)at); return; }
    for (uint16_t i = 0; i < len; i++) { px[at * 2] = hi; px[at * 2 + 1] = lo; at++; }
  }
  if (at != (uint32_t)W * H) { Serial.printf("#MIR partial %u\n", (unsigned)at); return; }
  mirrorOk = true; mirrorAt = millis(); needFull = true;
}

#endif

// 今日页（page 3）：专注时长 + 会话/轮次/花费 + 24 小时活跃热力条
static void drawTodayPage() {
  canvas.fillScreen(C_BG);
  for (const auto &r : STAR_RUNS) canvas.drawFastHLine(r.x, r.y, r.n, r.color);
  bool live = connected();
  char b[48];
  uiText(UI18, 14, 28, "今日", C_FG);
  char dn[16], wk[16]; splitDate(dn, sizeof(dn), wk, sizeof(wk));
  uiRight(UI14, 272, 27, live ? (*wk ? wk : "--") : "未连接", C_DIM);

  uint32_t sec = live ? todaySec : 0;
  const char *unit;
  if (sec < 3600) {                       // 不到一小时按"分"显示，避免 0:09 被读成 9 秒
    snprintf(b, sizeof(b), "%lu", (unsigned long)(sec / 60));
    unit = "分";
  } else {
    snprintf(b, sizeof(b), "%.1f", sec / 3600.0f);
    unit = "小时";
  }
  uiText(CLOCK52, 14, 96, b, live ? C_FG : C_DIM);
  uiText(UI18, 18 + uiWidth(CLOCK52, b), 96, unit, live ? C_LABEL : C_DIM);
  uiText(UI14, 16, 120, "专注时长", C_LABEL);
  canvas.drawFastHLine(12, 132, 260, 0x2945);

  const char *labels[3] = {"会话", "轮次", "花费"};
  const int xs[3] = {14, 110, 214};
  for (int i = 0; i < 3; i++) uiText(UI14, xs[i], 148, labels[i], C_LABEL);
  snprintf(b, sizeof(b), live && todaySess >= 0 ? "%d" : "--", todaySess);
  uiText(NUM20, xs[0], 172, b, C_FG);
  snprintf(b, sizeof(b), live && todayTurns >= 0 ? "%d" : "--", todayTurns);
  uiText(NUM20, xs[1], 172, b, C_FG);
  if (live && isfinite(cost)) snprintf(b, sizeof(b), "%.2f", cost); else snprintf(b, sizeof(b), "--");
  uiText(UI14, xs[2], 172, "¥", C_DIM);
  uiText(NUM20, xs[2] + 14, 172, b, C_FG);
  canvas.drawFastHLine(12, 182, 260, 0x2945);

  // 24 小时活跃热力条：越亮 = 这一小时干活越多
  const int X0 = 14, BAR_Y = 196, BAR_H = 18;
  int maxv = 60;
  for (int i = 0; i < 24; i++) if (live && hourActValid && hourAct[i] > maxv) maxv = hourAct[i];
  for (int i = 0; i < 24; i++) {
    int cx = X0 + i * 256 / 24;
    int cw = X0 + (i + 1) * 256 / 24 - cx - 1;
    uint16_t col = 0x18C3;
    if (live && hourActValid && hourAct[i] > 0) {
      int lv = 1 + 3 * hourAct[i] / maxv;            // 1..4 级
      col = lv >= 4 ? 0xFEAB : lv == 3 ? 0xD50C : lv == 2 ? 0x8B06 : 0x51A3;
    }
    canvas.fillRect(cx, BAR_Y, cw, BAR_H, col);
  }
  const char *ticks[5] = {"0", "6", "12", "18", "24"};
  for (int i = 0; i < 5; i++) {
    int tx = X0 + i * 256 / 4;                 // 首尾刻度贴边，不居中，避免被裁
    if (i == 0) uiText(NUM12, tx, 232, ticks[i], C_LABEL);
    else if (i == 4) uiRight(NUM12, tx + 2, 232, ticks[i], C_LABEL);
    else uiRight(NUM12, tx + uiWidth(NUM12, ticks[i]) / 2, 232, ticks[i], C_LABEL);
  }
  if (!live) uiRight(UI14, 272, 232, "未连接", C_DIM);
  canvas.pushSprite(&lcd, 0, 0);
}

// 近四周页（page 4）：4 周 × 7 天的活跃热力日历，周一开头
static void drawWeeksPage() {
  canvas.fillScreen(C_BG);
  for (const auto &r : STAR_RUNS) canvas.drawFastHLine(r.x, r.y, r.n, r.color);
  bool live = connected();
  uiText(UI18, 14, 28, "近四周", C_FG);
  uiRight(UI14, 272, 27, live ? "28 天" : "未连接", C_DIM);
  const int x0 = 26, y0 = 44, cw = 30, gap = 4;
  uint32_t total = 0; int actDays = 0;
  for (int i = 0; i < 28; i++) {
    int col = i % 7, row = i / 7;
    int x = x0 + col * (cw + gap), y = y0 + row * (cw + gap);
    int sec = live ? daySec[i] : 0;
    total += sec;
    if (sec > 60) actDays++;
    uint16_t c = 0x18C3;                               // 空
    if (sec > 0) {
      int h = sec / 3600;
      c = h >= 4 ? 0xFEAB : h >= 2 ? 0x3530 : h >= 1 ? 0x1B6B : 0x2A08;
    }
    canvas.fillRect(x, y, cw, cw, c);
  }
  const char *wd[7] = {"一", "二", "三", "四", "五", "六", "日"};
  for (int i = 0; i < 7; i++) {
    int cx = x0 + i * (cw + gap) + cw / 2;
    uiRight(UI14, cx + 7, 192, wd[i], C_LABEL);
  }
  canvas.drawFastHLine(12, 204, 260, 0x2945);
  char b[32];
  uiText(UI14, 14, 224, "合计", C_LABEL);
  snprintf(b, sizeof(b), "%.1f 小时", total / 3600.0f);
  uiText(UI16, 56, 224, b, C_FG);
  uiText(UI14, 176, 224, "有活动", C_LABEL);
  snprintf(b, sizeof(b), "%d 天", actDays);
  uiRight(UI16, 272, 224, b, live ? 0xFEAB : C_DIM);
  canvas.pushSprite(&lcd, 0, 0);
}

// 钱包页（page 5）：余额 / 还能用几天 / 本月·日均·今日
static void drawWalletsPage() {
  canvas.fillScreen(C_BG);
  for (const auto &r : STAR_RUNS) canvas.drawFastHLine(r.x, r.y, r.n, r.color);
  bool live = connected();
  char b[48];
  uiText(UI18, 14, 28, "钱包", C_FG);
  char dn[16], wk[16]; splitDate(dn, sizeof(dn), wk, sizeof(wk));
  uiRight(UI14, 272, 27, live ? (*wk ? wk : "--") : "未连接", C_DIM);

  uiText(UI16, 12, 94, "¥", C_DIM);
  if (live && isfinite(balance)) snprintf(b, sizeof(b), "%.2f", balance); else strcpy(b, "--");
  uiText(CLOCK52, 30, 94, b, live && isfinite(balance) ? C_FG : C_DIM);

  // 日均取近七天数据；余额 / 日均 = 还能用几天
  double sum = 0; int n = 0;
  if (live && weekValid) for (int i = 0; i < 7; i++) { sum += weekCost[i]; n++; }
  double daily = n ? sum / n : NAN;
  double days = (isfinite(daily) && daily > 0.01 && isfinite(balance)) ? balance / daily : NAN;

  uiText(UI14, 14, 122, "还能用", C_LABEL);
  if (isfinite(days)) {
    if (days > 99) strcpy(b, "99+"); else snprintf(b, sizeof(b), "%.0f", days);
    uiText(NUM28, 14, 152, b, 0xFEAB);
    uiText(UI18, 18 + uiWidth(NUM28, b), 152, "天", 0xFEAB);
  } else {
    uiText(NUM28, 14, 152, "--", C_DIM);
  }
  uiRight(UI14, 272, 150, "按近七天日均算", C_DIM);

  canvas.drawFastHLine(12, 166, 260, 0x2945);
  const char *labels[3] = {"本月", "日均", "今日"};
  const int xs[3] = {14, 110, 206};
  for (int i = 0; i < 3; i++) uiText(UI14, xs[i], 182, labels[i], C_LABEL);
  uiText(UI14, xs[0], 204, "¥", C_DIM);
  snprintf(b, sizeof(b), live && isfinite(monthCost) ? "%.2f" : "--", monthCost);
  uiText(NUM20, xs[0] + 14, 204, b, C_FG);
  uiText(UI14, xs[1], 204, "¥", C_DIM);
  snprintf(b, sizeof(b), isfinite(daily) ? "%.2f" : "--", daily);
  uiText(NUM20, xs[1] + 14, 204, b, C_FG);
  uiText(UI14, xs[2], 204, "¥", C_DIM);
  snprintf(b, sizeof(b), live && isfinite(cost) ? "%.2f" : "--", cost);
  uiText(NUM20, xs[2] + 14, 204, b, C_FG);
  canvas.pushSprite(&lcd, 0, 0);
}

static void drawAll() {
#if MIRROR_PAGE
  if (page == 3) {
    if (mirrorOk && (uint32_t)(millis() - mirrorAt) <= 3000) {
      canvas.pushSprite(&lcd, 0, 0);
      return;
    }
    canvas.fillScreen(C_BG);
    for (const auto &r : STAR_RUNS) canvas.drawFastHLine(r.x, r.y, r.n, r.color);
    uiText(UI18, 76, 116, "镜像未连接", C_DIM);
    uiText(UI14, 96, 146, "中键返回", C_LABEL);
    canvas.pushSprite(&lcd, 0, 0);
    return;
  }
#endif
  if (page == 5) { drawWalletsPage(); return; }
  if (page == 4) { drawWeeksPage(); return; }
  if (page == 3) { drawTodayPage(); return; }
  if (page == 0) { drawWeatherPage(); return; }
  if (page == 2) { drawStatsPage(); return; }
  if (page < 0 || page > 2) page = 1;
  const uint16_t gold=0xFEAB, rule=0x2945;
  bool live=connected();
  const char *displayState=live ? state : "offline";
  uint16_t col=stateColor(displayState);
  canvas.fillScreen(C_BG);
  for (const auto &r : STAR_RUNS) canvas.drawFastHLine(r.x,r.y,r.n,r.color);

  // Status is the accent; the date is supporting information.
  canvas.fillCircle(15,22,3,col);
  uiText(UI18,26,29,stateLabel(displayState),col);
  char buf[48];
  if (live && (!strcmp(state,"think") || !strcmp(state,"error"))) {
    if (secs<3600) snprintf(buf,sizeof(buf),"%02lu:%02lu",(unsigned long)(secs/60),(unsigned long)(secs%60));
    else if (secs<360000) snprintf(buf,sizeof(buf),"%lu:%02lu:%02lu",(unsigned long)(secs/3600),(unsigned long)(secs/60%60),(unsigned long)(secs%60));
    else snprintf(buf,sizeof(buf),"99:59:59");
    uiRight(UI16,272,28,buf,col);
  } else if (live) {
    char date[16],week[16]; splitDate(date,sizeof(date),week,sizeof(week));
    snprintf(buf,sizeof(buf),"%s  %s",date,week);
    uiRight(UI14,272,27,buf,C_LABEL);
  }

  // Main clock: open, unslashed digits. Weather occupies its own right column.
  uiText(CLOCK52,10,91,live ? clockStr : "--:--",live ? C_FG : C_DIM);
  if (live && wxCode>=0) drawWiIcon(canvas,168,42,36,wxIconIndex(wxCode),C_FG);
  if (live && wxTemp>-900) {
    snprintf(buf,sizeof(buf),"%d",wxTemp);
    uiRight(NUM28,262,69,buf,C_FG);
    uiText(UI16,264,56,"°",C_FG);
  } else uiRight(NUM28,272,69,"--",C_DIM);
  uiRight(UI16,272,92,live ? wxText(wxCode) : "--",live && wxCode>=0 ? gold : C_DIM);
  canvas.drawFastHLine(12,106,260,rule);

  // 28px rhythm, shared baselines, consistent label/value/unit treatment.
  uiText(UI16,12,132,"CPU",C_LABEL);
  if (live && cpuPct>=0) snprintf(buf,sizeof(buf),"%d",cpuPct); else snprintf(buf,sizeof(buf),"--");
  metricRight(132,132,buf,live && cpuPct>=0 ? "%" : "");
  uiText(UI16,152,132,"内存",C_LABEL);
  if (live && memPct>=0) snprintf(buf,sizeof(buf),"%d",memPct); else snprintf(buf,sizeof(buf),"--");
  metricRight(272,132,buf,live && memPct>=0 ? "%" : "");

  uiText(UI16,12,160,"磁盘可用",C_LABEL);
  if (live && diskG>=0) snprintf(buf,sizeof(buf),"%d",diskG); else snprintf(buf,sizeof(buf),"--");
  metricRight(272,160,buf,live && diskG>=0 ? "GB" : "");
  networkRate(12,188,live ? netDown : NAN,true);
  networkRate(152,188,live ? netUp : NAN,false);

  canvas.drawFastHLine(12,201,260,rule);
  uiText(UI16,12,226,"余额",C_LABEL);
  if (!live || !isfinite(balance)) snprintf(buf,sizeof(buf),"--");
  else snprintf(buf,sizeof(buf),"%.2f",balance);
  const UiFont *moneyFont=&NUM24;
  int amountWidth=uiWidth(*moneyFont,buf);
  // Keep cents for large amounts; first reduce type, then give the whale's space back.
  if (amountWidth>203) { moneyFont=&NUM20; amountWidth=uiWidth(*moneyFont,buf); }
  if (amountWidth>203) { snprintf(buf,sizeof(buf),"--"); amountWidth=uiWidth(*moneyFont,buf); }
  uiText(UI14,54,226,"¥",C_DIM);
  uiText(*moneyFont,69,228,buf,C_FG);
  // 往回多游一个身位（WH_W=42）：左极限 214 → 172，仍让金钱文字右侧留 12px 余量
  int minX=max(172,69+amountWidth+12);
  if (minX<=230) {
    int anim=0;
    if (live && !strcmp(state,"think")) anim=1;
    else if (live && !strcmp(state,"done")) anim=2;
    else if (live && !strcmp(state,"error")) anim=3;
    // Slow, small movement keeps peripheral decoration from competing with data.
    float t=millis()/1000.0f;
    int x=minX+(int)((230-minX)*(0.5f+0.5f*sinf(t*0.65f)));
    drawDeepy(anim,(millis()/180)%WH_COUNTS[anim],x,202,cosf(t*0.65f)>0);
  }
  canvas.pushSprite(&lcd,0,0);
}

static double number(const char *s) {
  if (!*s) return NAN;
  char *end; errno=0;
  double v=strtod(s,&end);
  return *end || errno || !isfinite(v) ? NAN : v;
}
static int boundedInt(const char *s,int lo,int hi,int missing) {
  float v=number(s);
  return !isfinite(v) || v<lo || v>hi ? missing : (int)v;
}

static bool timeText(const char *t) {
  return strlen(t)==5 && t[2]==':' && t[0]>='0' && t[0]<='2' && t[1]>='0' && t[1]<='9' &&
    t[3]>='0' && t[3]<='5' && t[4]>='0' && t[4]<='9' && !(t[0]=='2' && t[1]>'3');
}
static bool csvValues(const char *text,double *out,int count,double lo,double hi,bool integer) {
  const char *p=text;
  for(int i=0;i<count;i++){
    if(!*p)return false;
    char *end;errno=0;double v=strtod(p,&end);
    if(end==p || errno || !isfinite(v) || v<lo || v>hi || (integer && floor(v)!=v))return false;
    if(i<count-1 ? *end!=',' : *end!=0)return false;
    out[i]=v;p=end+(i<count-1?1:0);
  }
  return true;
}
static void parse(const String &s) {
  if(s.length()>MAX_LINE)return;
  char buf[MAX_LINE+1];memcpy(buf,s.c_str(),s.length()+1);
  char *parts[48]={};int n=1;parts[0]=buf;
  for(char *q=buf;*q;q++)if(*q=='|'){if(n==48)return;*q=0;parts[n++]=q+1;}
  if((n!=16 && n!=26 && n!=31 && n!=34 && n!=37 && n!=38 && n!=40 && n!=44 && n!=45 && n!=46) || strcmp(parts[0],"DSH"))return;
  if(strcmp(parts[1],"think") && strcmp(parts[1],"done") && strcmp(parts[1],"idle") && strcmp(parts[1],"error"))return;
  if(!*parts[2])return;
  uint64_t elapsed=0;
  for(const char *q=parts[2];*q;q++){if(*q<'0'||*q>'9')return;elapsed=elapsed*10+(*q-'0');if(elapsed>UINT32_MAX)return;}
  if(!timeText(parts[5]) || strlen(parts[8])>=sizeof(dateStr) || !validUtf8(parts[8]))return;
  strcpy(state,parts[1]);secs=(uint32_t)elapsed;balance=number(parts[3]);cost=number(parts[4]);
  strcpy(clockStr,parts[5]);strcpy(dateStr,parts[8]);
  wxCode=boundedInt(parts[6],0,99,-1);wxTemp=boundedInt(parts[7],-99,99,-999);
  // Optional fields must never retain data from a previous protocol revision.
  wxHum=wxWind=-1;wxTmax=wxTmin=-999;
  d2c=d3c=d4c=d5c=d6c=-1;d2max=d2min=d3max=d3min=d4max=d4min=d5max=d5min=d6max=d6min=-999;
  strcpy(srTime,"--:--");strcpy(ssTime,"--:--");hourN=0;weekValid=false;weekRec=-1;
  todaySec=0;todaySess=todayTurns=-1;hourActValid=false;for(int i=0;i<24;i++)hourAct[i]=0;
  for(int i=0;i<28;i++)daySec[i]=0;
  monthCost=NAN;
  for(int i=0;i<7;i++)weekCost[i]=NAN;
  cpuPct=memPct=diskG=-1;netDown=netUp=NAN;
  int mac=-1;
  if(n==16)mac=11;
  else {
    wxHum=boundedInt(parts[11],0,100,-1);wxWind=boundedInt(parts[12],0,999,-1);
    wxTmax=boundedInt(parts[13],-99,99,-999);wxTmin=boundedInt(parts[14],-99,99,-999);
    d2c=boundedInt(parts[15],0,99,-1);d2max=boundedInt(parts[16],-99,99,-999);d2min=boundedInt(parts[17],-99,99,-999);
    d3c=boundedInt(parts[18],0,99,-1);d3max=boundedInt(parts[19],-99,99,-999);d3min=boundedInt(parts[20],-99,99,-999);
    d4c=boundedInt(parts[21],0,99,-1);d4max=boundedInt(parts[22],-99,99,-999);d4min=boundedInt(parts[23],-99,99,-999);
    if(timeText(parts[24]))strcpy(srTime,parts[24]);if(timeText(parts[25]))strcpy(ssTime,parts[25]);
    if(n>=34){d5c=boundedInt(parts[26],0,99,-1);d5max=boundedInt(parts[27],-99,99,-999);d5min=boundedInt(parts[28],-99,99,-999);}
    if(n>=37){d6c=boundedInt(parts[29],0,99,-1);d6max=boundedInt(parts[30],-99,99,-999);d6min=boundedInt(parts[31],-99,99,-999);}
    if(n>=38){double values[24];if(csvValues(parts[32],values,24,-99,99,true)){hourN=24;for(int i=0;i<24;i++)hourT[i]=(int)values[i];}}
    if(n>=40){
      double values[7];weekValid=csvValues(parts[33],values,7,0,1e12,false);
      if(weekValid)for(int i=0;i<7;i++)weekCost[i]=values[i];
      weekRec=boundedInt(parts[34],0,1000000000,-1);
    }
    if(n>=44){
      todaySec=(uint32_t)boundedInt(parts[40],0,86400,0);
      todaySess=boundedInt(parts[41],0,9999,-1);
      todayTurns=boundedInt(parts[42],0,999999,-1);
      double hv[24];
      hourActValid=csvValues(parts[43],hv,24,0,3600,true);
      if(hourActValid)for(int i=0;i<24;i++)hourAct[i]=(int)hv[i];
    }
    if(n>=45){
      double dv[28];
      if(csvValues(parts[44],dv,28,0,86400,true))for(int i=0;i<28;i++)daySec[i]=(int)dv[i];
    }
    if(n>=46) monthCost=number(parts[45]);
    // 40/44 字段帧的 Mac 指标固定在 35..39（今日页字段追加在 40 之后）
    if(n>=40)mac=35; else if(n!=26)mac=n-5;
  }
  if(mac>=0){cpuPct=boundedInt(parts[mac],0,100,-1);memPct=boundedInt(parts[mac+1],0,100,-1);netDown=number(parts[mac+2]);netUp=number(parts[mac+3]);diskG=boundedInt(parts[mac+4],0,999999,-1);}
  lastFrame=millis();hasFrame=true;needFull=true;
}

void setup() {
  pinMode(BTN_L,INPUT_PULLDOWN); pinMode(BTN_M,INPUT_PULLDOWN); pinMode(BTN_R,INPUT_PULLDOWN);
  // 协议允许 768 字节/帧，而 USB-CDC 默认接收队列只有 256 字节；
  // 冬季帧（整点温度全是 -12）会到 272 字节，重绘期间到达就会丢尾部整帧作废。
  Serial.setRxBufferSize(32768);   // 单线程按字节搬运约 260 KB/s，队列要够垫
  Serial.begin(115200); rx.reserve(MAX_LINE);
  pinMode(4,OUTPUT); digitalWrite(4,LOW);
  lcd.init(); lcd.setRotation(3); lcd.fillScreen(C_BG);
  canvas.setColorDepth(16); canvas.setPsram(true);
  if (!canvas.createSprite(W,H)) { while (true) delay(1000); }
#if MIRROR_PAGE
  mirrorBuf = (uint8_t *)ps_malloc(MIRROR_BUF);
#endif
  pinMode(47,INPUT_PULLDOWN); pinMode(2,INPUT_PULLDOWN); pinMode(1,INPUT_PULLDOWN);
  drawAll(); digitalWrite(4,HIGH);
}
void loop() {
  while (Serial.available()) {
    char c=(char)Serial.read();
    if (c=='\n' || c=='\r') {
      if (!rxOverflow && rx=="SHOT") {
        if (needFull || !connected()) { drawAll(); needFull=false; }
        Serial.write((const uint8_t *)"DSHSHOT",7);
        Serial.write((const uint8_t *)canvas.getBuffer(),W*H*2);
        Serial.flush();
      } else if (!rxOverflow && rx=="PAGE?") {
        Serial.printf("PAGE %d\n", page);
#if MIRROR_PAGE
      } else if (!rxOverflow && rx=="MIR") {
        receiveMirror();
#endif
      } else if (!rxOverflow && rx.length()==5 && rx.startsWith("KEY ")) {
        char k = rx.charAt(4);
        if (k=='L') leftKey(); else if (k=='M') togglePage13(); else if (k=='R') rightKey();
      } else if (!rxOverflow && rx.length()) parse(rx);
      rx=""; rxOverflow=false;
    } else if (!rxOverflow) {
      if (rx.length()<MAX_LINE) rx+=c;
      else { rx=""; rxOverflow=true; }
    }
  }
  {
    const int pins[3]={BTN_L,BTN_M,BTN_R};
    uint32_t now=millis();
    for (int i=0;i<3;i++){
      bool v=digitalRead(pins[i]);
      if (v!=btnPrev[i]){ btnPrev[i]=v; btnT[i]=now; }
      else if (v && !btnHeld[i] && now-btnT[i]>=25){ btnHeld[i]=true; if(i==0) leftKey(); else if(i==1) togglePage13(); else if(i==2) rightKey(); }
      else if (!v) btnHeld[i]=false;
    }
  }
#if MIRROR_PAGE
  if (page == 3 && (uint32_t)(millis() - lastMirrorReq) >= 40) {
    Serial.print("MIR?\n");        // 要一帧；Mac 回一行 MIR + RLE
    lastMirrorReq = millis();
  }
#endif
  static uint32_t last=0;
  uint32_t iv = (page == 1) ? 120 : (page == 0 ? 1000 : 3000);
  if (needFull || (uint32_t)(millis()-last)>=iv) {
    drawAll(); needFull=false; last=millis();
  }
  delay(5);
}
