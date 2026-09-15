// Guard: the TFT WiFi strength bars map RSSI the same way the web does, and do not flap.
//
// Two invariants, both cheap to break by accident:
//   1. The thresholds (-60/-70/-80) must match rssiBars() in data/script.js. The phone and
//      the machine show the same link; disagreeing about how strong it is is worse than not
//      showing it at all.
//   2. The level must be DEBOUNCED. RSSI moves a few dB between reads, so a bare comparison
//      makes the bars flap whenever the link sits on a boundary - and every flap is a full
//      bitmap blit over SPI, on the task that also drives the run screen.
//
//   3. The four level bitmaps must be the SAME artwork, nested. image_WIFI_Lv0/1/2 were cut
//      out of image_WIFI_Connect by classifying each pixel into concentric bands, so each
//      level must be a strict SUBSET of the next one up - anything else means a stray pixel
//      survived the cut and the fan would grow a hole as the signal changed.
//
// Mirrors the logic rather than linking src/ (displayLCD.cpp pulls in the whole TFT stack).
// If you change wifiBarsFromRssi or the debounce, change it here too - that is the point.
//
// Build/run (host, no hardware):
//   g++ -O2 -std=c++17 tools/test_wifi_bars.cpp -o t && ./t
#include <cstdio>
#include <cassert>
int wifiBarsFromRssi(int rssi){ if(rssi>=-60) return 3; if(rssi>=-70) return 2; if(rssi>=-80) return 1; return 0; }
// mirror of the debounce, driven by an injected sample sequence
struct Deb { int shown=-1, pending=-1;
  int step(int rssi, bool up){ if(!up){shown=pending=-1;return -1;}
    int now=wifiBarsFromRssi(rssi);
    if(now==shown){pending=-1;return shown;}
    if(shown<0) shown=now; else if(now==pending) shown=now; else pending=now;
    return shown; } };
int main(){
  assert(wifiBarsFromRssi(-30)==3); assert(wifiBarsFromRssi(-60)==3);
  assert(wifiBarsFromRssi(-61)==2); assert(wifiBarsFromRssi(-70)==2);
  assert(wifiBarsFromRssi(-71)==1); assert(wifiBarsFromRssi(-80)==1);
  assert(wifiBarsFromRssi(-81)==0); assert(wifiBarsFromRssi(-99)==0);
  printf("  ok  nguong -60/-70/-80 dung\n");
  Deb d; assert(d.step(-55,true)==3);
  printf("  ok  lan doc dau tien hien ngay -> %d\n", 3);
  // a single dip across the boundary must NOT flip the display
  int a=d.step(-62,true); int b=d.step(-55,true);
  assert(a==3 && b==3);
  printf("  ok  mot lan tut qua nguong khong lam nhay vach\n");
  // two agreeing samples do commit
  Deb e; e.step(-55,true); e.step(-62,true); int c=e.step(-63,true);
  assert(c==2);
  printf("  ok  hai mau lien tiep moi doi muc -> %d\n", c);
  // dropping the link resets, and a reconnect shows immediately
  Deb f; f.step(-55,true); assert(f.step(0,false)==-1); assert(f.step(-75,true)==1);
  printf("  ok  mat ket noi reset, noi lai hien ngay\n");
  // --- the four bitmaps must NEST -------------------------------------------------------
  // Lv0/1/2 were cut out of image_WIFI_Connect by classifying each pixel into concentric
  // bands, so each level has to be a strict subset of the next. A stray pixel surviving the
  // cut would show up as a hole or a floating fragment that only appears at one signal level
  // - the kind of thing nobody sees until a machine is in a weak-signal room.
  {
    static const unsigned char L0[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x00,0x01,0xf0,0x00,0x03,0xb8,0x00,0x01,0x50,0x00,0x00,0xe0,0x00,0x00,0x40,0x00,0x00,0x00,0x00};
    static const unsigned char L1[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x00,0x03,0xf8,0x00,0x07,0x1c,0x00,0x0e,0xee,0x00,0x05,0xf4,0x00,0x03,0xb8,0x00,0x01,0x50,0x00,0x00,0xe0,0x00,0x00,0x40,0x00,0x00,0x00,0x00};
    static const unsigned char L2[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xf0,0x00,0x07,0xfc,0x00,0x0f,0x1e,0x00,0x1c,0xe7,0x00,0x3b,0xfb,0x80,0x17,0x1d,0x00,0x0e,0xee,0x00,0x05,0xf4,0x00,0x03,0xb8,0x00,0x01,0x50,0x00,0x00,0xe0,0x00,0x00,0x40,0x00,0x00,0x00,0x00};
    static const unsigned char L3[] = {0x01,0xf0,0x00,0x07,0xfc,0x00,0x1e,0x0f,0x00,0x39,0xf3,0x80,0x77,0xfd,0xc0,0xef,0x1e,0xe0,0x5c,0xe7,0x40,0x3b,0xfb,0x80,0x17,0x1d,0x00,0x0e,0xee,0x00,0x05,0xf4,0x00,0x03,0xb8,0x00,0x01,0x50,0x00,0x00,0xe0,0x00,0x00,0x40,0x00,0x00,0x00,0x00};
    const unsigned char *lv[4] = {L0, L1, L2, L3};
    int pop[4] = {0, 0, 0, 0};
    for (int k = 0; k < 3; k++)
      for (int i = 0; i < 48; i++)
        assert((lv[k][i] & ~lv[k + 1][i]) == 0);  // every lit pixel survives into the next level
    for (int k = 0; k < 4; k++)
      for (int i = 0; i < 48; i++)
        for (int bit = 0; bit < 8; bit++)
          if (lv[k][i] & (1 << bit)) pop[k]++;
    for (int k = 0; k < 3; k++) assert(pop[k] < pop[k + 1]);  // and each level adds a real arc
    printf("  ok  4 bitmap long nhau, so pixel %d < %d < %d < %d\n", pop[0], pop[1], pop[2], pop[3]);
  }
  printf("\nall checks passed\n"); return 0; }
