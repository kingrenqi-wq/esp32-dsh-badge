#include "../../dsh_badge/dsh_badge.ino"
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
std::vector<std::string> fields(){return {"DSH","idle","0","40.70","10.55","15:29","3","14","10月28日 周三","0","","21","12","15","-1","0","15","1","0","15","0","3","10","1","06:42","17:20","3","13","0","0","13","-1","2,1,0,-1,-1,-1,-1,-1,3,5,7,9,11,13,14,15,14,12,10,8,8,6,6,5","0,0,0,0,5.80,10.38,10.55","476","9","83","11.9","76.7","115"};}
String join(const std::vector<std::string>&f){String s;for(size_t i=0;i<f.size();i++){if(i)s+='|';s+=f[i];}return s;}
void save(const std::string &name){std::ofstream out(name,std::ios::binary);out<<"P6\n284 240\n255\n";for(auto c:canvas.pixels){unsigned char p[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};out.write((char*)p,3);}}
void pages(const std::string &prefix){for(page=0;page<3;page++){drawAll();assert(canvas.clipped==0);save(prefix+"-"+std::to_string(page)+".ppm");}}
int main(int argc,char**argv){
 setup();auto f=fields();parse(join(f));assert(hourN==24&&weekValid&&weekRec==476&&diskG==115);
 pages(std::string(argv[1])+"/normal");
 auto shortFrame=f;shortFrame.resize(26);parse(join(shortFrame));assert(cpuPct==-1&&isnan(netDown)&&!weekValid&&hourN==0);
 for(int n:{31,34,37,38,40}){auto q=f;q.resize(n);q[n-5]="10";q[n-4]="80";q[n-3]="1";q[n-2]="2";q[n-1]="115";parse(join(q));assert(cpuPct==10&&diskG==115);}
 f=fields();f[10]=std::string(120,'x');assert(join(f).size()>319&&join(f).size()<=768);parse(join(f));assert(weekValid&&hourN==24);
 for(auto bad:{"999999999999999999999999999999","1,,2","1x,2","--1,2","nan","1e300",""}){auto q=fields();q[32]=q[33]=bad;parse(join(q));assert(hourN==0&&!weekValid);}
 f=fields();f[8]="999999999999999999999999999999";parse(join(f));pages(std::string(argv[1])+"/bad-date");
 f=fields();f[8]=std::string("10月28日 ")+char(0xE5);auto prior=lastFrame;hostMillis++;parse(join(f));assert(lastFrame==prior);
 f=fields();f[7]="-99";for(int i:{13,14,16,17,19,20,22,23,27,28,30,31})f[i]="-99";f[33]="0,0,0,0,0,0,999999999999.99";f[34]="1000000000";parse(join(f));pages(std::string(argv[1])+"/extreme");
 f=fields();f[6]="-1";f[7]="nan";f[11]="nan";f[12]="nan";f[32]=f[33]="";f[4]="nan";parse(join(f));pages(std::string(argv[1])+"/missing");
 hostMillis+=16001;pages(std::string(argv[1])+"/offline");
 // Deterministic malformed-field fuzz under ASan/UBSan, including date and CSV parsers.
 std::mt19937 rng(1028);for(int i=0;i<1000;i++){auto q=fields();int field=i%3==0?8:i%3==1?32:33;std::string v;for(int j=0,n=rng()%30;j<n;j++)v+=char(1+rng()%255);q[field]=v;parse(join(q));page=i%3;drawAll();}
 assert(uiWidth(UI14,"a")==7);
 std::cout<<"PASS: 26/31/34/37/38/40-field frames, >319-byte frames, malformed CSV/UTF-8/date, 15 page snapshots, 1000 fuzz cases under ASan/UBSan\n";
}
