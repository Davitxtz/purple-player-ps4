/*
 * PURPLE PLAYER - interface de reprodutor de videos para PS4 (payload)
 *
 * Toda a interface e desenhada por software em um framebuffer 1920x1080
 * (ARGB), sem depender de SDL/fontes externas. A parte especifica do
 * console fica isolada nas funcoes plat_*() no fim do arquivo.
 *
 * Builds:
 *   PC (teste visual):  gcc -O2 -DPC_SIM main.c -o player_pc -lSDL2 -lm
 *   Captura sem janela: gcc -O2 -DHEADLESS main.c -o player_shot -lm
 *   PS4:                compile com o ps4-payload-sdk (sem flags) e complete
 *                       os TODO de plat_init/plat_poll/plat_present.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define W 1920
#define H 1080

static uint32_t fb[W * H];

enum { B_UP = 1, B_DOWN = 2, B_L1 = 4, B_R1 = 8, B_X = 16, B_O = 32, B_QUIT = 64 };

/* ---------- caminhos ---------- */
#ifdef PC_SIM
#define ROOT_USB0 "./usb"
#define ROOT_USB1 "./usb"
#define ROOT_CONSOLE "./console"
#define ROOT_REC "./gravacoes"
#define CFG_PATH "./purple_player.cfg"
#elif defined(HEADLESS)
#define ROOT_USB0 "/tmp/pp/usb"
#define ROOT_USB1 "/tmp/pp/usb"
#define ROOT_CONSOLE "/tmp/pp/console"
#define ROOT_REC "/tmp/pp/rec"
#define CFG_PATH "/tmp/pp/purple_player.cfg"
#else
#define ROOT_USB0 "/mnt/usb0"
#define ROOT_USB1 "/mnt/usb1"
#define ROOT_CONSOLE "/data"
#define ROOT_REC "/user/av_contents/video" /* varia conforme o firmware */
#define CFG_PATH "/data/purple_player.cfg"
#endif

/* ---------- fonte 5x7 (ASCII 32..95, minusculas viram maiusculas) ---------- */
static const uint8_t font[64][5] = {
{0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},
{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0,5,3,0,0},
{0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},{0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},
{0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
{0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},
{8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},
{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},
{0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},
{0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
{0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
{1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7F,0x41,0x41,0},
{2,4,8,0x10,0x20},{0,0x41,0x41,0x7F,0},{4,2,1,2,4},{0x40,0x40,0x40,0x40,0x40}};

/* ---------- primitivas de desenho ---------- */
static uint32_t mix(uint32_t a, uint32_t b, int t) {
    int r = ((a >> 16 & 255) * (255 - t) + (b >> 16 & 255) * t) / 255;
    int g = ((a >> 8 & 255) * (255 - t) + (b >> 8 & 255) * t) / 255;
    int bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}

static void rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = y < 0 ? 0 : y; j < y + h && j < H; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < W; i++) fb[j * W + i] = c;
}

static void rrect(int x, int y, int w, int h, int r, uint32_t c) {
    for (int j = 0; j < h; j++) {
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        int inset = dy ? r - (int)sqrtf((float)(r * r - dy * dy)) : 0;
        rect(x + inset, y + j, w - 2 * inset, 1, c);
    }
}

static void glyph(int x, int y, char ch, int s, uint32_t c) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) ch = '?';
    const uint8_t *g = font[ch - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] >> row & 1) rect(x + col * s, y + row * s, s, s, c);
}

static void text(int x, int y, const char *t, int s, uint32_t c) {
    for (; *t; t++, x += 6 * s) glyph(x, y, *t, s, c);
}
static int tw(const char *t, int s) { return (int)strlen(t) * 6 * s; }

/* Fantasminha desenhado por formulas: cabeca redonda, barra ondulada, olhos. */
static void ghost(int x, int y, int s, uint32_t body, uint32_t eye) {
    for (int j = 0; j < s; j++)
        for (int i = 0; i < s; i++) {
            float fx = (i + .5f) / s * 2 - 1, fy = (j + .5f) / s * 2 - 1;
            int in;
            if (fy <= -0.1f) in = fx * fx + (fy + 0.1f) * (fy + 0.1f) <= 0.64f;
            else in = fabsf(fx) <= 0.8f && fy <= 0.78f + 0.13f * sinf(fx * 9.4f);
            if (!in) continue;
            float ex = fabsf(fx) - 0.3f, ey = fy + 0.15f;
            fb[(y + j) * W + x + i] = (ex * ex + ey * ey <= 0.03f) ? eye : body;
        }
}

/* ---------- estado ---------- */
typedef struct { char name[256]; int dir; } Entry;
#define MAXE 1024
static Entry ents[MAXE];
static int nent, sel, scroll, tab, playing;
static char cwd[512], root[512], playname[256];

static int cfg_24h = 1, cfg_accent = 0, cfg_ghost = 1;
static const uint32_t accents[3] = {0xFFB07CFF, 0xFFE07AF5, 0xFF7C9CFF};
static const char *accent_names[3] = {"LILAS", "ROSA", "AZUL"};
static const char *tab_names[4] = {"PENDRIVE", "CONSOLE", "GRAVACOES", "CONFIG"};

static void cfg_load(void) {
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) return;
    if (fscanf(f, "%d %d %d", &cfg_24h, &cfg_accent, &cfg_ghost) != 3) { cfg_24h = 1; cfg_accent = 0; cfg_ghost = 1; }
    if (cfg_accent < 0 || cfg_accent > 2) cfg_accent = 0;
    fclose(f);
}
static void cfg_save(void) {
    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return;
    fprintf(f, "%d %d %d\n", cfg_24h, cfg_accent, cfg_ghost);
    fclose(f);
}

static int is_video(const char *n) {
    static const char *ext[] = {".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v", ".ts"};
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    for (unsigned i = 0; i < sizeof ext / sizeof *ext; i++) if (!strcasecmp(d, ext[i])) return 1;
    return 0;
}

static int cmp(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void load_dir(const char *path) {
    nent = sel = scroll = 0;
    snprintf(cwd, sizeof cwd, "%s", path);
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && nent < MAXE) {
            if (e->d_name[0] == '.') continue;
            char full[800];
            snprintf(full, sizeof full, "%s/%s", path, e->d_name);
            struct stat st;
            if (stat(full, &st)) continue;
            int dir = S_ISDIR(st.st_mode);
            if (!dir && !is_video(e->d_name)) continue;
            snprintf(ents[nent].name, sizeof ents[nent].name, "%s", e->d_name);
            ents[nent++].dir = dir;
        }
        closedir(d);
    }
    qsort(ents, nent, sizeof *ents, cmp);
    if (strcmp(path, root)) { /* entrada ".." para voltar */
        memmove(ents + 1, ents, sizeof *ents * (nent < MAXE - 1 ? nent : MAXE - 1));
        strcpy(ents[0].name, ".."); ents[0].dir = 1;
        if (nent < MAXE) nent++;
    }
}

static void open_tab(int t) {
    tab = t; playing = 0;
    if (t == 0) { /* pendrive: tenta usb0 e depois usb1 */
        snprintf(root, sizeof root, "%s", ROOT_USB0);
        if (access(root, R_OK)) snprintf(root, sizeof root, "%s", ROOT_USB1);
    } else if (t == 1) snprintf(root, sizeof root, "%s", ROOT_CONSOLE);
    else if (t == 2) snprintf(root, sizeof root, "%s", ROOT_REC);
    if (t < 3) load_dir(root);
}

/* TODO (PS4): iniciar a reproducao aqui, por exemplo com libSceAvPlayer. */
static void play(const char *path) {
    (void)path;
    playing = 1;
}

/* ---------- renderizacao ---------- */
static void draw_clock(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    static const char *dias[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
    char hora[32], dia[32];
    if (cfg_24h) snprintf(hora, sizeof hora, "%02d:%02d", tm->tm_hour, tm->tm_min);
    else snprintf(hora, sizeof hora, "%d:%02d %s", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
    snprintf(dia, sizeof dia, "%s %02d/%02d/%04d", dias[tm->tm_wday], tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    int right = W - 50, gs = 30, tx;
    if (cfg_ghost) { ghost(right - gs, 34, gs, 0xFFF4EBFF, 0xFF2A1250); right -= gs + 16; }
    tx = right - tw(hora, 3);
    text(tx, 28, hora, 3, 0xFFFFFFFF);
    tx = right - tw(dia, 2);
    text(tx, 62, dia, 2, 0xFFD2B8FF);
}

static void draw_settings(uint32_t acc) {
    char l[3][64];
    snprintf(l[0], 64, "FORMATO DA HORA:  %s", cfg_24h ? "24 HORAS" : "12 HORAS");
    snprintf(l[1], 64, "COR DE DESTAQUE:  %s", accent_names[cfg_accent]);
    snprintf(l[2], 64, "ICONE FANTASMA:  %s", cfg_ghost ? "LIGADO" : "DESLIGADO");
    for (int i = 0; i < 3; i++) {
        int y = 250 + i * 80;
        if (i == sel) rrect(110, y, 1700, 64, 16, mix(0xFF2A1250, acc, 90));
        text(150, y + 18, l[i], 4, i == sel ? 0xFFFFFFFF : 0xFFD2B8FF);
    }
}

static void render(void) {
    uint32_t acc = accents[cfg_accent];
    for (int y = 0; y < H; y++) rect(0, y, W, 1, mix(0xFF1A0B2E, 0xFF45198A, y * 255 / H));
    text(60, 38, "PURPLE PLAYER", 5, 0xFFFFFFFF);
    draw_clock();
    for (int i = 0; i < 4; i++) {
        int x = 60 + i * 330;
        rrect(x, 130, 310, 64, 22, i == tab ? acc : mix(0xFF1A0B2E, 0xFF6A35B8, 110));
        int n = tw(tab_names[i], 3);
        text(x + (310 - n) / 2, 130 + 22, tab_names[i], 3, i == tab ? 0xFF1A0B2E : 0xFFEADCFF);
    }
    rrect(80, 220, 1760, 760, 28, mix(0xFF1A0B2E, 0xFF000000, 60));
    if (tab == 3) draw_settings(acc);
    else if (playing) {
        text(150, 300, "REPRODUZINDO", 5, acc);
        text(150, 380, ents[sel].name, 3, 0xFFFFFFFF);
        text(150, 470, "(DECODIFICADOR AINDA NAO LIGADO - VEJA play())", 2, 0xFFD2B8FF);
    } else if (nent == 0) {
        text(150, 300, tab == 0 ? "NENHUM VIDEO OU PENDRIVE NAO ENCONTRADO" : "NENHUM VIDEO ENCONTRADO", 3, 0xFFD2B8FF);
    } else {
        int rows = 11;
        for (int r = 0; r < rows && scroll + r < nent; r++) {
            int i = scroll + r, y = 240 + r * 66;
            if (i == sel) rrect(110, y, 1700, 58, 16, mix(0xFF2A1250, acc, 90));
            char line[300];
            snprintf(line, sizeof line, "%s%s", ents[i].dir ? "> " : "  ", ents[i].name);
            line[56] = 0;
            text(150, y + 14, line, 4, i == sel ? 0xFFFFFFFF : 0xFFE3D2FF);
        }
    }
    text(80, 1010, "L1/R1 ABAS     CIMA/BAIXO MOVER     X OK     O VOLTAR", 2, 0xFFD2B8FF);
}

/* ---------- entrada ---------- */
static void handle(int b) {
    if (b & B_R1) open_tab((tab + 1) % 4);
    if (b & B_L1) open_tab((tab + 3) % 4);
    if (playing) { if (b & B_O) playing = 0; return; }
    int n = tab == 3 ? 3 : nent;
    if ((b & B_DOWN) && sel < n - 1) sel++;
    if ((b & B_UP) && sel > 0) sel--;
    if (sel < scroll) scroll = sel;
    if (sel > scroll + 10) scroll = sel - 10;
    if (b & B_X) {
        if (tab == 3) {
            if (sel == 0) cfg_24h = !cfg_24h;
            if (sel == 1) cfg_accent = (cfg_accent + 1) % 3;
            if (sel == 2) cfg_ghost = !cfg_ghost;
            cfg_save();
        } else if (nent) {
            char p[800];
            if (!strcmp(ents[sel].name, "..")) {
                snprintf(p, sizeof p, "%s", cwd);
                char *s = strrchr(p, '/'); if (s) *s = 0;
                load_dir(p);
            } else {
                snprintf(p, sizeof p, "%s/%s", cwd, ents[sel].name);
                if (ents[sel].dir) load_dir(p); else play(p), snprintf(playname, sizeof playname, "%s", ents[sel].name);
            }
        }
    }
    if ((b & B_O) && tab < 3 && strcmp(cwd, root)) {
        char p[512]; snprintf(p, sizeof p, "%s", cwd);
        char *s = strrchr(p, '/'); if (s) *s = 0;
        load_dir(p);
    }
}

/* ---------- plataforma ---------- */
#ifdef PC_SIM
#include <SDL2/SDL.h>
static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex;
static void plat_init(void) {
    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("Purple Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, 0);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
}
static int plat_poll(void) {
    SDL_Event e; int b = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) b |= B_QUIT;
        if (e.type == SDL_KEYDOWN) switch (e.key.keysym.sym) {
            case SDLK_UP: b |= B_UP; break;      case SDLK_DOWN: b |= B_DOWN; break;
            case SDLK_q: b |= B_L1; break;       case SDLK_e: b |= B_R1; break;
            case SDLK_RETURN: b |= B_X; break;   case SDLK_BACKSPACE: case SDLK_ESCAPE: b |= B_O; break;
        }
    }
    return b;
}
static void plat_present(void) {
    SDL_UpdateTexture(tex, NULL, fb, W * 4); SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL); SDL_RenderPresent(ren);
}
static void plat_delay(int ms) { SDL_Delay(ms); }
#elif defined(HEADLESS)
static int frames;
static void plat_init(void) {}
static int plat_poll(void) { return ++frames > 1 ? B_QUIT : 0; }
static void plat_present(void) {
    FILE *f = fopen("/tmp/shot.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { unsigned char p[3] = {fb[i] >> 16, fb[i] >> 8, fb[i]}; fwrite(p, 1, 3, f); }
    fclose(f);
}
static void plat_delay(int ms) { (void)ms; }
#else
/* TODO (PS4 payload): abrir sceVideoOut, registrar buffers e copiar fb em
 * plat_present(); abrir scePad e converter os botoes (L1, R1, D-pad, X, O)
 * nos bits B_* em plat_poll(). Os nomes exatos dependem do SDK usado. */
static void plat_init(void) {}
static int plat_poll(void) { return 0; }
static void plat_present(void) {}
static void plat_delay(int ms) { usleep(ms * 1000); }
#endif

int main(void) {
    cfg_load();
    plat_init();
    open_tab(0);
    int last_sec = -1;
    for (;;) {
        int b = plat_poll();
        if (b & B_QUIT) break;
        time_t t = time(NULL);
        int sec = (int)(t % 60);
        if (b || sec != last_sec || last_sec < 0) {
            if (b) handle(b);
            render();
            plat_present();
            last_sec = sec;
        }
        plat_delay(33);
    }
    return 0;
}
