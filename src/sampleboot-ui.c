/*
 * SampleBoot - full-screen boot selector UI for the Akai MPC Sample.
 *
 * Lists the stock firmware and every OS folder in <autorun>/mods, shows each
 * one's image (or a generated title screen) on the 320x240 framebuffer, reads
 * the surface encoder (raw MIDI) and the power button (evdev), and prints the
 * chosen entry's id ("stock" or the folder name) on stdout. Starting the
 * chosen system is left to the calling script.
 *
 * Controls: turn encoder = browse, push encoder = start,
 *           no input for --timeout seconds = start the shown entry,
 *           POWER = ask whether to switch off (push encoder or POWER again
 *           to confirm, turn encoder to go back; prints "__poweroff").
 *
 * Exit status: 0 with the id (or "__poweroff") on stdout, 1 on a fatal error (the caller then
 * starts the stock app).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "font_data.h"

#define W 320
#define H 240
#define FRAME_BYTES (W * H * 4)
#define MAX_ENTRIES 64
#define BAR 46        /* height of the top and bottom bars */
#define CONFIRM_S 10  /* the switch-off question goes back to the menu after this */
#define VERSION "1.0.2"
#define ARROW_H 18      /* height of the browse arrows, px */
#define ARROW_SW 2.0    /* their stroke width, px */
#define SPLASH_REVEAL 0.8  /* seconds the splash takes to appear, line by line */

/* ------------------------------------------------------------------ options */
static const char *opt_autorun, *opt_last = "", *opt_fb = "/dev/fb0";
static const char *opt_cards = "/proc/asound/cards", *opt_snd = "/dev/snd";
static const char *opt_input_class = "/sys/class/input", *opt_input_dir = "/dev/input";
static const char *opt_stock_bin = "/usr/bin/MPC Sample";
static const char *opt_splash = "dark";   /* dark, light or off: autorun/sampleboot/splash-<name>.png */
static double opt_splash_time = 2;
static int opt_timeout = 10, opt_timeout_browse = 20, opt_hold_cs = 80, opt_beep = 100;
static const char *opt_state = "/run/sampleboot";   /* where the codec volume is kept while lowered */
static volatile int main_volume = -1;                 /* surface Main Volume knob, 0..127, -1 = unknown */

static void logf_(const char *fmt, ...) {   /* one write per line: threads do not mix */
    char line[1024];
    va_list ap; va_start(ap, fmt);
    int n = snprintf(line, sizeof line, "sampleboot-ui: ");
    n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    if (write(2, line, (size_t)n) < 0) { /* nothing to do */ }
}

static double now_s(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ drawing */
static uint32_t fb[W * H];

static inline uint32_t rgb(int r, int g, int b) { return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }

static inline void blend_px(int x, int y, uint32_t c, int a) { /* a: 0..255 */
    if ((unsigned)x >= W || (unsigned)y >= H || a <= 0) return;
    uint32_t *p = &fb[y * W + x];
    if (a >= 255) { *p = c; return; }
    int r = ((*p >> 16 & 255) * (255 - a) + (c >> 16 & 255) * a) / 255;
    int g = ((*p >> 8 & 255) * (255 - a) + (c >> 8 & 255) * a) / 255;
    int b = ((*p & 255) * (255 - a) + (c & 255) * a) / 255;
    *p = rgb(r, g, b);
}

static void fill(int x0, int y0, int w, int h, uint32_t c, int a) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) blend_px(x, y, c, a);
}

/* Open arrow ("<" or ">"): two strokes at 45 degrees meeting at a square tip,
 * antialiased with 4x4 samples per pixel. (cx, cy) is the centre of the arrow,
 * h its height, sw the stroke width; dir -1 points left, +1 right. */
static int in_stroke(double px, double py, double sx, double sy, double ux, double uy, double len, double hw) {
    double dx = px - sx, dy = py - sy, t = dx * ux + dy * uy, d = dx * uy - dy * ux;
    return t >= 0 && t <= len && d >= -hw && d <= hw;
}
static void chevron(double cx, double cy, double h, double sw, int dir, uint32_t c) {
    double half = h / 2, hw = sw / 2, k = 0.70710678118654752;   /* 1/sqrt(2) */
    double tx = cx + dir * half / 2, len = half / k + hw;            /* each arm, + hw past the tip */
    double ux = -dir * k;                                            /* from the tip towards the ends */
    double s1x = tx - ux * hw, s1y = cy + k * hw, s2x = s1x, s2y = cy - k * hw;  /* starts, back past the tip */
    int x0 = (int)floor(cx - half / 2 - sw), x1 = (int)ceil(cx + half / 2 + sw);
    int y0 = (int)floor(cy - half - sw), y1 = (int)ceil(cy + half + sw);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int n = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    double px = x + (i + 0.5) / 4, py = y + (j + 0.5) / 4;
                    n += in_stroke(px, py, s1x, s1y, ux, -k, len, hw) || in_stroke(px, py, s2x, s2y, ux, k, len, hw);
                }
            if (n) blend_px(x, y, c, n * 255 / 16);
        }
}

/* UTF-8 -> code point; advances *s. Invalid bytes become '?'. */
static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp; int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) { cp = (p[0] & 0x1f) << 6 | (p[1] & 0x3f); n = 2; }
    else if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = (p[0] & 0x0f) << 12 | (p[1] & 0x3f) << 6 | (p[2] & 0x3f); n = 3;
    } else { cp = '?'; n = 1; while (p[n] && (p[n] & 0xc0) == 0x80) n++; }
    *s += n;
    return cp;
}

static const glyph_t *glyph(const font_t *f, uint32_t cp) {
    int i;
    if (cp >= 32 && cp <= 126) i = cp - 32;
    else if (cp >= 160 && cp <= 255) i = 95 + cp - 160;
    else if (cp == 0x2026) i = 191;
    else i = '?' - 32;
    return &f->g[i];
}

static int text_w(const font_t *f, const char *s) {
    int w = 0;
    while (*s) w += glyph(f, utf8_next(&s))->adv;
    return w;
}

static void text(const font_t *f, int x, int baseline, const char *s, uint32_t c) {
    while (*s) {
        const glyph_t *g = glyph(f, utf8_next(&s));
        const uint8_t *px = f->px + g->off;
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++)
                blend_px(x + g->x + xx, baseline + g->y + yy, c, px[yy * g->w + xx]);
        x += g->adv;
    }
}

/* Copies s into out, cut with an ellipsis so it fits maxw pixels. */
static void fit(const font_t *f, const char *s, int maxw, char *out, size_t outsz) {
    snprintf(out, outsz, "%s", s);
    if (text_w(f, out) <= maxw) return;
    size_t n = strlen(out);
    while (n > 0) {
        do n--; while (n > 0 && ((unsigned char)out[n] & 0xc0) == 0x80);
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%.*s\xe2\x80\xa6", (int)n, out);
        if (text_w(f, tmp) <= maxw || n == 0) { snprintf(out, outsz, "%s", tmp); return; }
    }
}

static void text_center(const font_t *f, int cx, int baseline, const char *s, int maxw, uint32_t c) {
    char buf[512];
    fit(f, s, maxw, buf, sizeof buf);
    text(f, cx - text_w(f, buf) / 2, baseline, buf, c);
}

/* ------------------------------------------------------------------ entries */
typedef struct {
    char id[256];          /* "stock" or the folder name in mods/ */
    char label[160];       /* name shown on screen */
    char image[1024];       /* image file, "" for the generated screen */
    char script[512];      /* the OS's start script, relative to its folder */
    uint32_t *pix;         /* decoded image, W*H, loaded on first view */
    int loaded;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int n_entries;

static const char *IMG_EXT[] = {"png", "jpg", "jpeg", "bmp", "raw"};

static int is_file(const char *p, off_t *size) {
    struct stat st;
    if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (size) *size = st.st_size;
    return 1;
}

/* Image for a folder: <base>.png/.jpg/.jpeg/.bmp/.raw (any letter case), else
 * (if any_raw) any top-level file of exactly 320x240x4 bytes, e.g. SampQOL's
 * qol-splash.raw. */
static void find_image(const char *dir, const char *base, int any_raw, char *out, size_t outsz) {
    out[0] = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char best[1024] = "", raw[1024] = "";
    int best_rank = 99;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        off_t size;
        if (!is_file(path, &size)) continue;
        size_t bl = strlen(base);
        if ((size_t)(dot - e->d_name) == bl && strncasecmp(e->d_name, base, bl) == 0)
            for (int i = 0; i < 5; i++)
                if (strcasecmp(dot + 1, IMG_EXT[i]) == 0 && i < best_rank) { best_rank = i; snprintf(best, sizeof best, "%s", path); }
        if (any_raw && !raw[0] && strcasecmp(dot + 1, "raw") == 0 && size == FRAME_BYTES)
            snprintf(raw, sizeof raw, "%s", path);
    }
    closedir(d);
    snprintf(out, outsz, "%s", best[0] ? best : raw);
}

static void read_line(const char *path, char *out, size_t outsz) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)outsz, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
    if ((unsigned char)out[0] == 0xef && (unsigned char)out[1] == 0xbb && (unsigned char)out[2] == 0xbf)
        memmove(out, out + 3, strlen(out + 3) + 1);           /* UTF-8 BOM */
    /* trim */
    char *s = out; while (isspace((unsigned char)*s)) s++;
    memmove(out, s, strlen(s) + 1);
    size_t n = strlen(out); while (n && isspace((unsigned char)out[n - 1])) out[--n] = 0;
}

/* Custom menu name: first line of sampleboot.txt in dir. Empty if none. */
static void read_label(const char *dir, char *out, size_t outsz) {
    char p[1100];
    snprintf(p, sizeof p, "%s/sampleboot.txt", dir);
    read_line(p, out, outsz);
}

static int stock_custom;   /* the stock entry was renamed with sampleboot/sampleboot.txt */

/* The script the firmware would run for this OS folder: the first
 * "scripts" path in its autorun.json that names a file in the folder (MPC
 * Simple's is launcher.sh), else autorun.sh (Melodee, SampQOL OS). Writes the
 * name, relative to dir, into out; returns 0 when found. */
static int entry_script(const char *dir, char *out, size_t outsz) {
    char p[1200];
    snprintf(p, sizeof p, "%s/autorun.json", dir);
    FILE *f = fopen(p, "rb");
    if (f) {
        static char js[65536];
        size_t n = fread(js, 1, sizeof js - 1, f);
        fclose(f);
        js[n] = 0;
        for (char *q = js; (q = strstr(q, "\"path\"")); ) {
            q += 6;
            while (isspace((unsigned char)*q)) q++;
            if (*q != ':') continue;
            q++;
            while (isspace((unsigned char)*q)) q++;
            if (*q != '"') continue;
            char *e = strchr(++q, '"');
            if (!e) break;
            size_t len = (size_t)(e - q);
            if (len && len < outsz && *q != '/') {
                char name[512];
                snprintf(name, sizeof name, "%.*s", (int)len, q);
                if (!strstr(name, "..")) {
                    snprintf(p, sizeof p, "%s/%s", dir, name);
                    if (is_file(p, NULL)) { snprintf(out, outsz, "%s", name); return 0; }
                }
            }
            q = e + 1;
        }
    }
    snprintf(p, sizeof p, "%s/autorun.sh", dir);
    if (is_file(p, NULL)) { snprintf(out, outsz, "autorun.sh"); return 0; }
    return -1;
}

static int cmp_entry(const void *a, const void *b) {
    return strcasecmp(((const entry_t *)a)->label, ((const entry_t *)b)->label);
}

static void scan_entries(void) {
    char path[1024];
    entry_t *s = &entries[n_entries++];
    snprintf(s->id, sizeof s->id, "stock");
    snprintf(path, sizeof path, "%s/sampleboot", opt_autorun);
    read_label(path, s->label, sizeof s->label);
    stock_custom = s->label[0] != 0;
    if (!stock_custom) snprintf(s->label, sizeof s->label, "AKAI original firmware");
    find_image(path, "sampleboot", 0, s->image, sizeof s->image);           /* sampleboot/sampleboot.png ... */

    snprintf(path, sizeof path, "%s/mods", opt_autorun);
    DIR *d = opendir(path);
    if (!d) { logf_("no mods folder: %s", path); return; }
    struct dirent *e;
    int first = n_entries;
    while ((e = readdir(d)) && n_entries < MAX_ENTRIES) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        char dir[1024], script[512];
        snprintf(dir, sizeof dir, "%s/%s", path, e->d_name);
        if (entry_script(dir, script, sizeof script)) continue;
        entry_t *m = &entries[n_entries++];
        snprintf(m->id, sizeof m->id, "%s", e->d_name);
        read_label(dir, m->label, sizeof m->label);
        if (!m->label[0]) snprintf(m->label, sizeof m->label, "%s", e->d_name);
        find_image(dir, "sampleboot", 1, m->image, sizeof m->image);
        snprintf(m->script, sizeof m->script, "%s", script);
    }
    closedir(d);
    qsort(&entries[first], n_entries - first, sizeof(entry_t), cmp_entry);
    for (int i = 0; i < n_entries; i++)
        logf_("entry %d: %s [%s] script: %s, image: %s", i + 1, entries[i].label, entries[i].id,
              entries[i].script[0] ? entries[i].script : "-",
              entries[i].image[0] ? entries[i].image : "(generated)");
}

/* Decodes and fits an image into W x H ("contain", dark letterbox). */
static uint32_t *load_image(const char *path) {
    const char *dot = strrchr(path, '.');
    uint32_t *out = malloc(sizeof(uint32_t) * W * H);
    if (!out) return NULL;
    if (dot && strcasecmp(dot + 1, "raw") == 0) {
        FILE *f = fopen(path, "rb");
        size_t n = f ? fread(out, 1, FRAME_BYTES, f) : 0;
        if (f) fclose(f);
        if (n != FRAME_BYTES) { free(out); return NULL; }
        for (int i = 0; i < W * H; i++) out[i] &= 0xffffff;   /* BGRX in memory = 0x00RRGGBB */
        return out;
    }
    FILE *f = fopen(path, "rb");
    if (!f) { free(out); return NULL; }
    int w, h, n;
    unsigned char *img = stbi_load_from_file(f, &w, &h, &n, 3);
    fclose(f);
    if (!img) { logf_("cannot decode %s: %s", path, stbi_failure_reason()); free(out); return NULL; }
    double sc = (double)W / w < (double)H / h ? (double)W / w : (double)H / h;
    int dw = (int)(w * sc + 0.5), dh = (int)(h * sc + 0.5);
    int ox = (W - dw) / 2, oy = (H - dh) / 2;
    for (int i = 0; i < W * H; i++) out[i] = rgb(10, 10, 12);
    for (int y = 0; y < dh; y++) {
        double sy = (y + 0.5) / sc - 0.5;
        int y0 = sy < 0 ? 0 : (int)sy, y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        double fy = sy - y0; if (fy < 0) fy = 0;
        for (int x = 0; x < dw; x++) {
            double sx = (x + 0.5) / sc - 0.5;
            int x0 = sx < 0 ? 0 : (int)sx, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            double fx = sx - x0; if (fx < 0) fx = 0;
            int c[3];
            for (int k = 0; k < 3; k++) {
                double a = img[(y0 * w + x0) * 3 + k] * (1 - fx) + img[(y0 * w + x1) * 3 + k] * fx;
                double b = img[(y1 * w + x0) * 3 + k] * (1 - fx) + img[(y1 * w + x1) * 3 + k] * fx;
                c[k] = (int)(a * (1 - fy) + b * fy + 0.5);
            }
            out[(oy + y) * W + ox + x] = rgb(c[0], c[1], c[2]);
        }
    }
    stbi_image_free(img);
    return out;
}

/* ------------------------------------------------------------------ firmware */
typedef struct { uint32_t s[8]; uint64_t len; uint8_t buf[64]; size_t n; } sha_t;
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
static void sha_block(sha_t *c, const uint8_t *p) {
    uint32_t w[64], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i*4] << 24 | p[i*4+1] << 16 | p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->s[0]; b = c->s[1]; cc = c->s[2]; d = c->s[3]; e = c->s[4]; f = c->s[5]; g = c->s[6]; h = c->s[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->s[0] += a; c->s[1] += b; c->s[2] += cc; c->s[3] += d; c->s[4] += e; c->s[5] += f; c->s[6] += g; c->s[7] += h;
}
static void sha_init(sha_t *c) {
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->s, iv, sizeof iv); c->len = 0; c->n = 0;
}
static void sha_update(sha_t *c, const uint8_t *p, size_t n) {
    c->len += n;
    while (n) {
        size_t k = 64 - c->n < n ? 64 - c->n : n;
        memcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
        if (c->n == 64) { sha_block(c, c->buf); c->n = 0; }
    }
}
static void sha_final(sha_t *c, char hex[65]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0, l[8];
    sha_update(c, &pad, 1);
    while (c->n != 56) sha_update(c, &z, 1);
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(c, l, 8);
    for (int i = 0; i < 8; i++) sprintf(hex + i * 8, "%08x", c->s[i]);
}

static char fw_version[64];
static volatile int fw_done;

static void *fw_thread(void *arg) {
    (void)arg;
    char hex[65] = "", path[1024], line[256];
    FILE *f = fopen(opt_stock_bin, "rb");
    if (f) {
        sha_t c; sha_init(&c);
        static uint8_t buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha_update(&c, buf, n);
        fclose(f);
        sha_final(&c, hex);
        snprintf(path, sizeof path, "%s/sampleboot/firmware.txt", opt_autorun);
        FILE *t = fopen(path, "r");
        while (t && fgets(line, sizeof line, t)) {
            char h[65], v[64];
            if (sscanf(line, "%64s %63s", h, v) == 2 && strcasecmp(h, hex) == 0) { snprintf(fw_version, sizeof fw_version, "%s", v); break; }
        }
        if (t) fclose(t);
    }
    logf_("stock binary sha256 %s, firmware %s", hex[0] ? hex : "(unreadable)", fw_version[0] ? fw_version : "unknown");
    fw_done = 1;
    return NULL;
}

/* ------------------------------------------------------------------ screens */

static void stock_label(char *out, size_t n) {
    if (stock_custom) snprintf(out, n, "%s", entries[0].label);
    else if (fw_version[0]) snprintf(out, n, "AKAI firmware %s", fw_version);
    else snprintf(out, n, "AKAI original firmware");
}

static const char *entry_label(int i) {
    static char buf[160];
    if (strcmp(entries[i].id, "stock") == 0) { stock_label(buf, sizeof buf); return buf; }
    return entries[i].label;
}

/* Generated title screen: the name in the middle of a plain template. Long
 * names are split over two lines at the space nearest the middle. */
/* ---- theme: every colour of every screen, from sampleboot.conf ----
 * Colours as "#RRGGBB", opacities in percent (0 = invisible, 100 = solid). */
static struct {
    uint32_t top_bg, top_line, top_title, top_countdown;
    uint32_t bot_bg, bot_line, bot_name, bot_info, bot_arrows;
    uint32_t tmpl_bg, tmpl_text, tmpl_sub;
    uint32_t pop_shade, pop_bg, pop_border, pop_title, pop_text, pop_info;
    uint32_t off_bg, off_text;
    int top_opacity, bot_opacity, pop_shade_opacity;
} th = {
    0x000000, 0x8a8a94, 0xffffff, 0x8a8a94,
    0x000000, 0x8a8a94, 0xffffff, 0x8a8a94, 0xe1e1e4,
    0x000000, 0xff0000, 0xaa0000,
    0x000000, 0x18181c, 0xd62828, 0xffffff, 0xe1e1e4, 0x8a8a94,
    0x000000, 0xffffff,
    73, 73, 78,
};

static const struct { const char *key; uint32_t *colour; int *percent; } theme_keys[] = {
    {"TOP_BAR_BG", &th.top_bg, NULL},          {"TOP_BAR_OPACITY", NULL, &th.top_opacity},
    {"TOP_BAR_LINE", &th.top_line, NULL},      {"TOP_BAR_TITLE", &th.top_title, NULL},
    {"TOP_BAR_COUNTDOWN", &th.top_countdown, NULL},
    {"BOTTOM_BAR_BG", &th.bot_bg, NULL},       {"BOTTOM_BAR_OPACITY", NULL, &th.bot_opacity},
    {"BOTTOM_BAR_LINE", &th.bot_line, NULL},   {"BOTTOM_BAR_NAME", &th.bot_name, NULL},
    {"BOTTOM_BAR_INFO", &th.bot_info, NULL},   {"BOTTOM_BAR_ARROWS", &th.bot_arrows, NULL},
    {"TEMPLATE_BG", &th.tmpl_bg, NULL},        {"TEMPLATE_TEXT", &th.tmpl_text, NULL},
    {"TEMPLATE_SUBTEXT", &th.tmpl_sub, NULL},
    {"POPUP_SHADE", &th.pop_shade, NULL},      {"POPUP_SHADE_OPACITY", NULL, &th.pop_shade_opacity},
    {"POPUP_BG", &th.pop_bg, NULL},            {"POPUP_BORDER", &th.pop_border, NULL},
    {"POPUP_TITLE", &th.pop_title, NULL},      {"POPUP_TEXT", &th.pop_text, NULL},
    {"POPUP_INFO", &th.pop_info, NULL},
    {"SWITCHOFF_BG", &th.off_bg, NULL},        {"SWITCHOFF_TEXT", &th.off_text, NULL},
};

static int pct(int p) { return (p < 0 ? 0 : p > 100 ? 100 : p) * 255 / 100; }

static int parse_colour(const char *s, uint32_t *out) {
    if (*s == '#') s++;
    if (strlen(s) != 6) return -1;
    for (int i = 0; i < 6; i++) if (!isxdigit((unsigned char)s[i])) return -1;
    *out = (uint32_t)strtoul(s, NULL, 16);
    return 0;
}

/* Reads the theme keys from sampleboot.conf (shell syntax: KEY="value" # comment).
 * Unknown keys belong to the start-up script; bad values keep the default. */
static void read_theme(void) {
    char path[1100], line[512];
    snprintf(path, sizeof path, "%s/sampleboot/sampleboot.conf", opt_autorun);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        char key[64]; int k = 0;
        while ((isupper((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_') && k < 63) key[k++] = *p++;
        key[k] = 0;
        if (!k || *p != '=') continue;
        p++;
        char val[128]; int n = 0;
        if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q && n < 127) val[n++] = *p++; }
        else while (*p && !isspace((unsigned char)*p) && n < 127) val[n++] = *p++;   /* as the shell: # inside a word is no comment */
        val[n] = 0;
        for (unsigned i = 0; i < sizeof theme_keys / sizeof theme_keys[0]; i++) {
            if (strcmp(key, theme_keys[i].key)) continue;
            if (theme_keys[i].colour && parse_colour(val, theme_keys[i].colour))
                logf_("sampleboot.conf: %s=\"%s\" is not a #RRGGBB colour, default kept", key, val);
            if (theme_keys[i].percent) {
                char *end; long v = strtol(val, &end, 10);
                if (*val && !*end && v >= 0 && v <= 100) *theme_keys[i].percent = (int)v;
                else logf_("sampleboot.conf: %s=%s is not 0-100, default kept", key, val);
            }
        }
    }
    fclose(f);
}

static void draw_template(int i) {
    for (int k = 0; k < W * H; k++) fb[k] = th.tmpl_bg;
    const uint32_t FG = th.tmpl_text, DIM = th.tmpl_sub;
    const char *name = entries[i].label;
    char sub[160] = "";
    if (strcmp(entries[i].id, "stock") == 0 && !stock_custom) {
        name = "AKAI";
        snprintf(sub, sizeof sub, "%s", fw_version[0] ? "" : "Original firmware");
        if (fw_version[0]) snprintf(sub, sizeof sub, "Original firmware %s", fw_version);
    }
    int cy = H / 2 - 6;
    const font_t *f = &FONT_BIG;
    if (text_w(f, name) <= W - 32) {
        text_center(f, W / 2, cy + 10, name, W - 32, FG);
    } else {
        /* two lines */
        char a[160], b[160];
        snprintf(a, sizeof a, "%s", name);
        int len = (int)strlen(a), best = -1;
        for (int k = 0; k < len; k++)
            if (a[k] == ' ' && (best < 0 || abs(k - len / 2) < abs(best - len / 2))) best = k;
        if (best > 0) {
            a[best] = 0; snprintf(b, sizeof b, "%s", a + best + 1);
            f = (text_w(&FONT_BIG, a) <= W - 32 && text_w(&FONT_BIG, b) <= W - 32) ? &FONT_BIG : &FONT_NAME;
            text_center(f, W / 2, cy - 4, a, W - 32, FG);
            text_center(f, W / 2, cy - 4 + f->height, b, W - 32, FG);
            cy += f->height / 2;
        } else text_center(&FONT_NAME, W / 2, cy + 6, name, W - 32, FG);
    }
    if (sub[0]) text_center(&FONT_SMALL, W / 2, cy + 44, sub, W - 32, DIM);
}

static void draw_entry_bg(int i) {
    entry_t *e = &entries[i];
    if (!e->loaded) {
        e->loaded = 1;
        if (e->image[0] && !(e->pix = load_image(e->image))) logf_("image not usable: %s", e->image);
    }
    if (e->pix) memcpy(fb, e->pix, sizeof fb);
    else draw_template(i);
}

static void draw_menu(int sel, int countdown) {
    char buf[160];
    draw_entry_bg(sel);
    /* top bar: same height as the bottom one, line at its lower edge */
    fill(0, 0, W, BAR, th.top_bg, pct(th.top_opacity));
    fill(0, BAR - 2, W, 2, th.top_line, 255);
    text(&FONT_NAME, 14, 28, "SampleBoot", th.top_title);
    if (countdown >= 0) {
        snprintf(buf, sizeof buf, "Auto start in %d s", countdown);
        text(&FONT_LABEL, W - 14 - text_w(&FONT_LABEL, buf), 28, buf, th.top_countdown);
    }
    /* bottom bar */
    fill(0, H - BAR, W, BAR, th.bot_bg, pct(th.bot_opacity));
    fill(0, H - BAR, W, 2, th.bot_line, 255);
    text_center(&FONT_NAME, W / 2, H - 25, entry_label(sel), W - 64, th.bot_name);
    snprintf(buf, sizeof buf, "%d / %d   \xc2\xb7   turn to browse   \xc2\xb7   push to start", sel + 1, n_entries);
    text_center(&FONT_SMALL, W / 2, H - 8, buf, W - 16, th.bot_info);
    if (n_entries > 1) {
        chevron(14.5, H - 29.5, ARROW_H, ARROW_SW, -1, th.bot_arrows);
        chevron(W - 13.5, H - 29.5, ARROW_H, ARROW_SW, +1, th.bot_arrows);
    }
}

/* Switch-off question, over the dimmed menu. */
static void draw_confirm(int sel, int left) {
    char buf[96];
    draw_menu(sel, -1);
    fill(0, 0, W, H, th.pop_shade, pct(th.pop_shade_opacity));
    int x0 = 24, y0 = 52, w = W - 48, h = 136;
    fill(x0, y0, w, h, th.pop_border, 255);               /* 2 px border */
    fill(x0 + 2, y0 + 2, w - 4, h - 4, th.pop_bg, 255);
    text_center(&FONT_NAME, W / 2, y0 + 34, "Switch off the MPC?", w - 16, th.pop_title);
    text_center(&FONT_SMALL, W / 2, y0 + 66, "Push ENCODER or press POWER: switch off", w - 12, th.pop_text);
    text_center(&FONT_SMALL, W / 2, y0 + 84, "Turn ENCODER: back to the menu", w - 12, th.pop_text);
    snprintf(buf, sizeof buf, "Back to the menu in %d s", left);
    text_center(&FONT_SMALL, W / 2, y0 + 116, buf, w - 12, th.pop_info);
}

static void draw_message(const char *msg) {
    for (int i = 0; i < W * H; i++) fb[i] = th.off_bg;
    text_center(&FONT_NAME, W / 2, H / 2 + 6, msg, W - 24, th.off_text);
}

/* The chosen entry's image, uncovered, with "Starting <name>" in the bottom bar. */
static void draw_starting(int sel) {
    char buf[200];
    draw_entry_bg(sel);
    fill(0, H - BAR, W, BAR, th.bot_bg, pct(th.bot_opacity));
    fill(0, H - BAR, W, 2, th.bot_line, 255);
    snprintf(buf, sizeof buf, "Starting %s", entry_label(sel));
    text_center(&FONT_NAME, W / 2, H - 17, buf, W - 24, th.bot_name);
}

static int fb_fd = -1;
static void present(void) {
    static uint8_t out[FRAME_BYTES];
    for (int i = 0; i < W * H; i++) {
        uint32_t p = fb[i];
        out[i * 4] = p & 255; out[i * 4 + 1] = p >> 8 & 255; out[i * 4 + 2] = p >> 16 & 255; out[i * 4 + 3] = 0;
    }
    if (fb_fd < 0) return;
    if (lseek(fb_fd, 0, SEEK_SET) < 0 && errno != ESPIPE) return;
    size_t done = 0;
    while (done < FRAME_BYTES) {
        ssize_t n = write(fb_fd, out + done, FRAME_BYTES - done);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
        done += (size_t)n;
    }
}

/* ------------------------------------------------------------------ splash */
/* One frame of the splash: the first `rows` lines of the image, the rest in
 * its background colour, and a 2-line "beam" at the edge, as an old CRT
 * drawing the picture. */
static void splash_frame(const uint32_t *img, int rows) {
    uint32_t bg = img[0];
    int lum = ((bg >> 16 & 255) * 3 + (bg >> 8 & 255) * 6 + (bg & 255)) / 10;
    uint32_t beam = lum > 128 ? 0x000000 : 0xffffff;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) fb[y * W + x] = y < rows ? img[y * W + x] : bg;
    for (int y = rows; y < rows + 2 && y < H; y++)
        for (int x = 0; x < W; x++) { fb[y * W + x] = img[y * W + x]; blend_px(x, y, beam, 110); }
}

static int midi_feed(uint8_t b, int *go);
/* Shows the splash for opt_splash_time seconds. Turning or pushing the
 * encoder, or pressing POWER, skips it (without browsing or starting). */
static void splash(int midi, int pwr) {
    if (!strcmp(opt_splash, "off") || !opt_splash[0] || strchr(opt_splash, '/') || opt_splash_time <= 0) return;
    char path[1100];
    snprintf(path, sizeof path, "%s/sampleboot/splash-%s.png", opt_autorun, opt_splash);
    if (!is_file(path, NULL)) { logf_("splash: %s not found", path); return; }
    uint32_t *img = load_image(path);
    if (!img) return;
    double t0 = now_s(), total = opt_splash_time > SPLASH_REVEAL ? opt_splash_time : SPLASH_REVEAL;
    int frames = 0, skipped = 0, shown = -1;
    for (;;) {
        double t = now_s() - t0;
        int rows = t >= SPLASH_REVEAL ? H : (int)(t / SPLASH_REVEAL * H);
        if (rows != shown) { splash_frame(img, rows); present(); frames++; shown = rows; }
        if (t >= total) break;
        int wait = rows < H ? 15 : (int)((total - t) * 1000) + 1;
        struct pollfd p[2]; int np = 0;
        if (midi >= 0) { p[np].fd = midi; p[np].events = POLLIN; np++; }
        if (pwr >= 0) { p[np].fd = pwr; p[np].events = POLLIN; np++; }
        if (poll(p, (nfds_t)np, wait) <= 0) continue;
        for (int k = 0; k < np; k++) {
            if (!(p[k].revents & POLLIN)) continue;
            if (p[k].fd == midi) {
                uint8_t buf[256]; int push = 0, steps = 0;
                ssize_t n = read(midi, buf, sizeof buf);
                for (ssize_t j = 0; j < n; j++) steps += midi_feed(buf[j], &push);
                if (steps || push) skipped = 1;
            } else {
                struct input_event ev[16];
                ssize_t n = read(pwr, ev, sizeof ev);
                for (ssize_t j = 0; j < n / (ssize_t)sizeof ev[0]; j++)
                    if (ev[j].type == EV_KEY && ev[j].code == KEY_POWER && ev[j].value == 1) skipped = 1;
            }
        }
        if (skipped) break;
    }
    free(img);
    logf_("splash %s: %d frames, %.2f s%s", opt_splash, frames, now_s() - t0, skipped ? ", skipped" : "");
}

/* ------------------------------------------------------------------ input */
/* Card number of the ALSA card "Surface" (the MPC's control surface), or -1. */
static int surface_card(void) {
    FILE *f = fopen(opt_cards, "r");
    char line[256];
    int card = -1;
    while (f && fgets(line, sizeof line, f)) {
        int n; char id[64];
        if (sscanf(line, " %d [%63[^]]", &n, id) == 2) {
            char *e = id + strlen(id); while (e > id && e[-1] == ' ') *--e = 0;
            if (strcmp(id, "Surface") == 0 || strstr(line, "Control Surface")) { card = n; break; }
        }
    }
    if (f) fclose(f);
    return card;
}

static void surface_lights_off(int fd);
static int midi_write_all(int fd, const uint8_t *buf, size_t n);

static int open_surface_midi(void) {
    int card = surface_card();
    if (card < 0) { logf_("surface MIDI card not found"); return -1; }
    char path[512];
    for (int dev = 0; dev < 8; dev++) {
        snprintf(path, sizeof path, "%s/midiC%dD%d", opt_snd, card, dev);
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            /* position request (inMusic SysEx 0x42, product 0x5C): the surface answers
             * with CC 19 (Main Volume), 16-18, 20 and the wheel at their positions */
            static const uint8_t req[] = {0xf0, 0x47, 0x7f, 0x5c, 0x42, 0x00, 0x00, 0xf7};
            logf_("encoder: %s", path);
            surface_lights_off(fd);
            if (midi_write_all(fd, req, sizeof req)) logf_("encoder: position request not sent");
            return fd;
        }
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) { logf_("encoder: %s (read only, Main Volume unknown until moved)", path); return fd; }
    }
    logf_("cannot open the surface MIDI port of card %d: %s", card, strerror(errno));
    return -1;
}

static int open_power_button(void) {
    DIR *d = opendir(opt_input_class);
    struct dirent *e;
    int fd = -1;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[512], name[128];
        snprintf(path, sizeof path, "%s/%s/device/name", opt_input_class, e->d_name);
        read_line(path, name, sizeof name);
        if (strcmp(name, "rk805 pwrkey") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", opt_input_dir, e->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) logf_("power button: %s", path);
        break;
    }
    if (d) closedir(d);
    return fd;
}

/* Writes all of buf to the (non-blocking) MIDI port, waiting up to 1 s for room. */
static int midi_write_all(int fd, const uint8_t *buf, size_t n) {
    size_t done = 0;
    double until = now_s() + 1.0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w > 0) { done += (size_t)w; continue; }
        if (w < 0 && errno != EAGAIN && errno != EINTR) return -1;
        if (now_s() > until) return -1;
        struct pollfd p = {fd, POLLOUT, 0};
        poll(&p, 1, 50);
    }
    return 0;
}

/* Every light of the surface off, as sample-base's "led off": the app that
 * ran before (the stock app or an OS, with a project open) leaves its pad and
 * button lights on, and they would stay lit under the menu. The next app sets
 * its own. Messages from sample-base's measurements (docs/HARDWARE.md, Lights). */
static void surface_lights_off(int fd) {
    static const uint8_t buttons[] = {6, 7, 1, 0, 35, 9, 11, 75, 43, 74, 40, 36, 53, 73, 80, 81, 82};
    uint8_t buf[512];
    size_t n = 0;
    for (int i = 0; i <= 16; i++) {                      /* pads 1-16 and the fader light: black */
        const uint8_t m[] = {0xf0, 0x47, 0x7f, 0x5c, 0x65, 0x00, 0x04, (uint8_t)i, 0, 0, 0, 0xf7};
        memcpy(buf + n, m, sizeof m); n += sizeof m;
    }
    for (int cc = 57; cc <= 64; cc++) { buf[n++] = 0xb0; buf[n++] = (uint8_t)cc; buf[n++] = 0; }   /* pad banks A-H */
    for (int cc = 83; cc <= 84; cc++) { buf[n++] = 0xb0; buf[n++] = (uint8_t)cc; buf[n++] = 0; }   /* level meters */
    for (size_t i = 0; i < sizeof buttons; i++) { buf[n++] = 0xb0; buf[n++] = buttons[i]; buf[n++] = 0; }
    if (midi_write_all(fd, buf, n) == 0) logf_("surface lights off");
    else logf_("surface lights: could not send (%s)", strerror(errno));
}

enum { EV_NONE, EV_NEXT, EV_PREV, EV_GO };

/* MIDI parser state */
static int m_status, m_need, m_n, m_sysex;
static uint8_t m_d[2];

/* Returns the number of steps (+ next, - previous) and sets *go. */
static int midi_feed(uint8_t b, int *go) {
    if (b >= 0xf8) return 0;                          /* realtime */
    if (b & 0x80) {
        if (b == 0xf0) { m_sysex = 1; m_status = 0; return 0; }
        m_sysex = 0;
        if (b < 0xf0) { m_status = b; m_need = ((b & 0xe0) == 0xc0) ? 1 : 2; m_n = 0; }
        else m_status = 0;
        return 0;
    }
    if (m_sysex || !m_status) return 0;
    m_d[m_n++] = b;
    if (m_n < m_need) return 0;
    m_n = 0;                                          /* running status kept */
    if ((m_status & 0xf0) == 0xb0 && m_d[0] == 19) {  /* Main Volume knob: CC 19, absolute */
        if (main_volume != m_d[1]) { main_volume = m_d[1]; }
        return 0;
    }
    if (m_status == 0xb0 && m_d[0] == 100) {          /* wheel: CC 100 ch 1 */
        if (m_d[1] > 0 && m_d[1] < 64) return m_d[1];
        if (m_d[1] >= 64) return m_d[1] - 128;
    } else if (m_status == 0x90 && m_d[0] == 111 && m_d[1] > 0) *go = 1;   /* wheel push */
    return 0;
}

/* Exit 0 if another process has the surface MIDI port open (any OS running
 * on the MPC reads the surface, whatever its process is called), 1 if not,
 * 2 if the port cannot be found. Compares device numbers, not paths. */
static int surface_busy(void) {
    int card = surface_card();
    if (card < 0) return 2;
    dev_t devs[8]; int nd = 0;
    char path[512];
    for (int dev = 0; dev < 8; dev++) {
        struct stat st;
        snprintf(path, sizeof path, "%s/midiC%dD%d", opt_snd, card, dev);
        if (stat(path, &st) == 0) devs[nd++] = st.st_rdev;
    }
    if (!nd) return 2;
    pid_t self = getpid();
    DIR *proc = opendir("/proc");
    struct dirent *p;
    while (proc && (p = readdir(proc))) {
        if (!isdigit((unsigned char)p->d_name[0]) || atoi(p->d_name) == self) continue;
        char fdd[300];
        snprintf(fdd, sizeof fdd, "/proc/%s/fd", p->d_name);
        DIR *fds = opendir(fdd);
        struct dirent *f;
        while (fds && (f = readdir(fds))) {
            if (f->d_name[0] == '.') continue;
            char fp[600]; struct stat st;
            snprintf(fp, sizeof fp, "%s/%s", fdd, f->d_name);
            if (stat(fp, &st) != 0 || !S_ISCHR(st.st_mode)) continue;
            for (int k = 0; k < nd; k++)
                if (st.st_rdev == devs[k]) {
                    char comm[64] = "", cp[300];
                    snprintf(cp, sizeof cp, "/proc/%s/comm", p->d_name);
                    read_line(cp, comm, sizeof comm);
                    printf("%s %s\n", p->d_name, comm);
                    closedir(fds); closedir(proc);
                    return 0;
                }
        }
        if (fds) closedir(fds);
    }
    if (proc) closedir(proc);
    return 1;
}

/* ------------------------------------------------------------------ audio */
/* Countdown beeps through the I2S card (AK4558: headphones, line out,
 * speaker), with plain ALSA kernel ioctls, no alsa-lib.
 *
 * The codec clicks when it powers up or changes rate. When the menu starts,
 * the stock app has just closed its 44.1 kHz stream and the kernel keeps the
 * codec powered for 5 s (pmdown_time). So the stream is opened at once, at
 * the rate the codec already runs at, and kept open with silence until the
 * menu ends: no power-up, no rate change, no click. If opening fails, the
 * menu simply stays silent. */
#define PCM_PERIOD 512
static int pcm_fd = -1;
static unsigned pcm_rate = 44100;
static pthread_mutex_t beep_mx = PTHREAD_MUTEX_INITIALIZER;
static double beep_freq, beep_amp;
static int beep_len, beep_pos = -1;
static volatile int audio_stop;
static volatile unsigned long audio_frames, audio_errors;
static volatile int audio_last_errno;
static int audio_card_no = -1;

static void hw_mask(struct snd_pcm_hw_params *p, int n, unsigned bit) {
    struct snd_mask *m = &p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m->bits, 0, sizeof m->bits);
    m->bits[bit >> 5] |= 1u << (bit & 31);
}
static void hw_int(struct snd_pcm_hw_params *p, int n, unsigned v) {
    struct snd_interval *i = &p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    i->min = i->max = v; i->integer = 1;
}

static int find_card(const char *want) {
    FILE *f = fopen(opt_cards, "r");
    char line[256]; int card = -1;
    while (f && fgets(line, sizeof line, f)) {
        int n; char id[64];
        if (sscanf(line, " %d [%63[^]]", &n, id) == 2) {
            char *e = id + strlen(id); while (e > id && e[-1] == ' ') *--e = 0;
            if (strcmp(id, want) == 0) { card = n; break; }
        }
    }
    if (f) fclose(f);
    return card;
}

static int audio_open(void) {
    int card = audio_card_no;
    char path[512];
    snprintf(path, sizeof path, "%s/pcmC%dD0p", opt_snd, card);
    int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { logf_("audio: cannot open %s: %s, no beeps", path, strerror(errno)); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    struct snd_pcm_hw_params hp;
    memset(&hp, 0, sizeof hp);
    for (int n = 0; n <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; n++)
        memset(hp.masks[n].bits, 0xff, sizeof hp.masks[n].bits);
    for (int n = 0; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n++) {
        hp.intervals[n].min = 0; hp.intervals[n].max = ~0u;
    }
    hp.rmask = ~0u; hp.cmask = 0; hp.info = ~0u;
    hw_mask(&hp, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hw_mask(&hp, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
    hw_mask(&hp, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 32);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_FRAME_BITS, 64);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_RATE, pcm_rate);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PCM_PERIOD);
    hw_int(&hp, SNDRV_PCM_HW_PARAM_PERIODS, 4);
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp) < 0) {
        logf_("audio: hw params refused (%s), no beeps", strerror(errno)); close(fd); return -1;
    }
    unsigned long buffer = PCM_PERIOD * 4;
    struct snd_pcm_sw_params sp;
    memset(&sp, 0, sizeof sp);
    sp.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sp.period_step = 1;
    sp.avail_min = PCM_PERIOD;
    sp.start_threshold = PCM_PERIOD * 2;
    sp.stop_threshold = buffer;
    sp.boundary = buffer;
    while (sp.boundary * 2 <= (unsigned long)(~0ul >> 1) - buffer) sp.boundary *= 2;
    if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sp) < 0 || ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
        logf_("audio: sw params/prepare refused (%s), no beeps", strerror(errno)); close(fd); return -1;
    }
    logf_("audio: %s, %u Hz, beep volume %d", path, pcm_rate, opt_beep);
    return fd;
}

static void *audio_thread(void *arg) {
    (void)arg;
    static int32_t buf[PCM_PERIOD * 2];
    while (!audio_stop) {
        pthread_mutex_lock(&beep_mx);
        for (int i = 0; i < PCM_PERIOD; i++) {
            double v = 0;
            if (beep_pos >= 0 && beep_pos < beep_len) {
                int fade = (int)(pcm_rate / 200);                     /* 5 ms in and out: no click */
                double env = 1.0;
                if (beep_pos < fade) env = (double)beep_pos / fade;
                if (beep_len - beep_pos < fade) env = (double)(beep_len - beep_pos) / fade;
                v = beep_amp * env * sin(2 * M_PI * beep_freq * beep_pos / pcm_rate);
                beep_pos++;
            }
            buf[2 * i] = buf[2 * i + 1] = (int32_t)(v * 2147483647.0);
        }
        pthread_mutex_unlock(&beep_mx);
        struct snd_xferi x = {0, buf, PCM_PERIOD};
        while (!audio_stop && ioctl(pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
            audio_errors++; audio_last_errno = errno;
            if (errno == EPIPE || errno == ESTRPIPE) { ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE); continue; }
            if (errno == EINTR || errno == EAGAIN) continue;
            if (!audio_stop) logf_("audio: write failed: %s, stream stopped", strerror(errno));
            audio_stop = 1;
        }
        if (!audio_stop) audio_frames += PCM_PERIOD;
    }
    return NULL;
}

/* ---- mixer: the built-in speaker ----
 * The stock app switches 'Builtin Speaker Switch' on while it runs and off
 * when it closes, so the menu would beep into nothing without headphones or
 * line out. With no jack plugged, the menu switches the speaker on (the stream
 * is already running, so the amplifier does not start on a powering codec)
 * and puts the switch back before the next app starts. */
static int ctl_fd = -1, spk_numid = -1, spk_orig = -1;

static int ctl_find(const char *name, int *value) {     /* numid, or -1 */
    struct snd_ctl_elem_list l;
    memset(&l, 0, sizeof l);
    if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) < 0 || !l.count) return -1;
    struct snd_ctl_elem_id *ids = calloc(l.count, sizeof *ids);
    if (!ids) return -1;
    l.space = l.count; l.pids = ids;
    int numid = -1;
    if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) == 0)
        for (unsigned i = 0; i < l.used; i++)
            if (strcmp((char *)ids[i].name, name) == 0) {
                struct snd_ctl_elem_value v;
                memset(&v, 0, sizeof v);
                v.id = ids[i];
                if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) == 0) { numid = (int)ids[i].numid; *value = (int)v.value.integer.value[0]; }
                break;
            }
    free(ids);
    return numid;
}

static int ctl_set(int numid, int value) {
    struct snd_ctl_elem_value v;
    memset(&v, 0, sizeof v);
    v.id.numid = (unsigned)numid;
    if (ioctl(ctl_fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) return -1;
    v.value.integer.value[0] = value;
    return ioctl(ctl_fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v);
}

/* ---- codec state (sysfs/debugfs, as documented by sample-base) ----
 * The AK4558's DAPM bias ("PM State" in the link's dapm_widget) tells whether
 * the codec is powered; its master clock (debugfs, 512 fs) tells the rate it
 * is set for. Opening the stream at the rate of a powered codec changes
 * nothing; opening it on a codec that is off powers it up, and the speaker's
 * amplifier would play the outputs settling as a click, so the speaker is
 * held off until they have settled (the jacks are behind the board's mute,
 * which opens 1 s after power-up on its own). */
static const char *opt_sys = "/sys", *opt_clk = "/sys/kernel/debug/clk/i2s1_mclkout_tx/clk_rate";

static void read_small(const char *path, char *out, size_t n) {
    out[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    ssize_t r = read(fd, out, n - 1);
    close(fd);
    out[r > 0 ? r : 0] = 0;
}

/* 1 powered, 0 off, -1 unknown */
static int codec_powered(int card) {
    char dir[512];
    snprintf(dir, sizeof dir, "%s/class/sound/card%d/device", opt_sys, card);
    DIR *d = opendir(dir);
    struct dirent *e;
    int state = -1;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[1024], text[4096];
        snprintf(p, sizeof p, "%s/%s/dapm_widget", dir, e->d_name);
        read_small(p, text, sizeof text);
        char *at = NULL, *q = text;
        while ((q = strstr(q, "PM State: "))) { at = q; q += 10; }
        if (!at) continue;
        at += 10;
        state = (!strncmp(at, "On", 2) || !strncmp(at, "Prepare", 7) || !strncmp(at, "Standby", 7)) ? 1 : 0;
        break;
    }
    if (d) closedir(d);
    return state;
}

/* the standard rate the master clock is set for, 0 if unknown */
static unsigned codec_rate(void) {
    static const unsigned rates[] = {44100, 48000, 88200, 96000, 32000, 22050, 24000, 16000};
    char t[64];
    read_small(opt_clk, t, sizeof t);
    double fs = strtod(t, NULL) / 512;
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++)
        if (fabs(fs - rates[i]) <= rates[i] * 0.0005) return rates[i];
    return 0;
}

/* Speaker policy: on with nothing in the headphone and line-out jacks, off
 * with something plugged (no beeps in the room while wearing headphones).
 * spk_held: off for a codec power-up until spk_until. */
static int spk_held, spk_now = -1;
static double spk_until;

static void speaker_apply(int on, const char *why) {
    if (spk_numid < 0 || spk_now == on) return;
    if (ctl_set(spk_numid, on) == 0) { spk_now = on; logf_("audio: speaker %s (%s)", on ? "on" : "off", why); }
    else logf_("audio: cannot switch the speaker: %s", strerror(errno));
}

static int jacks_plugged(void) {
    int hp = 0, lo = 0;
    ctl_find("Headphones Jack", &hp);
    ctl_find("Line Out Jack", &lo);
    return hp || lo;
}

/* called from the menu loop: end of the power-up hold, plugging and unplugging */
static void speaker_poll(void) {
    if (ctl_fd < 0 || spk_numid < 0) return;
    if (spk_held) {
        if (now_s() < spk_until) return;
        spk_held = 0;
    }
    int want = !jacks_plugged();
    speaker_apply(want, want ? "no headphones or line out" : "headphones or line out plugged");
}

static void speaker_restore(void) {
    if (spk_numid >= 0 && spk_orig >= 0 && spk_now != spk_orig) {
        if (ctl_set(spk_numid, spk_orig) == 0) logf_("audio: speaker back %s", spk_orig ? "on" : "off");
        else logf_("audio: cannot restore the speaker switch: %s", strerror(errno));
    }
    if (ctl_fd >= 0) { close(ctl_fd); ctl_fd = -1; }
}

/* ---- codec volume ramp (no click when the stock app is stopped) ----
 * Stopping the stock app cuts its output, scaled by its Main Volume, and the
 * jacks and speaker click. Before the start-up script stops it, "--volume-down"
 * lowers the codec's 'Playback Volume' to 0 in small steps and keeps the old
 * values in <state>/volume; once the menu's own (silent) stream runs, the
 * values come back the same way. "--volume-restore" puts them back at once,
 * for the script's error paths. */
static int vol_open(int *numid, long cur[2], unsigned *count) {
    int card = find_card("I2S");
    if (card < 0) return -1;
    char path[512];
    snprintf(path, sizeof path, "%s/controlC%d", opt_snd, card);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    struct snd_ctl_elem_list l; memset(&l, 0, sizeof l);
    *numid = -1;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) == 0 && l.count) {
        struct snd_ctl_elem_id *ids = calloc(l.count, sizeof *ids);
        l.space = l.count; l.pids = ids;
        if (ids && ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) == 0)
            for (unsigned i = 0; i < l.used; i++)
                if (strcmp((char *)ids[i].name, "Playback Volume") == 0) { *numid = (int)ids[i].numid; break; }
        free(ids);
    }
    struct snd_ctl_elem_info inf; memset(&inf, 0, sizeof inf);
    inf.id.numid = (unsigned)*numid;
    struct snd_ctl_elem_value v; memset(&v, 0, sizeof v);
    v.id.numid = (unsigned)*numid;
    if (*numid < 0 || ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &inf) < 0 || ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) {
        close(fd); return -1;
    }
    *count = inf.count > 2 ? 2 : inf.count;
    for (unsigned k = 0; k < *count; k++) cur[k] = v.value.integer.value[k];
    return fd;
}

static void vol_write(int fd, int numid, const long val[2], unsigned count) {
    struct snd_ctl_elem_value v; memset(&v, 0, sizeof v);
    v.id.numid = (unsigned)numid;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0) return;
    for (unsigned k = 0; k < count; k++) v.value.integer.value[k] = val[k];
    ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v);
}

/* Moves from 'from' to 'to' in about ms milliseconds. */
static void vol_ramp(int fd, int numid, const long from[2], const long to[2], unsigned count, int ms) {
    int steps = 40;
    for (int i = 1; i <= steps; i++) {
        long v[2];
        for (unsigned k = 0; k < count; k++) v[k] = from[k] + (to[k] - from[k]) * i / steps;
        vol_write(fd, numid, v, count);
        usleep((useconds_t)(ms * 1000 / steps));
    }
}

static int volume_down(void) {
    int numid; long cur[2] = {0, 0}, zero[2] = {0, 0}; unsigned count;
    char path[600];
    snprintf(path, sizeof path, "%s/volume", opt_state);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); logf_("volume: already lowered"); return 0; }   /* keep the first saved values */
    int fd = vol_open(&numid, cur, &count);
    if (fd < 0) { logf_("volume: 'Playback Volume' not found, not lowered"); return 1; }
    f = fopen(path, "w");
    if (!f) { logf_("volume: cannot write %s: %s, not lowered", path, strerror(errno)); close(fd); return 1; }
    fprintf(f, "%u %ld %ld\n", count, cur[0], cur[1]);
    fclose(f);
    vol_ramp(fd, numid, cur, zero, count, 200);
    logf_("volume: codec lowered from %ld,%ld to 0", cur[0], cur[1]);
    close(fd);
    return 0;
}

static int volume_restore(int ms) {
    char path[600];
    snprintf(path, sizeof path, "%s/volume", opt_state);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned c = 0; long want[2] = {0, 0};
    int ok = fscanf(f, "%u %ld %ld", &c, &want[0], &want[1]) == 3;
    fclose(f);
    int numid; long cur[2] = {0, 0}; unsigned count;
    int fd = vol_open(&numid, cur, &count);
    if (fd < 0 || !ok) { if (fd >= 0) close(fd); logf_("volume: cannot restore"); return 1; }
    if (ms > 0) vol_ramp(fd, numid, cur, want, count, ms);
    else vol_write(fd, numid, want, count);
    close(fd);
    unlink(path);
    logf_("volume: codec back to %ld,%ld", want[0], want[1]);
    return 0;
}

static void audio_start(void) {
    if (opt_beep <= 0) return;
    int card = find_card("I2S");
    if (card < 0) { logf_("audio: I2S card not found, no beeps"); return; }
    audio_card_no = card;
    char path[512];
    snprintf(path, sizeof path, "%s/controlC%d", opt_snd, card);
    if ((ctl_fd = open(path, O_RDWR | O_CLOEXEC)) >= 0) {
        int v = 0;
        spk_numid = ctl_find("Builtin Speaker Switch", &v);
        if (spk_numid >= 0) spk_orig = spk_now = v;
    } else logf_("audio: cannot open %s: %s", path, strerror(errno));

    int powered = codec_powered(card);
    unsigned rate = codec_rate();
    if (powered == 1 && rate) {
        pcm_rate = rate;
        logf_("audio: codec on at %u Hz, joining it", rate);
    } else {
        pcm_rate = 44100;
        logf_("audio: codec %s, powering it up at 44100 Hz with the speaker held off",
              powered == 0 ? "off" : powered == 1 ? "on at an unknown rate" : "state unknown");
        if (spk_numid >= 0) { speaker_apply(0, "held while the codec powers up"); spk_held = 1; spk_until = now_s() + 10; }
    }
    if ((pcm_fd = audio_open()) < 0) { spk_held = 0; speaker_restore(); return; }
    pthread_t th;
    if (pthread_create(&th, NULL, audio_thread, NULL) == 0) pthread_detach(th);
    else { close(pcm_fd); pcm_fd = -1; spk_held = 0; speaker_restore(); return; }
    usleep(60000);                                   /* the stream starts after 2 periods */
    if (spk_held) spk_until = now_s() + 1.1;         /* outputs settle, the board mute opens */
    speaker_poll();
    volume_restore(250);    /* the stream plays silence: bring the codec volume back */
}

static void audio_end(void) {      /* before leaving: the next app needs the card */
    if (pcm_fd < 0) return;
    logf_("audio: closing after %lu frames, %lu write errors", audio_frames, audio_errors);
    speaker_restore();      /* amplifier off while the stream still plays silence */
    usleep(20000);
    audio_stop = 1;
    ioctl(pcm_fd, SNDRV_PCM_IOCTL_DROP);
    usleep(20000);
    close(pcm_fd);
    pcm_fd = -1;
}

/* Diagnostics for the log: the stream's state at the first beep. */
static void audio_report(void) {
    logf_("audio: Main Volume knob %d/127", main_volume);
    static const char *st[] = {"OPEN", "SETUP", "PREPARED", "RUNNING", "XRUN", "DRAINING", "PAUSED", "SUSPENDED", "DISCONNECTED"};
    struct snd_pcm_status ps;
    memset(&ps, 0, sizeof ps);
    if (pcm_fd >= 0 && ioctl(pcm_fd, SNDRV_PCM_IOCTL_STATUS, &ps) == 0)
        logf_("audio: stream %s, %lu frames written, %lu write errors (last: %s), hw_ptr %lu",
              (unsigned)ps.state < 9 ? st[ps.state] : "?", audio_frames, audio_errors,
              audio_errors ? strerror(audio_last_errno) : "none", (unsigned long)ps.hw_ptr);
    else
        logf_("audio: %lu frames written, %lu write errors, status unavailable (%s)", audio_frames, audio_errors, strerror(errno));

}

/* Beep for "n seconds left" (5..1): louder and higher as 0 gets nearer.
 * With Main Volume at maximum, BEEP=100 puts the last beep at -10 dBFS and
 * BEEP=50 at -25 dBFS; each earlier one is 3 dB quieter. The knob scales it
 * like the apps do (squared taper): at 0 the beeps are silent. */
static void countdown_beep(int n) {
    if (pcm_fd < 0 || n < 1 || n > 5) return;
    int vol = opt_beep > 100 ? 100 : opt_beep;
    double db = -40.0 + 30.0 * vol / 100.0 - 3.0 * (n - 1);
    /* Main Volume, as the apps apply it: squared taper, 0 = silent */
    double knob = main_volume >= 0 ? main_volume / 127.0 : 0.5;
    double amp = pow(10, db / 20) * knob * knob;
    pthread_mutex_lock(&beep_mx);
    beep_freq = n == 1 ? 1320 : 880;
    beep_len = (int)(pcm_rate * (n == 1 ? 0.16 : 0.08));
    beep_amp = amp;
    beep_pos = 0;
    pthread_mutex_unlock(&beep_mx);
}

/* ------------------------------------------------------------------ main */
static void usage(void) {
    fprintf(stderr, "usage: sampleboot-ui --autorun DIR [--last ID] [--timeout S] [--timeout-browse S] [--beep 0-100]\n"
                    "       [--splash dark|light|off] [--splash-time S] [--fb PATH]\n"
                    "       [--cards PATH] [--snd DIR] [--input-class DIR] [--input-dir DIR] [--stock-bin PATH]\n"
                    "       sampleboot-ui --script DIR   (print the start script of an OS folder: autorun.json, else autorun.sh)\n"
                    "       sampleboot-ui --surface-busy   (exit 0 and print \"pid name\" if a process holds the surface port)\n"
                    "       sampleboot-ui --volume-down | --volume-restore   (codec volume ramp around stopping the stock app)\n"
                    "       sampleboot-ui --render DIR --autorun DIR   (writes every screen as .ppm, for testing)\n");
    exit(1);
}

static void save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint8_t p[3] = {fb[i] >> 16 & 255, fb[i] >> 8 & 255, fb[i] & 255}; fwrite(p, 1, 3, f); }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *render_dir = NULL;
    int busy_mode = 0, vol_mode = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--version")) { puts(VERSION); return 0; }
        if (!strcmp(a, "--surface-busy")) { busy_mode = 1; continue; }
        if (!strcmp(a, "--script")) {               /* print the start script of an OS folder */
            char out[512];
            if (!v || entry_script(v, out, sizeof out)) return 1;
            puts(out);
            return 0;
        }
        if (!strcmp(a, "--volume-down")) { vol_mode = 1; continue; }
        if (!strcmp(a, "--volume-restore")) { vol_mode = 2; continue; }
        if (!v) usage();
        if (!strcmp(a, "--autorun")) opt_autorun = v;
        else if (!strcmp(a, "--last")) opt_last = v;
        else if (!strcmp(a, "--timeout")) opt_timeout = atoi(v);
        else if (!strcmp(a, "--hold-cs")) opt_hold_cs = atoi(v);
        else if (!strcmp(a, "--timeout-browse")) opt_timeout_browse = atoi(v);
        else if (!strcmp(a, "--beep")) opt_beep = atoi(v);
        else if (!strcmp(a, "--splash")) opt_splash = v;
        else if (!strcmp(a, "--splash-time")) opt_splash_time = atof(v);
        else if (!strcmp(a, "--state")) opt_state = v;
        else if (!strcmp(a, "--sys")) opt_sys = v;
        else if (!strcmp(a, "--codec-clock")) opt_clk = v;
        else if (!strcmp(a, "--fb")) opt_fb = v;
        else if (!strcmp(a, "--cards")) opt_cards = v;
        else if (!strcmp(a, "--snd")) opt_snd = v;
        else if (!strcmp(a, "--input-class")) opt_input_class = v;
        else if (!strcmp(a, "--input-dir")) opt_input_dir = v;
        else if (!strcmp(a, "--stock-bin")) opt_stock_bin = v;
        else if (!strcmp(a, "--render")) render_dir = v;
        else usage();
        i++;
    }
    if (busy_mode) return surface_busy();
    if (vol_mode == 1) return volume_down();
    if (vol_mode == 2) return volume_restore(0);
    if (!opt_autorun) usage();
    logf_("SampleBoot %s", VERSION);
    read_theme();
    scan_entries();

    if (render_dir) {               /* test mode: every screen to PPM files */
        fw_thread(NULL);
        char p[1024];
        for (int i = 0; i < n_entries; i++) {
            draw_menu(i, opt_timeout); snprintf(p, sizeof p, "%s/menu_%d.ppm", render_dir, i); save_ppm(p);
            draw_starting(i); snprintf(p, sizeof p, "%s/start_%d.ppm", render_dir, i); save_ppm(p);
        }
        draw_confirm(0, CONFIRM_S); snprintf(p, sizeof p, "%s/confirm.ppm", render_dir); save_ppm(p);
        draw_message("Switching off\xe2\x80\xa6"); snprintf(p, sizeof p, "%s/off.ppm", render_dir); save_ppm(p);
        static const char *styles[] = {"dark", "light"};
        for (int s = 0; s < 2; s++) {
            snprintf(p, sizeof p, "%s/sampleboot/splash-%s.png", opt_autorun, styles[s]);
            uint32_t *img = load_image(p);
            if (!img) continue;
            splash_frame(img, H); snprintf(p, sizeof p, "%s/splash_%s.ppm", render_dir, styles[s]); save_ppm(p);
            splash_frame(img, H / 2); snprintf(p, sizeof p, "%s/splash_%s_half.ppm", render_dir, styles[s]); save_ppm(p);
            free(img);
        }
        return 0;
    }

    if (n_entries == 1) { puts("stock"); return 0; }

    pthread_t th;
    if (pthread_create(&th, NULL, fw_thread, NULL) == 0) pthread_detach(th);
    else fw_done = 1;

    int sel = 0;
    for (int i = 0; i < n_entries; i++) if (!strcmp(entries[i].id, opt_last)) sel = i;

    fb_fd = open(opt_fb, O_WRONLY | O_CLOEXEC);
    if (fb_fd < 0) logf_("cannot open %s: %s (menu not visible)", opt_fb, strerror(errno));
    int midi = open_surface_midi(), pwr = open_power_button();
    /* The audio opens with the menu. Opening it can click a little (the
     * speaker's amplifier, the outputs): heard as the menu appears, which is
     * less noticeable than just before the beeps (tried in 1.7.0). */
    splash(midi, pwr);
    if (opt_timeout > 0) audio_start();
    if (pcm_fd < 0) volume_restore(250);    /* no menu stream: restore anyway */
    if (opt_timeout_browse < opt_timeout) opt_timeout_browse = opt_timeout;

    /* POWER no longer browses: during the menu it asks whether to switch off. */
    (void)opt_hold_cs;
    double last_input = now_s(), confirm_since = -1, last_pwr = -10;
    int shown_sel = -1, shown_cd = -2, shown_fw = 0, shown_confirm = -1, go = 0, poweroff = 0;
    int browsed = 0, beeped = 0, reported = 0;
    double next_spk_poll = 0;    /* after the first turn the countdown is --timeout-browse */
    while (!go && !poweroff) {
        double t = now_s();
        int confirm = confirm_since >= 0;
        int cd;
        if (confirm) {
            cd = (int)(CONFIRM_S - (t - confirm_since) + 0.999);
            if (t - confirm_since >= CONFIRM_S) {           /* no answer: back to the menu */
                logf_("switch-off question timed out");
                confirm_since = -1; last_input = t; continue;
            }
        } else {
            int limit = browsed ? opt_timeout_browse : opt_timeout;
            cd = opt_timeout > 0 ? (int)(limit - (t - last_input) + 0.999) : -1;
            if (opt_timeout > 0 && t - last_input >= limit) { logf_("timeout"); break; }
            if (cd >= 1 && cd <= 5 && cd != beeped) {
                countdown_beep(cd);
                if (!reported && pcm_fd >= 0) { reported = 1; audio_report(); }
                beeped = cd;
            }
            if (cd > 5) beeped = 0;
        }
        if (sel != shown_sel || cd != shown_cd || fw_done != shown_fw || confirm != shown_confirm) {
            if (confirm) draw_confirm(sel, cd); else draw_menu(sel, cd);
            present();
            shown_sel = sel; shown_cd = cd; shown_fw = fw_done; shown_confirm = confirm;
        }
        struct pollfd p[2]; int np = 0;
        if (midi >= 0) { p[np].fd = midi; p[np].events = POLLIN; np++; }
        if (pwr >= 0) { p[np].fd = pwr; p[np].events = POLLIN; np++; }
        int r = poll(p, (nfds_t)np, 100);
        if (r < 0 && errno != EINTR) { usleep(100000); continue; }
        int steps = 0, push = 0, power = 0;
        for (int k = 0; k < np; k++) {
            if (!(p[k].revents & (POLLIN | POLLERR | POLLHUP))) continue;
            if (p[k].fd == midi) {
                uint8_t buf[256];
                ssize_t n = read(midi, buf, sizeof buf);
                if (n <= 0 && (p[k].revents & (POLLERR | POLLHUP))) { close(midi); midi = -1; continue; }
                for (ssize_t j = 0; j < n; j++) steps += midi_feed(buf[j], &push);
            } else {
                struct input_event ev[16];
                ssize_t n = read(pwr, ev, sizeof ev);
                if (n <= 0 && (p[k].revents & (POLLERR | POLLHUP))) { close(pwr); pwr = -1; continue; }
                for (ssize_t j = 0; j < n / (ssize_t)sizeof ev[0]; j++)
                    if (ev[j].type == EV_KEY && ev[j].code == KEY_POWER && ev[j].value == 1 && now_s() - last_pwr > 0.4) {
                        power = 1; last_pwr = now_s();
                    }
            }
        }
        if (pcm_fd >= 0 && now_s() >= next_spk_poll) { speaker_poll(); next_spk_poll = now_s() + 0.3; }
        if (confirm) {
            if (push || power) { poweroff = 1; logf_("switch off confirmed"); }
            else if (steps) { confirm_since = -1; last_input = now_s(); browsed = 1; logf_("switch off cancelled"); }
            continue;
        }
        if (power) { confirm_since = now_s(); logf_("power button: asking to switch off"); continue; }
        if (push) go = 1;
        if (steps || go) last_input = now_s();
        if (steps) { sel = ((sel + steps) % n_entries + n_entries) % n_entries; browsed = 1; }   /* carousel wraps */
    }
    audio_end();
    if (poweroff) {
        if (midi >= 0) close(midi);
        if (pwr >= 0) close(pwr);
        draw_message("Switching off\xe2\x80\xa6"); present();
        if (fb_fd >= 0) close(fb_fd);
        printf("__poweroff\n");
        fflush(stdout);
        return 0;
    }
    if (midi >= 0) close(midi);
    if (pwr >= 0) close(pwr);
    logf_("selected: %s", entries[sel].id);
    draw_starting(sel); present();
    if (fb_fd >= 0) close(fb_fd);
    printf("%s\n", entries[sel].id);
    fflush(stdout);
    return 0;
}
