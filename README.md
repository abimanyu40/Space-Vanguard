# 🚀 SPACE VANGUARD 2.0: STAR CITADEL

A high-speed retro arcade space shooter written purely in **C (C11)** using the Windows Console API featuring **Zero-Flicker Double Buffering** with half-block characters (96x56 resolution), particle explosion systems, real-time multi-threaded chiptune audio synthesis, multi-phase bullet-hell boss battles, and diverse power-ups.

---

## 🎮 How to Run the Game

You can run the game directly using one of the following methods:

1. **Double-Click File**:
   - Double-click [`run.bat`](file:///d:/Project/Space%20Vanguard/run.bat) or [`game.exe`](file:///d:/Project/Space%20Vanguard/game.exe) in File Explorer.
2. **Via Terminal / PowerShell**:
   ```powershell
   .\run.bat
   # or
   .\game.exe
   ```

---

## 🕹️ Game Controls

| Key | Action |
| :--- | :--- |
| **`W, A, S, D`** or **Arrow Keys** | Move Starfighter in all directions |
| **`SPACE`**, **`Z`**, or **`J`** | Fire Laser (Tap or Hold) |
| **`SHIFT`** (Hold) | **Focus Mode** (Precision movement & tightened shot spread) |
| **`B`**, **`X`**, or **`K`** | Launch **Smart Bomb** (Clears screen bullets & grants temporary invulnerability) |
| **`M`** | Toggle Background Music (ON/OFF) |
| **`N`** | Toggle Sound Effects (ON/OFF) |
| **`P`** or **`ESC`** | Pause Game |
| **`R`** | Retry / Play Again (on Game Over screen) |
| **`ENTER`** | Confirm / Menu Select |

---

## 💎 Features & Game Mechanics

### 1. Weapon System & Level Up
Increase your starfighter's firepower by collecting **`[P]`** power capsules:
- **Level 1**: Dual Forward Lasers
- **Level 2**: Angled Twin Cannons
- **Level 3**: Homing Missile Pods unlocked
- **Level 4**: Wide Spread Quad Lasers
- **Level 5**: **MAX POWER** (Heavy Plasma Core + Dual Homing Missile Barrage)

### 2. Pickups & Power-Ups
Destroyed enemies have chances to drop various items:
- **`[P]` Power Up**: Upgrades starfighter weapon level (Max Lv 5; awards 5,000 pts when at MAX)
- **`[B]` Smart Bomb**: Adds +1 screen-clearing bomb stock (Max 5)
- **`[S]` Energy Shield**: Grants protective barrier absorbing 1 fatal hit
- **`[+]` 1UP Extra Life**: Grants +1 reserve life (Earned via pickup or every 150,000 points)
- **`[◆]` Crystal Gem**: Boosts score and sustains the chain multiplier gauge; fly towards the top of the screen to auto-collect!

### 3. Enemy Fleet & Boss Battles
- **Drone**: Quick scouts moving in sine waves or curved arcs.
- **Fighter**: Tactical interceptors flying in V-formations with targeted spread shots.
- **Bomber**: Heavy armored gunships firing radial projectile rings.
- **Diver**: High-velocity kamikaze dive-bombers aimed straight at your cockpit.
- **Orb**: Heavy energy turrets spinning spiral bullet-hell patterns.
- **Asteroid**: Space rocks that fracture into smaller pebbles upon destruction.
- **BOSS**: Sector flagship featuring 3 distinct attack phases, massive health bar, and dynamic bullet hell patterns!

### 4. Scoring, Graze & Chain Multiplier
- **Chain Multiplier**: Eliminate enemies consecutively to build your combo multiplier up to **9x**!
- **Graze System**: Skim closely past enemy projectiles without taking damage to earn bonus score and extend your chain meter.
- **Save System**: High scores and user settings are automatically saved to [`vanguard.sav`](file:///d:/Project/Space%20Vanguard/vanguard.sav).

---

## 🛠️ How to Compile (Terminal)

If you modify the source code in [`main.c`](file:///d:/Project/Space%20Vanguard/main.c), compile it directly in your terminal (PowerShell or Command Prompt):

```powershell
clang -O2 -std=c11 main.c -o game.exe -lwinmm -luser32
```

*(Alternatively, if you use GCC / MinGW):*
```powershell
gcc -O2 -std=c11 main.c -o game.exe -lwinmm -luser32
```

To run the game right after compilation:
```powershell
.\game.exe
```

---

## ⚡ Technical Highlights
- **Console Half-Block Double Buffering**: Uses Windows Console API (`CHAR_INFO` and `WriteConsoleOutputW`) with unicode half-block characters (`▀`) to render a smooth 96x56 color framebuffer with zero flickering (100% zero-flicker).
- **Asynchronous Audio Synthesizer**: Low-latency multi-channel sound synthesizer using Windows `waveOut` API running on a dedicated worker thread for chiptune melodies and retro sound effects without stalling the 60 FPS gameplay loop.
- **Collision & Particle Physics**: Sub-pixel precision floating-point movement, trigonometric wave patterns, radial shockwaves, screen shake, and responsive controls.
