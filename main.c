/* ==========================================================================
 *  SPACE VANGUARD 2.0 : STAR CITADEL
 *  Arcade shoot 'em up for Windows Console - written purely in C.
 *
 *  Main features v2.0
 *   - 96x56 "pixel" graphics using half-block characters (2 pixels per cell)
 *   - Pixel-art sprites, explosion particles, shockwaves, screen shake, flash
 *   - Real-time audio synthesizer (winmm): chiptune SFX + background music
 *   - 3 themed sectors (planets & nebulae) + 3-phase bullet-hell bosses
 *   - 5 power levels + homing missiles, bombs, shields, graze, chain multiplier
 *   - Top-5 leaderboard with name entry, 3 difficulty levels
 *
 *  Controls: WASD/Arrows move, SPACE/Z/J shoot, SHIFT focus,
 *            B/X/K bomb, P/ESC pause, M music, N sound effects
 *
 *  Build   : gcc -O2 -std=c11 main.c -o game.exe -lwinmm -luser32
 *            (or: clang -O2 -std=c11 main.c -o game.exe -lwinmm -luser32)
 *  Selftest: game.exe --selftest   (headless simulation crash test)
 * ========================================================================== */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
// clang-format off
#include <windows.h>
#include <mmsystem.h>
// clang-format on
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#endif

/* ------------------------------------------------------------------------ */
/*  Constants                                                                */
/* ------------------------------------------------------------------------ */
#define COLS 96 /* console width (characters)                   */
#define ROWS 30 /* console height (characters)                  */
#define PW 96   /* pixel area width                             */
#define PH 56   /* pixel area height (rows 1..28 x 2)           */
#define PROW0 1 /* first character row of pixel area            */
#define PI_F 3.14159265f
#define TAU_F 6.28318531f
#define NROWS(a) ((int)(sizeof(a) / sizeof((a)[0])))

static volatile bool running = true;
static bool g_selftest = false, g_god = false;

/* ------------------------------------------------------------------------ */
/*  Utilities                                                                */
/* ------------------------------------------------------------------------ */
static uint32_t g_rng = 0x9E3779B9u;
static uint32_t Rnd(void) {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return g_rng = x;
}
static float Rf(void) { return (float)(Rnd() & 0xFFFFFF) / 16777216.0f; }
static float Rr(float a, float b) { return a + (b - a) * Rf(); }
static int Ri(int a, int b) {
  return b <= a ? a : a + (int)(Rnd() % (uint32_t)(b - a + 1));
}
static float Clampf(float v, float a, float b) {
  return v < a ? a : (v > b ? b : v);
}
static int Mini(int a, int b) { return a < b ? a : b; }
static int Maxi(int a, int b) { return a > b ? a : b; }
static int Rnd2i(float v) { return (int)floorf(v + 0.5f); }

/* ------------------------------------------------------------------------ */
/*  Screen: pixel framebuffer + text layer -> CHAR_INFO half-block           */
/* ------------------------------------------------------------------------ */
static HANDLE hOut, hIn;
static DWORD origInMode;
static uint8_t fb[PH][PW];
static wchar_t tch[ROWS][COLS];
static uint8_t tfg[ROWS][COLS], tbg[ROWS][COLS];
static CHAR_INFO cbuf[ROWS * COLS];
static int scrOX = 0, scrOY = 0, winOK = 1;
static int shX = 0, shY = 0, useShake = 0;
static const uint8_t DIM[16] = {0, 0, 0, 0, 0, 0, 0, 8, 0, 1, 2, 3, 4, 5, 6, 7};

static void PSet(int x, int y, int c) {
  if (useShake) {
    x += shX;
    y += shY;
  }
  if ((unsigned)x < (unsigned)PW && (unsigned)y < (unsigned)PH)
    fb[y][x] = (uint8_t)c;
}
static void PRaw(int x, int y, int c) {
  if ((unsigned)x < (unsigned)PW && (unsigned)y < (unsigned)PH)
    fb[y][x] = (uint8_t)c;
}
static void PCircle(float cx, float cy, float r, int c) {
  int n = (int)(r * 6.0f) + 12;
  for (int i = 0; i < n; i++) {
    float a = TAU_F * (float)i / (float)n;
    PSet(Rnd2i(cx + cosf(a) * r), Rnd2i(cy + sinf(a) * r), c);
  }
}
static void TClear(void) { memset(tch, 0, sizeof tch); }
static void TPut(int c, int r, wchar_t ch, int fg, int bg) {
  if ((unsigned)c < COLS && (unsigned)r < ROWS) {
    tch[r][c] = ch;
    tfg[r][c] = (uint8_t)fg;
    tbg[r][c] = (uint8_t)bg;
  }
}
static void TPrint(int c, int r, const char *s, int fg, int bg) {
  for (; *s; s++)
    TPut(c++, r, (wchar_t)(unsigned char)*s, fg, bg);
}
static void TPrintC(int r, const char *s, int fg, int bg) {
  TPrint((COLS - (int)strlen(s)) / 2, r, s, fg, bg);
}

static void Present(void) {
  for (int r = 0; r < ROWS; r++) {
    int pr = r - PROW0;
    bool inP = pr >= 0 && pr < PH / 2;
    for (int c = 0; c < COLS; c++) {
      CHAR_INFO *ci = &cbuf[r * COLS + c];
      int top = 0, bot = 0;
      if (inP) {
        top = fb[pr * 2][c];
        bot = fb[pr * 2 + 1][c];
      }
      if (tch[r][c]) {
        ci->Char.UnicodeChar = tch[r][c];
        ci->Attributes = (WORD)(tfg[r][c] | (tbg[r][c] << 4));
      } else if (top == bot) {
        ci->Char.UnicodeChar = L' ';
        ci->Attributes = (WORD)(top << 4);
      } else {
        ci->Char.UnicodeChar = 0x2580; /* upper half block */
        ci->Attributes = (WORD)(top | (bot << 4));
      }
    }
  }
  COORD sz = {COLS, ROWS}, org = {0, 0};
  SMALL_RECT rc = {(SHORT)scrOX, (SHORT)scrOY, (SHORT)(scrOX + COLS - 1),
                   (SHORT)(scrOY + ROWS - 1)};
  WriteConsoleOutputW(hOut, cbuf, sz, org, &rc);
}

static void HideCursor(void) {
  CONSOLE_CURSOR_INFO ci = {1, FALSE};
  SetConsoleCursorInfo(hOut, &ci);
}
static void ClearWindow(const CONSOLE_SCREEN_BUFFER_INFO *i) {
  DWORD w;
  int ww = i->srWindow.Right - i->srWindow.Left + 1;
  for (int y = i->srWindow.Top; y <= i->srWindow.Bottom; y++) {
    COORD o = {i->srWindow.Left, (SHORT)y};
    FillConsoleOutputCharacterW(hOut, L' ', (DWORD)ww, o, &w);
    FillConsoleOutputAttribute(hOut, 0, (DWORD)ww, o, &w);
  }
}
static void CheckWindow(void) {
  static int lw = -1, lh = -1, ll = -1, lt = -1;
  CONSOLE_SCREEN_BUFFER_INFO i;
  if (!GetConsoleScreenBufferInfo(hOut, &i))
    return;
  int ww = i.srWindow.Right - i.srWindow.Left + 1,
      wh = i.srWindow.Bottom - i.srWindow.Top + 1;
  if (ww != lw || wh != lh || i.srWindow.Left != ll || i.srWindow.Top != lt) {
    lw = ww;
    lh = wh;
    ll = i.srWindow.Left;
    lt = i.srWindow.Top;
    ClearWindow(&i);
    HideCursor();
  }
  winOK = (ww >= COLS && wh >= ROWS);
  if (winOK) {
    scrOX = i.srWindow.Left + (ww - COLS) / 2;
    scrOY = i.srWindow.Top + (wh - ROWS) / 2;
  } else {
    wchar_t msg[160];
    DWORD w;
    int n = swprintf(
        msg, 160,
        L" Window is too small (%dx%d). Please resize to at least %dx%d. ", ww,
        wh, COLS, ROWS);
    COORD o = {i.srWindow.Left, i.srWindow.Top};
    if (n > ww)
      n = ww;
    WriteConsoleOutputCharacterW(hOut, msg, (DWORD)n, o, &w);
    FillConsoleOutputAttribute(hOut, 0x4F, (DWORD)n, o, &w);
  }
}
static void SetupConsole(void) {
  hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  hIn = GetStdHandle(STD_INPUT_HANDLE);
  SetConsoleTitleW(L"SPACE VANGUARD 2.0 - Star Citadel");
  GetConsoleMode(hIn, &origInMode);
  DWORD m = origInMode;
  m &= ~(DWORD)(ENABLE_QUICK_EDIT_MODE | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
  m |= ENABLE_EXTENDED_FLAGS;
  SetConsoleMode(hIn, m);
  /* In classic console host, window size can be forced.
     In Windows Terminal (WT_SESSION), user manages size; game centers. */
  if (!getenv("WT_SESSION")) {
    CONSOLE_SCREEN_BUFFER_INFO i;
    if (GetConsoleScreenBufferInfo(hOut, &i)) {
      int ww = i.srWindow.Right - i.srWindow.Left + 1,
          wh = i.srWindow.Bottom - i.srWindow.Top + 1;
      if (ww != COLS || wh != ROWS) {
        SMALL_RECT tiny = {0, 0, 1, 1};
        SetConsoleWindowInfo(hOut, TRUE, &tiny);
        COORD bs = {COLS, ROWS};
        SetConsoleScreenBufferSize(hOut, bs);
        SMALL_RECT rc = {0, 0, COLS - 1, ROWS - 1};
        SetConsoleWindowInfo(hOut, TRUE, &rc);
      }
    }
  }
  HideCursor();
}
static void RestoreConsole(void) {
  CONSOLE_SCREEN_BUFFER_INFO i;
  if (GetConsoleScreenBufferInfo(hOut, &i)) {
    DWORD w;
    int ww = i.srWindow.Right - i.srWindow.Left + 1;
    for (int y = i.srWindow.Top; y <= i.srWindow.Bottom; y++) {
      COORD o = {i.srWindow.Left, (SHORT)y};
      FillConsoleOutputCharacterW(hOut, L' ', (DWORD)ww, o, &w);
      FillConsoleOutputAttribute(hOut, 7, (DWORD)ww, o, &w);
    }
    COORD home = {i.srWindow.Left, i.srWindow.Top};
    SetConsoleCursorPosition(hOut, home);
  }
  SetConsoleTextAttribute(hOut, 7);
  CONSOLE_CURSOR_INFO ci = {25, TRUE};
  SetConsoleCursorInfo(hOut, &ci);
  SetConsoleMode(hIn, origInMode);
}

/* ------------------------------------------------------------------------ */
/*  Font pixel 3x5                                                           */
/* ------------------------------------------------------------------------ */
static const uint8_t FONT[128][5] = {
    ['A'] = {2, 5, 7, 5, 5}, ['B'] = {6, 5, 6, 5, 6}, ['C'] = {3, 4, 4, 4, 3},
    ['D'] = {6, 5, 5, 5, 6}, ['E'] = {7, 4, 6, 4, 7}, ['F'] = {7, 4, 6, 4, 4},
    ['G'] = {3, 4, 5, 5, 3}, ['H'] = {5, 5, 7, 5, 5}, ['I'] = {7, 2, 2, 2, 7},
    ['J'] = {1, 1, 1, 5, 2}, ['K'] = {5, 5, 6, 5, 5}, ['L'] = {4, 4, 4, 4, 7},
    ['M'] = {5, 7, 7, 5, 5}, ['N'] = {6, 5, 5, 5, 5}, ['O'] = {2, 5, 5, 5, 2},
    ['P'] = {6, 5, 6, 4, 4}, ['Q'] = {2, 5, 5, 6, 3}, ['R'] = {6, 5, 6, 5, 5},
    ['S'] = {3, 4, 2, 1, 6}, ['T'] = {7, 2, 2, 2, 2}, ['U'] = {5, 5, 5, 5, 7},
    ['V'] = {5, 5, 5, 5, 2}, ['W'] = {5, 5, 7, 7, 5}, ['X'] = {5, 5, 2, 5, 5},
    ['Y'] = {5, 5, 2, 2, 2}, ['Z'] = {7, 1, 2, 4, 7}, ['0'] = {7, 5, 5, 5, 7},
    ['1'] = {2, 6, 2, 2, 7}, ['2'] = {6, 1, 2, 4, 7}, ['3'] = {6, 1, 2, 1, 6},
    ['4'] = {5, 5, 7, 1, 1}, ['5'] = {7, 4, 6, 1, 6}, ['6'] = {3, 4, 7, 5, 7},
    ['7'] = {7, 1, 2, 2, 2}, ['8'] = {7, 5, 7, 5, 7}, ['9'] = {7, 5, 7, 1, 6},
    ['!'] = {2, 2, 2, 0, 2}, ['-'] = {0, 0, 7, 0, 0}, [':'] = {0, 2, 0, 2, 0},
    ['.'] = {0, 0, 0, 0, 2}, ['+'] = {0, 2, 7, 2, 0}, ['/'] = {1, 1, 2, 4, 4},
    ['#'] = {5, 7, 5, 7, 5},
};
static int PTextEx(int x, int y, const char *s, int c, int sc,
                   const uint8_t *grad) {
  int cx = x;
  for (; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch >= 'a' && ch <= 'z')
      ch = (unsigned char)(ch - 32);
    const uint8_t *g = FONT[ch & 127];
    for (int r = 0; r < 5; r++) {
      int col = grad ? grad[r] : c;
      for (int k = 0; k < 3; k++)
        if (g[r] & (4 >> k))
          for (int sy = 0; sy < sc; sy++)
            for (int sx = 0; sx < sc; sx++)
              PSet(cx + k * sc + sx, y + r * sc + sy, col);
    }
    cx += 4 * sc;
  }
  return cx - x;
}
static int PText(int x, int y, const char *s, int c, int sc) {
  return PTextEx(x, y, s, c, sc, NULL);
}
static int PTextW(const char *s, int sc) {
  int n = (int)strlen(s);
  return n ? n * 4 * sc - sc : 0;
}
static void PTextCEx(int y, const char *s, int c, int sc, const uint8_t *grad,
                     bool shadow) {
  int x = (PW - PTextW(s, sc)) / 2;
  if (shadow)
    PTextEx(x + 1, y + 1, s, 0, sc, NULL);
  PTextEx(x, y, s, c, sc, grad);
}

/* ------------------------------------------------------------------------ */
/*  Pixel-art sprites (letter = color, '.' = transparent)                    */
/*  K black b dark-blue g dark-green c dark-cyan r dark-red m dark-magenta   */
/*  y dark-yellow w gray k dark-gray B blue G green C cyan R red M magenta   */
/*  Y yellow W white. Symmetrical sprite needs only half + center column     */
/* ------------------------------------------------------------------------ */
typedef struct {
  int8_t px[16][32];
  int w, h;
} Sprite;

static int ColorOf(char ch) {
  switch (ch) {
  case 'K':
    return 0;
  case 'b':
    return 1;
  case 'g':
    return 2;
  case 'c':
    return 3;
  case 'r':
    return 4;
  case 'm':
    return 5;
  case 'y':
    return 6;
  case 'w':
    return 7;
  case 'k':
    return 8;
  case 'B':
    return 9;
  case 'G':
    return 10;
  case 'C':
    return 11;
  case 'R':
    return 12;
  case 'M':
    return 13;
  case 'Y':
    return 14;
  case 'W':
    return 15;
  default:
    return -1;
  }
}
static void SprFrom(Sprite *s, const char *const *rows, int h, bool sym) {
  memset(s->px, 0xFF, sizeof s->px);
  s->h = h;
  s->w = 0;
  for (int y = 0; y < h && y < 16; y++) {
    int n = (int)strlen(rows[y]);
    int w = sym ? n * 2 - 1 : n;
    for (int x = 0; x < n && x < 32; x++) {
      int c = ColorOf(rows[y][x]);
      s->px[y][x] = (int8_t)c;
      if (sym && w - 1 - x < 32)
        s->px[y][w - 1 - x] = (int8_t)c;
    }
    if (w > s->w)
      s->w = w;
  }
}
static void SprDraw(const Sprite *s, int cx, int cy, bool flash,
                    const int8_t *remap) {
  int x0 = cx - s->w / 2, y0 = cy - s->h / 2;
  for (int y = 0; y < s->h; y++)
    for (int x = 0; x < s->w; x++) {
      int c = s->px[y][x];
      if (c < 0)
        continue;
      if (flash)
        c = 15;
      else if (remap && remap[c] >= 0)
        c = remap[c];
      PSet(x0 + x, y0 + y, c);
    }
}

static const char *const SP_PLAYER[] = {"....W", "...WC", "...CB", "..kCB",
                                        ".kRCB", "kwwBC", "kwBBB", ".R.kk"};
static const char *const SP_DRONE[] = {".G..", "GGGG", "GgWg", ".GGG", "..g."};
static const char *const SP_FIGHTER[] = {"Y....", "YY.RR", "YYRRW",
                                         ".YRRR", "..Y.R", "....R"};
static const char *const SP_BOMBER[] = {"...mMMM", "..mMWWW", ".mMMbMM",
                                        "mMMMMMM", "mMkMkMk", ".mm.mMM",
                                        "...m..m"};
static const char *const SP_DIVER[] = {"R..", "RR.", ".RW", ".RR", "..R"};
static const char *const SP_ORB[] = {"..CC", ".CBB", "CBBW", "CBWW",
                                     "CBBW", ".CBB", "..CC"};
static const char *const SP_ROCK[] = {"..kwk..", ".kwwwk.", "kwwkwwk",
                                      "kwwwwyk", ".kywwk.", "..kkk.."};
static const char *const SP_PEBBLE[] = {".kw.", "kwwk", ".kk."};
static const char *const SP_BOSS[] = {
    "...........RRRRR", ".........RRMMMMM", "..kkk...RMMMWWWW",
    ".kwwwk.RMMWWBBBB", "kwwwwwkRMWBBCCCC", "kwRRwwwMMWBCCWWW",
    "kwRRwwwwMWBCCWWW", "kwwwwwkkMMBBCCCC", ".kwwwk.RMMMBBBBB",
    "..kRk..RRMMMMMMM", "..kRk...RrRRrRRR", "...Y.....R..R..R",
};

enum {
  E_DRONE,
  E_FIGHTER,
  E_BOMBER,
  E_DIVER,
  E_ORB,
  E_ROCK,
  E_PEBBLE,
  E_BOSS,
  E_TYPES
};
static Sprite sPlayer, sDrone, sFighter, sBomber, sDiver, sOrb, sRock, sPebble,
    sBoss;
static Sprite *ESPR[E_TYPES];
static int8_t REMAP[3][16];

static void InitSprites(void) {
  SprFrom(&sPlayer, SP_PLAYER, NROWS(SP_PLAYER), true);
  SprFrom(&sDrone, SP_DRONE, NROWS(SP_DRONE), true);
  SprFrom(&sFighter, SP_FIGHTER, NROWS(SP_FIGHTER), true);
  SprFrom(&sBomber, SP_BOMBER, NROWS(SP_BOMBER), true);
  SprFrom(&sDiver, SP_DIVER, NROWS(SP_DIVER), true);
  SprFrom(&sOrb, SP_ORB, NROWS(SP_ORB), true);
  SprFrom(&sRock, SP_ROCK, NROWS(SP_ROCK), false);
  SprFrom(&sPebble, SP_PEBBLE, NROWS(SP_PEBBLE), false);
  SprFrom(&sBoss, SP_BOSS, NROWS(SP_BOSS), true);
  ESPR[E_DRONE] = &sDrone;
  ESPR[E_FIGHTER] = &sFighter;
  ESPR[E_BOMBER] = &sBomber;
  ESPR[E_DIVER] = &sDiver;
  ESPR[E_ORB] = &sOrb;
  ESPR[E_ROCK] = &sRock;
  ESPR[E_PEBBLE] = &sPebble;
  ESPR[E_BOSS] = &sBoss;
  memset(REMAP, 0xFF, sizeof REMAP);
  REMAP[1][13] = 10;
  REMAP[1][12] = 2;
  REMAP[1][4] = 2; /* sector 2 boss: green  */
  REMAP[2][13] = 14;
  REMAP[2][12] = 6; /* sector 3 boss: gold   */
}

/* ------------------------------------------------------------------------ */
/*  Audio: real-time synthesizer via waveOut (SFX + chiptune music)          */
/* ------------------------------------------------------------------------ */
#define SR 22050
#define ABUF 512
#define NBUF 4
#define NVOICE 20
enum { WV_SQ, WV_TRI, WV_SAW, WV_NOISE, WV_SINE };
enum { MUS_OFF, MUS_TITLE, MUS_GAME, MUS_BOSS };
enum {
  S_SHOOT,
  S_MISSILE,
  S_HIT,
  S_EXPL_S,
  S_EXPL_M,
  S_EXPL_L,
  S_POWER,
  S_GEM,
  S_BOMB,
  S_DIE,
  S_GRAZE,
  S_WARN,
  S_MENU,
  S_SELECT,
  S_1UP,
  S_SHIELD,
  S_DIVE,
  S_PHASE,
  S_COUNT
};

typedef struct {
  int on, wave, len, pos, delay;
  float ph, f0, f1, vol, duty, nv;
} Voice;
typedef struct {
  int wave;
  float ph, f, vol, env, dec, duty, nv, slide;
} MCh;

static Voice vc[NVOICE];
static MCh mc[6];
static CRITICAL_SECTION acs;
static HWAVEOUT hwo;
static WAVEHDR whdr[NBUF];
static short abuf[NBUF][ABUF];
static HANDLE aEvt, aThr;
static volatile LONG aRun = 0;
static int audioOK = 0;
static volatile int musicOn = 1, sfxOn = 1, musicMode = MUS_OFF;
static int musicCur = -1, mStep = 0, mLeft = 0;
static uint32_t arng = 0x1234567u;
static int gframe = 0;

static float ANoise(void) {
  arng ^= arng << 13;
  arng ^= arng >> 17;
  arng ^= arng << 5;
  return (float)(arng & 0xFFFF) / 32768.0f - 1.0f;
}
static float Mtof(int n) {
  return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);
}
static float HalfLife(float sec) {
  return powf(0.5f, 1.0f / (sec * (float)SR));
}
static float Osc(int wave, float *ph, float f, float duty, float *nv) {
  *ph += f / (float)SR;
  if (*ph >= 1.0f) {
    *ph -= floorf(*ph);
    if (wave == WV_NOISE)
      *nv = ANoise();
  }
  switch (wave) {
  case WV_SQ:
    return *ph < duty ? 1.0f : -1.0f;
  case WV_TRI:
    return 4.0f * fabsf(*ph - 0.5f) - 1.0f;
  case WV_SAW:
    return 2.0f * *ph - 1.0f;
  case WV_NOISE:
    return *nv;
  default:
    return sinf(*ph * TAU_F);
  }
}

/* Music track: A minor, 8 bars x 16 steps. Am F C G | Am F G E */
static const int MEL[128] = {
    76, 0,  0,  74, 72, 0,  69, 0,  72, 0,  74, 0,  76, 0,  0,  0,  77, 0,  0,
    76, 74, 0,  72, 0,  69, 0,  72, 0,  77, 0,  76, 0,  76, 0,  0,  74, 72, 0,
    67, 0,  72, 0,  76, 0,  79, 0,  0,  0,  77, 0,  76, 0,  74, 0,  71, 0,  74,
    0,  0,  0,  76, 0,  74, 0,  81, 0,  0,  79, 76, 0,  0,  72, 74, 0,  76, 0,
    69, 0,  0,  0,  72, 0,  0,  74, 77, 0,  0,  76, 74, 0,  72, 0,  69, 0,  0,
    0,  71, 0,  74, 0,  79, 0,  0,  77, 76, 0,  74, 0,  71, 0,  74, 0,  76, 0,
    0,  0,  71, 0,  0,  0,  68, 0,  71, 0,  76, 0,  0,  0,
};
static const int BROOT[8] = {45, 41, 48, 43, 45, 41, 43, 40};
static const int AROOT[8] = {57, 53, 60, 55, 57, 53, 55, 52};
static const int MINOR[8] = {1, 0, 0, 0, 1, 0, 0, 0};
static const int BPAT[16] = {1, 0, 1, 2, 0, 1, 0, 2, 1, 0, 1, 2, 0, 1, 2, 1};
static const int APAT[8] = {0, 1, 2, 3, 2, 1, 0, 1};

static void MNote(int ch, int wave, float f, float vol, float dec, float duty,
                  float slide) {
  MCh *c = &mc[ch];
  c->wave = wave;
  c->f = f;
  c->vol = vol;
  c->env = 1.0f;
  c->dec = dec;
  c->duty = duty;
  c->slide = slide;
}
static void MusicStep(int mode) {
  int bar = (mStep / 16) % 8, s = mStep % 16;
  int n = MEL[bar * 16 + s];
  if (n > 0 && mode != MUS_TITLE)
    MNote(0, WV_SQ, Mtof(n), 0.07f, HalfLife(0.30f), 0.5f, 1.0f);
  if (n > 0 && mode == MUS_TITLE)
    MNote(0, WV_TRI, Mtof(n + 12), 0.12f, HalfLife(0.35f), 0.5f, 1.0f);
  int tones[4] = {0, MINOR[bar] ? 3 : 4, 7, 12};
  MNote(1, WV_SQ, Mtof(AROOT[bar] + tones[APAT[s % 8]]), 0.028f,
        HalfLife(0.05f), 0.125f, 1.0f);
  int b = BPAT[s];
  if (b)
    MNote(2, WV_TRI, Mtof(BROOT[bar] + (b == 2 ? 12 : 0)), 0.22f,
          HalfLife(0.13f), 0.5f, 1.0f);
  if (mode >= MUS_GAME) {
    bool kick = (s == 0 || s == 8 || s == 10) ||
                (mode == MUS_BOSS && (s == 4 || s == 12));
    if (kick)
      MNote(3, WV_SINE, 150.0f, 0.38f, HalfLife(0.07f), 0.5f, 0.9993f);
    if (s == 4 || s == 12)
      MNote(4, WV_NOISE, 9000.0f, 0.15f, HalfLife(0.045f), 0.5f, 1.0f);
    if (s % 2 == 1 || mode == MUS_BOSS)
      MNote(5, WV_NOISE, 22050.0f, 0.045f, HalfLife(0.012f), 0.5f, 1.0f);
  } else if (s % 4 == 2) {
    MNote(5, WV_NOISE, 22050.0f, 0.03f, HalfLife(0.012f), 0.5f, 1.0f);
  }
  mStep++;
}
static void FillAudio(short *out) {
  EnterCriticalSection(&acs);
  int mode = musicOn ? musicMode : MUS_OFF;
  if (mode != musicCur) {
    musicCur = mode;
    mStep = 0;
    mLeft = 0;
    for (int c = 0; c < 6; c++)
      mc[c].env = 0;
  }
  int bpm = mode == MUS_BOSS ? 152 : (mode == MUS_TITLE ? 112 : 136);
  int sps = SR * 60 / (bpm * 4);
  for (int i = 0; i < ABUF; i++) {
    float mix = 0, mus = 0;
    if (mode != MUS_OFF) {
      if (--mLeft <= 0) {
        MusicStep(mode);
        mLeft = sps;
      }
      for (int c = 0; c < 6; c++) {
        MCh *m = &mc[c];
        if (m->env > 0.0005f) {
          mus += Osc(m->wave, &m->ph, m->f, m->duty, &m->nv) * m->vol * m->env;
          m->env *= m->dec;
          m->f *= m->slide;
        }
      }
    }
    for (int v = 0; v < NVOICE; v++) {
      Voice *p = &vc[v];
      if (!p->on)
        continue;
      if (p->delay > 0) {
        p->delay--;
        continue;
      }
      float t = (float)p->pos / (float)p->len;
      float f = p->f0 + (p->f1 - p->f0) * t;
      float env = 1.0f - t;
      if (p->pos < 64)
        env *= (float)p->pos / 64.0f;
      mix += Osc(p->wave, &p->ph, f, p->duty, &p->nv) * env * p->vol;
      if (++p->pos >= p->len)
        p->on = 0;
    }
    float s = mix * 0.9f + mus * 0.7f;
    if (s > 1.0f)
      s = 1.0f;
    if (s < -1.0f)
      s = -1.0f;
    out[i] = (short)(s * 30000.0f);
  }
  LeaveCriticalSection(&acs);
}
static DWORD WINAPI AudioThread(LPVOID p) {
  (void)p;
  while (aRun) {
    WaitForSingleObject(aEvt, 30);
    for (int i = 0; i < NBUF && aRun; i++) {
      if (whdr[i].dwFlags & WHDR_DONE) {
        FillAudio(abuf[i]);
        whdr[i].dwFlags &= ~(DWORD)WHDR_DONE;
        waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR));
      }
    }
  }
  return 0;
}
static void AudioInit(void) {
  InitializeCriticalSection(&acs);
  WAVEFORMATEX wf;
  memset(&wf, 0, sizeof wf);
  wf.wFormatTag = WAVE_FORMAT_PCM;
  wf.nChannels = 1;
  wf.nSamplesPerSec = SR;
  wf.wBitsPerSample = 16;
  wf.nBlockAlign = 2;
  wf.nAvgBytesPerSec = SR * 2;
  aEvt = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)aEvt, 0, CALLBACK_EVENT) !=
      MMSYSERR_NOERROR) {
    audioOK = 0;
    return;
  }
  for (int i = 0; i < NBUF; i++) {
    memset(&whdr[i], 0, sizeof(WAVEHDR));
    whdr[i].lpData = (LPSTR)abuf[i];
    whdr[i].dwBufferLength = ABUF * sizeof(short);
    waveOutPrepareHeader(hwo, &whdr[i], sizeof(WAVEHDR));
    FillAudio(abuf[i]);
    waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR));
  }
  audioOK = 1;
  aRun = 1;
  aThr = CreateThread(NULL, 0, AudioThread, NULL, 0, NULL);
  if (aThr)
    SetThreadPriority(aThr, THREAD_PRIORITY_ABOVE_NORMAL);
}
static void AudioShutdown(void) {
  if (!audioOK)
    return;
  aRun = 0;
  SetEvent(aEvt);
  if (aThr) {
    WaitForSingleObject(aThr, 1000);
    CloseHandle(aThr);
  }
  waveOutReset(hwo);
  for (int i = 0; i < NBUF; i++)
    waveOutUnprepareHeader(hwo, &whdr[i], sizeof(WAVEHDR));
  waveOutClose(hwo);
  CloseHandle(aEvt);
}
static void SetMusic(int m) { musicMode = m; }

static void Tone(int wave, float f0, float f1, int ms, float vol, float duty,
                 int delayMs) {
  int best = 0, bestPos = -1;
  for (int i = 0; i < NVOICE; i++) {
    if (!vc[i].on) {
      best = i;
      break;
    }
    if (vc[i].pos > bestPos) {
      bestPos = vc[i].pos;
      best = i;
    }
  }
  Voice *v = &vc[best];
  v->on = 1;
  v->wave = wave;
  v->f0 = f0;
  v->f1 = f1;
  v->len = Maxi(1, ms * SR / 1000);
  v->pos = 0;
  v->delay = delayMs * SR / 1000;
  v->vol = vol;
  v->duty = duty;
  v->ph = 0;
  v->nv = 0;
}
static int sfxLast[S_COUNT];
static const int SFX_GAP[S_COUNT] = {4,  6, 3,  3, 4, 8,  5,  2, 10,
                                     10, 3, 30, 2, 5, 10, 10, 6, 20};
static void Sfx(int id) {
  if (!audioOK || !sfxOn)
    return;
  if (gframe - sfxLast[id] < SFX_GAP[id] && gframe >= sfxLast[id])
    return;
  sfxLast[id] = gframe;
  EnterCriticalSection(&acs);
  switch (id) {
  case S_SHOOT:
    Tone(WV_SQ, 1500, 500, 45, 0.05f, 0.25f, 0);
    break;
  case S_MISSILE:
    Tone(WV_NOISE, 3000, 800, 90, 0.05f, 0.5f, 0);
    break;
  case S_HIT:
    Tone(WV_SQ, 260, 120, 30, 0.06f, 0.5f, 0);
    break;
  case S_EXPL_S:
    Tone(WV_NOISE, 5000, 300, 220, 0.25f, 0.5f, 0);
    Tone(WV_SQ, 180, 40, 120, 0.07f, 0.5f, 0);
    break;
  case S_EXPL_M:
    Tone(WV_NOISE, 4000, 150, 400, 0.32f, 0.5f, 0);
    Tone(WV_TRI, 140, 30, 300, 0.25f, 0.5f, 0);
    break;
  case S_EXPL_L:
    Tone(WV_NOISE, 3000, 60, 900, 0.40f, 0.5f, 0);
    Tone(WV_TRI, 100, 20, 700, 0.35f, 0.5f, 0);
    Tone(WV_NOISE, 1500, 80, 700, 0.20f, 0.5f, 120);
    break;
  case S_POWER:
    for (int k = 0; k < 4; k++)
      Tone(WV_SQ, 660.0f * (1 + k * 0.5f), 660.0f * (1 + k * 0.5f), 70, 0.07f,
           0.5f, k * 60);
    break;
  case S_GEM:
    Tone(WV_TRI, 2000, 2600, 50, 0.07f, 0.5f, 0);
    break;
  case S_BOMB:
    Tone(WV_NOISE, 6000, 50, 1400, 0.45f, 0.5f, 0);
    Tone(WV_SQ, 400, 30, 1000, 0.10f, 0.5f, 0);
    Tone(WV_TRI, 80, 20, 1200, 0.40f, 0.5f, 0);
    break;
  case S_DIE:
    Tone(WV_NOISE, 4000, 40, 1200, 0.45f, 0.5f, 0);
    Tone(WV_SQ, 600, 40, 900, 0.12f, 0.5f, 0);
    break;
  case S_GRAZE:
    Tone(WV_SQ, 2800, 3200, 18, 0.025f, 0.5f, 0);
    break;
  case S_WARN:
    for (int k = 0; k < 4; k++) {
      Tone(WV_SQ, 400, 900, 200, 0.08f, 0.5f, k * 420);
      Tone(WV_SQ, 900, 400, 200, 0.08f, 0.5f, k * 420 + 200);
    }
    break;
  case S_MENU:
    Tone(WV_SQ, 880, 880, 35, 0.05f, 0.5f, 0);
    break;
  case S_SELECT:
    Tone(WV_SQ, 660, 1320, 90, 0.07f, 0.5f, 0);
    Tone(WV_SQ, 990, 1980, 90, 0.05f, 0.25f, 40);
    break;
  case S_1UP: {
    const float n[4] = {1046, 1318, 1568, 2093};
    for (int k = 0; k < 4; k++)
      Tone(WV_SQ, n[k], n[k], 100, 0.07f, 0.5f, k * 80);
  } break;
  case S_SHIELD:
    Tone(WV_TRI, 400, 1600, 200, 0.12f, 0.5f, 0);
    break;
  case S_DIVE:
    Tone(WV_SQ, 1200, 300, 250, 0.04f, 0.125f, 0);
    break;
  case S_PHASE:
    Tone(WV_SAW, 100, 600, 500, 0.12f, 0.5f, 0);
    break;
  default:
    break;
  }
  LeaveCriticalSection(&acs);
}

/* ------------------------------------------------------------------------ */
/*  Input (GetAsyncKeyState + press detection) */
/* ------------------------------------------------------------------------ */
static uint8_t kNow[256], kOld[256];
#define KD(k) (kNow[(k)])
#define KP(k) (kNow[(k)] && !kOld[(k)])
static bool PUp(void) { return KP(VK_UP) || KP('W'); }
static bool PDown(void) { return KP(VK_DOWN) || KP('S'); }
static bool PLeft(void) { return KP(VK_LEFT) || KP('A'); }
static bool PRight(void) { return KP(VK_RIGHT) || KP('D'); }
static bool POK(void) {
  return KP(VK_RETURN) || KP(VK_SPACE) || KP('Z') || KP('J');
}
static bool PBack(void) { return KP(VK_ESCAPE) || KP(VK_BACK); }

static bool HasFocus(void) {
  HWND cw = GetConsoleWindow();
  if (!cw)
    return true;
  HWND fg = GetForegroundWindow();
  if (IsWindowVisible(cw))
    return fg == cw;                  /* classic console host  */
  HWND own = GetWindow(cw, GW_OWNER); /* Windows Terminal      */
  if (own)
    return own == fg || GetAncestor(own, GA_ROOT) == fg;
  return true;
}

/* ------------------------------------------------------------------------ */
/*  Save data: high scores + settings                                        */
/* ------------------------------------------------------------------------ */
typedef struct {
  char name[4];
  long long score;
  int stage;
} HiEntry;
typedef struct {
  uint32_t magic;
  int diff, music, sfx;
  HiEntry hs[5];
} SaveData;
#define SAVE_MAGIC 0x56414E32u
static SaveData save;
static char savePath[MAX_PATH];
static int difficulty = 1;
static const char *DIFF_NAME[3] = {"EASY", "NORMAL", "HARD"};

static void LoadSave(void) {
  GetModuleFileNameA(NULL, savePath, MAX_PATH);
  char *sl = strrchr(savePath, '\\');
  if (sl)
    sl[1] = 0;
  else
    savePath[0] = 0;
  strcat(savePath, "vanguard.sav");
  FILE *f = fopen(savePath, "rb");
  bool ok = false;
  if (f) {
    ok = fread(&save, sizeof save, 1, f) == 1 && save.magic == SAVE_MAGIC;
    fclose(f);
  }
  if (!ok) {
    const char *n[5] = {"ACE", "NEO", "ZED", "KAI", "RIO"};
    const long long sc[5] = {200000, 120000, 80000, 40000, 15000};
    memset(&save, 0, sizeof save);
    save.magic = SAVE_MAGIC;
    save.diff = 1;
    save.music = 1;
    save.sfx = 1;
    for (int i = 0; i < 5; i++) {
      strcpy(save.hs[i].name, n[i]);
      save.hs[i].score = sc[i];
      save.hs[i].stage = 5 - i;
    }
  }
  difficulty = (save.diff >= 0 && save.diff <= 2) ? save.diff : 1;
  musicOn = save.music ? 1 : 0;
  sfxOn = save.sfx ? 1 : 0;
}
static void WriteSave(void) {
  if (g_selftest)
    return;
  save.magic = SAVE_MAGIC;
  save.diff = difficulty;
  save.music = musicOn;
  save.sfx = sfxOn;
  FILE *f = fopen(savePath, "wb");
  if (f) {
    fwrite(&save, sizeof save, 1, f);
    fclose(f);
  }
}
static long long HiScore(void) { return save.hs[0].score; }
static int QualifyRank(long long sc) {
  for (int i = 0; i < 5; i++)
    if (sc > save.hs[i].score)
      return i;
  return -1;
}

/* ------------------------------------------------------------------------ */
/*  Game entities                                                            */
/* ------------------------------------------------------------------------ */
enum { MV_LINE, MV_SINE, MV_HOVER, MV_ARC, MV_DIVE, MV_BOSS };
enum { PK_POWER, PK_BOMB, PK_LIFE, PK_SHIELD, PK_GEM };
enum { PA_FIRE, PA_SPARK, PA_SMOKE };
enum { SPH_INTRO, SPH_WAVES, SPH_WARNING, SPH_BOSS, SPH_CLEAR };
enum { ST_TITLE, ST_HOWTO, ST_SCORES, ST_PLAY, ST_PAUSE, ST_GAMEOVER, ST_NAME };

typedef struct {
  bool on, entered;
  int type, mv;
  float x, y, vx, vy, bx, by, p1, p2;
  int hp, maxhp, t, stay, flash, shootT, dying, score, w, h;
} Enemy;
typedef struct {
  bool on;
  float x, y, vx, vy, spd;
  int kind, dmg, tgt, life;
} PShot;
typedef struct {
  bool on, grazed;
  float x, y, vx, vy;
  int kind;
} EShot;
typedef struct {
  bool on;
  int type, t;
  float x, y, vx, vy;
} Pickup;
typedef struct {
  bool on;
  int type, life, max;
  float x, y, vx, vy;
  uint8_t col;
} Part;
typedef struct {
  bool on;
  float x, y, r, dr;
  int life, max;
  uint8_t col;
} Ring;
typedef struct {
  bool on;
  float x, y;
  int life;
  uint8_t col;
  char txt[16];
} Popup;
typedef struct {
  float x, y, spd;
  uint8_t col;
  int layer;
} Star;
typedef struct {
  float x, y;
  int lives, bombs, power;
  int invuln, fireT, missT, dead, respawnT, shieldT, focus;
  int bombT;
  float bombR, bombX, bombY;
  long long score, nextExtend;
  int chain, chainT, maxChain, graze, stageGraze, kills, noMiss;
} Player;

#define MAX_EN 48
#define MAX_PS 128
#define MAX_ES 400
#define MAX_PK 96
#define MAX_PA 700
#define MAX_RG 24
#define MAX_PP 24
#define MAX_ST 80

static Enemy en[MAX_EN];
static PShot ps[MAX_PS];
static EShot es[MAX_ES];
static Pickup pk[MAX_PK];
static Part pa[MAX_PA];
static Ring rg[MAX_RG];
static Popup pp[MAX_PP];
static Star stars[MAX_ST];
static Player P;

static int state = ST_TITLE, menuSel = 0, pauseSel = 0;
static int stage = 1, loopN = 0, theme = 0, sphase = SPH_INTRO, stimer = 0,
           waveT = 0, wavesDone = 0, wavesTotal = 0;
static float scroll = 1.0f, bgScroll = 0.0f, planetY = 40.0f, shake = 0.0f;
static int flashT = 0, flashCol = 15, bossIdx = -1, goTimer = 0, nameRank = -1,
           namePos = 0, scoreHL = -1;
static long long bonusBoss = 0, bonusNoMiss = 0, bonusGraze = 0;
static char nameBuf[4] = "AAA";
static float titleShipX = 48.0f;
static const char *SECTOR[3] = {"ORION BELT", "NEBULA VEIL", "CRIMSON CORE"};

static void EnterGameOver(void);

/* ---- difficulty ---- */
static float BSpd(void) {
  static const float d[3] = {0.8f, 1.0f, 1.15f};
  return d[difficulty] * (1.0f + 0.12f * (float)loopN);
}
static float FireMul(void) {
  static const float d[3] = {0.6f, 1.0f, 1.35f};
  return d[difficulty] * (1.0f + 0.25f * (float)loopN) *
         (1.0f + 0.05f * (float)theme);
}

/* ---- effects ---- */
static void AddPart(float x, float y, float vx, float vy, int life, int type,
                    int col) {
  static int cur = 0;
  for (int k = 0; k < MAX_PA; k++) {
    int i = (cur + k) % MAX_PA;
    if (!pa[i].on) {
      Part *p = &pa[i];
      p->on = true;
      p->x = x;
      p->y = y;
      p->vx = vx;
      p->vy = vy;
      p->life = p->max = Maxi(1, life);
      p->type = type;
      p->col = (uint8_t)col;
      cur = i + 1;
      return;
    }
  }
}
static void AddRing(float x, float y, float r, float dr, int life, int col) {
  for (int i = 0; i < MAX_RG; i++)
    if (!rg[i].on) {
      Ring *g = &rg[i];
      g->on = true;
      g->x = x;
      g->y = y;
      g->r = r;
      g->dr = dr;
      g->life = g->max = life;
      g->col = (uint8_t)col;
      return;
    }
}
static void AddPopup(float x, float y, const char *txt, int col) {
  for (int i = 0; i < MAX_PP; i++)
    if (!pp[i].on) {
      Popup *p = &pp[i];
      p->on = true;
      p->x = x;
      p->y = y;
      p->life = 45;
      p->col = (uint8_t)col;
      strncpy(p->txt, txt, 15);
      p->txt[15] = 0;
      return;
    }
}
static void AddPickup(int type, float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_PK; i++)
    if (!pk[i].on) {
      Pickup *p = &pk[i];
      p->on = true;
      p->type = type;
      p->t = 0;
      p->x = x;
      p->y = y;
      p->vx = vx;
      p->vy = vy;
      return;
    }
}
static void DropGems(float x, float y, int n) {
  for (int i = 0; i < n; i++)
    AddPickup(PK_GEM, x, y, Rr(-0.8f, 0.8f), Rr(-1.2f, 0.2f));
}
static void Explode(float x, float y, int size) {
  int n = 6 + size * 9;
  for (int i = 0; i < n; i++) {
    float a = Rf() * TAU_F, s = Rr(0.15f, 0.5f + (float)size * 0.32f);
    AddPart(x, y, cosf(a) * s, sinf(a) * s * 0.8f, Ri(7, 12 + size * 5),
            PA_FIRE, 15);
  }
  for (int i = 0; i < size * 4; i++) {
    float a = Rf() * TAU_F, s = Rr(0.8f, 1.6f + (float)size * 0.3f);
    AddPart(x, y, cosf(a) * s, sinf(a) * s, Ri(5, 12), PA_SPARK,
            Rf() < 0.5f ? 14 : 15);
  }
  if (size >= 2)
    AddRing(x, y, 1.0f, 0.7f + (float)size * 0.35f, 10 + size * 3,
            size >= 3 ? 15 : 14);
  if (size >= 3)
    for (int i = 0; i < size * 3; i++) {
      float a = Rf() * TAU_F, s = Rr(0.1f, 0.4f);
      AddPart(x + cosf(a) * 2, y + sinf(a) * 2, cosf(a) * s, sinf(a) * s,
              Ri(15, 30), PA_SMOKE, 8);
    }
  if (shake < (float)size * 1.6f)
    shake = (float)size * 1.6f;
  Sfx(size <= 1 ? S_EXPL_S : (size == 2 ? S_EXPL_M : S_EXPL_L));
}

/* ---- enemy bullets ---- */
static float AimAt(float x, float y) { return atan2f(P.y - y, P.x - x); }
static void EFire(float x, float y, float ang, float spd, int kind) {
  for (int i = 0; i < MAX_ES; i++)
    if (!es[i].on) {
      EShot *b = &es[i];
      float s = spd * BSpd();
      b->on = true;
      b->grazed = false;
      b->x = x;
      b->y = y;
      b->vx = cosf(ang) * s;
      b->vy = sinf(ang) * s;
      b->kind = kind;
      return;
    }
}
static void EFan(float x, float y, float ang, int n, float spread, float spd,
                 int kind) {
  if (n <= 1) {
    EFire(x, y, ang, spd, kind);
    return;
  }
  for (int i = 0; i < n; i++)
    EFire(x, y, ang - spread * 0.5f + spread * (float)i / (float)(n - 1), spd,
          kind);
}
static void ERing(float x, float y, int n, float off, float spd, int kind) {
  for (int i = 0; i < n; i++)
    EFire(x, y, off + TAU_F * (float)i / (float)n, spd, kind);
}
static void ClearBulletsNear(float x, float y, float r) {
  for (int i = 0; i < MAX_ES; i++) {
    if (!es[i].on)
      continue;
    float dx = es[i].x - x, dy = es[i].y - y;
    if (dx * dx + dy * dy < r * r) {
      es[i].on = false;
      AddPart(es[i].x, es[i].y, 0, 0, 6, PA_SPARK, 7);
    }
  }
}

/* ---- score ---- */
static int Mult(void) {
  int m = 1 + P.chain / 10;
  return m > 9 ? 9 : m;
}
static void AddScore(long long v) {
  P.score += v;
  while (P.score >= P.nextExtend) {
    P.nextExtend += 150000;
    if (P.lives < 9)
      P.lives++;
    Sfx(S_1UP);
    AddPopup(P.x, P.y - 8, "1UP", 10);
  }
}
static void ClearBulletsToGems(void) {
  int k = 0;
  for (int i = 0; i < MAX_ES; i++) {
    if (!es[i].on)
      continue;
    es[i].on = false;
    AddPart(es[i].x, es[i].y, 0, 0, 8, PA_SPARK, 11);
    if ((k++ % 3) == 0)
      AddPickup(PK_GEM, es[i].x, es[i].y, 0, 0);
    P.score += 10;
  }
}

/* ---- enemies ---- */
static Enemy *SpawnEnemy(int type, float x, float y, int mv) {
  for (int i = 0; i < MAX_EN; i++) {
    if (en[i].on)
      continue;
    Enemy *e = &en[i];
    memset(e, 0, sizeof *e);
    e->on = true;
    e->type = type;
    e->mv = mv;
    e->x = e->bx = x;
    e->y = e->by = y;
    e->w = ESPR[type]->w;
    e->h = ESPR[type]->h;
    e->stay = 360;
    switch (type) {
    case E_DRONE:
      e->hp = 2;
      e->score = 100;
      e->shootT = Ri(90, 240);
      break;
    case E_FIGHTER:
      e->hp = 6;
      e->score = 300;
      e->shootT = Ri(40, 80);
      break;
    case E_BOMBER:
      e->hp = 28;
      e->score = 1500;
      e->shootT = 90;
      break;
    case E_DIVER:
      e->hp = 2;
      e->score = 200;
      e->shootT = 999;
      break;
    case E_ORB:
      e->hp = 14;
      e->score = 800;
      e->shootT = 1;
      break;
    case E_ROCK:
      e->hp = 8;
      e->score = 150;
      e->shootT = 999;
      break;
    case E_PEBBLE:
      e->hp = 2;
      e->score = 50;
      e->shootT = 999;
      break;
    case E_BOSS:
      e->hp = (int)(900.0f * (1.0f + 0.3f * (float)(stage - 1)));
      e->score = 50000;
      break;
    default:
      break;
    }
    if (type != E_BOSS)
      e->hp = (int)ceilf((float)e->hp *
                         (1.0f + 0.5f * (float)loopN + 0.08f * (float)theme));
    e->maxhp = e->hp;
    return e;
  }
  return NULL;
}
static int NearestEnemy(float x, float y) {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &en[i];
    if (!e->on || e->t < 0 || e->dying || e->y < 0 || e->y > PH)
      continue;
    float dx = e->x - x, dy = e->y - y, d = dx * dx + dy * dy;
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}
static int CountEnemies(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].on && en[i].type != E_BOSS)
      n++;
  return n;
}

static void KillEnemy(Enemy *e) {
  if (e->type == E_BOSS) {
    if (!e->dying) {
      e->dying = 150;
      e->hp = 0;
      ClearBulletsToGems();
      Sfx(S_EXPL_L);
      shake = 6;
    }
    return;
  }
  P.chain++;
  P.chainT = 150;
  P.kills++;
  if (P.chain > P.maxChain)
    P.maxChain = P.chain;
  long long sc = (long long)e->score * Mult();
  AddScore(sc);
  char b[16];
  snprintf(b, sizeof b, "%lld", sc);
  AddPopup(e->x, e->y - 3, b, sc >= 1000 ? 14 : 15);
  static const int SIZE[E_TYPES] = {1, 2, 3, 1, 2, 2, 1, 4};
  static const int GEMS[E_TYPES] = {1, 2, 6, 1, 4, 1, 0, 0};
  Explode(e->x, e->y, SIZE[e->type]);
  DropGems(e->x, e->y, GEMS[e->type]);
  if (P.kills % 22 == 0)
    AddPickup(PK_POWER, e->x, e->y, 0, -0.6f);
  else if (e->type == E_BOMBER)
    AddPickup(Rf() < 0.55f ? PK_POWER : PK_BOMB, e->x, e->y, 0, -0.6f);
  else if (e->type == E_ORB && Rf() < 0.35f)
    AddPickup(PK_POWER, e->x, e->y, 0, -0.6f);
  else if (Rf() < 0.012f)
    AddPickup(PK_SHIELD, e->x, e->y, 0, -0.6f);
  if (e->type == E_ROCK)
    for (int k = 0; k < 3; k++) {
      Enemy *s = SpawnEnemy(E_PEBBLE, e->x, e->y, MV_LINE);
      if (s) {
        s->vx = Rr(-0.6f, 0.6f);
        s->vy = Rr(0.2f, 0.7f);
        s->entered = true;
      }
    }
  if ((loopN >= 1 || difficulty == 2) &&
      (e->type == E_DRONE || e->type == E_DIVER) && !P.dead) {
    float dx = e->x - P.x, dy = e->y - P.y;
    if (dx * dx + dy * dy > 16.0f * 16.0f)
      ERing(e->x, e->y, 4 + loopN * 2, Rf() * TAU_F, 0.6f, 0);
  }
  e->on = false;
}
static void DamageEnemy(Enemy *e, int dmg) {
  if (!e->on || e->dying || e->t < 0)
    return;
  if (e->type == E_BOSS && e->t < 150)
    return;
  e->hp -= dmg;
  e->flash = 2;
  if (e->hp <= 0)
    KillEnemy(e);
  else {
    P.score += 10;
    Sfx(S_HIT);
  }
}

/* ---- player ---- */
static void HurtPlayer(void) {
  if (g_god || P.dead || P.invuln > 0 || P.bombT > 0)
    return;
  if (P.shieldT > 0) {
    P.shieldT = 0;
    P.invuln = 90;
    shake = 5;
    AddRing(P.x, P.y, 3, 1.5f, 16, 11);
    Sfx(S_SHIELD);
    ClearBulletsNear(P.x, P.y, 18);
    return;
  }
  P.dead = 1;
  P.respawnT = 110;
  P.lives--;
  P.noMiss = 0;
  P.chain = 0;
  P.chainT = 0;
  Explode(P.x, P.y, 3);
  Explode(P.x, P.y, 2);
  flashT = 4;
  flashCol = 12;
  shake = 12;
  Sfx(S_DIE);
  if (P.power > 1) {
    P.power--;
    AddPickup(PK_POWER, P.x, P.y, 0, -1.2f);
  }
  P.bombs = Maxi(P.bombs, 2);
  ClearBulletsNear(P.x, P.y, 999);
}
static void PShotAdd(float x, float y, float angDeg, int kind, int dmg,
                     float spd) {
  for (int i = 0; i < MAX_PS; i++)
    if (!ps[i].on) {
      PShot *b = &ps[i];
      float a = (angDeg - 90.0f) * PI_F / 180.0f;
      b->on = true;
      b->x = x;
      b->y = y;
      b->spd = spd;
      b->vx = cosf(a) * spd;
      b->vy = sinf(a) * spd;
      b->kind = kind;
      b->dmg = dmg;
      b->tgt = -1;
      b->life = 0;
      return;
    }
}
static void PlayerFire(void) {
  float sp = P.focus ? 0.3f : 1.0f;
  PShotAdd(P.x - 1, P.y - 4, 0, 0, 1, 3.6f);
  PShotAdd(P.x + 1, P.y - 4, 0, 0, 1, 3.6f);
  if (P.power >= 2) {
    PShotAdd(P.x - 3, P.y - 2, -6 * sp, 0, 1, 3.4f);
    PShotAdd(P.x + 3, P.y - 2, 6 * sp, 0, 1, 3.4f);
  }
  if (P.power >= 4) {
    PShotAdd(P.x - 4, P.y - 1, -13 * sp, 0, 1, 3.2f);
    PShotAdd(P.x + 4, P.y - 1, 13 * sp, 0, 1, 3.2f);
  }
  if (P.power >= 5)
    PShotAdd(P.x, P.y - 5, 0, 0, 2, 3.8f);
  Sfx(S_SHOOT);
}
static void FireMissiles(void) {
  int pairs = P.power >= 5 ? 2 : 1;
  for (int k = 0; k < pairs; k++) {
    PShotAdd(P.x - 4, P.y + k, -60.0f - k * 20.0f, 1, 3, 0.8f);
    PShotAdd(P.x + 4, P.y + k, 60.0f + k * 20.0f, 1, 3, 0.8f);
  }
  Sfx(S_MISSILE);
}
static void UpdatePlayer(void) {
  if (P.bombT > 0) {
    P.bombT--;
    P.bombR += 2.3f;
    for (int i = 0; i < MAX_EN; i++) {
      Enemy *e = &en[i];
      if (!e->on || e->t < 0 || e->dying)
        continue;
      float dx = e->x - P.bombX, dy = e->y - P.bombY;
      if (dx * dx + dy * dy < P.bombR * P.bombR)
        DamageEnemy(e, 1);
    }
  }
  if (P.chainT > 0 && --P.chainT == 0)
    P.chain = 0;
  if (P.dead) {
    if (--P.respawnT <= 0) {
      if (P.lives > 0) {
        P.dead = 0;
        P.x = PW / 2.0f;
        P.y = PH - 6;
        P.invuln = 150;
      } else
        EnterGameOver();
    }
    return;
  }
  P.focus = KD(VK_SHIFT) ? 1 : 0;
  float dx = 0, dy = 0;
  if (KD(VK_LEFT) || KD('A'))
    dx -= 1;
  if (KD(VK_RIGHT) || KD('D'))
    dx += 1;
  if (KD(VK_UP) || KD('W'))
    dy -= 1;
  if (KD(VK_DOWN) || KD('S'))
    dy += 1;
  if (dx != 0 && dy != 0) {
    dx *= 0.7071f;
    dy *= 0.7071f;
  }
  float sp = P.focus ? 0.55f : 1.15f;
  P.x = Clampf(P.x + dx * sp, 4, PW - 5);
  P.y = Clampf(P.y + dy * sp, 8, PH - 5);

  bool fire = KD(VK_SPACE) || KD('J') || KD('Z');
  if (P.fireT > 0)
    P.fireT--;
  if (P.missT > 0)
    P.missT--;
  if (fire && P.fireT <= 0) {
    PlayerFire();
    P.fireT = 5;
  }
  if (fire && P.power >= 3 && P.missT <= 0) {
    FireMissiles();
    P.missT = P.power >= 5 ? 14 : 22;
  }
  if ((KP('B') || KP('K') || KP('X')) && P.bombs > 0 && P.bombT == 0) {
    P.bombs--;
    P.bombT = 70;
    P.bombR = 2;
    P.bombX = P.x;
    P.bombY = P.y;
    P.invuln = Maxi(P.invuln, 100);
    flashT = 6;
    flashCol = 15;
    shake = 10;
    AddRing(P.x, P.y, 2, 2.2f, 30, 14);
    Sfx(S_BOMB);
  }
  if (P.invuln > 0)
    P.invuln--;
  if (P.shieldT > 0)
    P.shieldT--;
  if (gframe % 2 == 0) {
    AddPart(P.x - 3, P.y + 4, Rr(-0.1f, 0.1f) - dx * 0.2f, Rr(0.6f, 1.0f),
            Ri(4, 7), PA_FIRE, 0);
    AddPart(P.x + 3, P.y + 4, Rr(-0.1f, 0.1f) - dx * 0.2f, Rr(0.6f, 1.0f),
            Ri(4, 7), PA_FIRE, 0);
  }
}

/* ---- boss ---- */
static void UpdateBoss(Enemy *e) {
  int t = e->t;
  float fm = FireMul();
  if (e->dying > 0) {
    e->dying--;
    e->x += Rr(-0.3f, 0.3f);
    e->y += 0.05f;
    if (e->dying % 6 == 0)
      Explode(e->x + Rr(-14, 14), e->y + Rr(-5, 5), Ri(1, 2));
    if (e->dying == 0) {
      Explode(e->x, e->y, 4);
      for (int k = 0; k < 5; k++)
        Explode(e->x + Rr(-12, 12), e->y + Rr(-5, 5), 3);
      flashT = 8;
      flashCol = 15;
      shake = 14;
      DropGems(e->x, e->y, 24);
      AddPickup(PK_POWER, e->x - 6, e->y, 0, -0.6f);
      AddPickup(PK_BOMB, e->x + 6, e->y, 0, -0.6f);
      long long sc = (long long)e->score * stage;
      AddScore(sc);
      char b[16];
      snprintf(b, sizeof b, "%lld", sc);
      AddPopup(e->x, e->y, b, 14);
      e->on = false;
      bossIdx = -1;
      sphase = SPH_CLEAR;
      stimer = 0;
      SetMusic(MUS_TITLE);
    }
    return;
  }
  if (t < 150) {
    e->y += (13.0f - e->y) * 0.03f;
    return;
  }
  float f = (float)e->hp / (float)e->maxhp;
  int ph = f > 0.66f ? 0 : (f > 0.33f ? 1 : 2);
  if (ph != (int)e->p1) {
    e->p1 = (float)ph;
    Sfx(S_PHASE);
    flashT = 4;
    flashCol = 12;
    shake = 8;
    ClearBulletsToGems();
    Explode(e->x - 12, e->y, 2);
    Explode(e->x + 12, e->y, 2);
  }
  int tt = t - 150;
  float spd = ph == 2 ? 0.02f : 0.012f;
  e->x = PW / 2.0f + sinf((float)tt * spd) * 26.0f;
  e->y = 13.0f + sinf((float)tt * spd * 1.9f) * 4.0f;
  if (P.dead)
    return;
  float lx = e->x - 12, rx = e->x + 12, gy = e->y + 5;
  int iv;
  switch (ph) {
  case 0:
    iv = Maxi(12, (int)(55.0f / fm));
    if (tt % iv == 0) {
      EFan(lx, gy, AimAt(lx, gy), 3, 0.5f, 0.95f, 0);
      EFan(rx, gy, AimAt(rx, gy), 3, 0.5f, 0.95f, 0);
    }
    if ((tt % 150) < 50 && tt % Maxi(3, (int)(7.0f / fm)) == 0)
      EFire(e->x, e->y + 6, PI_F / 2 + sinf((float)tt * 0.15f) * 0.5f, 1.3f, 2);
    break;
  case 1:
    iv = Maxi(2, (int)(5.0f / fm));
    if (tt % iv == 0)
      for (int k = 0; k < 3; k++)
        EFire(e->x, e->y + 3, (float)tt * 0.06f + (float)k * TAU_F / 3.0f,
              0.65f, 0);
    if (tt % Maxi(30, (int)(100.0f / fm)) == 0) {
      ERing(lx, gy, 12, Rf() * TAU_F, 0.5f, 1);
      ERing(rx, gy, 12, Rf() * TAU_F, 0.5f, 1);
    }
    break;
  default:
    iv = Maxi(2, (int)(6.0f / fm));
    if (tt % iv == 0)
      for (int k = 0; k < 2; k++) {
        EFire(e->x, e->y + 3, (float)tt * 0.085f + (float)k * PI_F, 0.75f, 0);
        EFire(e->x, e->y + 3, -(float)tt * 0.085f + (float)k * PI_F + 0.5f,
              0.75f, 2);
      }
    if (tt % Maxi(25, (int)(70.0f / fm)) == 0) {
      EFan(lx, gy, AimAt(lx, gy), 5, 0.7f, 1.1f, 1);
      EFan(rx, gy, AimAt(rx, gy), 5, 0.7f, 1.1f, 1);
    }
    break;
  }
}

/* ---- regular enemy AI ---- */
static void EnemyShoot(Enemy *e, float fm) {
  switch (e->type) {
  case E_DRONE:
    EFire(e->x, e->y + 2, AimAt(e->x, e->y), 0.8f, 0);
    e->shootT = (int)((float)Ri(160, 320) / fm);
    break;
  case E_FIGHTER:
    if (e->t < 40) {
      e->shootT = 10;
      break;
    }
    EFan(e->x, e->y + 3, AimAt(e->x, e->y), 3, 0.55f, 0.9f, 0);
    e->shootT = (int)((float)Ri(90, 130) / fm);
    break;
  case E_BOMBER:
    if ((e->t / 60) % 2)
      ERing(e->x, e->y, 12, (float)e->t * 0.1f, 0.5f, 1);
    else
      EFan(e->x, e->y + 4, AimAt(e->x, e->y), 5, 0.9f, 0.85f, 0);
    e->shootT = (int)(80.0f / fm);
    break;
  case E_ORB:
    e->shootT = 1;
    if (e->t > 50 && (e->t / 100) % 2 == 0 &&
        e->t % Maxi(3, (int)(6.0f / fm)) == 0) {
      float a = (float)e->t * 0.13f;
      EFire(e->x, e->y, a, 0.7f, 2);
      EFire(e->x, e->y, a + PI_F, 0.7f, 2);
    }
    break;
  default:
    e->shootT = 999;
    break;
  }
}
static void UpdateEnemies(void) {
  float fm = FireMul();
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &en[i];
    if (!e->on)
      continue;
    if (e->t < 0) {
      e->t++;
      continue;
    }
    e->t++;
    if (e->flash > 0)
      e->flash--;
    if (e->type == E_BOSS) {
      UpdateBoss(e);
    } else {
      switch (e->mv) {
      case MV_LINE:
        e->x += e->vx;
        e->y += e->vy;
        break;
      case MV_SINE:
        e->by += e->vy;
        e->y = e->by;
        e->x = e->bx + sinf((float)e->t * e->p1) * e->p2;
        break;
      case MV_HOVER:
        if (e->t < e->stay)
          e->y += (e->p1 - e->y) * 0.04f;
        else {
          e->vy -= 0.02f;
          e->y += e->vy;
        }
        e->x = e->bx + sinf((float)e->t * 0.02f) * e->p2;
        break;
      case MV_ARC:
        e->x += e->vx;
        e->y += e->vy;
        e->vy += e->p1;
        break;
      case MV_DIVE:
        if (e->t < 45) {
          e->y += (e->p1 - e->y) * 0.08f;
          e->x += sinf((float)e->t * 0.4f) * 0.3f;
        } else if (e->t == 45) {
          float a = AimAt(e->x, e->y);
          e->vx = cosf(a) * 1.3f;
          e->vy = sinf(a) * 1.3f;
          if (e->vy < 0.3f)
            e->vy = 0.3f;
          Sfx(S_DIVE);
        } else {
          e->x += e->vx;
          e->y += e->vy;
          if (e->vx * e->vx + e->vy * e->vy < 9.0f) {
            e->vx *= 1.015f;
            e->vy *= 1.015f;
          }
        }
        break;
      default:
        break;
      }
      if (!e->entered && e->x > 2 && e->x < PW - 2 && e->y > 2 && e->y < PH - 2)
        e->entered = true;
      if ((e->entered &&
           (e->y > PH + 10 || e->y < -14 || e->x < -14 || e->x > PW + 14)) ||
          e->t > 2400) {
        e->on = false;
        continue;
      }
      if (!P.dead && e->entered && e->y > 1 && e->y < P.y - 10 &&
          --e->shootT <= 0)
        EnemyShoot(e, fm);
      if ((e->type == E_BOMBER || e->type == E_ORB) && e->hp < e->maxhp / 2 &&
          gframe % 4 == 0)
        AddPart(e->x + Rr(-3, 3), e->y, Rr(-0.1f, 0.1f), Rr(0.2f, 0.5f),
                Ri(10, 18), PA_SMOKE, 8);
    }
    if (e->on && !P.dead && !e->dying && e->t >= 0) {
      float dx = fabsf(e->x - P.x), dy = fabsf(e->y - P.y);
      if (dx < (float)e->w * 0.5f - 0.5f && dy < (float)e->h * 0.5f - 0.5f) {
        HurtPlayer();
        if (e->type != E_BOSS)
          DamageEnemy(e, 99);
      }
    }
  }
}

/* ---- bullets ---- */
static void UpdatePShots(void) {
  for (int i = 0; i < MAX_PS; i++) {
    PShot *b = &ps[i];
    if (!b->on)
      continue;
    if (b->kind == 1) {
      if (b->tgt < 0 || !en[b->tgt].on || en[b->tgt].dying || en[b->tgt].t < 0)
        b->tgt = NearestEnemy(b->x, b->y);
      float cur = atan2f(b->vy, b->vx), want = -PI_F / 2;
      if (b->tgt >= 0)
        want = atan2f(en[b->tgt].y - b->y, en[b->tgt].x - b->x);
      float d = want - cur;
      while (d > PI_F)
        d -= TAU_F;
      while (d < -PI_F)
        d += TAU_F;
      float turn = b->life < 8 ? 0.0f : 0.14f;
      cur += Clampf(d, -turn, turn);
      b->spd = fminf(b->spd + 0.1f, 2.8f);
      b->vx = cosf(cur) * b->spd;
      b->vy = sinf(cur) * b->spd;
      b->life++;
      if (b->life % 2 == 0)
        AddPart(b->x, b->y, 0, 0.1f, 8, PA_SMOKE, 8);
    }
    b->x += b->vx;
    b->y += b->vy;
    if (b->y < -4 || b->y > PH + 4 || b->x < -4 || b->x > PW + 4) {
      b->on = false;
      continue;
    }
    for (int j = 0; j < MAX_EN; j++) {
      Enemy *e = &en[j];
      if (!e->on || e->t < 0 || e->dying || e->y < 0)
        continue;
      if (fabsf(b->x - e->x) <= (float)e->w * 0.5f &&
          fabsf(b->y - e->y) <= (float)e->h * 0.5f) {
        DamageEnemy(e, b->dmg);
        AddPart(b->x, b->y, Rr(-0.5f, 0.5f), Rr(-0.5f, 0.2f), Ri(3, 6),
                PA_SPARK, b->kind ? 14 : 11);
        b->on = false;
        break;
      }
    }
  }
}
static void UpdateEShots(void) {
  for (int i = 0; i < MAX_ES; i++) {
    EShot *b = &es[i];
    if (!b->on)
      continue;
    b->x += b->vx;
    b->y += b->vy;
    if (b->x < -3 || b->x > PW + 3 || b->y < -6 || b->y > PH + 3) {
      b->on = false;
      continue;
    }
    if (P.bombT > 0) {
      float dx = b->x - P.bombX, dy = b->y - P.bombY;
      if (dx * dx + dy * dy < P.bombR * P.bombR) {
        b->on = false;
        AddPart(b->x, b->y, 0, 0, 6, PA_SPARK, 14);
        if (Rf() < 0.25f)
          AddPickup(PK_GEM, b->x, b->y, 0, 0);
        P.score += 10;
        continue;
      }
    }
    if (P.dead)
      continue;
    float dx = b->x - P.x, dy = b->y - P.y, d2 = dx * dx + dy * dy;
    float r = b->kind == 1 ? 1.8f : 1.1f;
    if (d2 < r * r && P.invuln == 0 && P.bombT == 0) {
      b->on = false;
      HurtPlayer();
      continue;
    }
    if (!b->grazed && d2 < 36.0f && P.invuln == 0) {
      b->grazed = true;
      P.graze++;
      P.stageGraze++;
      AddScore(50);
      if (P.chain > 0)
        P.chainT = Mini(150, P.chainT + 4);
      AddPart(P.x + dx * 0.5f, P.y + dy * 0.5f, dx * 0.1f, dy * 0.1f, 6,
              PA_SPARK, 15);
      Sfx(S_GRAZE);
    }
  }
}

/* ---- items ---- */
static void Collect(Pickup *p) {
  p->on = false;
  switch (p->type) {
  case PK_POWER:
    if (P.power < 5) {
      P.power++;
      AddPopup(P.x, P.y - 7, P.power == 5 ? "MAX POWER" : "POWER UP", 14);
    } else {
      AddScore(5000);
      AddPopup(P.x, P.y - 7, "5000", 14);
    }
    Sfx(S_POWER);
    break;
  case PK_BOMB:
    if (P.bombs < 5)
      P.bombs++;
    else
      AddScore(5000);
    AddPopup(P.x, P.y - 7, "BOMB", 13);
    Sfx(S_POWER);
    break;
  case PK_LIFE:
    if (P.lives < 9)
      P.lives++;
    AddPopup(P.x, P.y - 7, "1UP", 10);
    Sfx(S_1UP);
    break;
  case PK_SHIELD:
    P.shieldT = 600;
    AddPopup(P.x, P.y - 7, "SHIELD", 11);
    Sfx(S_SHIELD);
    break;
  default:
    AddScore(50LL * Mult());
    if (P.chain > 0)
      P.chainT = Mini(150, P.chainT + 20);
    Sfx(S_GEM);
    break;
  }
}
static void UpdatePickups(void) {
  for (int i = 0; i < MAX_PK; i++) {
    Pickup *p = &pk[i];
    if (!p->on)
      continue;
    p->t++;
    float dx = P.x - p->x, dy = P.y - p->y,
          d = sqrtf(dx * dx + dy * dy) + 0.001f;
    bool gem = p->type == PK_GEM;
    bool magnet =
        !P.dead && p->t > 15 &&
        ((gem && (d < 22 || P.y < 20 || sphase == SPH_CLEAR || P.bombT > 0)) ||
         (!gem && d < 10));
    if (magnet) {
      float mx = gem ? 2.2f : 1.2f;
      p->vx += dx / d * 0.35f;
      p->vy += dy / d * 0.35f;
      float v = sqrtf(p->vx * p->vx + p->vy * p->vy);
      if (v > mx) {
        p->vx *= mx / v;
        p->vy *= mx / v;
      }
    } else if (gem) {
      p->vx *= 0.95f;
      p->vy += 0.03f;
      if (p->vy > 0.5f)
        p->vy = 0.5f;
    } else {
      p->vx = sinf((float)p->t * 0.05f) * 0.3f;
      p->vy += 0.02f;
      if (p->vy > 0.35f)
        p->vy = 0.35f;
    }
    p->x = Clampf(p->x + p->vx, 3, PW - 4);
    p->y += p->vy;
    if (p->y > PH + 4) {
      p->on = false;
      continue;
    }
    if (!P.dead && fabsf(dx) < 4.5f && fabsf(dy) < 4.5f)
      Collect(p);
  }
}

/* ---- particles, rings, popups, stars ---- */
static void UpdateFX(void) {
  for (int i = 0; i < MAX_PA; i++) {
    Part *p = &pa[i];
    if (!p->on)
      continue;
    if (--p->life <= 0) {
      p->on = false;
      continue;
    }
    p->x += p->vx;
    p->y += p->vy;
    float drag =
        p->type == PA_FIRE ? 0.92f : (p->type == PA_SPARK ? 0.95f : 0.9f);
    p->vx *= drag;
    p->vy *= drag;
    if (p->type == PA_SMOKE)
      p->y += 0.12f;
  }
  for (int i = 0; i < MAX_RG; i++) {
    Ring *g = &rg[i];
    if (!g->on)
      continue;
    g->r += g->dr;
    g->dr *= 0.93f;
    if (--g->life <= 0)
      g->on = false;
  }
  for (int i = 0; i < MAX_PP; i++) {
    Popup *p = &pp[i];
    if (!p->on)
      continue;
    p->y -= 0.25f;
    if (--p->life <= 0)
      p->on = false;
  }
}
static void InitStars(void) {
  for (int i = 0; i < MAX_ST; i++) {
    Star *s = &stars[i];
    s->x = Rr(0, PW);
    s->y = Rr(0, PH);
    s->layer = i < 40 ? 0 : (i < 64 ? 1 : 2);
    s->spd = s->layer == 0 ? 0.08f : (s->layer == 1 ? 0.2f : 0.45f);
    s->col = (uint8_t)(s->layer == 0 ? 8 : (s->layer == 1 ? 7 : 15));
  }
}
static void UpdateStars(void) {
  for (int i = 0; i < MAX_ST; i++) {
    Star *s = &stars[i];
    s->y += s->spd * scroll;
    if (s->y >= PH) {
      s->y -= PH;
      s->x = Rr(0, PW);
    }
  }
  bgScroll += 0.12f * scroll;
}

/* ------------------------------------------------------------------------ */
/*  Stage & enemy waves                                                      */
/* ------------------------------------------------------------------------ */
static void SpawnWave(int id) {
  Enemy *e;
  switch (id) {
  case 0: { /* sine wave drone snake */
    float amp = Rr(8, 22), bx = Rr(amp + 5, PW - amp - 5);
    for (int i = 0; i < 6; i++) {
      if (!(e = SpawnEnemy(E_DRONE, bx, -5, MV_SINE)))
        break;
      e->vy = 0.42f;
      e->p1 = 0.05f;
      e->p2 = amp;
      e->t = -i * 12;
    }
  } break;
  case 1: { /* curved sweep from sides */
    int side = Ri(0, 1);
    float y0 = Rr(3, 12);
    for (int i = 0; i < 7; i++) {
      if (!(e = SpawnEnemy(E_DRONE, side ? PW + 5 : -5, y0, MV_ARC)))
        break;
      e->vx = side ? -0.9f : 0.9f;
      e->vy = 0.5f;
      e->p1 = -0.008f;
      e->t = -i * 10;
    }
  } break;
  case 2: { /* V fighter formation */
    static const float ox[5] = {0, -9, 9, -18, 18}, oy[5] = {0, -4, -4, -8, -8};
    float cx = Rr(26, PW - 26);
    for (int i = 0; i < 5; i++) {
      if (!(e = SpawnEnemy(E_FIGHTER, cx + ox[i], -8 + oy[i], MV_HOVER)))
        break;
      e->p1 = 18 + oy[i];
      e->p2 = 5;
      e->stay = Ri(380, 460);
      e->t = -i * 6;
    }
  } break;
  case 3: { /* dive / kamikaze */
    int n = 3 + Mini(stage, 4);
    for (int i = 0; i < n; i++) {
      if (!(e = SpawnEnemy(E_DIVER, Rr(8, PW - 8), -4, MV_DIVE)))
        break;
      e->p1 = Rr(6, 18);
      e->t = -i * 22;
    }
  } break;
  case 4: /* asteroid rain */
    for (int i = 0; i < 6; i++) {
      if (!(e = SpawnEnemy(E_ROCK, Rr(6, PW - 6), -6, MV_LINE)))
        break;
      e->vx = Rr(-0.15f, 0.15f);
      e->vy = Rr(0.25f, 0.45f);
      e->t = -i * 25;
    }
    break;
  case 5: { /* bomber + escort */
    int n = stage >= 3 ? 2 : 1;
    for (int k = 0; k < n; k++) {
      float x = n == 1 ? PW / 2.0f : (k ? PW * 0.72f : PW * 0.28f);
      if (!(e = SpawnEnemy(E_BOMBER, x, -8, MV_HOVER)))
        break;
      e->p1 = 13;
      e->p2 = 8;
      e->stay = 600;
    }
    float bx = Rr(20, PW - 20);
    for (int i = 0; i < 4; i++) {
      if (!(e = SpawnEnemy(E_DRONE, bx, -5, MV_SINE)))
        break;
      e->vy = 0.45f;
      e->p1 = 0.06f;
      e->p2 = 14;
      e->t = -60 - i * 12;
    }
  } break;
  default: /* spiral shooting orb */
    for (int k = 0; k < 2; k++) {
      if (!(e = SpawnEnemy(E_ORB, k ? PW * 0.7f : PW * 0.3f, -6, MV_HOVER)))
        break;
      e->p1 = 16;
      e->p2 = 4;
      e->stay = 420;
    }
    break;
  }
}
static void StartStage(int s) {
  stage = s;
  loopN = (s - 1) / 3;
  theme = (s - 1) % 3;
  sphase = SPH_INTRO;
  stimer = 0;
  wavesDone = 0;
  waveT = 30;
  wavesTotal = 9 + Mini(s, 6);
  planetY = -36;
  scroll = 5.0f;
  P.noMiss = 1;
  P.stageGraze = 0;
  SetMusic(MUS_GAME);
}
static void UpdateStage(void) {
  stimer++;
  switch (sphase) {
  case SPH_INTRO:
    scroll += (1.0f - scroll) * 0.03f;
    if (stimer >= 160) {
      sphase = SPH_WAVES;
      stimer = 0;
      scroll = 1.0f;
    }
    break;
  case SPH_WAVES:
    if (wavesDone < wavesTotal) {
      if (--waveT <= 0) {
        int maxId = Mini(6, stage + 1 + wavesDone / 4);
        SpawnWave(wavesDone == 0 ? 0 : Ri(0, maxId));
        wavesDone++;
        waveT = Maxi(100, 230 - stage * 10 - wavesDone * 5);
      }
    } else if (CountEnemies() == 0 || stimer > 4000) {
      sphase = SPH_WARNING;
      stimer = 0;
      SetMusic(MUS_OFF);
      Sfx(S_WARN);
    }
    break;
  case SPH_WARNING:
    if (stimer >= 180) {
      Enemy *b = SpawnEnemy(E_BOSS, PW / 2.0f, -12, MV_BOSS);
      bossIdx = b ? (int)(b - en) : -1;
      sphase = SPH_BOSS;
      stimer = 0;
      SetMusic(MUS_BOSS);
    }
    break;
  case SPH_CLEAR:
    if (stimer == 1) {
      bonusBoss = 20000LL * stage;
      bonusNoMiss = P.noMiss ? 50000LL * (loopN + 1) : 0;
      bonusGraze = (long long)P.stageGraze * 100;
      AddScore(bonusBoss + bonusNoMiss + bonusGraze);
      Sfx(S_POWER);
    }
    if (stimer >= 330)
      StartStage(stage + 1);
    break;
  default:
    break;
  }
}
static void NewGame(void) {
  memset(en, 0, sizeof en);
  memset(ps, 0, sizeof ps);
  memset(es, 0, sizeof es);
  memset(pk, 0, sizeof pk);
  memset(pa, 0, sizeof pa);
  memset(rg, 0, sizeof rg);
  memset(pp, 0, sizeof pp);
  memset(&P, 0, sizeof P);
  static const int lives[3] = {5, 3, 3}, bombs[3] = {3, 3, 2};
  P.x = PW / 2.0f;
  P.y = PH - 6;
  P.lives = lives[difficulty];
  P.bombs = bombs[difficulty];
  P.power = 1;
  P.invuln = 120;
  P.nextExtend = 150000;
  P.noMiss = 1;
  bossIdx = -1;
  shake = 0;
  flashT = 0;
  StartStage(1);
  state = ST_PLAY;
  Sfx(S_SELECT);
}
static void EnterGameOver(void) {
  state = ST_GAMEOVER;
  goTimer = 0;
  nameRank = QualifyRank(P.score);
  SetMusic(MUS_OFF);
}

/* ------------------------------------------------------------------------ */
/*  Render                                                                   */
/* ------------------------------------------------------------------------ */
static void DrawPlanetRing(int cx, int cy, int r, bool front) {
  float A = (float)r * 1.9f, B = (float)r * 0.32f;
  for (int i = 0; i < 180; i++) {
    float a = TAU_F * (float)i / 180.0f, s = sinf(a);
    if ((s >= 0) != front)
      continue;
    for (int k = 0; k < 2; k++)
      PRaw(cx + Rnd2i(cosf(a) * (A - (float)k * 2.0f)),
           cy + Rnd2i(s * (B - (float)k * 0.6f)), k ? 8 : 7);
  }
}
static void DrawPlanet(void) {
  static const uint8_t PD[3] = {1, 6, 4}, PM[3] = {9, 14, 12},
                       PL[3] = {11, 15, 14};
  int cx = theme == 0 ? 70 : (theme == 1 ? 24 : 64), cy = Rnd2i(planetY),
      r = theme == 1 ? 12 : 17;
  if (cy - r - 8 > PH || cy + r + 8 < 0)
    return;
  if (theme == 1)
    DrawPlanetRing(cx, cy, r, false);
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y > r * r)
        continue;
      float nx = (float)x / (float)r, ny = (float)y / (float)r;
      float nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny));
      float l = -0.55f * nx - 0.45f * ny + 0.7f * nz +
                0.12f * sinf(ny * 9.0f + nx * 2.0f);
      bool d = ((x + y) & 1) == 0;
      int c;
      if (l > 0.85f)
        c = PL[theme];
      else if (l > 0.7f)
        c = d ? PL[theme] : PM[theme];
      else if (l > 0.45f)
        c = PM[theme];
      else if (l > 0.3f)
        c = d ? PM[theme] : PD[theme];
      else if (l > 0.05f)
        c = PD[theme];
      else if (l > -0.1f)
        c = d ? PD[theme] : 0;
      else
        c = 0;
      PRaw(cx + x, cy + y, c);
    }
  if (theme == 1)
    DrawPlanetRing(cx, cy, r, true);
}
static void DrawStars(int lmin, int lmax) {
  for (int i = 0; i < MAX_ST; i++) {
    Star *s = &stars[i];
    if (s->layer < lmin || s->layer > lmax)
      continue;
    int x = (int)s->x, y = (int)s->y;
    PRaw(x, y, s->col);
    if (s->layer == 2) {
      int len = (int)(scroll * s->spd * 1.5f);
      for (int k = 1; k <= len; k++)
        PRaw(x, y - k, k < 3 ? 7 : 8);
    }
  }
}
static void DrawBackground(void) {
  static const uint8_t N1[3] = {1, 5, 4}, N2[3] = {3, 1, 6};
  int c1 = N1[theme], c2 = N2[theme];
  memset(fb, 0, sizeof fb);
  for (int y = 0; y < PH; y++) {
    float wy = (float)y - bgScroll;
    for (int x = 0; x < PW; x++) {
      float fx = (float)x;
      float n = sinf(fx * 0.11f + wy * 0.05f) +
                sinf(wy * 0.09f - fx * 0.07f + 2.1f) +
                0.7f * sinf((fx + wy) * 0.06f + 0.5f) +
                0.5f * sinf(fx * 0.21f - wy * 0.17f);
      if (n > 2.55f)
        fb[y][x] = (uint8_t)(((x + y) & 1) ? c1 : c2);
      else if (n > 2.0f)
        fb[y][x] = (uint8_t)c1;
      else if (n > 1.55f && ((x + y) & 1) == 0)
        fb[y][x] = (uint8_t)c1;
    }
  }
  DrawStars(0, 0);
  DrawPlanet();
  DrawStars(1, 2);
}
static void DrawPickupAt(int type, int x, int y, int t) {
  if (type == PK_GEM) {
    int c = (t / 4) % 2 ? 11 : 9;
    PSet(x, y, 15);
    PSet(x - 1, y, c);
    PSet(x + 1, y, c);
    PSet(x, y - 1, c);
    PSet(x, y + 1, c);
    return;
  }
  int border;
  const char *L;
  switch (type) {
  case PK_POWER:
    border = (t / 5) % 2 ? 12 : 14;
    L = "P";
    break;
  case PK_BOMB:
    border = (t / 5) % 2 ? 13 : 5;
    L = "B";
    break;
  case PK_LIFE:
    border = (t / 5) % 2 ? 10 : 2;
    L = "+";
    break;
  default:
    border = (t / 5) % 2 ? 11 : 3;
    L = "S";
    break;
  }
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++)
      PSet(x + dx, y + dy, 0);
  for (int i = -3; i <= 3; i++) {
    PSet(x + i, y - 3, border);
    PSet(x + i, y + 3, border);
    PSet(x - 3, y + i, border);
    PSet(x + 3, y + i, border);
  }
  PText(x - 1, y - 2, L, 15, 1);
}
static void DrawEnemy(const Enemy *e) {
  if (e->t < 0)
    return;
  if (e->type == E_BOSS) {
    int8_t rm[16];
    memcpy(rm, REMAP[theme], 16);
    if ((int)e->p1 == 2 && (gframe / 5) % 2) {
      rm[12] = 14;
      rm[13] = 12;
    }
    bool fl = e->dying ? (gframe % 4 < 2) : (e->flash > 0 && gframe % 3 == 0);
    SprDraw(ESPR[E_BOSS], Rnd2i(e->x), Rnd2i(e->y), fl, rm);
    return;
  }
  SprDraw(ESPR[e->type], Rnd2i(e->x), Rnd2i(e->y), e->flash > 0, NULL);
}
static int FireCol(int life, int max) {
  float f = (float)life / (float)max;
  if (f > 0.82f)
    return 15;
  if (f > 0.62f)
    return 14;
  if (f > 0.42f)
    return 12;
  if (f > 0.22f)
    return 4;
  return 8;
}
static void DrawParticles(void) {
  for (int i = 0; i < MAX_PA; i++) {
    Part *p = &pa[i];
    if (!p->on)
      continue;
    int x = Rnd2i(p->x), y = Rnd2i(p->y);
    if (p->type == PA_FIRE) {
      int c = FireCol(p->life, p->max);
      PSet(x, y, c);
      if (p->max > 8 && p->life * 10 > p->max * 7) {
        PSet(x + 1, y, c);
        PSet(x, y + 1, c);
        PSet(x + 1, y + 1, c);
      }
    } else if (p->type == PA_SPARK) {
      PSet(x, y, p->life > 3 ? p->col : DIM[p->col]);
    } else {
      if (p->life < 4 && ((x + y) & 1))
        continue;
      PSet(x, y, 8);
    }
  }
}
static void DrawHUD(void) {
  char b[64];
  for (int c = 0; c < COLS; c++) {
    TPut(c, 0, L' ', 7, 0);
    TPut(c, ROWS - 1, L' ', 7, 0);
  }
  TPrint(1, 0, "SCORE", 3, 0);
  snprintf(b, sizeof b, "%010lld", P.score);
  TPrint(7, 0, b, 15, 0);
  TPrint(20, 0, "HI", 3, 0);
  snprintf(b, sizeof b, "%010lld", P.score > HiScore() ? P.score : HiScore());
  TPrint(23, 0, b, 14, 0);
  int m = Mult();
  snprintf(b, sizeof b, "x%d", m);
  TPrint(36, 0, b, m > 1 ? 14 : 8, 0);
  if (P.chain > 1) {
    snprintf(b, sizeof b, "CHAIN %d", P.chain);
    TPrint(40, 0, b, 13, 0);
    int bars = P.chainT / 15;
    for (int i = 0; i < 10; i++)
      TPut(52 + i, 0, i < bars ? 0x2588 : 0x2591, i < bars ? 13 : 8, 0);
  }
  snprintf(b, sizeof b, "STAGE %d  %s", stage, SECTOR[theme]);
  TPrint(COLS - 1 - (int)strlen(b), 0, b, 11, 0);

  TPrint(1, ROWS - 1, "LIFE", 3, 0);
  for (int i = 0; i < P.lives && i < 9; i++)
    TPut(6 + i, ROWS - 1, 0x2665, 12, 0);
  TPrint(16, ROWS - 1, "BOMB", 3, 0);
  for (int i = 0; i < P.bombs; i++)
    TPut(21 + i, ROWS - 1, 0x2666, 13, 0);
  TPrint(28, ROWS - 1, "POWER", 3, 0);
  for (int i = 0; i < 5; i++)
    TPut(34 + i, ROWS - 1, 0x25A0,
         i < P.power ? (P.power == 5 && (gframe / 4) % 2 ? 15 : 14) : 8, 0);
  if (P.power == 5)
    TPrint(40, ROWS - 1, "MAX", 14, 0);
  snprintf(b, sizeof b, "GRAZE %d", P.graze);
  TPrint(45, ROWS - 1, b, 7, 0);
  if (P.shieldT > 0)
    TPrint(57, ROWS - 1, "SHIELD", (P.shieldT > 120 || gframe % 8 < 4) ? 11 : 3,
           0);
  TPrint(COLS - 26, ROWS - 1, "P:PAUSE  M:MUSIC  N:SFX", 8, 0);
}
static void RenderPlay(void) {
  DrawBackground();
  if (state == ST_PLAY && shake > 0.6f) {
    int s = (int)(shake + 0.5f);
    shX = Ri(-s, s) / 2;
    shY = Ri(-s, s) / 2;
  } else {
    shX = shY = 0;
  }
  useShake = 1;
  for (int i = 0; i < MAX_RG; i++)
    if (rg[i].on)
      PCircle(rg[i].x, rg[i].y, rg[i].r,
              rg[i].life * 2 > rg[i].max ? rg[i].col : DIM[rg[i].col]);
  if (P.bombT > 0) {
    PCircle(P.bombX, P.bombY, P.bombR, (gframe / 2) % 2 ? 15 : 14);
    if (P.bombR > 4)
      PCircle(P.bombX, P.bombY, P.bombR - 3, 12);
  }
  for (int i = 0; i < MAX_PK; i++)
    if (pk[i].on)
      DrawPickupAt(pk[i].type, Rnd2i(pk[i].x), Rnd2i(pk[i].y), pk[i].t);
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].on && en[i].type == E_BOSS)
      DrawEnemy(&en[i]);
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].on && en[i].type != E_BOSS)
      DrawEnemy(&en[i]);
  for (int i = 0; i < MAX_PS; i++) {
    PShot *b = &ps[i];
    if (!b->on)
      continue;
    int x = Rnd2i(b->x), y = Rnd2i(b->y);
    if (b->kind == 1) {
      PSet(x, y, 14);
      PSet(x, y + 1, 12);
    } else if (b->dmg >= 2) {
      PSet(x, y, 15);
      PSet(x, y + 1, 14);
      PSet(x, y + 2, 14);
    } else {
      PSet(x, y, 15);
      PSet(x, y + 1, 11);
      PSet(x, y + 2, 3);
    }
  }
  if (!P.dead) {
    int px = Rnd2i(P.x), py = Rnd2i(P.y);
    if (P.invuln == 0 || (gframe / 3) % 2 == 0) {
      SprDraw(&sPlayer, px, py, false, NULL);
      int fl = Ri(1, 3);
      for (int k = 0; k < fl; k++) {
        PSet(px - 3, py + 4 + k, k == 0 ? 14 : 12);
        PSet(px + 3, py + 4 + k, k == 0 ? 14 : 12);
      }
      if (P.focus)
        PSet(px, py, gframe % 4 < 2 ? 12 : 15);
    }
    if (P.shieldT > 0 && (P.shieldT > 120 || gframe % 6 < 3))
      PCircle(P.x, P.y, 6.5f, gframe % 8 < 4 ? 11 : 3);
  }
  DrawParticles();
  for (int i = 0; i < MAX_ES; i++) {
    EShot *b = &es[i];
    if (!b->on)
      continue;
    int x = Rnd2i(b->x), y = Rnd2i(b->y);
    if (b->kind == 1) {
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
          PSet(x + dx, y + dy, (dx == 0 && dy == 0) ? 15 : 13);
    } else if (b->kind == 2) {
      PSet(x, y, 15);
      PSet(Rnd2i(b->x - b->vx * 1.5f), Rnd2i(b->y - b->vy * 1.5f), 11);
    } else {
      PSet(x, y, (gframe / 3) % 2 ? 15 : 14);
      PSet(x - 1, y, 12);
      PSet(x + 1, y, 12);
      PSet(x, y - 1, 12);
      PSet(x, y + 1, 12);
    }
  }
  for (int i = 0; i < MAX_PP; i++) {
    Popup *p = &pp[i];
    if (!p->on)
      continue;
    if (p->life < 10 && gframe % 2)
      continue;
    PText(Rnd2i(p->x) - PTextW(p->txt, 1) / 2, Rnd2i(p->y), p->txt, p->col, 1);
  }
  useShake = 0;

  /* boss HP bar */
  if (bossIdx >= 0 && en[bossIdx].on && sphase == SPH_BOSS &&
      !en[bossIdx].dying && en[bossIdx].t > 0) {
    Enemy *e = &en[bossIdx];
    float f = (float)e->hp / (float)e->maxhp;
    int x0 = 18, x1 = PW - 3, fill = x0 + (int)((float)(x1 - x0) * f);
    int c = f > 0.66f ? 10 : (f > 0.33f ? 14 : 12);
    PText(2, 0, "BOSS", (gframe / 8) % 2 ? 12 : 15, 1);
    for (int x = x0; x <= x1; x++) {
      PRaw(x, 1, x <= fill ? c : 8);
      PRaw(x, 2, x <= fill ? DIM[c] : 0);
    }
    for (int k = 1; k < 3; k++) {
      int mx = x0 + (x1 - x0) * k / 3;
      PRaw(mx, 1, 0);
      PRaw(mx, 2, 0);
    }
  }
  /* banner */
  if (sphase == SPH_INTRO && stimer > 20) {
    char b[32];
    snprintf(b, sizeof b, "STAGE %d", stage);
    int nshow = Mini((int)strlen(b), (stimer - 20) / 4 + 1);
    char sub[32];
    memcpy(sub, b, (size_t)nshow);
    sub[nshow] = 0;
    static const uint8_t GS[5] = {15, 14, 14, 12, 4};
    PTextEx((PW - PTextW(b, 2)) / 2 + 1, 19, sub, 0, 2, NULL);
    PTextEx((PW - PTextW(b, 2)) / 2, 18, sub, 15, 2, GS);
    if (stimer > 60)
      PTextCEx(31, SECTOR[theme], 11, 1, NULL, true);
    if (stimer > 80 && loopN > 0) {
      snprintf(b, sizeof b, "LOOP %d", loopN + 1);
      PTextCEx(38, b, 12, 1, NULL, true);
    }
  }
  if (sphase == SPH_WARNING) {
    for (int x = 0; x < PW; x++)
      for (int k = 0; k < 2; k++) {
        int on = ((x + gframe / 2) % 8) < 4;
        PRaw(x, 15 + k, on ? 12 : 0);
        PRaw(x, 35 + k, on ? 12 : 0);
      }
    if ((stimer / 10) % 2 == 0)
      PTextCEx(19, "WARNING", 12, 2, NULL, true);
    PTextCEx(30, "BOSS APPROACHING", 15, 1, NULL, true);
  }
  if (sphase == SPH_CLEAR) {
    static const uint8_t GC[5] = {15, 11, 11, 9, 1};
    PTextCEx(6, "STAGE CLEAR", 15, 2, GC, true);
    char b[64];
    if (stimer > 40) {
      snprintf(b, sizeof b, "BOSS BONUS      %10lld", bonusBoss);
      TPrintC(14, b, 15, 0);
    }
    if (stimer > 80) {
      snprintf(b, sizeof b, "NO MISS BONUS   %10lld", bonusNoMiss);
      TPrintC(16, b, bonusNoMiss ? 10 : 8, 0);
    }
    if (stimer > 120) {
      snprintf(b, sizeof b, "GRAZE BONUS     %10lld", bonusGraze);
      TPrintC(18, b, 13, 0);
    }
    if (stimer > 180)
      TPrintC(21, "PREPARING FOR NEXT SECTOR...", (gframe / 10) % 2 ? 14 : 7,
              0);
  }
  if (flashT > 0)
    for (int y = 0; y < PH; y++)
      for (int x = 0; x < PW; x++)
        if (flashT >= 4 || ((x + y + gframe) & 1) == 0)
          fb[y][x] = (uint8_t)flashCol;
  DrawHUD();
}

static const uint8_t G_BLUE[5] = {15, 11, 11, 3, 9};
static const uint8_t G_FIRE[5] = {15, 14, 14, 12, 4};
static const uint8_t G_RED[5] = {15, 12, 12, 4, 4};

static void RenderTitle(void) {
  DrawBackground();
  useShake = 0;
  int bob = Rnd2i(sinf((float)gframe * 0.06f) * 1.2f);
  PTextCEx(4 + bob, "SPACE", 15, 2, G_BLUE, true);
  PTextCEx(16 + bob, "VANGUARD", 15, 2, G_FIRE, true);
  useShake = 0;
  int sx = Rnd2i(titleShipX);
  SprDraw(&sPlayer, sx, 50, false, NULL);
  for (int k = 0; k < Ri(1, 3); k++) {
    PSet(sx - 3, 54 + k, k ? 12 : 14);
    PSet(sx + 3, 54 + k, k ? 12 : 14);
  }
  DrawParticles();

  TPrintC(14, "~  S T A R   C I T A D E L   ::   E D I T I O N   2 . 0  ~", 13,
          0);
  char items[7][40];
  snprintf(items[0], 40, "START MISSION");
  snprintf(items[1], 40, "DIFFICULTY  < %s >", DIFF_NAME[difficulty]);
  snprintf(items[2], 40, "MUSIC       %s", musicOn ? "ON" : "OFF");
  snprintf(items[3], 40, "SOUND FX    %s", sfxOn ? "ON" : "OFF");
  snprintf(items[4], 40, "HOW TO PLAY");
  snprintf(items[5], 40, "LEADERBOARD");
  snprintf(items[6], 40, "QUIT");
  for (int i = 0; i < 7; i++) {
    bool sel = i == menuSel;
    TPrint(38, 16 + i, items[i], sel ? ((gframe / 8) % 2 ? 14 : 15) : 7, 0);
    if (sel)
      TPut(36, 16 + i, 0x25BA, 14, 0);
  }
  char b[64];
  snprintf(b, sizeof b, "HI-SCORE  %s  %lld", save.hs[0].name,
           save.hs[0].score);
  TPrintC(24, b, 14, 0);
  TPrintC(ROWS - 1, "UP/DOWN: SELECT   LEFT/RIGHT: CHANGE   ENTER/SPACE: OK", 8,
          0);
  TPrint(COLS - 6, 0, "v2.0", 8, 0);
}
static void RenderHowTo(void) {
  DrawBackground();
  for (int y = 0; y < PH; y++)
    for (int x = 0; x < PW; x++)
      fb[y][x] = DIM[fb[y][x]];
  TPrintC(1, "=====  HOW TO PLAY  =====", 14, 0);
  static const char *L[] = {
      "CONTROLS",
      "WASD / ARROWS    Move starfighter",
      "SPACE / Z / J    Fire (hold)",
      "SHIFT (hold)     Focus mode: slow & precision",
      "B / X / K        Bomb: clear bullets, invuln",
      "P / ESC          Pause",
      "M / N            Music / Sound FX toggle",
      "",
      "SYSTEM",
      "- Hitbox is a single point at ship cockpit",
      "- GRAZE: near-miss bullets = bonus score",
      "- CHAIN: consecutive kills = score mult",
      "  (max x9). Crystals sustain chain meter",
      "- Fly towards top = auto-collect gems",
      "- Extra life every 150,000 points",
      "- Power 3+ unlocks homing missiles",
      "- Boss at sector end with 3 attack phases",
  };
  for (int i = 0; i < NROWS(L); i++)
    TPrint(3, 3 + i, L[i], (i == 0 || i == 8) ? 11 : 7, 0);
  TPrint(55, 3, "ITEMS", 11, 0);
  static const char *IT[4] = {"POWER  weapon +1 (max 5)",
                              "BOMB   bomb stock +1", "SHIELD absorb 1 hit",
                              "1UP    extra life +1"};
  static const int ITY[4] = {PK_POWER, PK_BOMB, PK_SHIELD, PK_LIFE};
  for (int i = 0; i < 4; i++) {
    int row = 5 + i * 4;
    DrawPickupAt(ITY[i], 56, (row - 1) * 2 + 1, gframe);
    TPrint(61, row, IT[i], 15, 0);
  }
  DrawPickupAt(PK_GEM, 56, (21 - 1) * 2 + 1, gframe);
  TPrint(61, 21, "CRYSTAL  +score & chain", 15, 0);
  TPrint(3, 22, "ENEMIES", 11, 0);
  SprDraw(&sDrone, 8, 50, false, NULL);
  SprDraw(&sFighter, 20, 50, false, NULL);
  SprDraw(&sDiver, 31, 50, false, NULL);
  SprDraw(&sOrb, 42, 50, false, NULL);
  SprDraw(&sBomber, 56, 50, false, NULL);
  SprDraw(&sRock, 70, 50, false, NULL);
  SprDraw(&sBoss, 82, 47, false, REMAP[0]);
  TPrintC(ROWS - 1, "ENTER / ESC : BACK", 8, 0);
}
static void RenderScores(void) {
  DrawBackground();
  PTextCEx(4, "TOP PILOTS", 15, 2, G_FIRE, true);
  char b[80];
  for (int i = 0; i < 5; i++) {
    snprintf(b, sizeof b, "%d.   %-3s   %012lld   STAGE %2d", i + 1,
             save.hs[i].name, save.hs[i].score, save.hs[i].stage);
    int col = i == scoreHL ? ((gframe / 6) % 2 ? 14 : 15) : (i == 0 ? 14 : 15);
    TPrintC(10 + i * 2, b, col, 0);
  }
  TPrintC(ROWS - 3, "ENTER / ESC : BACK", 8, 0);
}
static void RenderGameOver(void) {
  DrawBackground();
  useShake = 0;
  DrawParticles();
  PTextCEx(6, "GAME OVER", 15, 2, G_RED, true);
  char b[64];
  snprintf(b, sizeof b, "FINAL SCORE  %012lld", P.score);
  TPrint(32, 11, b, 15, 0);
  snprintf(b, sizeof b, "STAGE        %d", stage);
  TPrint(32, 13, b, 7, 0);
  snprintf(b, sizeof b, "ENEMIES      %d", P.kills);
  TPrint(32, 14, b, 7, 0);
  snprintf(b, sizeof b, "GRAZE        %d", P.graze);
  TPrint(32, 15, b, 7, 0);
  snprintf(b, sizeof b, "MAX CHAIN    %d", P.maxChain);
  TPrint(32, 16, b, 7, 0);
  if (goTimer > 60) {
    if (nameRank >= 0) {
      snprintf(b, sizeof b, "*** NEW RECORD!  RANK #%d ***", nameRank + 1);
      TPrintC(19, b, (gframe / 8) % 2 ? 14 : 15, 0);
      TPrintC(22, "ENTER : ENTER NAME", 10, 0);
    } else {
      TPrintC(22, "ENTER : MAIN MENU      R : RETRY",
              (gframe / 15) % 2 ? 15 : 7, 0);
    }
  }
}
static void RenderName(void) {
  DrawBackground();
  PTextCEx(6, "NEW RECORD!", 15, 2, G_FIRE, true);
  int x0 = (PW - (3 * 12 - 3)) / 2;
  for (int i = 0; i < 3; i++) {
    char s[2] = {nameBuf[i], 0};
    bool cur = i == namePos;
    if (!cur || (gframe / 8) % 2 == 0)
      PTextEx(x0 + i * 12, 22, s, cur ? 14 : 15, 3, NULL);
    if (cur)
      for (int k = 0; k < 9; k++)
        PRaw(x0 + i * 12 + k, 39, 14);
  }
  char b[64];
  snprintf(b, sizeof b, "SCORE %lld", P.score);
  TPrintC(23, b, 15, 0);
  TPrintC(26, "UP/DOWN: LETTER   LEFT/RIGHT: MOVE   ENTER: OK", 7, 0);
}
static void RenderPauseOverlay(void) {
  for (int y = 0; y < PH; y++)
    for (int x = 0; x < PW; x++)
      fb[y][x] = DIM[fb[y][x]];
  PTextCEx(14, "PAUSE", 15, 2, NULL, true);
  static const char *it[2] = {"RESUME", "RETURN TO MENU"};
  for (int i = 0; i < 2; i++) {
    bool sel = i == pauseSel;
    TPrint(40, 15 + i, it[i], sel ? 14 : 7, 0);
    if (sel)
      TPut(38, 15 + i, 0x25BA, 14, 0);
  }
}
static void Render(void) {
  TClear();
  switch (state) {
  case ST_TITLE:
    RenderTitle();
    break;
  case ST_HOWTO:
    RenderHowTo();
    break;
  case ST_SCORES:
    RenderScores();
    break;
  case ST_PLAY:
    RenderPlay();
    break;
  case ST_PAUSE:
    RenderPlay();
    RenderPauseOverlay();
    break;
  case ST_GAMEOVER:
    RenderGameOver();
    break;
  case ST_NAME:
    RenderName();
    break;
  default:
    break;
  }
}

/* ------------------------------------------------------------------------ */
/*  Update per state                                                         */
/* ------------------------------------------------------------------------ */
static void UpdateTitle(void) {
  theme = 0;
  planetY = 38.0f;
  scroll = 1.0f;
  UpdateStars();
  UpdateFX();
  titleShipX = 48.0f + sinf((float)gframe * 0.02f) * 36.0f;
  if (gframe % 2 == 0) {
    AddPart(titleShipX - 3, 54, Rr(-0.1f, 0.1f), Rr(0.6f, 1.0f), Ri(4, 7),
            PA_FIRE, 0);
    AddPart(titleShipX + 3, 54, Rr(-0.1f, 0.1f), Rr(0.6f, 1.0f), Ri(4, 7),
            PA_FIRE, 0);
  }
  if (PUp()) {
    menuSel = (menuSel + 6) % 7;
    Sfx(S_MENU);
  }
  if (PDown()) {
    menuSel = (menuSel + 1) % 7;
    Sfx(S_MENU);
  }
  int dir = PLeft() ? -1 : (PRight() ? 1 : 0);
  if (dir) {
    if (menuSel == 1)
      difficulty = (difficulty + dir + 3) % 3;
    else if (menuSel == 2)
      musicOn = !musicOn;
    else if (menuSel == 3)
      sfxOn = !sfxOn;
    if (menuSel >= 1 && menuSel <= 3) {
      Sfx(S_MENU);
      WriteSave();
    }
  }
  if (PBack()) {
    menuSel = 6;
    Sfx(S_MENU);
  }
  if (POK()) {
    switch (menuSel) {
    case 0:
      NewGame();
      break;
    case 1:
      difficulty = (difficulty + 1) % 3;
      Sfx(S_MENU);
      WriteSave();
      break;
    case 2:
      musicOn = !musicOn;
      Sfx(S_MENU);
      WriteSave();
      break;
    case 3:
      sfxOn = !sfxOn;
      Sfx(S_MENU);
      WriteSave();
      break;
    case 4:
      state = ST_HOWTO;
      Sfx(S_SELECT);
      break;
    case 5:
      state = ST_SCORES;
      scoreHL = -1;
      Sfx(S_SELECT);
      break;
    default:
      running = false;
      break;
    }
  }
}
static void UpdatePlay(void) {
  if (PBack() || KP('P')) {
    state = ST_PAUSE;
    pauseSel = 0;
    Sfx(S_MENU);
    return;
  }
  UpdateStars();
  planetY += 0.022f * scroll;
  UpdatePlayer();
  UpdateStage();
  UpdateEnemies();
  UpdatePShots();
  UpdateEShots();
  UpdatePickups();
  UpdateFX();
  shake *= 0.86f;
  if (shake < 0.3f)
    shake = 0;
  if (flashT > 0)
    flashT--;
}
static void UpdatePause(void) {
  if (KP('P') || KP(VK_ESCAPE)) {
    state = ST_PLAY;
    return;
  }
  if (PUp() || PDown()) {
    pauseSel ^= 1;
    Sfx(S_MENU);
  }
  if (POK()) {
    if (pauseSel == 0)
      state = ST_PLAY;
    else {
      state = ST_TITLE;
      menuSel = 0;
      SetMusic(MUS_TITLE);
    }
    Sfx(S_SELECT);
  }
}
static void UpdateGameOver(void) {
  goTimer++;
  UpdateStars();
  UpdateFX();
  if (goTimer < 60)
    return;
  if (nameRank >= 0) {
    if (POK()) {
      state = ST_NAME;
      strcpy(nameBuf, "AAA");
      namePos = 0;
      Sfx(S_SELECT);
    }
  } else {
    if (KP('R'))
      NewGame();
    else if (POK() || PBack()) {
      state = ST_TITLE;
      menuSel = 0;
      SetMusic(MUS_TITLE);
      Sfx(S_SELECT);
    }
  }
}
static void UpdateName(void) {
  UpdateStars();
  if (PUp()) {
    nameBuf[namePos] =
        nameBuf[namePos] >= 'Z' ? 'A' : (char)(nameBuf[namePos] + 1);
    Sfx(S_MENU);
  }
  if (PDown()) {
    nameBuf[namePos] =
        nameBuf[namePos] <= 'A' ? 'Z' : (char)(nameBuf[namePos] - 1);
    Sfx(S_MENU);
  }
  if (PLeft() && namePos > 0) {
    namePos--;
    Sfx(S_MENU);
  }
  if (PRight() && namePos < 2) {
    namePos++;
    Sfx(S_MENU);
  }
  if (POK()) {
    if (namePos < 2) {
      namePos++;
      Sfx(S_MENU);
      return;
    }
    int r = nameRank;
    for (int i = 4; i > r; i--)
      save.hs[i] = save.hs[i - 1];
    memcpy(save.hs[r].name, nameBuf, 4);
    save.hs[r].score = P.score;
    save.hs[r].stage = stage;
    WriteSave();
    scoreHL = r;
    state = ST_SCORES;
    SetMusic(MUS_TITLE);
    Sfx(S_1UP);
  }
}
static void UpdateSimpleScreen(void) {
  UpdateStars();
  if (POK() || PBack()) {
    state = ST_TITLE;
    scoreHL = -1;
    Sfx(S_MENU);
  }
}

/* ------------------------------------------------------------------------ */
/*  Main loop                                                                */
/* ------------------------------------------------------------------------ */
static void SelfTestInput(void) {
  static int dir = 0;
  memset(kNow, 0, sizeof kNow);
  if (state == ST_PLAY) {
    kNow[VK_SPACE] = 1;
    if (gframe % 25 == 0)
      dir = Ri(0, 8);
    if (dir == 1 || dir == 5 || dir == 6)
      kNow[VK_LEFT] = 1;
    if (dir == 2 || dir == 7 || dir == 8)
      kNow[VK_RIGHT] = 1;
    if (dir == 3 || dir == 5 || dir == 7)
      kNow[VK_UP] = 1;
    if (dir == 4 || dir == 6 || dir == 8)
      kNow[VK_DOWN] = 1;
    if (Ri(0, 400) == 0)
      kNow['B'] = 1;
    if (Ri(0, 3000) == 0)
      kNow['P'] = 1;
    kNow[VK_SHIFT] = (uint8_t)((gframe / 200) % 3 == 0);
  } else if (state == ST_PAUSE) {
    if (gframe % 20 == 0)
      kNow['P'] = 1;
  } else if (gframe % 12 == 0) {
    kNow[VK_RETURN] = 1;
  }
}
static void PollInput(void) {
  memcpy(kOld, kNow, sizeof kNow);
  if (g_selftest) {
    SelfTestInput();
    return;
  }
  for (int k = 8; k < 256; k++)
    kNow[k] = (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;
}
static void Tick(void) {
  PollInput();
  gframe++;
  if (state != ST_NAME) {
    if (KP('M')) {
      musicOn = !musicOn;
      WriteSave();
    }
    if (KP('N')) {
      sfxOn = !sfxOn;
      WriteSave();
    }
  }
  if (!g_selftest && state == ST_PLAY && !HasFocus()) {
    state = ST_PAUSE;
    pauseSel = 0;
  }
  switch (state) {
  case ST_TITLE:
    UpdateTitle();
    break;
  case ST_HOWTO:
  case ST_SCORES:
    UpdateSimpleScreen();
    break;
  case ST_PLAY:
    UpdatePlay();
    break;
  case ST_PAUSE:
    UpdatePause();
    break;
  case ST_GAMEOVER:
    UpdateGameOver();
    break;
  case ST_NAME:
    UpdateName();
    break;
  default:
    break;
  }
}
static BOOL WINAPI CtrlHandler(DWORD t) {
  (void)t;
  running = false;
  return TRUE;
}

static int RunSelfTest(void) {
  g_selftest = true;
  LoadSave();
  InitSprites();
  InitStars();
  int frames = 240000, games = 0, maxStage = 0, bosses = 0, prev = -1,
      prevPhase = -1;
  for (int f = 0; f < frames; f++) {
    Tick();
    if (prev != ST_PLAY && state == ST_PLAY) {
      games++;
      g_god = (games % 2 == 0);
    }
    if (state == ST_PLAY && stage > maxStage)
      maxStage = stage;
    if (state == ST_PLAY && sphase == SPH_CLEAR && prevPhase == SPH_BOSS)
      bosses++;
    prev = state;
    prevPhase = sphase;
    if (f % 5 == 0)
      Render();
  }
  printf("SELFTEST OK  frames=%d games=%d maxStage=%d bossesDefeated=%d\n",
         frames, games, maxStage, bosses);
  return 0;
}

int main(int argc, char **argv) {
  g_rng ^= (uint32_t)GetTickCount();
  for (int i = 0; i < S_COUNT; i++)
    sfxLast[i] = -1000;
  if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
    return RunSelfTest();

  SetupConsole();
  LoadSave();
  InitSprites();
  InitStars();
  AudioInit();
  SetMusic(MUS_TITLE);
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  timeBeginPeriod(1);

  LARGE_INTEGER fq, t0, t1;
  QueryPerformanceFrequency(&fq);
  QueryPerformanceCounter(&t0);
  double acc = 0.0;
  const double dt = 1.0 / 60.0;
  int checkT = 0;
  while (running) {
    QueryPerformanceCounter(&t1);
    acc += (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart;
    t0 = t1;
    if (acc > 0.2)
      acc = 0.2;
    bool stepped = false;
    while (acc >= dt && running) {
      acc -= dt;
      if (--checkT <= 0) {
        CheckWindow();
        checkT = 20;
      }
      if (winOK)
        Tick();
      else if (state == ST_PLAY) {
        state = ST_PAUSE;
        pauseSel = 0;
      }
      stepped = true;
    }
    if (stepped && winOK) {
      Render();
      Present();
    }
    FlushConsoleInputBuffer(hIn);
    Sleep(1);
  }

  AudioShutdown();
  WriteSave();
  timeEndPeriod(1);
  RestoreConsole();
  printf("Thank you for playing SPACE VANGUARD 2.0!\n");
  return 0;
}
