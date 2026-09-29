#include <nds.h>
#include <filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "yarn.h"

typedef struct { u32 w, h, col, flags, dlwords, paloff, texoff, dloff; } Group;

static int stat_v, stat_p;
typedef struct { s32 x0, x1, y0, y1, z0, z1; } Box;
static Box *boxes; static int nboxes;
static int load_col(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    u32 n; if (fread(&n, 4, 1, f) != 1) { fclose(f); return 0; }
    boxes = malloc(n * sizeof(Box)); nboxes = fread(boxes, sizeof(Box), n, f);
    fclose(f); return 1;
}
#define PR 1100          /* player radius 0.27m in 20.12 */
static void collide(int *px, int *pz, int feet, int head)
{
    for (int it = 0; it < 2; it++)
        for (int i = 0; i < nboxes; i++) {
            Box *b = &boxes[i];
            if (b->y1 <= feet + 1200 || b->y0 >= head) continue;
            if (*px + PR <= b->x0 || *px - PR >= b->x1 || *pz + PR <= b->z0 || *pz - PR >= b->z1) continue;
            int l = *px + PR - b->x0, r = b->x1 - (*px - PR), d = *pz + PR - b->z0, u = b->z1 - (*pz - PR);
            int m = l; if (r < m) m = r; if (d < m) m = d; if (u < m) m = u;
            if (m == l) *px -= l; else if (m == r) *px += r; else if (m == d) *pz -= d; else *pz += u;
        }
}
static u8 *lvl;
static Group *grp;
static u32 ngrp;
static int texid[256];

static int texsize_enum(int n)
{
    switch (n) { case 8: return TEXTURE_SIZE_8; case 16: return TEXTURE_SIZE_16; case 32: return TEXTURE_SIZE_32;
                 case 64: return TEXTURE_SIZE_64; case 128: return TEXTURE_SIZE_128; default: return TEXTURE_SIZE_256; }
}

static int load_level(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    lvl = memalign(4, sz);
    if (!lvl || fread(lvl, 1, sz, f) != (size_t)sz) return 0;
    fclose(f);
    ngrp = ((u32 *)lvl)[1];
    grp = (Group *)(lvl + 8);
    for (u32 i = 0; i < ngrp; i++) {
        Group *g = &grp[i];
        texid[i] = -1;
        if (g->flags & 1) {
            glGenTextures(1, &texid[i]);
            glBindTexture(0, texid[i]);
            u32 fl = TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
            if (g->flags & 2) fl |= GL_TEXTURE_COLOR0_TRANSPARENT;
            glTexImage2D(0, 0, GL_RGB256, texsize_enum(g->w), texsize_enum(g->h), 0, fl, lvl + g->texoff);
            glColorTableEXT(0, 0, 256, 0, 0, (u16 *)(lvl + g->paloff));
        }
    }
    return 1;
}

static void draw_level(void)
{
    for (u32 i = 0; i < ngrp; i++) {
        Group *g = &grp[i];
        if (g->flags & 1) glBindTexture(0, texid[i]); else glBindTexture(0, 0);
        glMaterialf(GL_DIFFUSE, g->col | BIT(15));
        glMaterialf(GL_AMBIENT, RGB15(12, 12, 12));
        glCallList((u32 *)(lvl + g->dloff));
    }
}

static void init_hw(void)
{
    videoSetMode(MODE_0_3D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    vramSetBankD(VRAM_D_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleDemoInit();
    glInit();
    glEnable(GL_TEXTURE_2D | GL_ANTIALIAS);
    glClearColor(2, 2, 3, 31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);

}

static void wrap_print(const char *t);

typedef struct { const char *name, *node; int x, z; } Interact;
static const Interact INTERACTS[] = {
    { "Radio", "Radiostart", -2949, -10690 },
};
#define NINTERACT (int)(sizeof(INTERACTS) / sizeof(INTERACTS[0]))
#define REACH (5325)   /* 1.3 m in 20.12 */

static char speaker[24];
static YarnEvent dev;
static int dtype;     /* 0 = none, YE_LINE, YE_OPTIONS */
static int dsel;

static void dlg_show(void)
{
    consoleClear();
    if (dtype == YE_LINE) {
        if (speaker[0]) printf("[%s]\n", speaker);
        wrap_print(dev.text);
        printf("\n[A] next");
    } else if (dtype == YE_OPTIONS) {
        for (int i = 0; i < dev.nopt; i++) { printf("%c ", i == dsel ? '>' : ' '); wrap_print(dev.opt[i]); }
    }
}

static void dlg_advance(void)
{
    for (;;) {
        int t = yarn_step(&dev);
        if (t == YE_DONE) { dtype = 0; consoleClear(); speaker[0] = 0; return; }
        if (t == YE_COMMAND) {
            /* SetupName <name> sets the speaker; other commands are stubs for now */
            if (!strncmp(dev.text, "SetupName", 9)) {
                const char *a = dev.text + 9; while (*a == ' ') a++;
                while (*a && *a != ' ') a++;   /* skip the speaker object id */
                while (*a == ' ') a++;
                strncpy(speaker, a, sizeof speaker - 1); speaker[sizeof speaker - 1] = 0;
            }
            continue;
        }
        dtype = t; dsel = 0; dlg_show(); return;
    }
}

static void dlg_begin(const char *node) { speaker[0] = 0; yarn_start(node); dlg_advance(); }

static void dlg_input(u32 d)
{
    if (dtype == YE_LINE) { if (d & KEY_A) dlg_advance(); }
    else if (dtype == YE_OPTIONS) {
        if (d & KEY_UP) { dsel = (dsel + dev.nopt - 1) % dev.nopt; dlg_show(); }
        else if (d & KEY_DOWN) { dsel = (dsel + 1) % dev.nopt; dlg_show(); }
        else if (d & KEY_A) { yarn_choose(dsel); dlg_advance(); }
    }
}

static void run_cabin(void)
{
    if (!nboxes) load_col("nitro:/tower.col");
    int px = -9830, py = 400, pz = 4550;          // 20.12 (Player Inside spawn)
    int yaw = 0, pitch = 0;              // 15-bit angle
    int frames = 0;
    touchPosition t0, t1; int wasTouch = 0;

    while (1) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        int s = sinLerp(yaw), c = cosLerp(yaw);
        if (dtype) {
            dlg_input(kd);
        } else {
            int sp = (k & KEY_R) ? 900 : 450;
            int fwd = 0, str = 0;
            if (k & KEY_Y) {   /* Y held + D-pad = look */
                yaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * 500;
                pitch += (((k & KEY_UP) ? 1 : 0) - ((k & KEY_DOWN) ? 1 : 0)) * 300;
            } else {
                fwd = ((k & KEY_UP) ? 1 : 0) - ((k & KEY_DOWN) ? 1 : 0);
                str = ((k & KEY_RIGHT) ? 1 : 0) - ((k & KEY_LEFT) ? 1 : 0);
            }
            px += ((-s * fwd + c * str) * sp) >> 12;
            pz += ((-c * fwd - s * str) * sp) >> 12;
            collide(&px, &pz, py - 6200, py + 400);
            if (kd & KEY_A) {
                for (int i = 0; i < NINTERACT; i++) {
                    long long dx = INTERACTS[i].x - px, dz = INTERACTS[i].z - pz;
                    long long d2 = dx * dx + dz * dz;
                    long long fdot = dx * (-s) + dz * (-c);   /* facing test */
                    if (d2 < (long long)REACH * REACH && fdot > 0) { dlg_begin(INTERACTS[i].node); break; }
                }
            }
        }
        if ((k & KEY_TOUCH) && !dtype) {
            touchRead(&t1);
            if (wasTouch) { yaw -= (t1.px - t0.px) * 90; pitch += (t1.py - t0.py) * 70; }
            t0 = t1; wasTouch = 1;
        } else wasTouch = 0;
        if (pitch > 4000) pitch = 4000; if (pitch < -4000) pitch = -4000;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(70, 256.0 / 192.0, 0.05, 30);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotateXi(pitch);
        glRotateYi(-yaw);
        glTranslatef32(-px, -py, -pz);
        glLight(0, RGB15(31, 31, 29), floattov10(0.4), floattov10(-0.8), floattov10(-0.3));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_ID(1));
        draw_level();
        glFlush(0);
        while (GFX_STATUS & BIT(27)) ;
        stat_v = GFX_VERTEX_RAM_USAGE; stat_p = GFX_POLYGON_RAM_USAGE;

        frames++;
        swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank();
        if (keysDown() & KEY_START) break;
        if (!dtype && (frames % 20) == 0) { consoleClear(); printf("A: interact  Y+pad: look\nStylus: look  R: run\npos %d %d yaw %d\n", px >> 8, pz >> 8, yaw); }
    }
}

static void wrap_print(const char *t)
{
    int col = 0; char word[64];
    while (*t) {
        int n = 0;
        while (*t && *t != ' ' && *t != '\n' && n < 60) word[n++] = *t++;
        word[n] = 0;
        if (col + n > 31) { putchar('\n'); col = 0; }
        fputs(word, stdout); col += n;
        if (*t == ' ') { if (col < 31) { putchar(' '); col++; } t++; }
        else if (*t == '\n') { putchar('\n'); col = 0; t++; }
    }
    putchar('\n');
}

static void story_reader(void)
{
    int sel = 0, nn = yarn_node_count();
    while (1) {
        consoleClear();
        printf("STORY NODES (up/down, A play, B back)\n\n");
        int top = sel - 8; if (top < 0) top = 0;
        for (int i = top; i < nn && i < top + 20; i++) printf("%c%s\n", i == sel ? '>' : ' ', yarn_node_name(i));
        while (1) {
            swiWaitForVBlank(); scanKeys(); u32 d = keysDown();
            if (d & KEY_UP) { sel = (sel + nn - 1) % nn; break; }
            if (d & KEY_DOWN) { sel = (sel + 1) % nn; break; }
            if (d & KEY_B) return;
            if (d & KEY_A) goto play;
        }
        continue;
    play:
        yarn_start(yarn_node_name(sel));
        YarnEvent e; int choice;
        for (;;) {
            int t = yarn_step(&e);
            if (t == YE_DONE) break;
            if (t == YE_COMMAND) continue;
            consoleClear();
            if (t == YE_LINE) {
                wrap_print(e.text);
                printf("\n[A]");
                do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
                if (keysDown() & KEY_B) break;
            } else {
                choice = 0;
                for (;;) {
                    consoleClear();
                    for (int i = 0; i < e.nopt; i++) { printf("%c ", i == choice ? '>' : ' '); wrap_print(e.opt[i]); }
                    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_UP | KEY_DOWN)));
                    u32 d = keysDown();
                    if (d & KEY_UP) choice = (choice + e.nopt - 1) % e.nopt;
                    else if (d & KEY_DOWN) choice = (choice + 1) % e.nopt;
                    else break;
                }
                yarn_choose(choice);
            }
        }
    }
}

int main(void)
{
    init_hw();
    if (!nitroFSInit(NULL)) { printf("nitroFS init failed\n"); while (1) swiWaitForVBlank(); }
    if (!yarn_load("nitro:/story.bin")) { printf("story.bin missing\n"); while (1) swiWaitForVBlank(); }
    int sel = 0;
    for (;;) {
        consoleClear();
        printf("IRONBARK LOOKOUT (DS)\n\n%c Walk the cabin\n%c Read story nodes\n", sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ');
        do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
        u32 d = keysDown();
        if (d & (KEY_UP | KEY_DOWN)) sel ^= 1;
        if (d & KEY_A) {
            if (sel == 0) { if (!lvl && !load_level("nitro:/tower.bin")) { printf("no level\n"); continue; } run_cabin(); }
            else story_reader();
        }
    }
}
