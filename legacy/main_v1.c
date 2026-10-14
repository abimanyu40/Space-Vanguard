/*
 ============================================================================
  SPACE VANGUARD: STAR CITADEL (Cyber Arcade Shooter)
  A High-Performance, Zero-Flicker Windows Console Game in C
  Controls:
    WASD or Arrow Keys : Move Starfighter
    SPACE / J          : Fire Lasers
    B / K              : Deploy Smart Bomb (Screen Wipe)
    M                  : Mute / Unmute Retro Audio
    P / ESC            : Pause Game
    R                  : Restart (on Game Over)
    Q                  : Quit
 ============================================================================
*/

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 28
#define PLAY_TOP      2
#define PLAY_BOTTOM   26

#define MAX_STARS        45
#define MAX_BULLETS      64
#define MAX_ENEMY_BULLETS 48
#define MAX_ENEMIES      20
#define MAX_PARTICLES    96
#define MAX_POWERUPS     8

// Colors (Windows Console Attributes)
#define C_BLACK       0
#define C_BLUE        FOREGROUND_BLUE
#define C_GREEN       FOREGROUND_GREEN
#define C_CYAN        (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define C_RED         FOREGROUND_RED
#define C_MAGENTA     (FOREGROUND_RED | FOREGROUND_BLUE)
#define C_YELLOW      (FOREGROUND_RED | FOREGROUND_GREEN)
#define C_WHITE       (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

#define C_BRIGHT_BLUE    (C_BLUE | FOREGROUND_INTENSITY)
#define C_BRIGHT_GREEN   (C_GREEN | FOREGROUND_INTENSITY)
#define C_BRIGHT_CYAN    (C_CYAN | FOREGROUND_INTENSITY)
#define C_BRIGHT_RED     (C_RED | FOREGROUND_INTENSITY)
#define C_BRIGHT_MAGENTA (C_MAGENTA | FOREGROUND_INTENSITY)
#define C_BRIGHT_YELLOW  (C_YELLOW | FOREGROUND_INTENSITY)
#define C_BRIGHT_WHITE   (C_WHITE | FOREGROUND_INTENSITY)

// Sound Types
typedef enum {
    SND_NONE = 0,
    SND_SHOOT,
    SND_EXPLODE,
    SND_HIT,
    SND_POWERUP,
    SND_BOMB,
    SND_BOSS_HIT,
    SND_GAMEOVER
} SoundId;

static volatile SoundId g_soundQueue = SND_NONE;
static volatile bool g_soundEnabled = true;
static volatile bool g_soundThreadRunning = true;

// Sound Worker Thread (Prevents Beep from blocking the 60fps game loop)
DWORD WINAPI SoundThread(LPVOID lpParam) {
    (void)lpParam;
    while (g_soundThreadRunning) {
        if (g_soundEnabled && g_soundQueue != SND_NONE) {
            SoundId s = g_soundQueue;
            g_soundQueue = SND_NONE;
            switch (s) {
                case SND_SHOOT:
                    Beep(920, 25);
                    break;
                case SND_EXPLODE:
                    Beep(220, 45);
                    Beep(140, 50);
                    break;
                case SND_HIT:
                    Beep(350, 20);
                    break;
                case SND_POWERUP:
                    Beep(580, 30);
                    Beep(880, 40);
                    Beep(1170, 50);
                    break;
                case SND_BOMB:
                    Beep(700, 30);
                    Beep(400, 40);
                    Beep(200, 70);
                    Beep(100, 100);
                    break;
                case SND_BOSS_HIT:
                    Beep(280, 30);
                    break;
                case SND_GAMEOVER:
                    Beep(440, 90);
                    Beep(330, 110);
                    Beep(220, 150);
                    Beep(160, 220);
                    break;
                default:
                    break;
            }
        }
        Sleep(10);
    }
    return 0;
}

void TriggerSound(SoundId s) {
    if (g_soundEnabled) {
        g_soundQueue = s;
    }
}

// Double Buffer Structure
static HANDLE hStdout;
static CHAR_INFO consoleBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static COORD bufferSize = { SCREEN_WIDTH, SCREEN_HEIGHT };
static COORD bufferCoord = { 0, 0 };
static SMALL_RECT writeRegion = { 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1 };

void BufferClear(void) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        consoleBuffer[i].Char.AsciiChar = ' ';
        consoleBuffer[i].Attributes = C_BLACK;
    }
}

void BufferSetChar(int x, int y, char c, WORD attr) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        int idx = y * SCREEN_WIDTH + x;
        consoleBuffer[idx].Char.AsciiChar = c;
        consoleBuffer[idx].Attributes = attr;
    }
}

void BufferPrint(int x, int y, const char* str, WORD attr) {
    while (*str && x < SCREEN_WIDTH) {
        if (x >= 0 && y >= 0 && y < SCREEN_HEIGHT) {
            BufferSetChar(x, y, *str, attr);
        }
        x++;
        str++;
    }
}

void BufferPresent(void) {
    WriteConsoleOutputA(hStdout, consoleBuffer, bufferSize, bufferCoord, &writeRegion);
}

// Game Entities
typedef struct {
    float x, y;
    float speed;
    char ch;
    WORD color;
} Star;

typedef struct {
    float x, y;
    float vx, vy;
    bool active;
    WORD color;
} Bullet;

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    char ch;
    WORD color;
    bool active;
} Particle;

typedef enum {
    PWR_SHIELD,
    PWR_WEAPON,
    PWR_BOMB,
    PWR_LIFE,
    PWR_OVERDRIVE
} PowerType;

typedef struct {
    float x, y;
    PowerType type;
    bool active;
} PowerUp;

typedef enum {
    ENEMY_SCOUT,     // Fast zig-zag
    ENEMY_RAIDER,    // Medium, shoots
    ENEMY_BOMBER,    // Heavy, 2-way shot
    ENEMY_ASTEROID,  // Drifts, splits
    ENEMY_MINI_AST,  // Asteroid piece
    ENEMY_BOSS       // Huge boss dreadnought
} EnemyType;

typedef struct {
    float x, y;
    float vx, vy;
    int hp;
    int maxHp;
    EnemyType type;
    int shootTimer;
    int moveTimer;
    bool active;
} Enemy;

typedef struct {
    float x, y;
    int lives;
    int shield;        // Max 3
    int weaponLevel;   // 1 to 4
    int bombs;         // Screen wipes
    int overdriveTimer;// Rapid fire frames
    int shootCooldown;
    int invulnTimer;   // Invincibility after hit
    int score;
    int highScore;
    int combo;
    int comboTimer;
    int enemiesKilled;
} Player;

typedef enum {
    STATE_TITLE,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_GAMEOVER
} GameState;

// Global Game Context
static Star stars[MAX_STARS];
static Bullet playerBullets[MAX_BULLETS];
static Bullet enemyBullets[MAX_ENEMY_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static Particle particles[MAX_PARTICLES];
static PowerUp powerups[MAX_POWERUPS];
static Player player;
static GameState gameState = STATE_TITLE;
static int currentWave = 1;
static int waveSpawnTimer = 0;
static int bossActive = 0;
static int frameCounter = 0;
static int screenShake = 0;

// High Score Persistence
void LoadHighScore(void) {
    player.highScore = 0;
    FILE* f = fopen("highscore.dat", "rb");
    if (f) {
        fread(&player.highScore, sizeof(int), 1, f);
        fclose(f);
    }
}

void SaveHighScore(void) {
    if (player.score > player.highScore) {
        player.highScore = player.score;
        FILE* f = fopen("highscore.dat", "wb");
        if (f) {
            fwrite(&player.highScore, sizeof(int), 1, f);
            fclose(f);
        }
    }
}

// Particle System
void SpawnExplosion(float x, float y, int count, WORD baseColor) {
    char chars[] = { '*', '+', '.', '%', '#' };
    for (int i = 0; i < count; i++) {
        for (int p = 0; p < MAX_PARTICLES; p++) {
            if (!particles[p].active) {
                particles[p].active = true;
                particles[p].x = x;
                particles[p].y = y;
                float angle = ((float)rand() / RAND_MAX) * 6.283185f;
                float spd = 0.4f + ((float)rand() / RAND_MAX) * 1.6f;
                particles[p].vx = cosf(angle) * spd * 1.5f;
                particles[p].vy = sinf(angle) * spd * 0.7f;
                particles[p].life = 8.0f + rand() % 14;
                particles[p].ch = chars[rand() % 5];
                particles[p].color = (rand() % 2 == 0) ? baseColor : C_BRIGHT_WHITE;
                break;
            }
        }
    }
}

// Spawn PowerUp
void SpawnPowerUp(float x, float y) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) {
            powerups[i].active = true;
            powerups[i].x = x;
            powerups[i].y = y;
            int r = rand() % 100;
            if (r < 30) powerups[i].type = PWR_SHIELD;
            else if (r < 60) powerups[i].type = PWR_WEAPON;
            else if (r < 80) powerups[i].type = PWR_OVERDRIVE;
            else if (r < 92) powerups[i].type = PWR_BOMB;
            else powerups[i].type = PWR_LIFE;
            break;
        }
    }
}

// Initialize Stars
void InitStars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = (float)(rand() % SCREEN_WIDTH);
        stars[i].y = (float)(rand() % (SCREEN_HEIGHT - 2) + 1);
        int layer = rand() % 3;
        if (layer == 0) {
            stars[i].speed = 0.15f;
            stars[i].ch = '.';
            stars[i].color = FOREGROUND_INTENSITY; // Dark gray
        } else if (layer == 1) {
            stars[i].speed = 0.35f;
            stars[i].ch = '+';
            stars[i].color = C_CYAN;
        } else {
            stars[i].speed = 0.75f;
            stars[i].ch = '*';
            stars[i].color = C_BRIGHT_WHITE;
        }
    }
}

// Initialize / Reset Game
void ResetGame(void) {
    player.x = SCREEN_WIDTH / 2.0f;
    player.y = SCREEN_HEIGHT - 4.0f;
    player.lives = 3;
    player.shield = 3;
    player.weaponLevel = 1;
    player.bombs = 2;
    player.overdriveTimer = 0;
    player.shootCooldown = 0;
    player.invulnTimer = 60;
    player.score = 0;
    player.combo = 0;
    player.comboTimer = 0;
    player.enemiesKilled = 0;

    currentWave = 1;
    waveSpawnTimer = 30;
    bossActive = 0;
    screenShake = 0;

    for (int i = 0; i < MAX_BULLETS; i++) playerBullets[i].active = false;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) enemyBullets[i].active = false;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
    for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;

    LoadHighScore();
}

// Player Shoot
void PlayerShoot(void) {
    if (player.shootCooldown > 0) return;

    int cooldown = (player.overdriveTimer > 0) ? 3 : 7;
    player.shootCooldown = cooldown;
    TriggerSound(SND_SHOOT);

    int spawned = 0;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!playerBullets[i].active) {
            if (player.weaponLevel == 1) {
                // Single Center
                playerBullets[i].active = true;
                playerBullets[i].x = player.x;
                playerBullets[i].y = player.y - 1;
                playerBullets[i].vx = 0.0f;
                playerBullets[i].vy = -1.3f;
                playerBullets[i].color = C_BRIGHT_YELLOW;
                break;
            } else if (player.weaponLevel == 2) {
                // Dual Shots
                if (spawned == 0) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x - 1;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = 0.0f;
                    playerBullets[i].vy = -1.3f;
                    playerBullets[i].color = C_BRIGHT_CYAN;
                    spawned++;
                } else {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x + 1;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = 0.0f;
                    playerBullets[i].vy = -1.3f;
                    playerBullets[i].color = C_BRIGHT_CYAN;
                    break;
                }
            } else if (player.weaponLevel == 3) {
                // Triple Spread
                if (spawned == 0) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x - 1;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = -0.3f;
                    playerBullets[i].vy = -1.2f;
                    playerBullets[i].color = C_BRIGHT_GREEN;
                    spawned++;
                } else if (spawned == 1) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = 0.0f;
                    playerBullets[i].vy = -1.3f;
                    playerBullets[i].color = C_BRIGHT_GREEN;
                    spawned++;
                } else {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x + 1;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = 0.3f;
                    playerBullets[i].vy = -1.2f;
                    playerBullets[i].color = C_BRIGHT_GREEN;
                    break;
                }
            } else {
                // Weapon Level 4 (Plasma Cannons)
                if (spawned == 0) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x - 2;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = -0.2f;
                    playerBullets[i].vy = -1.4f;
                    playerBullets[i].color = C_BRIGHT_MAGENTA;
                    spawned++;
                } else if (spawned == 1) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x;
                    playerBullets[i].y = player.y - 1.2f;
                    playerBullets[i].vx = 0.0f;
                    playerBullets[i].vy = -1.5f;
                    playerBullets[i].color = C_BRIGHT_WHITE;
                    spawned++;
                } else {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x + 2;
                    playerBullets[i].y = player.y - 1;
                    playerBullets[i].vx = 0.2f;
                    playerBullets[i].vy = -1.4f;
                    playerBullets[i].color = C_BRIGHT_MAGENTA;
                    break;
                }
            }
        }
    }
}

// Deploy Smart Bomb
void DeploySmartBomb(void) {
    if (player.bombs <= 0) return;
    player.bombs--;
    screenShake = 12;
    TriggerSound(SND_BOMB);

    // Clear all enemy bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemyBullets[i].active) {
            enemyBullets[i].active = false;
            SpawnExplosion(enemyBullets[i].x, enemyBullets[i].y, 3, C_BRIGHT_YELLOW);
        }
    }

    // Heavy damage to all enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].hp -= 8;
            SpawnExplosion(enemies[i].x, enemies[i].y, 10, C_BRIGHT_RED);
            if (enemies[i].hp <= 0) {
                enemies[i].active = false;
                player.score += (enemies[i].type == ENEMY_BOSS) ? 3000 : 300;
                player.enemiesKilled++;
            }
        }
    }
}

// Player Hit Logic
void PlayerHit(void) {
    if (player.invulnTimer > 0) return;

    screenShake = 8;
    if (player.shield > 0) {
        player.shield--;
        player.invulnTimer = 45;
        TriggerSound(SND_HIT);
        SpawnExplosion(player.x, player.y, 8, C_BRIGHT_CYAN);
    } else {
        player.lives--;
        player.shield = 2; // Temporary shield on respawn
        player.invulnTimer = 75;
        if (player.weaponLevel > 1) player.weaponLevel--;
        SpawnExplosion(player.x, player.y, 20, C_BRIGHT_RED);
        TriggerSound(SND_EXPLODE);

        if (player.lives <= 0) {
            gameState = STATE_GAMEOVER;
            SaveHighScore();
            TriggerSound(SND_GAMEOVER);
        }
    }
}

// Spawn Enemy Helper
void SpawnEnemy(EnemyType type, float x, float y) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].active = true;
            enemies[i].type = type;
            enemies[i].x = x;
            enemies[i].y = y;
            enemies[i].shootTimer = 20 + rand() % 40;
            enemies[i].moveTimer = 0;

            switch (type) {
                case ENEMY_SCOUT:
                    enemies[i].hp = 1;
                    enemies[i].maxHp = 1;
                    enemies[i].vx = ((rand() % 2 == 0) ? 0.35f : -0.35f);
                    enemies[i].vy = 0.28f;
                    break;
                case ENEMY_RAIDER:
                    enemies[i].hp = 2;
                    enemies[i].maxHp = 2;
                    enemies[i].vx = 0.25f;
                    enemies[i].vy = 0.18f;
                    break;
                case ENEMY_BOMBER:
                    enemies[i].hp = 4;
                    enemies[i].maxHp = 4;
                    enemies[i].vx = 0.15f;
                    enemies[i].vy = 0.12f;
                    break;
                case ENEMY_ASTEROID:
                    enemies[i].hp = 3;
                    enemies[i].maxHp = 3;
                    enemies[i].vx = ((float)(rand() % 100) - 50.0f) / 250.0f;
                    enemies[i].vy = 0.15f + ((float)(rand() % 100)) / 400.0f;
                    break;
                case ENEMY_MINI_AST:
                    enemies[i].hp = 1;
                    enemies[i].maxHp = 1;
                    enemies[i].vx = ((float)(rand() % 100) - 50.0f) / 150.0f;
                    enemies[i].vy = 0.25f;
                    break;
                case ENEMY_BOSS:
                    enemies[i].hp = 35 + currentWave * 10;
                    enemies[i].maxHp = enemies[i].hp;
                    enemies[i].vx = 0.35f;
                    enemies[i].vy = 0.05f;
                    bossActive = 1;
                    break;
            }
            break;
        }
    }
}

// Wave Spawner
void UpdateWaveSpawner(void) {
    if (bossActive) return;

    waveSpawnTimer--;
    if (waveSpawnTimer <= 0) {
        // Check if boss should appear (every 5 waves)
        if (currentWave % 5 == 0 && !bossActive) {
            SpawnEnemy(ENEMY_BOSS, SCREEN_WIDTH / 2.0f, 4.0f);
            waveSpawnTimer = 600;
            return;
        }

        // Spawn a squadron / wave
        int groupSize = 2 + (currentWave % 4);
        int typeRoll = rand() % 100;
        EnemyType etype = ENEMY_SCOUT;

        if (typeRoll < 40) etype = ENEMY_SCOUT;
        else if (typeRoll < 70) etype = ENEMY_RAIDER;
        else if (typeRoll < 85) etype = ENEMY_BOMBER;
        else etype = ENEMY_ASTEROID;

        float startX = 10.0f + (rand() % (SCREEN_WIDTH - 25));
        for (int i = 0; i < groupSize; i++) {
            SpawnEnemy(etype, startX + (i * 5.0f), (float)(PLAY_TOP - 2 - (i * 2)));
        }

        waveSpawnTimer = 65 - (currentWave * 2);
        if (waveSpawnTimer < 25) waveSpawnTimer = 25;
    }
}

// Spawn Enemy Bullet
void EnemyShoot(float x, float y, float vx, float vy) {
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemyBullets[i].active) {
            enemyBullets[i].active = true;
            enemyBullets[i].x = x;
            enemyBullets[i].y = y;
            enemyBullets[i].vx = vx;
            enemyBullets[i].vy = vy;
            enemyBullets[i].color = C_BRIGHT_RED;
            break;
        }
    }
}

// Update Game World
void UpdateGame(void) {
    frameCounter++;

    // Screen Shake decay
    if (screenShake > 0) screenShake--;

    // Update Stars
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y >= SCREEN_HEIGHT) {
            stars[i].y = 1.0f;
            stars[i].x = (float)(rand() % SCREEN_WIDTH);
        }
    }

    // Overdrive and Timers
    if (player.overdriveTimer > 0) player.overdriveTimer--;
    if (player.shootCooldown > 0) player.shootCooldown--;
    if (player.invulnTimer > 0) player.invulnTimer--;

    // Combo system decay
    if (player.comboTimer > 0) {
        player.comboTimer--;
        if (player.comboTimer <= 0) player.combo = 0;
    }

    // Player Movement (WASD / Arrows)
    float moveSpeed = (player.overdriveTimer > 0) ? 1.0f : 0.75f;
    if ((GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000)) {
        player.x -= moveSpeed;
    }
    if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000)) {
        player.x += moveSpeed;
    }
    if ((GetAsyncKeyState(VK_UP) & 0x8000) || (GetAsyncKeyState('W') & 0x8000)) {
        player.y -= moveSpeed * 0.7f;
    }
    if ((GetAsyncKeyState(VK_DOWN) & 0x8000) || (GetAsyncKeyState('S') & 0x8000)) {
        player.y += moveSpeed * 0.7f;
    }

    // Clamping Player to Play Area
    if (player.x < 3.0f) player.x = 3.0f;
    if (player.x > SCREEN_WIDTH - 4.0f) player.x = SCREEN_WIDTH - 4.0f;
    if (player.y < PLAY_TOP + 3.0f) player.y = PLAY_TOP + 3.0f;
    if (player.y > PLAY_BOTTOM - 1.0f) player.y = PLAY_BOTTOM - 1.0f;

    // Shooting input
    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState('J') & 0x8000)) {
        PlayerShoot();
    }

    // Update Player Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (playerBullets[i].active) {
            playerBullets[i].x += playerBullets[i].vx;
            playerBullets[i].y += playerBullets[i].vy;

            if (playerBullets[i].y < PLAY_TOP || playerBullets[i].x < 0 || playerBullets[i].x >= SCREEN_WIDTH) {
                playerBullets[i].active = false;
            }
        }
    }

    // Update Enemy Bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemyBullets[i].active) {
            enemyBullets[i].x += enemyBullets[i].vx;
            enemyBullets[i].y += enemyBullets[i].vy;

            // Collision with Player
            if (fabs(enemyBullets[i].x - player.x) < 1.6f && fabs(enemyBullets[i].y - player.y) < 1.0f) {
                enemyBullets[i].active = false;
                PlayerHit();
            }

            if (enemyBullets[i].y > PLAY_BOTTOM || enemyBullets[i].x < 0 || enemyBullets[i].x >= SCREEN_WIDTH) {
                enemyBullets[i].active = false;
            }
        }
    }

    // Update Enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].moveTimer++;

            // AI Movements
            switch (enemies[i].type) {
                case ENEMY_SCOUT:
                    enemies[i].x += enemies[i].vx;
                    enemies[i].y += enemies[i].vy;
                    if (enemies[i].x <= 2 || enemies[i].x >= SCREEN_WIDTH - 3) {
                        enemies[i].vx = -enemies[i].vx;
                    }
                    break;
                case ENEMY_RAIDER:
                    enemies[i].x += enemies[i].vx;
                    enemies[i].y += enemies[i].vy;
                    if (enemies[i].x <= 4 || enemies[i].x >= SCREEN_WIDTH - 5) {
                        enemies[i].vx = -enemies[i].vx;
                    }
                    // Shooting
                    enemies[i].shootTimer--;
                    if (enemies[i].shootTimer <= 0) {
                        enemies[i].shootTimer = 40 + rand() % 40;
                        EnemyShoot(enemies[i].x, enemies[i].y + 1, 0.0f, 0.55f);
                    }
                    break;
                case ENEMY_BOMBER:
                    enemies[i].x += enemies[i].vx;
                    enemies[i].y += enemies[i].vy;
                    if (enemies[i].x <= 6 || enemies[i].x >= SCREEN_WIDTH - 7) {
                        enemies[i].vx = -enemies[i].vx;
                    }
                    enemies[i].shootTimer--;
                    if (enemies[i].shootTimer <= 0) {
                        enemies[i].shootTimer = 55 + rand() % 30;
                        EnemyShoot(enemies[i].x - 1, enemies[i].y + 1, -0.2f, 0.45f);
                        EnemyShoot(enemies[i].x + 1, enemies[i].y + 1,  0.2f, 0.45f);
                    }
                    break;
                case ENEMY_ASTEROID:
                case ENEMY_MINI_AST:
                    enemies[i].x += enemies[i].vx;
                    enemies[i].y += enemies[i].vy;
                    break;
                case ENEMY_BOSS:
                    // Boss moves back and forth at top
                    enemies[i].x += enemies[i].vx;
                    if (enemies[i].x < 14 || enemies[i].x > SCREEN_WIDTH - 15) {
                        enemies[i].vx = -enemies[i].vx;
                    }
                    // Boss attack patterns
                    enemies[i].shootTimer--;
                    if (enemies[i].shootTimer <= 0) {
                        enemies[i].shootTimer = 25;
                        // Triple spread
                        EnemyShoot(enemies[i].x - 4, enemies[i].y + 2, -0.3f, 0.5f);
                        EnemyShoot(enemies[i].x, enemies[i].y + 3, 0.0f, 0.6f);
                        EnemyShoot(enemies[i].x + 4, enemies[i].y + 2, 0.3f, 0.5f);
                    }
                    break;
            }

            // Player-Enemy Collision
            float colDist = (enemies[i].type == ENEMY_BOSS) ? 6.0f : 1.8f;
            if (fabs(enemies[i].x - player.x) < colDist && fabs(enemies[i].y - player.y) < 1.4f) {
                PlayerHit();
                if (enemies[i].type != ENEMY_BOSS) {
                    enemies[i].hp = 0;
                }
            }

            // Check if Enemy passed bottom boundary
            if (enemies[i].y > PLAY_BOTTOM + 1 && enemies[i].type != ENEMY_BOSS) {
                enemies[i].active = false;
            }

            // Check Bullet-Enemy Collisions
            for (int b = 0; b < MAX_BULLETS; b++) {
                if (playerBullets[b].active) {
                    float hitDist = (enemies[i].type == ENEMY_BOSS) ? 5.0f : 1.5f;
                    if (fabs(playerBullets[b].x - enemies[i].x) < hitDist &&
                        fabs(playerBullets[b].y - enemies[i].y) < 1.2f) {
                        
                        playerBullets[b].active = false;
                        enemies[i].hp--;
                        SpawnExplosion(playerBullets[b].x, playerBullets[b].y, 3, C_BRIGHT_YELLOW);

                        if (enemies[i].type == ENEMY_BOSS) {
                            TriggerSound(SND_BOSS_HIT);
                        }

                        // Destroyed enemy
                        if (enemies[i].hp <= 0) {
                            enemies[i].active = false;
                            player.enemiesKilled++;
                            player.combo++;
                            player.comboTimer = 90;
                            int comboMultiplier = (player.combo > 4) ? 4 : player.combo;

                            int basePts = 100;
                            if (enemies[i].type == ENEMY_RAIDER) basePts = 250;
                            else if (enemies[i].type == ENEMY_BOMBER) basePts = 450;
                            else if (enemies[i].type == ENEMY_ASTEROID) basePts = 150;
                            else if (enemies[i].type == ENEMY_BOSS) basePts = 5000;

                            player.score += basePts * comboMultiplier;

                            // Asteroid split
                            if (enemies[i].type == ENEMY_ASTEROID) {
                                SpawnEnemy(ENEMY_MINI_AST, enemies[i].x - 1.5f, enemies[i].y);
                                SpawnEnemy(ENEMY_MINI_AST, enemies[i].x + 1.5f, enemies[i].y);
                            }

                            // Boss defeated
                            if (enemies[i].type == ENEMY_BOSS) {
                                bossActive = 0;
                                currentWave++;
                                screenShake = 16;
                                SpawnExplosion(enemies[i].x, enemies[i].y, 35, C_BRIGHT_RED);
                                TriggerSound(SND_EXPLODE);
                                player.bombs = (player.bombs < 3) ? player.bombs + 1 : 3;
                            } else {
                                SpawnExplosion(enemies[i].x, enemies[i].y, 10, C_BRIGHT_YELLOW);
                                TriggerSound(SND_EXPLODE);

                                // Chance to drop power-up (30%)
                                if ((rand() % 100) < 32) {
                                    SpawnPowerUp(enemies[i].x, enemies[i].y);
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Update PowerUps
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active) {
            powerups[i].y += 0.22f; // Slowly drift downwards

            // Check collection by player
            if (fabs(powerups[i].x - player.x) < 2.2f && fabs(powerups[i].y - player.y) < 1.2f) {
                powerups[i].active = false;
                TriggerSound(SND_POWERUP);
                SpawnExplosion(player.x, player.y, 8, C_BRIGHT_GREEN);

                switch (powerups[i].type) {
                    case PWR_SHIELD:
                        if (player.shield < 3) player.shield++;
                        player.score += 200;
                        break;
                    case PWR_WEAPON:
                        if (player.weaponLevel < 4) player.weaponLevel++;
                        player.score += 300;
                        break;
                    case PWR_BOMB:
                        if (player.bombs < 3) player.bombs++;
                        player.score += 500;
                        break;
                    case PWR_LIFE:
                        if (player.lives < 5) player.lives++;
                        player.score += 1000;
                        break;
                    case PWR_OVERDRIVE:
                        player.overdriveTimer = 180; // 3 seconds of hyper fire
                        player.score += 400;
                        break;
                }
            }

            if (powerups[i].y > PLAY_BOTTOM) {
                powerups[i].active = false;
            }
        }
    }

    // Update Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life -= 1.0f;
            if (particles[i].life <= 0 || particles[i].y < 0 || particles[i].y >= SCREEN_HEIGHT) {
                particles[i].active = false;
            }
        }
    }

    // Check wave progression
    if (!bossActive && frameCounter % 400 == 0) {
        currentWave++;
    }

    UpdateWaveSpawner();
}

// Render World
void RenderGame(void) {
    BufferClear();

    // 1. Draw Starfield
    for (int i = 0; i < MAX_STARS; i++) {
        BufferSetChar((int)stars[i].x, (int)stars[i].y, stars[i].ch, stars[i].color);
    }

    // 2. Draw Borders and HUD
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        BufferSetChar(x, 1, '=', C_BRIGHT_BLUE);
        BufferSetChar(x, PLAY_BOTTOM, '=', C_BRIGHT_BLUE);
    }

    // Top HUD Line
    char hudStr[128];
    char shieldStr[8] = "";
    for (int s = 0; s < 3; s++) {
        if (s < player.shield) strcat(shieldStr, "#");
        else strcat(shieldStr, "-");
    }

    sprintf(hudStr, " SCORE:%06d  HI:%06d  LIVES:%d  SHIELD:[%s]  BOMB:%d  WAVE:%d",
            player.score, player.highScore, player.lives, shieldStr, player.bombs, currentWave);
    BufferPrint(1, 0, hudStr, C_BRIGHT_CYAN);

    // Audio and Overdrive indicator
    if (player.overdriveTimer > 0) {
        BufferPrint(SCREEN_WIDTH - 12, 0, "[HYPER!]", C_BRIGHT_YELLOW);
    } else {
        BufferPrint(SCREEN_WIDTH - 9, 0, g_soundEnabled ? "[SND:ON]" : "[SND:OFF]", C_CYAN);
    }

    // Combo Indicator
    if (player.combo > 1) {
        char comboStr[32];
        sprintf(comboStr, "COMBO x%d!", (player.combo > 4) ? 4 : player.combo);
        BufferPrint(SCREEN_WIDTH - 22, 2, comboStr, C_BRIGHT_YELLOW);
    }

    // Boss Health Bar
    if (bossActive) {
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active && enemies[i].type == ENEMY_BOSS) {
                char bossBar[64];
                int barLen = 24;
                int filled = (int)((float)enemies[i].hp / enemies[i].maxHp * barLen);
                if (filled < 0) filled = 0;
                char bar[32] = "";
                for (int b = 0; b < barLen; b++) {
                    bar[b] = (b < filled) ? '#' : '-';
                }
                bar[barLen] = '\0';
                sprintf(bossBar, "DREADNOUGHT BOSS: [%s]", bar);
                BufferPrint(SCREEN_WIDTH / 2 - 19, 2, bossBar, C_BRIGHT_RED);
                break;
            }
        }
    }

    // 3. Draw PowerUps
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active) {
            int px = (int)powerups[i].x;
            int py = (int)powerups[i].y;
            switch (powerups[i].type) {
                case PWR_SHIELD:
                    BufferPrint(px - 1, py, "[S]", C_BRIGHT_CYAN);
                    break;
                case PWR_WEAPON:
                    BufferPrint(px - 1, py, "[W]", C_BRIGHT_YELLOW);
                    break;
                case PWR_BOMB:
                    BufferPrint(px - 1, py, "[B]", C_BRIGHT_RED);
                    break;
                case PWR_LIFE:
                    BufferPrint(px - 1, py, "[+]", C_BRIGHT_GREEN);
                    break;
                case PWR_OVERDRIVE:
                    BufferPrint(px - 1, py, "[O]", C_BRIGHT_MAGENTA);
                    break;
            }
        }
    }

    // 4. Draw Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) {
            BufferSetChar((int)particles[i].x, (int)particles[i].y, particles[i].ch, particles[i].color);
        }
    }

    // 5. Draw Player Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (playerBullets[i].active) {
            char bch = (player.weaponLevel >= 4) ? 'O' : '|';
            BufferSetChar((int)playerBullets[i].x, (int)playerBullets[i].y, bch, playerBullets[i].color);
        }
    }

    // 6. Draw Enemy Bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemyBullets[i].active) {
            BufferSetChar((int)enemyBullets[i].x, (int)enemyBullets[i].y, '*', enemyBullets[i].color);
        }
    }

    // 7. Draw Enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            int ex = (int)enemies[i].x;
            int ey = (int)enemies[i].y;
            switch (enemies[i].type) {
                case ENEMY_SCOUT:
                    BufferSetChar(ex, ey, 'v', C_BRIGHT_GREEN);
                    break;
                case ENEMY_RAIDER:
                    BufferPrint(ex - 1, ey, "/V\\", C_BRIGHT_YELLOW);
                    break;
                case ENEMY_BOMBER:
                    BufferPrint(ex - 2, ey, "<[W]>", C_BRIGHT_MAGENTA);
                    break;
                case ENEMY_ASTEROID:
                    BufferPrint(ex - 1, ey, "(@)", C_WHITE);
                    break;
                case ENEMY_MINI_AST:
                    BufferSetChar(ex, ey, 'o', C_WHITE);
                    break;
                case ENEMY_BOSS:
                    // Massive multi-line ASCII boss dreadnought
                    BufferPrint(ex - 9, ey - 1, " /===[ OMEGA CORE ]===\\ ", C_BRIGHT_RED);
                    BufferPrint(ex - 9, ey,     "|<##>   <==[XX]==>   <##>|", C_BRIGHT_YELLOW);
                    BufferPrint(ex - 9, ey + 1, " \\==/   \\========/   \\==/ ", C_BRIGHT_RED);
                    break;
            }
        }
    }

    // 8. Draw Player Starfighter
    int px = (int)player.x;
    int py = (int)player.y;
    bool blink = (player.invulnTimer > 0 && (player.invulnTimer / 4) % 2 == 0);
    if (!blink) {
        WORD shipColor = (player.overdriveTimer > 0) ? C_BRIGHT_MAGENTA : C_BRIGHT_CYAN;
        BufferPrint(px - 1, py - 1, " ^ ", C_BRIGHT_WHITE);
        BufferPrint(px - 2, py,     "<|=|>", shipColor);

        // Thruster flame
        char thruster = (frameCounter % 2 == 0) ? 'v' : '|';
        BufferSetChar(px, py + 1, thruster, C_BRIGHT_YELLOW);

        // Draw Shield Aura if active
        if (player.shield > 0) {
            BufferSetChar(px - 3, py, '(', C_BRIGHT_CYAN);
            BufferSetChar(px + 3, py, ')', C_BRIGHT_CYAN);
        }
    }

    // Bottom Help Text
    BufferPrint(2, SCREEN_HEIGHT - 1, "WASD:Move  SPACE:Shoot  B:SmartBomb  M:Sound  P:Pause  Q:Quit", C_CYAN);

    BufferPresent();
}

// Title Screen
void RenderTitleScreen(void) {
    BufferClear();

    // Background stars
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].y += stars[i].speed * 0.5f;
        if (stars[i].y >= SCREEN_HEIGHT) stars[i].y = 0;
        BufferSetChar((int)stars[i].x, (int)stars[i].y, stars[i].ch, stars[i].color);
    }

    // Title Banner
    int y = 3;
    BufferPrint(10, y++, "  ____  ____   _    ____ _____   ____  _   _ _   _ _   _ _____ ____  ", C_BRIGHT_CYAN);
    BufferPrint(10, y++, " / ___||  _ \\ / \\  / ___| ____| |  _ \\| | | | \\ | | \\ | | ____|  _ \\ ", C_BRIGHT_CYAN);
    BufferPrint(10, y++, " \\___ \\| |_) / _ \\| |   |  _|   | |_) | | | |  \\| |  \\| |  _| | |_) |", C_BRIGHT_WHITE);
    BufferPrint(10, y++, "  ___) |  __/ ___ \\ |___| |___  |  _ <| |_| | |\\  | |\\  | |___|  _ < ", C_BRIGHT_BLUE);
    BufferPrint(10, y++, " |____/|_| /_/   \\_\\____|_____| |_| \\_\\\\___/|_| \\_|_| \\_|_____|_| \\_\\", C_BRIGHT_BLUE);

    y += 2;
    BufferPrint(24, y++, "=== STAR CITADEL: TERMINAL EDITION ===", C_BRIGHT_YELLOW);

    y += 2;
    BufferPrint(20, y++, "[ CARA BERMAIN / CONTROLS ]", C_BRIGHT_GREEN);
    BufferPrint(20, y++, "  W, A, S, D / Panah : Mengendalikan Starfighter", C_WHITE);
    BufferPrint(20, y++, "  SPASI / J          : Tembak Laser", C_WHITE);
    BufferPrint(20, y++, "  B / K              : Bom Pintar (Hancurkan Semua Peluru)", C_WHITE);
    BufferPrint(20, y++, "  M                  : Aktif / Nonaktifkan Suara Retro", C_WHITE);
    BufferPrint(20, y++, "  P / ESC            : Jeda Permainan (Pause)", C_WHITE);

    y += 2;
    BufferPrint(20, y++, "[ ITEM POWER-UP ]", C_BRIGHT_GREEN);
    BufferPrint(20, y++, "  [S] Shield   [W] Senjata   [B] Smart Bomb   [O] Overdrive", C_BRIGHT_MAGENTA);

    y += 2;
    char hiscoreStr[64];
    sprintf(hiscoreStr, "REKOR TERTINGGI (HIGH SCORE): %06d", player.highScore);
    BufferPrint(22, y++, hiscoreStr, C_BRIGHT_YELLOW);

    y += 2;
    if ((frameCounter / 15) % 2 == 0) {
        BufferPrint(23, y, ">> TEKAN [SPASI] UNTUK MEMULAI <<", C_BRIGHT_WHITE);
    }

    BufferPresent();
}

// Game Over Screen
void RenderGameOverScreen(void) {
    BufferClear();

    // Background stars
    for (int i = 0; i < MAX_STARS; i++) {
        BufferSetChar((int)stars[i].x, (int)stars[i].y, stars[i].ch, stars[i].color);
    }

    int y = 5;
    BufferPrint(22, y++, "  ____    _    __  __ _____    _____     _______ ____  ", C_BRIGHT_RED);
    BufferPrint(22, y++, " / ___|  / \\  |  \\/  | ____|  / _ \\ \\   / / ____|  _ \\ ", C_BRIGHT_RED);
    BufferPrint(22, y++, "| |  _  / _ \\ | |\\/| |  _|   | | | \\ \\ / /|  _| | |_) |", C_BRIGHT_YELLOW);
    BufferPrint(22, y++, "| |_| |/ ___ \\| |  | | |___  | |_| |\\ V / | |___|  _ < ", C_BRIGHT_RED);
    BufferPrint(22, y++, " \\____/_/   \\_\\_|  |_|_____|  \\___/  \\_/  |_____|_| \\_\\", C_BRIGHT_RED);

    y += 3;
    char scoreStr[64];
    sprintf(scoreStr, "SKOR AKHIR       : %06d", player.score);
    BufferPrint(28, y++, scoreStr, C_BRIGHT_WHITE);

    sprintf(scoreStr, "MUSUH DIHANCURKAN: %d", player.enemiesKilled);
    BufferPrint(28, y++, scoreStr, C_BRIGHT_CYAN);

    sprintf(scoreStr, "WAVE TERCAPAI    : %d", currentWave);
    BufferPrint(28, y++, scoreStr, C_BRIGHT_GREEN);

    if (player.score >= player.highScore && player.score > 0) {
        BufferPrint(28, y++, "*** REKOR BARU TERCAPAI! ***", C_BRIGHT_YELLOW);
    } else {
        sprintf(scoreStr, "REKOR TERTINGGI  : %06d", player.highScore);
        BufferPrint(28, y++, scoreStr, C_WHITE);
    }

    y += 3;
    BufferPrint(25, y++, "TEKAN [R] : MAIN LAGI", C_BRIGHT_GREEN);
    BufferPrint(25, y++, "TEKAN [Q] : KELUAR KE WINDOWS", C_BRIGHT_RED);

    BufferPresent();
}

// Pause Screen Overlay
void RenderPauseScreen(void) {
    int boxX = SCREEN_WIDTH / 2 - 16;
    int boxY = SCREEN_HEIGHT / 2 - 4;

    for (int y = boxY; y <= boxY + 7; y++) {
        for (int x = boxX; x <= boxX + 32; x++) {
            BufferSetChar(x, y, ' ', C_BLACK);
        }
    }

    for (int x = boxX; x <= boxX + 32; x++) {
        BufferSetChar(x, boxY, '=', C_BRIGHT_YELLOW);
        BufferSetChar(x, boxY + 7, '=', C_BRIGHT_YELLOW);
    }

    BufferPrint(boxX + 11, boxY + 2, "[ PAUSED ]", C_BRIGHT_WHITE);
    BufferPrint(boxX + 4, boxY + 4, "Tekan [P] untuk Lanjut", C_BRIGHT_CYAN);
    BufferPrint(boxX + 4, boxY + 5, "Tekan [Q] untuk Keluar", C_BRIGHT_RED);

    BufferPresent();
}

// Setup Windows Console
void InitConsole(void) {
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    // Set Window and Screen Buffer Size
    COORD coord = { SCREEN_WIDTH, SCREEN_HEIGHT };
    SetConsoleScreenBufferSize(hStdout, coord);

    SMALL_RECT rect = { 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1 };
    SetConsoleWindowInfo(hStdout, TRUE, &rect);

    // Hide blinking cursor
    CONSOLE_CURSOR_INFO cursorInfo;
    cursorInfo.dwSize = 1;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hStdout, &cursorInfo);

    // Set Console Title
    SetConsoleTitleA("SPACE VANGUARD: STAR CITADEL - C Arcade Game");
}

int main(void) {
    srand((unsigned int)time(NULL));
    InitConsole();
    InitStars();
    LoadHighScore();

    // Launch background audio thread
    HANDLE hThread = CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);

    bool running = true;
    bool pKeyPressed = false;
    bool bKeyPressed = false;
    bool mKeyPressed = false;

    // Game loop timing (Target ~30-40 FPS for smooth terminal responsiveness)
    while (running) {
        frameCounter++;

        // Global keys
        if ((GetAsyncKeyState('M') & 0x8000)) {
            if (!mKeyPressed) {
                g_soundEnabled = !g_soundEnabled;
                mKeyPressed = true;
            }
        } else {
            mKeyPressed = false;
        }

        switch (gameState) {
            case STATE_TITLE:
                RenderTitleScreen();
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
                    ResetGame();
                    gameState = STATE_PLAYING;
                }
                if (GetAsyncKeyState('Q') & 0x8000) {
                    running = false;
                }
                Sleep(30);
                break;

            case STATE_PLAYING:
                // Bomb trigger
                if ((GetAsyncKeyState('B') & 0x8000) || (GetAsyncKeyState('K') & 0x8000)) {
                    if (!bKeyPressed) {
                        DeploySmartBomb();
                        bKeyPressed = true;
                    }
                } else {
                    bKeyPressed = false;
                }

                // Pause trigger
                if ((GetAsyncKeyState('P') & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                    if (!pKeyPressed) {
                        gameState = STATE_PAUSED;
                        pKeyPressed = true;
                    }
                } else {
                    pKeyPressed = false;
                }

                // Quit trigger
                if (GetAsyncKeyState('Q') & 0x8000) {
                    running = false;
                }

                UpdateGame();
                RenderGame();
                Sleep(28); // ~35 FPS
                break;

            case STATE_PAUSED:
                RenderPauseScreen();
                if ((GetAsyncKeyState('P') & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                    if (!pKeyPressed) {
                        gameState = STATE_PLAYING;
                        pKeyPressed = true;
                    }
                } else {
                    pKeyPressed = false;
                }
                if (GetAsyncKeyState('Q') & 0x8000) {
                    running = false;
                }
                Sleep(50);
                break;

            case STATE_GAMEOVER:
                RenderGameOverScreen();
                if (GetAsyncKeyState('R') & 0x8000) {
                    ResetGame();
                    gameState = STATE_PLAYING;
                }
                if (GetAsyncKeyState('Q') & 0x8000) {
                    running = false;
                }
                Sleep(40);
                break;
        }
    }

    // Cleanup
    g_soundThreadRunning = false;
    if (hThread) {
        WaitForSingleObject(hThread, 500);
        CloseHandle(hThread);
    }

    // Restore Console Settings
    CONSOLE_CURSOR_INFO cursorInfo;
    cursorInfo.dwSize = 25;
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hStdout, &cursorInfo);
    SetConsoleTextAttribute(hStdout, C_WHITE);
    system("cls");

    printf("\nTerima kasih telah memainkan Space Vanguard: Star Citadel!\n");
    return 0;
}
