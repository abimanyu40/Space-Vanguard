# 🚀 SPACE VANGUARD: STAR CITADEL

Sebuah arcade space shooter retro berkecepatan tinggi yang ditulis murni dalam **Bahasa C (C99)** menggunakan Windows Console API dengan teknik **Zero-Flicker Double Buffering**, sistem partikel ledakan, audio chiptune retro multi-thread, boss battles, dan ragam power-up.

---

## 🎮 Cara Menjalankan Game

Kamu bisa langsung menjalankan game dengan salah satu cara berikut:

1. **Klik Dua Kali File**:
   - Double-click [`run.bat`](file:///d:/Project/run.bat) atau [`game.exe`](file:///d:/Project/game.exe) di File Explorer.
2. **Lewat Terminal / PowerShell**:
   ```powershell
   .\run.bat
   # atau
   .\game.exe
   ```

---

## 🕹️ Kontrol Permainan

| Tombol | Fungsi |
| :--- | :--- |
| **`W, A, S, D`** atau **Tombol Panah** | Menggerakkan pesawat Starfighter ke segala arah |
| **`SPASI`** atau **`J`** | Menembakkan Laser (Tekan atau Tahan) |
| **`B`** atau **`K`** | Meluncurkan **Smart Bomb** (Menghancurkan peluru & memberi damage massal) |
| **`M`** | Mute / Unmute Efek Suara Retro (Audio Chiptune) |
| **`P`** atau **`ESC`** | Jeda Permainan (Pause) |
| **`R`** | Main Lagi (pada layar Game Over) |
| **`Q`** | Keluar dari Permainan |

---

## 💎 Fitur & Mekanisme Permainan

### 1. Sistem Senjata & Level Up
Tingkatkan kekuatan tembakan pesawatmu dengan mengambil item **`[W]`**:
- **Level 1**: *Single Laser* (Tembakan lurus terfokus)
- **Level 2**: *Dual Blasters* (Dua tembakan paralel beruntun)
- **Level 3**: *Triple Spread* (Tembakan menyebar sudut 3 arah)
- **Level 4**: *Plasma Cannons* (Tiga bola energi plasma super cepat)

### 2. Item Power-Up
Musuh yang hancur memiliki peluang menjatuhkan power-up:
- **`[S]` Shield**: Menambah 1 baris perisai pelindung pesawat (Maks. 3)
- **`[W]` Weapon**: Menaikkan level senjata Starfighter (Maks. Lv 4)
- **`[B]` Smart Bomb**: Menambah 1 stok bom penghancur layar (Maks. 3)
- **`[+]` Extra Life**: Tambahan 1 nyawa cadangan (Maks. 5)
- **`[O]` Overdrive**: Masuk ke mode **Hyper Speed** & tembakan super cepat selama 3 detik!

### 3. Ragam Musuh & Boss
- **Scout (`v`)**: Cepat, gerakan zig-zag, 1 HP (100 pts)
- **Raider (`/V\`)**: Kecepatan sedang, menembak laser ke arah bawah, 2 HP (250 pts)
- **Bomber (`<[W]>`)**: Tanky dan lambat, menembakkan 2 peluru menyebar, 4 HP (450 pts)
- **Asteroid (`(@)`)**: Asteroid luar angkasa yang pecah menjadi serpihan batu kecil (`o`) saat ditembak, 3 HP (150 pts)
- **DREADNOUGHT BOSS (`/===[ OMEGA CORE ]===\`)**:
  - Muncul di setiap Wave 5, 10, dst.
  - Memiliki Health Bar di layar atas, menembakkan barrage peluru 3 arah, dan bergerak menyapu area perang luar angkasa!

### 4. Sistem Skor & Combo Multiplier
- Mengalahkan musuh secara beruntun akan memicu **Combo Multiplier** (hingga **4x lipat skor**)!
- Rekor tertinggi (*High Score*) otomatis tersimpan ke file `highscore.dat`.

---

## 🛠️ Cara Re-Compile Ulang (Opsional)

Jika kamu ingin memodifikasi kode sumber di [`main.c`](file:///d:/Project/main.c):
1. Cukup jalankan [`build.bat`](file:///d:/Project/build.bat), atau:
2. Jalankan perintah berikut di PowerShell / Command Prompt:
   ```cmd
   clang -O2 -Wall main.c -o game.exe -lkernel32 -luser32
   ```

---

## ⚡ Sorotan Arsitektur Teknis
- **Double Buffering Console**: Menggunakan struktur `CHAR_INFO` dan fungsi `WriteConsoleOutputA` dari Windows API untuk memindahkan frame buffer ke layar secara instan dalam 1 operasi tanpa kedip (*100% zero-flicker*).
- **Asynchronous Audio Thread**: Efek suara arcade menggunakan `Beep()` yang dijalankan di background thread (`CreateThread`) sehingga sama sekali tidak menghambat fluiditas game loop di 35-40 FPS.
- **Particle System**: Efek percikan ledakan puing bintang beranimasi dengan kalkulasi sudut polar trigonometris (`sinf`, `cosf`).
