#pragma once
// Host-only raster adapter for drawing the real sketch; never used by Arduino.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
using std::max; using std::min; using std::isfinite;
#define SPI2_HOST 2
#define OUTPUT 1
#define INPUT_PULLDOWN 2
#define HIGH 1
#define LOW 0
inline uint32_t hostMillis=1000;
inline uint32_t millis(){return hostMillis;}
inline void delay(unsigned t){hostMillis+=t;}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline bool digitalRead(int){return false;}
class String: public std::string {
public:
 using std::string::string; using std::string::operator=;
 bool startsWith(const char *s)const{return rfind(s,0)==0;}
 char charAt(size_t i)const{return i<size()?at(i):0;}
};
struct FakeSerial {
 void begin(int){} int available(){return 0;} int read(){return -1;}
 void write(const uint8_t*,size_t){} void flush(){}
 size_t setRxBufferSize(size_t n){rxBuffer=n;return n;}
 size_t rxBuffer=256;
 int printf(const char *, ...){return 0;}   // 固件的诊断输出，宿主端忽略
 void print(const char *){}
 size_t readBytes(uint8_t *, size_t n){return n;}
};
inline FakeSerial Serial;
namespace lgfx {
struct Config {
 int spi_host=0,spi_mode=0,freq_write=0,pin_sclk=0,pin_mosi=0,pin_miso=0,pin_dc=0;
 int pin_cs=0,pin_rst=0,pin_busy=0,panel_width=0,panel_height=0,offset_x=0,offset_y=0;
 bool invert=false,rgb_order=false;
};
class Bus_SPI { public: Config config(){return {};}; void config(Config){}; };
class Panel_ST7789 {public: Config config(){return {};}; void config(Config){}; void setBus(Bus_SPI*){};};
class LGFX_Device {public: void setPanel(Panel_ST7789*){} void init(){} void setRotation(int){} void fillScreen(uint16_t){};};
}
class LGFX_Sprite {
public:
 int w=0,h=0; std::vector<uint16_t> pixels; unsigned clipped=0;
 LGFX_Sprite(lgfx::LGFX_Device*){}
 void setColorDepth(int){} void setPsram(bool){}
 void* createSprite(int width,int height){w=width;h=height;pixels.resize(w*h);return pixels.data();}
 void* getBuffer(){return pixels.data();}
 void fillScreen(uint16_t c){std::fill(pixels.begin(),pixels.end(),c);clipped=0;}
 uint16_t readPixel(int x,int y){return x>=0&&x<w&&y>=0&&y<h ? pixels[y*w+x] : 0;}
 void drawPixel(int x,int y,uint16_t c){if(x>=0&&x<w&&y>=0&&y<h)pixels[y*w+x]=c;else clipped++;}
 void drawFastHLine(int x,int y,int n,uint16_t c){for(int i=0;i<n;i++)drawPixel(x+i,y,c);}
 void drawFastVLine(int x,int y,int n,uint16_t c){for(int i=0;i<n;i++)drawPixel(x,y+i,c);}
 void fillCircle(int x,int y,int r,uint16_t c){for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++)if(xx*xx+yy*yy<=r*r)drawPixel(x+xx,y+yy,c);}
 void drawLine(int x0,int y0,int x1,int y1,uint16_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;for(;;){drawPixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}}
 void fillRect(int x,int y,int width,int height,uint16_t c){for(int yy=0;yy<height;yy++)drawFastHLine(x,y+yy,width,c);}
 void pushSprite(lgfx::LGFX_Device*,int,int){}
};
