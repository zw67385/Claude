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
static int skipbox = -1;   /* collider disabled this frame (open door) */
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
            if (i == skipbox) continue;
            Box *b = &boxes[i];
            if (b->y1 <= feet + 1200 || b->y0 >= head) continue;
            if (*px + PR <= b->x0 || *px - PR >= b->x1 || *pz + PR <= b->z0 || *pz - PR >= b->z1) continue;
            int l = *px + PR - b->x0, r = b->x1 - (*px - PR), d = *pz + PR - b->z0, u = b->z1 - (*pz - PR);
            int m = l; if (r < m) m = r; if (d < m) m = d; if (u < m) m = u;
            if (m == l) *px -= l; else if (m == r) *px += r; else if (m == d) *pz -= d; else *pz += u;
        }
}
typedef struct {
    u8 *data; Group *grp; u32 n;
    s32 scale, tx, ty, tz;          /* 20.12: scale, translation from ORIGIN to level centre */
    int tex[160];
} Level;
static Level L_in, L_out, L_door;
static u32 g_amb = RGB15(12, 12, 12), g_lights = POLY_FORMAT_LIGHT0;

static int texsize_enum(int n)
{
    switch (n) { case 8: return TEXTURE_SIZE_8; case 16: return TEXTURE_SIZE_16; case 32: return TEXTURE_SIZE_32;
                 case 64: return TEXTURE_SIZE_64; case 128: return TEXTURE_SIZE_128; default: return TEXTURE_SIZE_256; }
}

static u8 *load_file(const char *path, long *szp)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *d = memalign(4, sz);
    if (d && fread(d, 1, sz, f) != (size_t)sz) { free(d); d = NULL; }
    fclose(f);
    if (szp) *szp = sz;
    return d;
}

/* LVL1: 'LVL1', u32 n, s32 scale, cx, cy, cz (world centre, z negated), n x Group, blob */
#define ORG_X (315 * 4096)
#define ORG_Y (45 * 4096 + 1024)
#define ORG_Z (327 * 4096)
static int load_level(Level *L, const char *path)
{
    u8 *d = load_file(path, NULL);
    if (!d || memcmp(d, "LVL1", 4)) return 0;
    s32 *h = (s32 *)d;
    L->data = d; L->n = h[1]; L->grp = (Group *)(d + 24);
    L->scale = h[2]; L->tx = h[3] - ORG_X; L->ty = h[4] - ORG_Y; L->tz = h[5] + ORG_Z;
    for (u32 i = 0; i < L->n && i < 160; i++) {
        Group *g = &L->grp[i];
        L->tex[i] = 0;
        if (g->flags & 1) {
            glGenTextures(1, &L->tex[i]);
            glBindTexture(0, L->tex[i]);
            u32 fl = TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
            if (g->flags & 2) fl |= GL_TEXTURE_COLOR0_TRANSPARENT;
            if (!glTexImage2D(0, 0, GL_RGB256, texsize_enum(g->w), texsize_enum(g->h), 0, fl, d + g->texoff))
                printf("tex vram full %lu\n", i);
            glColorTableEXT(0, 0, (g->flags >> 8) & 0x1FF, 0, 0, (u16 *)(d + g->paloff));
        }
    }
    return 1;
}

/* pass 0: opaque groups, pass 1: translucent groups (drawn after every opaque one) */
static void draw_level_rot(Level *L, int pass, int rot)
{
    glPushMatrix();
    glTranslatef32(L->tx, L->ty, L->tz);
    if (rot) glRotateYi(rot);
    if (L->scale != 4096) glScalef32(L->scale, L->scale, L->scale);
    for (u32 i = 0; i < L->n; i++) {
        Group *g = &L->grp[i];
        int a = g->flags >> 24;
        if ((a < 31) != pass) continue;
        if (pass) glPolyFmt(POLY_ALPHA(a) | POLY_CULL_NONE | g_lights | POLY_ID(2 + (i & 31)));
        glBindTexture(0, (g->flags & 1) ? L->tex[i] : 0);
        glMaterialf(GL_DIFFUSE, g->col | BIT(15));
        glMaterialf(GL_AMBIENT, g_amb);
        glCallList((u32 *)(L->data + g->dloff));
    }
    glPopMatrix(1);
}
#define draw_level(L, p) draw_level_rot(L, p, 0)

/* FLR1 walkable heightfield: 'FLR1', s32 gx0, gz0, u32 nx, nz, u16 starts[nx*nz+1], s16 heights (1/256 m) */
static u8 *flr; static s32 fgx0, fgz0; static u32 fnx, fnz; static u16 *fstart; static s16 *fh;
static int load_floor(const char *path)
{
    if (!(flr = load_file(path, NULL)) || memcmp(flr, "FLR1", 4)) return 0;
    s32 *h = (s32 *)flr;
    fgx0 = h[1]; fgz0 = h[2]; fnx = h[3]; fnz = h[4];
    fstart = (u16 *)(flr + 20); fh = (s16 *)(flr + 20 + 2 * (fnx * fnz + 1));
    return 1;
}
#define NOFLOOR (-0x7FFFFFFF)
/* highest walkable surface at (x,z) not above 'lim' (20.12) */
static int floor_at(int x, int z, int lim)
{
    if (!flr) return NOFLOOR;
    int ix = (x - fgx0) >> 10, iz = (z - fgz0) >> 10;
    if (ix < 0 || iz < 0 || ix >= (int)fnx || iz >= (int)fnz) return NOFLOOR;
    int c = iz * fnx + ix, best = NOFLOOR;
    for (int i = fstart[c]; i < fstart[c + 1]; i++) { int y = fh[i] << 4; if (y <= lim) best = y; }
    return best;
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

/* ---- text pack: 'TXT1', u32 n, n x "table.key\0value\0" ---- */
static char *txt; static int ntxt;
static const char *T(const char *key)
{
    const char *p = txt + 8;
    for (int i = 0; i < ntxt; i++) {
        const char *v = p + strlen(p) + 1;
        if (!strcmp(p, key)) return v;
        p = v + strlen(v) + 1;
    }
    return key;
}
static const char *S(const char *k) { static char b[48]; snprintf(b, sizeof b, "ep4_subs.%s", k); return T(b); }

/* ---- sub-screen HUD: subtitle + interaction prompt + controls hint ---- */
static char subbuf[160]; static int sub_t;
static const char *prompt, *ctrl_hint;
static int hud_dirty = 1;
static void sub(const char *text, int secs) { strncpy(subbuf, text, sizeof subbuf - 1); sub_t = secs * 20; hud_dirty = 1; }

/* ---- dialogue ---- */
static char speaker[24];
static YarnEvent dev;
static int dlg_active, dtype, dsel;
static void (*dlg_done)(void);
static int exit_radio;

static void dlg_show(void)
{
    consoleClear();
    if (speaker[0]) printf("[%s]\n", speaker);
    if (dtype == YE_LINE) { wrap_print(dev.text); printf("\n[A] next"); }
    else for (int i = 0; i < dev.nopt; i++) { printf("%c ", i == dsel ? '>' : ' '); wrap_print(dev.opt[i]); }
}

static void dlg_advance(void)
{
    for (;;) {
        int t = yarn_step(&dev);
        if (t == YE_DONE) {
            dlg_active = 0; speaker[0] = 0; hud_dirty = 1;
            void (*cb)(void) = dlg_done; dlg_done = NULL;
            if (cb) cb();
            return;
        }
        if (t == YE_COMMAND) {
            if (!strncmp(dev.text, "SetupName", 9)) {
                const char *a = dev.text + 9; while (*a == ' ') a++;
                while (*a && *a != ' ') a++;   /* skip the speaker object id */
                while (*a == ' ') a++;
                strncpy(speaker, a, sizeof speaker - 1); speaker[sizeof speaker - 1] = 0;
            } else if (!strncmp(dev.text, "ExitRadio", 9)) exit_radio = 1;
            continue;   /* PlayStatic/StopStatic/PlaySFX/VO_*: audio, not ported yet */
        }
        dtype = t; dsel = 0; dlg_show(); return;
    }
}
static void dlg_begin(const char *node, void (*done)(void))
{
    speaker[0] = 0; dlg_done = done;
    if (!yarn_start(node)) { if (done) done(); return; }
    dlg_active = 1; dlg_advance();
}
static void dlg_input(u32 d)
{
    if (dtype == YE_LINE) { if (d & KEY_A) dlg_advance(); }
    else {
        if (d & KEY_UP) { dsel = (dsel + dev.nopt - 1) % dev.nopt; dlg_show(); }
        else if (d & KEY_DOWN) { dsel = (dsel + 1) % dev.nopt; dlg_show(); }
        else if (d & KEY_A) { yarn_choose(dsel); dlg_advance(); }
    }
}

static void fade(int out)
{
    for (int i = 0; i <= 16; i += 2) { setBrightness(3, out ? -i : -(16 - i)); swiWaitForVBlank(); }
}

/* ---- Seq1 state (names follow the original Seq1 manager) ---- */
static struct {
    int gen, shed, locked, door_open, door_ang, light, flash;
    int stove_open, wood_hand, wood_in_stove, can_pick_wood, fire;
    int to_trigger_stove, stove_done, repeat_stove, firewood_once, first_radio_done, ask_report, report_done;
    int connor_started, radio_hint, inside_first, sleep;
    int blinds[11];
    int temp10, wind;
} G;
enum { M_WALK, M_SEAT, M_PC, M_FORM };
static int mode;

enum { EV_LONGHIKE = 1, EV_HOME, EV_CONNOR, EV_STOVECONVO, EV_SMOKE, EV_REPORTDONE2, EV_RADIOHINT };
static struct { int t, id; } evq[8];
static void after(int tenths, int id)
{
    for (int i = 0; i < 8; i++) if (!evq[i].id) { evq[i].t = tenths * 2; evq[i].id = id; return; }
}

/* interactables (DS frame, 20.12) */
enum { I_GEN, I_SHED, I_SWITCH, I_DOOR, I_WOOD, I_STOVE, I_BED, I_DESK, I_MATCH, I_THERMO, I_ANEMO, I_BLIND };
typedef struct { int id, x, y, z; } Spot;
static const Spot SPOTS[] = {
    { I_GEN, -12970, -67336, 6715 }, { I_SHED, -37669, -68183, 91559 }, { I_SWITCH, -12165, -326, 2064 },
    { I_DOOR, -12479, -1487, 4354 }, { I_WOOD, -42121, -73124, 101799 }, { I_STOVE, -10300, -4400, 9590 },
    { I_BED, 9113, -5374, 6612 }, { I_DESK, 9570, -964, -4823 }, { I_DESK, 8781, -4538, -5106 },
    { I_DESK, 5653, -4229, -5459 }, { I_DESK, 9064, -1796, -7854 }, { I_MATCH, -10268, -2818, 8620 },
    { I_THERMO, 2256, -596, -12504 }, { I_ANEMO, -552, -2117, -11100 },
    { I_BLIND + 0, -6579, 4210, -13067 }, { I_BLIND + 1, 3057, 4210, -13073 }, { I_BLIND + 2, 10207, 4210, -13080 },
    { I_BLIND + 3, 12612, 4210, 10014 }, { I_BLIND + 4, 6237, 4210, 12359 }, { I_BLIND + 5, -10499, 4210, 12338 },
    { I_BLIND + 6, -3400, 4210, 12353 }, { I_BLIND + 7, -12921, 4210, 8438 }, { I_BLIND + 8, -12921, 4210, -10707 },
    { I_BLIND + 9, -12943, 4210, -3610 }, { I_BLIND + 10, 12608, 4210, 2852 },
};
#define NSPOTS (int)(sizeof(SPOTS) / sizeof(SPOTS[0]))
#define REACH 7373   /* 1.8 m */
#define DOOR_BOX 261
#define DOOR_CLOSED 280      /* ~+3 deg: door.bin is baked 3 deg ajar */
#define DOOR_OPEN (-8200)

static const char *spot_label(int id)
{
    if (id >= I_BLIND) return G.blinds[id - I_BLIND] ? "Open shutter" : "Close shutter";
    switch (id) {
    case I_GEN: return G.gen ? NULL : "Start generator";
    case I_SHED: return "Light switch";
    case I_SWITCH: return "Light switch";
    case I_DOOR: return G.locked ? "Unlock door" : G.door_open ? "Close door" : "Open door";
    case I_WOOD: return G.can_pick_wood && !G.wood_hand && !G.wood_in_stove ? "Pick up firewood" : NULL;
    case I_STOVE: return G.stove_open && G.wood_hand ? "Place wood in stove" : G.stove_open ? "Close stove" : "Open stove";
    case I_BED: return "Sleep";
    case I_DESK: return "Sit down";
    case I_MATCH: return G.wood_in_stove && !G.fire ? "Light the stove" : NULL;
    case I_THERMO: return "Check thermometer";
    case I_ANEMO: return "Check anemometer";
    }
    return NULL;
}

static void stove_convo_done(void) { }
static void on_radiostart_done(void) { G.first_radio_done = 1; yarn_set("firstRadioConversationDone", 100); }
static void on_firewood_done(void) { }
static void on_smoke_done(void) { G.ask_report = 1; }

static void use_spot(int id)
{
    char b[64];
    if (id >= I_BLIND) { G.blinds[id - I_BLIND] ^= 1; return; }
    switch (id) {
    case I_GEN: fade(1); G.gen = 1; G.shed = 1; fade(0); break;
    case I_SHED: if (G.gen) G.shed ^= 1; break;
    case I_SWITCH: if (G.gen) G.light ^= 1; else sub(S("GeneratorOn"), 4); break;
    case I_DOOR:
        if (G.locked) { G.locked = 0; sub("*click* the door unlocked", 3); }
        else G.door_open ^= 1;
        break;
    case I_WOOD: G.wood_hand = 1; ctrl_hint = "A: place in stove (open it)"; break;
    case I_STOVE:
        if (G.stove_open && G.wood_hand) { G.wood_hand = 0; G.wood_in_stove = 1; ctrl_hint = NULL; break; }
        if (G.to_trigger_stove && !G.stove_done) {
            sub(S("NoFirewood"), 4); G.stove_done = 1; G.repeat_stove = 0; after(40, EV_STOVECONVO);
        }
        G.stove_open ^= 1;
        break;
    case I_BED:
        if (!G.report_done) sub(S("ReportTonight"), 4);
        else if (!G.fire) sub(S("GettingCold"), 4);
        else {
            int all = 1; for (int i = 0; i < 11; i++) all &= G.blinds[i];
            if (!all) sub(S("CloseBoards"), 5);
            else if (G.door_open) sub(S("CloseDoor"), 5);
            else G.sleep = 1;
        }
        break;
    case I_DESK:
        fade(1); mode = M_SEAT; hud_dirty = 1;
        if (!G.radio_hint) { G.radio_hint = 1; after(10, EV_RADIOHINT); }
        fade(0);
        break;
    case I_MATCH: G.fire = 1; G.stove_open = 0; yarn_set("fireLit", 100); after(50, EV_SMOKE); break;
    case I_THERMO: snprintf(b, sizeof b, "%s %d.%d F", S("CheckTemp"), G.temp10 / 10, G.temp10 % 10); sub(b, 5); break;
    case I_ANEMO: snprintf(b, sizeof b, "the wind speed was %d mph", G.wind); sub(b, 5); break;
    }
    hud_dirty = 1;
}

static void start_radio(void)
{
    if (!G.gen) { sub(S("GeneratorOn"), 4); return; }
    if (!G.first_radio_done) { sub(S("ConnorOnRadio"), 3); return; }
    if (G.stove_done && !G.fire && !G.firewood_once) {
        G.firewood_once = 1; G.can_pick_wood = 1; dlg_begin("Firewood", on_firewood_done);
    } else if (G.ask_report) dlg_begin(G.report_done ? "ReportDone" : "Report", NULL);
    else if (G.repeat_stove) dlg_begin("Stove", NULL);
    else dlg_begin("Nothing", NULL);
}

/* ---- computer: service report form (sub screen, D-pad edited) ---- */
static const char *WEATHER[] = { "Cloud", "Mist", "Clear", "Wind", "Rain", "Heat Waves", "Blizzard", "Thunderstorm", "Dust Storm" };
static int f_row, f_temp, f_wind, f_weather, f_campers;
static void form_show(void)
{
    consoleClear();
    printf("  IRONBARK SERVICE REPORT\n\n");
    const char *lab[] = { "Reported by", "Time", "Temperature", "Wind (mph)", "Weather", "Campers", "[ SUBMIT ]" };
    for (int r = 0; r < 7; r++) {
        printf("%c %-12s", r == f_row ? '>' : ' ', lab[r]);
        switch (r) {
        case 0: printf("Tower 11"); break;
        case 1: printf("%s", T("ep4_intro.Night1")); break;
        case 2: if (f_temp < 0) printf("--.-"); else printf("%d.%d F", f_temp / 10, f_temp % 10); break;
        case 3: if (f_wind < 0) printf("--"); else printf("%d", f_wind); break;
        case 4: printf("%s", WEATHER[f_weather]); break;
        case 5: if (f_campers < 0) printf("-"); else printf("%d", f_campers); break;
        }
        printf("\n");
    }
    printf("\nUp/Down: field  Left/Right: edit\nR/L: x10   A: submit   B: close\n");
    if (subbuf[0] && sub_t > 0) { printf("\n"); wrap_print(subbuf); }
}
static void form_input(u32 d, u32 held)
{
    int step = (held & (KEY_R | KEY_L)) ? 10 : 1, dv = 0;
    if (d & KEY_UP) f_row = (f_row + 6) % 7;
    if (d & KEY_DOWN) f_row = (f_row + 1) % 7;
    if (d & KEY_RIGHT) dv = step;
    if (d & KEY_LEFT) dv = -step;
    if (dv) switch (f_row) {
        case 2: f_temp = (f_temp < 0 ? 450 : f_temp + dv); if (f_temp < 0) f_temp = 0; if (f_temp > 999) f_temp = 999; break;
        case 3: f_wind = (f_wind < 0 ? 15 : f_wind + dv); if (f_wind < 0) f_wind = 0; if (f_wind > 99) f_wind = 99; break;
        case 4: f_weather = (f_weather + (dv > 0 ? 1 : 8)) % 9; break;
        case 5: f_campers = (f_campers < 0 ? 0 : f_campers + dv); if (f_campers < 0) f_campers = 0; if (f_campers > 20) f_campers = 20; break;
    }
    if (d & KEY_A) {
        if (f_temp != G.temp10 || f_wind != G.wind) sub(S("MistakeReport"), 4);
        else if (f_weather != 2) sub(S("RightWeatherCondition"), 4);
        else if (f_campers < 0) sub(S("MistakeReport"), 4);
        else {
            G.report_done = 1; yarn_set("reportDone", 100);
            sub("Report submitted.", 3); mode = M_SEAT;
            if (G.fire) after(30, EV_REPORTDONE2);
        }
    }
    if (d & KEY_B) mode = M_PC;
    hud_dirty = 1;
}

static void fire_event(int id, int ok_walk, int *rearm)
{
    switch (id) {
    case EV_LONGHIKE: sub(S("Seq1LongHike"), 6); break;
    case EV_HOME: sub(S("Seq1Home"), 6); break;
    case EV_RADIOHINT: sub(S("Seq1Radio"), 7); break;
    case EV_CONNOR:
        mode = M_SEAT; G.to_trigger_stove = 1; G.repeat_stove = 1;
        dlg_begin("Radiostart", on_radiostart_done);
        break;
    case EV_STOVECONVO: if (!ok_walk) { *rearm = 1; break; } dlg_begin("Stove", stove_convo_done); break;
    case EV_SMOKE: if (!ok_walk) { *rearm = 1; break; } dlg_begin("Smoke", on_smoke_done); break;
    case EV_REPORTDONE2: dlg_begin("ReportDone2", NULL); break;
    }
}

static void intro(void)
{
    setBrightness(1, -16);
    for (int i = 1; i <= 17; i++) {
        char k[32]; snprintf(k, sizeof k, "ep4_intro.IntroText%d", i);
        const char *t = T(k); int n = strlen(t);
        for (int c = 1; c <= n; c++) {   /* typed out */
            char b[200]; int m = c < 199 ? c : 199; memcpy(b, t, m); b[m] = 0;
            consoleClear(); printf("\n\n"); wrap_print(b);
            swiWaitForVBlank(); swiWaitForVBlank(); scanKeys();
            if (keysDown() & KEY_A) break;
            if (keysDown() & KEY_START) goto skip;
        }
        consoleClear(); printf("\n\n"); wrap_print(t); printf("\n\n            [A]");
        do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_START)));
        if (keysDown() & KEY_START) break;
    }
skip:
    consoleClear();
    printf("\n\n\n\n\n     %s\n\n     %s\n\n     %s\n", T("ep4_intro.IronbarkLookout"), T("ep3_controls.FirstNight"), T("ep4_intro.Night1"));
    for (int i = 0; i < 120; i++) swiWaitForVBlank();
    consoleClear();
    setBrightness(1, 0);
}

static void hud_draw(int px, int fy, int pz)
{
    consoleClear();
    printf("%s  %s\n", T("ep3_controls.FirstNight"), T("ep4_intro.Night1"));
    printf("--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[12;0H");
    if (mode == M_SEAT) printf("A: Computer   X: Radio\nB: Get up\n");
    else if (prompt) printf("A: %s\n", prompt);
    printf("\x1b[18;0H");
    if (ctrl_hint) printf("%s\n", ctrl_hint);
    printf("Pad: move  Y+pad/stylus: look\nR: run  X: flashlight %s\n", G.flash ? "(on)" : "");
#if 1
    printf("%d %d %d g%d m%d", px >> 8, fy >> 8, pz >> 8, G.gen, mode);
#else
    (void)px; (void)fy; (void)pz;
#endif
}

static void pc_show(void)
{
    consoleClear();
    printf("  TOWER 11 WORKSTATION\n\n  Smiley:\n");
    wrap_print(T("ep4_smiley.Report"));
    printf("\n  A: Open service report\n  B: Log off\n");
    if (sub_t > 0) { printf("\n"); wrap_print(subbuf); }
}

#define EYE 6300        /* eye height 1.54 m */
#define STEPUP 1843     /* 0.45 m */
/* cabin interior box (origin frame): draw the interior level inside it, the exterior outside */
#define INSIDE(x, y, z) ((x) > -11500 && (x) < 11500 && (z) > -12000 && (z) < 11200 && (y) > -8000)
#define SEAT_X 5558
#define SEAT_Y (-1400)
#define SEAT_Z (-5544)
#define SEAT_YAW (-9100)
static void run_seq1(void)
{
    if (!nboxes) load_col("nitro:/tower.col");
    if (!flr) load_floor("nitro:/tower.flr");
    memset(&G, 0, sizeof G); memset(evq, 0, sizeof evq);
    intro();
    srand(REG_VCOUNT ^ (TIMER0_DATA << 3));
    G.temp10 = 440 + rand() % 31; G.wind = 17 + rand() % 4; G.locked = 1; G.flash = 1; G.door_ang = DOOR_CLOSED;
    f_row = 0; f_temp = -1; f_wind = -1; f_weather = 0; f_campers = -1;
    mode = M_WALK; ctrl_hint = NULL; subbuf[0] = 0; sub_t = 0;
    after(50, EV_LONGHIKE);

    int px = -14 * 4096, pz = -10 * 4096;   /* end of the trail, facing the tower */
    int fy = floor_at(px, pz, 0x7FFFFFF);
    if (fy == NOFLOOR) fy = -60000;
    int py, yaw = -11378, pitch = 0, frames = 0;
    touchPosition t0, t1; int wasTouch = 0;
    const char *last_prompt = NULL;

    while (1) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        int s = sinLerp(yaw), c = cosLerp(yaw);
        int in = INSIDE(px, fy, pz);
        if (dlg_active) {
            dlg_input(kd);
            if (exit_radio) { exit_radio = 0; }
        } else if (mode == M_FORM) {
            form_input(kd, k);
            if (mode == M_FORM && hud_dirty) { form_show(); hud_dirty = 0; }
        } else if (mode == M_PC) {
            if (kd & KEY_A) {
                mode = M_FORM; hud_dirty = 1;
                if (!G.connor_started) { G.connor_started = 1; after(50, EV_CONNOR); }
            } else if (kd & KEY_B) { mode = M_SEAT; hud_dirty = 1; }
            else if (hud_dirty) { pc_show(); hud_dirty = 0; }
        } else if (mode == M_SEAT) {
            if (kd & KEY_A) { if (G.gen) { mode = M_PC; hud_dirty = 1; } else sub(S("GeneratorOn"), 4); }
            else if (kd & KEY_X) start_radio();
            else if (kd & KEY_B) { fade(1); mode = M_WALK; px = 5600; pz = -3000; yaw = SEAT_YAW; hud_dirty = 1; fade(0); }
        } else {
            int sp = (k & KEY_R) ? 900 : 450;
            int fwd = 0, str = 0;
            if (k & KEY_Y) {   /* Y held + D-pad = look */
                yaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * 500;
                pitch += (((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0)) * 300;
            } else {
                fwd = ((k & KEY_UP) ? 1 : 0) - ((k & KEY_DOWN) ? 1 : 0);
                str = ((k & KEY_RIGHT) ? 1 : 0) - ((k & KEY_LEFT) ? 1 : 0);
            }
            int ox = px, oz = pz;
            px += ((-s * fwd + c * str) * sp) >> 12;
            pz += ((-c * fwd - s * str) * sp) >> 12;
            skipbox = G.door_open ? DOOR_BOX : -1;
            collide(&px, &pz, fy, fy + 6600);
            /* walkable-floor check: step up <= 0.45 m, never walk off a >0.7 m drop */
            int f = floor_at(px, pz, fy + STEPUP);
            if (f == NOFLOOR || f < fy - 2900) {
                int fx = floor_at(px, oz, fy + STEPUP), fz = floor_at(ox, pz, fy + STEPUP);   /* slide along the edge */
                if (fx != NOFLOOR && fx >= fy - 2900) { pz = oz; f = fx; }
                else if (fz != NOFLOOR && fz >= fy - 2900) { px = ox; f = fz; }
                else { px = ox; pz = oz; f = floor_at(px, pz, fy + STEPUP); }
            }
            if (f != NOFLOOR) { if (f > fy) fy = f; else { fy -= 800; if (fy < f) fy = f; } }
            if (kd & (KEY_SELECT | KEY_L)) {   /* debug: cycle teleport spots */
                /* generator, outside the door, inside the door, stove, desk, bed, thermometer */
                static const int tp[][4] = {{-12970, 11000, 0, -50000}, {-17500, 4354, -8192, 0}, {-9000, 4354, 8192, 0}, {-10300, 5500, 0, 0},
                                            {6000, -2000, 0, 0}, {6500, 6612, -8192, 0}, {2256, -8500, 0, 0}};
                static int ti = 0;
                px = tp[ti][0]; pz = tp[ti][1]; yaw = tp[ti][2]; ti = (ti + 1) % 7;
                fy = floor_at(px, pz, tp[(ti + 6) % 7][3]); if (fy == NOFLOOR) fy = -5800;
                hud_dirty = 1;
            }
            if (kd & KEY_X) { G.flash ^= 1; hud_dirty = 1; }
            /* nearest usable spot in reach and in front of the camera */
            int best = -1; long long bd = (long long)REACH * REACH;
            int ey = fy + EYE;
            for (int i = 0; i < NSPOTS; i++) {
                if (!spot_label(SPOTS[i].id)) continue;
                long long dx = SPOTS[i].x - px, dy = SPOTS[i].y - ey, dz = SPOTS[i].z - pz;
                long long d2 = dx * dx + dy * dy + dz * dz;
                if (d2 >= bd || dx * (-s) + dz * (-c) <= 0) continue;
                best = i; bd = d2;
            }
            prompt = best >= 0 ? spot_label(SPOTS[best].id) : NULL;
            if (prompt != last_prompt) { last_prompt = prompt; hud_dirty = 1; }
            if ((kd & KEY_A) && best >= 0) use_spot(SPOTS[best].id);
            if (in && !G.inside_first) { G.inside_first = 1; sub(S("Seq1Cabin"), 6); after(60, EV_HOME); }
        }
        if ((k & KEY_TOUCH) && mode == M_WALK && !dlg_active) {
            touchRead(&t1);
            if (wasTouch) { yaw -= (t1.px - t0.px) * 90; pitch += (t1.py - t0.py) * 70; }
            t0 = t1; wasTouch = 1;
        } else wasTouch = 0;
        if (pitch > 4000) pitch = 4000; if (pitch < -4000) pitch = -4000;

        /* timers */
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        int ok_walk = in && mode == M_WALK && !dlg_active;
        for (int i = 0; i < 8; i++) if (evq[i].id && --evq[i].t <= 0) {
            int id = evq[i].id, rearm = 0; evq[i].id = 0;
            if (dlg_active && id != EV_LONGHIKE && id != EV_HOME && id != EV_RADIOHINT) rearm = 1;
            else fire_event(id, ok_walk, &rearm);
            if (rearm) { evq[i].id = id; evq[i].t = 10; }
        }
        int target = G.door_open ? DOOR_OPEN : DOOR_CLOSED;   /* 0.5 s swing */
        if (G.door_ang != target) { int d = target - G.door_ang, st = 850; G.door_ang += d > st ? st : d < -st ? -st : d; }

        /* lighting: night outside, flashlight as a view-space light, cabin light when powered */
        int cabin = in && G.gen && G.light;
        g_amb = cabin ? RGB15(13, 12, 10) : RGB15(4, 4, 6);
        g_lights = POLY_FORMAT_LIGHT0 | (G.flash ? POLY_FORMAT_LIGHT1 : 0);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(70, 256.0 / 192.0, 0.05, 80);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLight(1, RGB15(22, 21, 17), 0, 0, floattov10(-0.99));
        int cx = px, cz = pz, cyaw = yaw, cpitch = pitch;
        if (mode != M_WALK) { cx = SEAT_X; cz = SEAT_Z; cyaw = SEAT_YAW; cpitch = 300; }
        glRotateXi(cpitch);
        glRotateYi(-cyaw);
        py = mode != M_WALK ? SEAT_Y : fy + EYE;
        glTranslatef32(-cx, -py, -cz);
        glLight(0, cabin ? RGB15(31, 29, 24) : RGB15(6, 7, 11), floattov10(0.4), floattov10(-0.8), floattov10(-0.3));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        in = INSIDE(cx, py - EYE, cz);
        for (int pass = 0; pass < 2; pass++) {
            if (in) draw_level(&L_in, pass);
            draw_level(&L_out, pass);
            if (L_door.data) draw_level_rot(&L_door, pass, G.door_ang);
            if (!pass) glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        }
        glFlush(0);
        while (GFX_STATUS & BIT(27)) ;
        stat_v = GFX_VERTEX_RAM_USAGE; stat_p = GFX_POLYGON_RAM_USAGE;

        frames++;
        swiWaitForVBlank(); swiWaitForVBlank(); swiWaitForVBlank();
        if (hud_dirty && !dlg_active && (mode == M_WALK || mode == M_SEAT)) { hud_draw(px, fy, pz); hud_dirty = 0; }
        if (G.sleep) {
            fade(1); consoleClear();
            printf("\n\n\n\n     %s\n\n     %s\n\n  (Seq2 not ported yet)\n", T("ep4_intro.Night2"), T("ep4_intro.TimeIntro2"));
            for (int i = 0; i < 180; i++) swiWaitForVBlank();
            fade(0); break;
        }
        if (keysDown() & KEY_START) break;
    }
    setBrightness(3, 0);
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
    { long sz; txt = (char *)load_file("nitro:/text.bin", &sz); ntxt = txt ? ((u32 *)txt)[1] : 0; }
    int sel = 0;
    for (;;) {
        consoleClear();
        printf("IRONBARK LOOKOUT (DS)\n\n%c New game\n%c Read story nodes\n", sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ');
        do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
        u32 d = keysDown();
        if (d & (KEY_UP | KEY_DOWN)) sel ^= 1;
        if (d & KEY_A) {
            if (sel == 0) { if (!L_in.data && (!load_level(&L_in, "nitro:/tower.bin") || !load_level(&L_out, "nitro:/outside.bin"))) { printf("no level\n"); continue; } if (!L_door.data) load_level(&L_door, "nitro:/door.bin"); run_seq1(); }
            else story_reader();
        }
    }
}
