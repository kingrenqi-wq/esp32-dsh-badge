// 边界用例补充审查（2026-10-28）：补齐 audit.cpp 没覆盖的协议/解析边界。
// 只跑宿主端，不碰设备。全部用例断言"接受/拒绝"是否符合协议约定。
//
//   clang++ -std=c++17 -fsanitize=address,undefined -g -I tools/host-preview \
//     tools/host-preview/edge-audit.cpp -o /tmp/dsh-edge && /tmp/dsh-edge
#include "../../dsh_badge/dsh_badge.ino"
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void check(bool ok, const char *msg) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) fails++;
}

static std::vector<std::string> base() {
  return {"DSH", "idle", "0", "40.70", "10.55", "15:29", "3", "14", "10月28日 周三", "0", "",
          "21", "12", "15", "-1", "0", "15", "1", "0", "15", "0", "3", "10", "1", "06:42", "17:20",
          "3", "13", "0", "0", "13", "-1",
          "2,1,0,-1,-1,-1,-1,-1,3,5,7,9,11,13,14,15,14,12,10,8,8,6,6,5",
          "0,0,0,0,5.80,10.38,10.55", "476", "9", "83", "11.9", "76.7", "115"};
}
static std::string join(const std::vector<std::string> &f) {
  std::string s;
  for (size_t i = 0; i < f.size(); i++) { if (i) s += '|'; s += f[i]; }
  return s;
}
static String S(const std::string &x) { return String(x.c_str()); }

// 返回 true 表示这一帧被接受（心跳前进）
static bool feed(const std::vector<std::string> &q) {
  uint32_t before = lastFrame;
  hostMillis++;
  parse(S(join(q)));
  return lastFrame != before;
}

int main() {
  setup();
  auto f = base();

  // 行长边界：768 接受、769 拒绝（协议上限，不含换行）
  {
    auto q = f; std::string pad(768 - join(q).size(), 'x'); q[10] = pad;
    check(join(q).size() == 768, "768 字节帧尺寸正确");
    check(feed(q), "768 字节整帧被接受");
    auto q2 = f; q2[10] = pad + "x";
    check(join(q2).size() == 769 && !feed(q2), "769 字节整帧被拒绝");
  }
  // 分隔符超限
  {
    std::string s = "DSH"; for (int i = 0; i < 47; i++) s += "|x";
    check(!feed(std::vector<std::string>{s}), "48 段以上被拒绝");
  }
  // 时间与秒数
  {
    auto q = f; q[5] = "24:00"; check(!feed(q), "24:00 被拒绝");
    q[5] = "23:59";               check(feed(q),  "23:59 被接受");
    q = f; q[2] = "4294967295";   check(feed(q),  "UINT32_MAX 秒数被接受");
    q[2] = "4294967296";          check(!feed(q), "UINT32_MAX+1 秒数被拒绝");
  }
  // 日期缓冲边界（dateStr[32]）
  {
    auto q = f; q[8] = "10月28日 啊啊啊啊啊啊ab";   // 31 字节，合法 UTF-8
    check(q[8].size() == 31 && feed(q), "31 字节日期被接受");
    q[8] += "c";                                     // 32 字节
    check(!feed(q), "32 字节日期被拒绝");
    q[8] = std::string("10月28日 ") + char(0xE5);    // 截断的多字节序列
    check(!feed(q), "截断 UTF-8 日期被拒绝");
  }
  // 湿度 / 温度边界
  {
    auto q = f; q[11] = "100";   check(feed(q) && wxHum == 100, "湿度 100 被接受");
    q[11] = "100.5";             check(feed(q) && wxHum == -1,  "湿度 100.5 视为缺失");
    q[11] = "101";               check(feed(q) && wxHum == -1,  "湿度 101 视为缺失");
    q = f; q[7] = "-99";         check(feed(q) && wxTemp == -99,  "温度 -99 是合法值");
    q[7] = "-100";               check(feed(q) && wxTemp == -999, "温度 -100 视为缺失");
  }
  // CSV 边界
  {
    const char *bad[] = {"1,2,3", "-0.5,0,0,0,0,0,0", "0.1,0,0,0,0,0,1e13", "", "0,0,0,0,0,0,nan"};
    for (auto c : bad) {
      auto q = f; q[33] = c; feed(q);
      check(!weekValid, (std::string("坏七日数据被拒绝: ") + c).c_str());
    }
    auto q = f; q[33] = "0,0,0,0,0,0,1000000000000";
    feed(q); check(weekValid, "七日数据上限 1e12 被接受");
    // 记录但不视为失败：C 数字语法被放行（见审查报告 P3）
    q[33] = "1e3,0,0,0,0,0,0"; feed(q);
    printf("NOTE  七日数据接受 C 数字语法 \"1e3\"（weekValid=%d）\n", (int)weekValid);
    q = f; q[3] = "0x10"; feed(q);
    printf("NOTE  余额接受 C 十六进制语法 \"0x10\"（balance=%.0f）\n", balance);
  }
  // 字段数每涨一次，都要确认"新增那几段"真的被解析了（这坑踩过三次）
  {
    auto q = base();
    q.resize(44);
    q[40] = "6388"; q[41] = "4"; q[42] = "72";
    q[43] = "0,0,0,0,0,0,0,39,0,888,22,0,0,0,0,0,0,0,0,3224,1939,275,0,0";
    feed(q);
    check(todaySec == 6388 && todaySess == 4 && todayTurns == 72 && hourAct[19] == 3224, "44 字段：今日三项 + 小时数据到位");

    q.push_back("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,9788,16535,20314,6854,0,0,0,0");
    feed(q);
    check(daySec[20] == 9788 && daySec[23] == 6854, "45 字段：近四周 28 天数据到位");

    q.push_back("46.43");
    feed(q);
    check(isfinite(monthCost) && monthCost > 46 && monthCost < 47, "46 字段：本月累计到位");
  }

  // 三页在极端值下不得画到画布外
  {
    auto q = f;
    q[7] = "-99";
    for (int i : {13, 14, 16, 17, 19, 20, 22, 23, 27, 28, 30, 31}) q[i] = "-99";
    q[3] = "123456789012.34";
    q[33] = "0,0,0,0,0,0,999999999999.99";
    q[34] = "1000000000";
    feed(q);
    for (page = 0; page < 3; page++) {
      drawAll();
      char m[64]; snprintf(m, sizeof(m), "极端值 page=%d 绘制无越界", page);
      check(canvas.clipped == 0, m);
    }
  }
  printf(fails ? "\nEDGE: %d FAIL\n" : "\nEDGE: ALL PASS\n", fails);
  return fails != 0;
}
