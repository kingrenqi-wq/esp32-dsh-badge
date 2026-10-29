#include "../../dsh_badge/dsh_badge.ino"
#include <fstream>
#include <cassert>
int main(int argc,char**argv) {
  setup();
  const char *frame=argc>2?argv[2]:"DSH|idle|0|48.50|3.22|09:51|1|7|10月28日 周三|0||8|87|8|63|115";
  parse(String(frame));
  if(argc>3)hostMillis+=16000;
  drawAll();
  assert(canvas.clipped==0);
  std::ofstream out(argv[1],std::ios::binary); out<<"P6\n284 240\n255\n";
  for(uint16_t c:canvas.pixels){unsigned char p[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};out.write((char*)p,3);}
}
