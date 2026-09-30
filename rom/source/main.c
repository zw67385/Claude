#include <nds.h>
#include <nds/arm9/postest.h>
#include <filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "yarn.h"
#include <maxmod9.h>
#include "soundbank.h"
#include "vo_map.h"

/* ---- audio: samples live in NitroFS and are loaded into RAM only while needed ---- */
static u8 sfx_loaded[MSL_NSAMPS];
static int vo_cur = -1; static mm_sfxhand vo_h;
static int amb_cur = -1; static mm_sfxhand amb_h;
static int gen_on_snd; static mm_sfxhand gen_h;
static mm_sfxhand sfx_play(int id, int vol)
{
    if (id < 0) return 0;
    if (!sfx_loaded[id]) { mmLoadEffect(id); sfx_loaded[id] = 1; }
    mm_sound_effect e = { { (mm_word)id }, 1024, 0, (mm_byte)vol, 128 };
    return mmEffectEx(&e);
}
#define sfx(id) sfx_play((id), 255)
static void sfx_free(int id) { if (id >= 0 && sfx_loaded[id]) { mmUnloadEffect(id); sfx_loaded[id] = 0; } }
static void vo_play(int id)   /* one radio line at a time; the previous one is freed */
{
    if (vo_cur >= 0) { mmEffectCancel(vo_h); sfx_free(vo_cur); }
    vo_cur = id; vo_h = id >= 0 ? sfx_play(id, 255) : 0;
}
static int amb2_cur = -1; static mm_sfxhand amb2_h;   /* second ambience layer (crickets) */
static void amb2_set(int id)
{
    if (id == amb2_cur) return;
    if (amb2_cur >= 0) { mmEffectCancel(amb2_h); sfx_free(amb2_cur); }
    amb2_cur = id; amb2_h = id >= 0 ? sfx_play(id, 110) : 0;
}
static int sting_cur = -1; static mm_sfxhand sting_h;   /* one music stinger at a time; the previous one is freed */
static void sting(int id)
{
    if (sting_cur >= 0) { mmEffectCancel(sting_h); if (sting_cur != id) sfx_free(sting_cur); }
    sting_cur = id; sting_h = id >= 0 ? sfx_play(id, 230) : 0;
}
static int static_on; static mm_sfxhand static_h;
static void amb_set(int id)
{
    if (id == amb_cur) return;
    if (amb_cur >= 0) { mmEffectCancel(amb_h); sfx_free(amb_cur); }
    amb_cur = id; amb_h = id >= 0 ? sfx_play(id, 150) : 0;
    if (id < 0) amb2_set(-1);
}
static int mus_cur = -1; static mm_sfxhand mus_h;
static void music_set(int id)   /* one looping music track at a time */
{
    if (id == mus_cur) return;
    if (mus_cur >= 0) { mmEffectCancel(mus_h); sfx_free(mus_cur); }
    mus_cur = id; mus_h = id >= 0 ? sfx_play(id, 190) : 0;
}
/* the RV radio (Radio.cs): plays its songs in turn, SELECT mutes / unmutes it (the song runs on silently) */
static const short *rad_pl, *rad_len; static int rad_n, rad_i, rad_mute; static u32 rad_t0;
static volatile u32 vb_total;
static void radio_start(const short *pl, const short *len, int n)
{
    if (rad_pl == pl) return;
    rad_pl = pl; rad_len = len; rad_n = n; rad_i = 0; rad_t0 = vb_total; music_set(pl[0]); if (rad_mute) mmEffectVolume(mus_h, 0);
}
/* scene change: free every one-shot still in RAM (loops, music and the current VO stay) */
static void sfx_flush(void)
{
    if (static_on) { static_on = 0; mmEffectCancel(static_h); }
    if (sting_cur >= 0) { mmEffectCancel(sting_h); sting_cur = -1; }
    for (int i = 0; i < MSL_NSAMPS; i++)
        if (i != amb_cur && i != amb2_cur && i != mus_cur && i != vo_cur && i != SFX_GEN_RUN && i != SFX_GEN_ON) sfx_free(i);
}
static void radio_stop(void) { rad_pl = NULL; music_set(-1); }
static void radio_tick(u32 kd)
{
    if (!rad_pl) return;
    if (kd & KEY_L) sfx(SFX_HORN);
    if (kd & KEY_SELECT) { rad_mute ^= 1; sfx(SFX_CLICK); mmEffectVolume(mus_h, rad_mute ? 0 : 190); }
    if (mus_cur >= 0 && vb_total - rad_t0 > (u32)rad_len[rad_i] * 60) {   /* song over: next one */
        rad_t0 = vb_total;
        mmEffectCancel(mus_h); sfx_free(mus_cur); mus_cur = -1;
        rad_i = (rad_i + 1) % rad_n; music_set(rad_pl[rad_i]); if (rad_mute) mmEffectVolume(mus_h, 0);
    }
}
static const short RAD_FIRST[] = { SFX_RAD1, SFX_RAD2, SFX_RAD3 }, RAD_TRAIL[] = { SFX_RAD_TUE };
static const short RAD_FIRST_LEN[] = { 82, 93, 80 }, RAD_TRAIL_LEN[] = { 54 };   /* seconds */
static void gen_snd(int on, int vol)
{
    if (on && !gen_on_snd) { gen_h = sfx_play(SFX_GEN_RUN, vol); gen_on_snd = 1; }
    else if (!on && gen_on_snd) { mmEffectCancel(gen_h); gen_on_snd = 0; }
    else if (on) mmEffectVolume(gen_h, vol);
}

typedef struct { u32 w, h, col, flags, dlwords, paloff, texoff, dloff; } Group;

static int stat_v, stat_p, stat_vb;
static volatile int vb_count;
static void on_vblank(void) { vb_count++; vb_total++; }
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
/* a level group cut into ~2 m cells at load time: each is box-tested against the view on its own */
typedef struct { u32 *dl; u16 grp, nv; s16 x0, y0, z0, x1, y1, z1; } Cell;
typedef struct {
    u8 *data; Group *grp; u32 n;
    s32 scale, tx, ty, tz;          /* 20.12: scale, translation from ORIGIN to level centre */
    int tex[160];
    Cell *cell; u32 *cdl; int ncell, bud;   /* bud: scenery, drawn under the budget */
} Level;
/* Per-frame geometry budget. The DS keeps at most 6144 vertices / 2048 polygons a frame (the rest vanish) and
   can only rasterise so many polygons per scanline (past that, lines show the clear colour): everything that
   is drawn goes through here, nearest first, and stops at g_vbudget vertices. */
static int g_vbudget = 5400, g_pbudget = 800;     /* stored vertices / polygons a frame */
static int g_sv, g_sp, g_since;                    /* hardware counts at the last sync, vertices sent since */
static u32 g_fmt;
#define glPolyFmt(x) glPolyFmt(g_fmt = (x))
/* The polygon budget follows the rasteriser: RDLINES_COUNT is how many of its 48 buffered lines were left at the
   tightest point of the last frame (0 plus the underflow flag = lines were lost). Near it, far cells go first;
   with room to spare the budget creeps back up. */
static void rd_adapt(void)
{
    int rl = GFX_RDLINES_COUNT & 63, under = GFX_CONTROL & BIT(12);
    if (under) GFX_CONTROL |= BIT(12);
    if (under || rl < 3) g_pbudget -= 120; else if (rl < 8) g_pbudget -= 25; else if (rl > 16) g_pbudget += 5;
    if (g_pbudget < 200) g_pbudget = 200; if (g_pbudget > 1000) g_pbudget = 1000;
}
#define glFlush(x) (rd_adapt(), g_sv = g_sp = g_since = 0, glFlush(x))
/* The geometry engine drops back faces and off-screen polygons before storing them, so the budget is kept
   against its own vertex/polygon RAM counters, re-read (after the FIFO drains) every few hundred vertices. */
/* Only scenery (the streamed world, the big level shells) is budgeted; models - the cab, props, people -
   always draw and are sent first, and scenery gets what is left, but never less than its nearest ~250 polygons. */
static int g_bud, g_pfloor;
static void bud_sync(void)
{
    while (GFX_STATUS & BIT(27)) ;
    g_sv = GFX_VERTEX_RAM_USAGE; g_sp = GFX_POLYGON_RAM_USAGE; g_since = 0;
}
static void bud_begin(void) { bud_sync(); g_pfloor = g_sp + 250; g_bud = 1; }
static void bud_end(void) { g_bud = 0; }
static int budget_take(int nv)
{
    if (!g_bud) { g_since += nv; return 1; }
    if (g_since + nv > 240) bud_sync();
    int pmax = g_pbudget > g_pfloor ? g_pbudget : g_pfloor;
    if (g_sv + g_since + nv > g_vbudget || g_sp + (g_since + nv) / 3 > pmax) return 0;
    g_since += nv; return 1;
}
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
static void level_split(Level *L);
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
    L->cell = NULL; L->cdl = NULL; L->ncell = 0;
    level_split(L);
    return 1;
}

static int dl_verts(const u32 *w)
{
    u32 n = w[0], i = 1; int v = 0;
    while (i <= n) {
        u32 pk = w[i++];
        for (int j = 0; j < 4; j++) {
            int op = (pk >> (8 * j)) & 0xFF;
            if (op == 0x20 || op == 0x21 || op == 0x22 || op == 0x40) i++;
            else if (op == 0x23) { i += 2; v++; }
        }
    }
    return v;
}

/* ---- load-time cell split: parse a group's display list into primitives, bucket them by position, re-emit */
typedef struct { u32 nrm, tc, xy, z; } DVtx;
typedef struct { u32 key; u16 v, mode; } DPrim;
static int prim_cmp(const void *a, const void *b) { u32 x = ((const DPrim *)a)->key, y = ((const DPrim *)b)->key; return x < y ? -1 : x > y; }
static u32 *pk_out; static int pk_n, pk_hdr; static u32 pk_ops;
static void pk_cmd(int op, int np, u32 a, u32 b)
{
    if (pk_n == 0) { pk_hdr = 0; }
    if (pk_n % 4 == 0) { pk_hdr = pk_ops; pk_out[pk_ops++] = 0; }
    pk_out[pk_hdr] |= (u32)op << (8 * (pk_n % 4)); pk_n++;
    if (np > 0) pk_out[pk_ops++] = a;
    if (np > 1) pk_out[pk_ops++] = b;
}
static int dl_parse(const u32 *w, DVtx *vt, int *nvt, DPrim *pr, int *npr, int maxv)
{
    u32 n = w[0], i = 1, nrm = 0, tc = 0, mode = 0; int first = *nvt, run = 0;
    while (i <= n) {
        u32 pk = w[i++];
        for (int j = 0; j < 4; j++) {
            int op = (pk >> (8 * j)) & 0xFF;
            if (op == 0x20) i++;
            else if (op == 0x21) nrm = w[i++];
            else if (op == 0x22) tc = w[i++];
            else if (op == 0x23) {
                if (*nvt >= maxv) return 0;
                vt[*nvt].nrm = nrm; vt[*nvt].tc = tc; vt[*nvt].xy = w[i]; vt[*nvt].z = w[i + 1]; i += 2;
                (*nvt)++; run++;
                int k = mode == 1 ? 4 : 3;
                if (run == k) { pr[*npr].v = *nvt - k; pr[*npr].mode = mode; (*npr)++; run = 0; }
            }
            else if (op == 0x40) { mode = w[i++]; run = 0; }
        }
    }
    (void)first; return 1;
}
static void level_split(Level *L)
{
    int tv = 0, mv = 0;
    for (u32 g = 0; g < L->n; g++) { int v = dl_verts((u32 *)(L->data + L->grp[g].dloff)); tv += v; if (v > mv) mv = v; }
    /* worst case per vertex: normal + texcoord + vertex (4 params, 3 command bytes); per cell: size, begin, end */
    DVtx *vt = malloc(mv * sizeof(DVtx) + 64); DPrim *pr = malloc((mv / 3 + 2) * sizeof(DPrim));
    Cell *cl = malloc(sizeof(Cell) * (tv / 3 + 8)); u32 *out = malloc(tv * 5 * 4 + (tv / 3 + 8) * 16);
    if (!vt || !pr || !out || !cl) { free(vt); free(pr); free(out); free(cl); return; }
    /* 2 m cells: model units are 1/4096, world = model * scale / 4096 */
    int shift = 10; long long cellm = 2LL * 4096 * 4096 / (L->scale ? L->scale : 4096);
    while ((1LL << (shift + 1)) <= cellm && shift < 14) shift++;
    int nc = 0; pk_out = out; pk_ops = 0;
    for (u32 g = 0; g < L->n; g++) {
        int nvt = 0, npr = 0;
        if (!dl_parse((u32 *)(L->data + L->grp[g].dloff), vt, &nvt, pr, &npr, mv)) continue;
        for (int p = 0; p < npr; p++) {
            DVtx *v = &vt[pr[p].v]; int k = pr[p].mode == 1 ? 4 : 3; int sx = 0, sy = 0, sz = 0;
            for (int q = 0; q < k; q++) { sx += (s16)(v[q].xy & 0xFFFF); sy += (s16)(v[q].xy >> 16); sz += (s16)(v[q].z & 0xFFFF); }
            sx /= k; sy /= k; sz /= k;
            pr[p].key = ((u32)((sx + 32768) >> shift) << 20) | ((u32)((sy + 32768) >> shift) << 10) | (u32)((sz + 32768) >> shift) | (pr[p].mode << 30);
        }
        qsort(pr, npr, sizeof(DPrim), prim_cmp);
        for (int a = 0; a < npr;) {
            int b = a; while (b < npr && pr[b].key == pr[a].key) b++;
            Cell *c = &cl[nc++]; c->grp = g; c->nv = 0;
            c->x0 = c->y0 = c->z0 = 32767; c->x1 = c->y1 = c->z1 = -32768;
            u32 start = pk_ops; pk_out[pk_ops++] = 0; pk_n = 0;
            pk_cmd(0x40, 1, pr[a].mode, 0);
            u32 lastn = 0xFFFFFFFF; int tex = L->grp[g].flags & 1;
            for (int p = a; p < b; p++) {
                int k = pr[p].mode == 1 ? 4 : 3;
                for (int q = 0; q < k; q++) {
                    DVtx *v = &vt[pr[p].v + q];
                    if (v->nrm != lastn) { pk_cmd(0x21, 1, v->nrm, 0); lastn = v->nrm; }
                    if (tex) pk_cmd(0x22, 1, v->tc, 0);
                    pk_cmd(0x23, 2, v->xy, v->z);
                    s16 x = v->xy & 0xFFFF, y = v->xy >> 16, z = v->z & 0xFFFF;
                    if (x < c->x0) c->x0 = x; if (x > c->x1) c->x1 = x;
                    if (y < c->y0) c->y0 = y; if (y > c->y1) c->y1 = y;
                    if (z < c->z0) c->z0 = z; if (z > c->z1) c->z1 = z;
                }
                c->nv += k;
            }
            pk_cmd(0x41, 0, 0, 0);
            pk_out[start] = pk_ops - start - 1;
            c->dl = (u32 *)(uintptr_t)start;          /* offset for now: out may still move */
            a = b;
        }
    }
    free(vt); free(pr);
    L->cdl = realloc(out, pk_ops * 4); if (!L->cdl) L->cdl = out;
    for (int i = 0; i < nc; i++) cl[i].dl = L->cdl + (uintptr_t)cl[i].dl;
    L->cell = realloc(cl, sizeof(Cell) * (nc ? nc : 1)); if (!L->cell) L->cell = cl;
    L->ncell = nc;
}

/* frustum test of a model-space box under the current matrices; also its view depth (for near-to-far order) */
static int cell_vis(const Cell *c, int *w)
{
    if (c->x1 - c->x0 > 32000 || c->y1 - c->y0 > 32000 || c->z1 - c->z0 > 32000) { *w = 0; return 1; }
    if (!BoxTest(c->x0, c->y0, c->z0, c->x1 - c->x0 + 1, c->y1 - c->y0 + 1, c->z1 - c->z0 + 1)) return 0;
    PosTest((c->x0 + c->x1) / 2, (c->y0 + c->y1) / 2, (c->z0 + c->z1) / 2);
    *w = PosTestWresult(); return 1;
}

/* pass 0: opaque groups, pass 1: translucent groups (drawn after every opaque one) */
static void draw_level_rot(Level *L, int pass, int rot)
{
    glPushMatrix();
    glTranslatef32(L->tx, L->ty, L->tz);
    if (rot) glRotateYi(rot);
    if (L->scale != 4096) glScalef32(L->scale, L->scale, L->scale);
    if (!L->cell) {
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
        return;
    }
    static u16 vis[1024]; static int vw[1024]; int nv = 0; u32 fmt = g_fmt;
    for (int i = 0; i < L->ncell && nv < 1024; i++) {
        Cell *c = &L->cell[i];
        if (((L->grp[c->grp].flags >> 24) < 31) != pass) continue;
        int w; if (!cell_vis(c, &w)) continue;
        int b = nv++;
        while (b > 0 && vw[b - 1] > w) { vw[b] = vw[b - 1]; vis[b] = vis[b - 1]; b--; }
        vw[b] = w; vis[b] = i;
    }
    int last = -1;
    if (L->bud) bud_begin();
    for (int n = 0; n < nv; n++) {
        Cell *c = &L->cell[vis[n]];
        if (!budget_take(c->nv)) continue;
        Group *g = &L->grp[c->grp]; int a = g->flags >> 24;
        if (c->grp != last) {
            last = c->grp;
            glPolyFmt(pass ? (u32)(POLY_ALPHA(a) | POLY_CULL_NONE | g_lights | POLY_ID(2 + (c->grp & 31))) : fmt);
            glBindTexture(0, (g->flags & 1) ? L->tex[c->grp] : 0);
            glMaterialf(GL_DIFFUSE, g->col | BIT(15));
            glMaterialf(GL_AMBIENT, g_amb);
        }
        glCallList(c->dl);
    }
    bud_end();
    g_fmt = fmt;
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
    /* distance fog: 32-step ramp covering ~3 m .. ~40 m of the depth buffer (near 0.05, far 80) */
    glFogShift(4);
    glFogOffset(0x7C00);
    for (int i = 0; i < 32; i++) glFogDensity(i, i < 8 ? 0 : (i - 8) * 6 > 124 ? 124 : (i - 8) * 6);
}
static void sky(int r, int g, int b) { glClearColor(r, g, b, 31); glFogColor(r, g, b, 31); }

/* sky dome: the original skybox cubemaps baked to a 512x64 horizon panorama (tools/make_sky.py),
   drawn as a textured cylinder around the camera; fog takes the horizon colour so the world
   dissolves into the sky the way Unity's fog + skybox did. One sky texture in VRAM at a time. */
static struct { const char *name; int tex, on; u16 hor, zen; } SK;
#define glResetTextures() (SK.name = NULL, SK.on = 0, glResetTextures())
static void sky_use(const char *name)
{
    if (!name) { SK.on = 0; return; }
    if (SK.name && !strcmp(SK.name, name)) { SK.on = 1; return; }
    char path[40]; sprintf(path, "nitro:/sky/%s.sky", name);
    u8 *d = load_file(path, NULL);
    if (!d || memcmp(d, "SKY1", 4)) { free(d); SK.on = 0; return; }
    if (SK.name) glDeleteTextures(1, &SK.tex);
    u16 *h = (u16 *)(d + 4);
    glGenTextures(1, &SK.tex); glBindTexture(0, SK.tex);
    glTexImage2D(0, 0, GL_RGB256, TEXTURE_SIZE_512, TEXTURE_SIZE_64, 0, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S, d + 12 + 512);
    glColorTableEXT(0, 0, 256, 0, 0, (u16 *)(d + 12));
    SK.hor = h[2]; SK.zen = h[3]; SK.name = name; SK.on = 1;
    free(d);
}
/* clear to the zenith, fog to the horizon */
static void sky_col(void)
{
    glClearColor(SK.zen & 31, (SK.zen >> 5) & 31, SK.zen >> 10, 31);
    glFogColor(SK.hor & 31, (SK.hor >> 5) & 31, SK.hor >> 10, 31);
}
/* call with the camera rotation loaded and the translation not yet applied */
static int sky_hide;   /* indoors: the roof covers it, the clear colour shows through the windows */
static void sky_dome(float far)
{
    if (!SK.on || sky_hide) return;
    sky_col();
    glPushMatrix();
    int R = floattof32(far * 0.9f);
    glScalef32(R, R, R);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
    glBindTexture(0, SK.tex);
    glColor(RGB15(31, 31, 31));
    /* rings: zenith cap, +55 deg (texture top), -15 deg (texture bottom), below the ground */
    static const v16 ry[4] = { 20480, 5849, -1097, -8192 };
    static const int rv[4] = { 0, 0, 64, 64 };
    glBegin(GL_QUADS);
    for (int b = 0; b < 3; b++)
        for (int i = 0; i < 16; i++) {
            int a0 = i * 2048, a1 = a0 + 2048;
            v16 x0 = sinLerp(a0), z0 = -cosLerp(a0), x1 = sinLerp(a1), z1 = -cosLerp(a1);
            glTexCoord2t16(inttot16(i * 32), inttot16(rv[b]));      glVertex3v16(x0, ry[b], z0);
            glTexCoord2t16(inttot16(i * 32 + 32), inttot16(rv[b])); glVertex3v16(x1, ry[b], z1);
            glTexCoord2t16(inttot16(i * 32 + 32), inttot16(rv[b + 1])); glVertex3v16(x1, ry[b + 1], z1);
            glTexCoord2t16(inttot16(i * 32), inttot16(rv[b + 1]));  glVertex3v16(x0, ry[b + 1], z0);
        }
    glEnd();
    glPopMatrix(1);
}

static void wrap_print(const char *t);
static void game_cmd(const char *c);

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

/* "{0}" -> $total_check (the only substituted value in the story), bare True/False option texts -> Yes/No */
static const char *dlg_text(const char *t)
{
    static char b[2][160]; static int w;
    if (!strcmp(t, "True")) return "Yes";
    if (!strcmp(t, "False")) return "No";
    const char *p = strstr(t, "{0}");
    if (!p) return t;
    int v = yarn_get("total_check");
    char *o = b[w ^= 1];
    snprintf(o, 160, "%.*s%d.%02d%s", (int)(p - t), t, v / 100, v % 100, p + 3);
    return o;
}
static void dlg_show(void)
{
    consoleClear();
    if (speaker[0]) printf("[%s]\n", speaker);
    if (dtype == YE_LINE) { wrap_print(dlg_text(dev.text)); printf("\n[A] next"); }
    else for (int i = 0; i < dev.nopt; i++) {   /* "^exit+" marks the original's leave-conversation option */
        const char *o = dev.opt[i];
        if (!strncmp(o, "^exit+", 6)) { o += 6; while (*o == ' ') o++; if (!*o) o = "..."; }
        printf("%c ", i == dsel ? '>' : ' '); wrap_print(dlg_text(o));
    }
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
                { int n = 0; while (*a && n < (int)sizeof speaker - 1) { if (*a != '"') speaker[n++] = *a; a++; } speaker[n] = 0; }
            } else if (!strncmp(dev.text, "ExitRadio", 9)) exit_radio = 1;
            else if (!strncmp(dev.text, "HideName", 8)) speaker[0] = 0;
            else if (!strncmp(dev.text, "VO_", 3)) {   /* VO_SEQn Tower <idx> */
                static const struct { const char *k; const short *v; int n; } VS[] = {
                    { "VO_SEQ1", VO_SEQ1, sizeof VO_SEQ1 / 2 }, { "VO_SEQ3", VO_SEQ3, sizeof VO_SEQ3 / 2 },
                    { "VO_SEQ4", VO_SEQ4, sizeof VO_SEQ4 / 2 }, { "VO_SEQ5", VO_SEQ5, sizeof VO_SEQ5 / 2 },
                    { "VO_SEQ6", VO_SEQ6, sizeof VO_SEQ6 / 2 }, { "VO_SEQ7", VO_SEQ7, sizeof VO_SEQ7 / 2 },
                    { "VO_SEQ8", VO_SEQ8, sizeof VO_SEQ8 / 2 }, { "VO_HIKER", VO_HIKER, sizeof VO_HIKER / 2 } };
                const char *sp = strchr(dev.text, ' '); int kl = sp ? sp - dev.text : (int)strlen(dev.text);
                const char *ix = strrchr(dev.text, ' '); int i = ix ? atoi(ix + 1) : 0;
                for (unsigned j = 0; j < sizeof VS / sizeof *VS; j++)
                    if ((int)strlen(VS[j].k) == kl && !strncmp(VS[j].k, dev.text, kl) && i >= 0 && i < VS[j].n) vo_play(VS[j].v[i]);
            }
            else if (!strncmp(dev.text, "PlayStatic", 10) || !strncmp(dev.text, "TestStatic", 10)) { if (!static_on) { static_on = 1; static_h = sfx_play(SFX_STATIC, 140); } }
            else if (!strncmp(dev.text, "StopStatic", 10)) { if (static_on) { static_on = 0; mmEffectCancel(static_h); } }
            else {   /* dialogueSFX.cs / WatchTowerManager stingers, then the scene's own handler */
                static const struct { const char *k; short id; } ST[] = {
                    { "RadioOutOfService", SFX_CREEPIES }, { "WhatWereYouDoing", SFX_HAUNT }, { "SubtleTension", SFX_TENSION },
                    { "ScaryViolin", SFX_VIOLIN_S }, { "EerieStrings", SFX_EERIE }, { "LowAndDeep", SFX_LOWDEEP },
                    { "PlayCreepySFX", SFX_HAUNT }, { "PlayMid", SFX_CREEPIES }, { "PlayMysteryBuilder", SFX_MYSTERY },
                    { "PlaySpookySiren", SFX_SIREN }, { "PlayScaryViolin", SFX_VIOLIN_S } };
                for (unsigned j = 0; j < sizeof ST / sizeof *ST; j++) if (!strncmp(dev.text, ST[j].k, strlen(ST[j].k))) sting(ST[j].id);
                if (!strncmp(dev.text, "PlaySFX1", 8)) sfx(SFX_WALKIE1);
                else if (!strncmp(dev.text, "PlaySFX2", 8)) sfx(SFX_WALKIE2);
                game_cmd(dev.text);
            }
            continue;
        }
        dtype = t; dsel = 0; if (t == YE_LINE) sfx_play(SFX_POP, 150); dlg_show(); return;
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

/* ---- game state (names follow the original WatchTowerManager) ---- */
static int seq;   /* 1..8 */
static struct {
    int gen, shed, locked, door_open, door_ang, light, flash;
    int stove_open, wood_hand, wood_in_stove, can_pick_wood, fire;
    int to_trigger_stove, stove_done, repeat_stove, firewood_once, first_radio_done, ask_report, report_done;
    int connor_started, radio_hint, inside_first, sleep;
    int blinds[11];
    int temp10, wind;
    /* Seq2..8 */
    int peed, convo1, convo2, standup1, standup2, campers, checkcampers;
    int ingr_hand, ingr_in, cass_hand, cass_cooked, cass_where, cook_t, eaten, oven_open, cass_first;
    int cult_walk, cult_gone, skull, flare, anxious, knock, billy, badguy, connor_su, connor_c, bino_used;
    int power_out, gas_hand, gas_filled, update_done, cult_seen, flash_off, hidden, run, ground_first;
    int next;   /* pending sequence change (0 none, 9 = ending) */
} G;
enum { M_WALK, M_SEAT, M_PC, M_FORM };
static int mode;
enum { CW_NONE, CW_OVEN, CW_MICRO };   /* where the casserole is cooking */

enum { EV_LONGHIKE = 1, EV_HOME, EV_CONNOR, EV_STOVECONVO, EV_SMOKE, EV_REPORTDONE2, EV_RADIOHINT,
       EV_SUB, EV_S2SUBS, EV_S3CONVO, EV_S3CONFIRM, EV_S4STAND, EV_S5WALK, EV_S5GONE, EV_S5SUBS2, EV_S6FLARE,
       EV_S6START, EV_ANXIOUS, EV_KNOCK, EV_S6CONNORSU, EV_S7CONNOR, EV_S7POWER, EV_S8STAND, EV_S8SUBS, EV_S8COME,
       EV_S8LEAVE };
static struct { int t, id; const char *s; } evq[12];
static void after(int tenths, int id)
{
    for (int i = 0; i < 12; i++) if (!evq[i].id) { evq[i].t = tenths * 2; evq[i].id = id; evq[i].s = NULL; return; }
}
static void sub_after(int tenths, const char *key)   /* delayed subtitle (ep4_subs key) */
{
    for (int i = 0; i < 12; i++) if (!evq[i].id) { evq[i].t = tenths * 2; evq[i].id = EV_SUB; evq[i].s = key; return; }
}

/* interactables (DS frame, 20.12) */
enum { I_GEN, I_SHED, I_SWITCH, I_DOOR, I_WOOD, I_STOVE, I_BED, I_DESK, I_MATCH, I_THERMO, I_ANEMO,
       I_PEE2, I_POTTY, I_FRIDGE, I_CASS, I_OVEN, I_MICRO, I_SKULL, I_GAS, I_MOUNT, I_BLIND };
typedef struct { int id, x, y, z; } Spot;
static const Spot SPOTS[] = {
    { I_GEN, -12970, -67336, 6715 }, { I_SHED, -37669, -68183, 91559 }, { I_SWITCH, -12165, -326, 2064 },
    { I_DOOR, -12479, -1487, 4354 }, { I_WOOD, -42121, -73124, 101799 }, { I_STOVE, -10300, -4400, 9590 },
    { I_BED, 9113, -5374, 6612 }, { I_DESK, 9570, -964, -4823 }, { I_DESK, 8781, -4538, -5106 },
    { I_DESK, 5653, -4229, -5459 }, { I_DESK, 9064, -1796, -7854 }, { I_MATCH, -10268, -2818, 8620 },
    { I_THERMO, 2256, -596, -12504 }, { I_ANEMO, -552, -2117, -11100 },
    { I_PEE2, -20047, -2670, -18190 }, { I_POTTY, -20652, -66990, -2695 }, { I_FRIDGE, -10916, -4378, 412 },
    { I_CASS, -9655, -2502, -10147 }, { I_OVEN, -9114, -5254, -2965 }, { I_MICRO, -10382, -2961, 1595 },
    { I_SKULL, -18853, -4100, 4059 }, { I_GAS, -33346, -73023, 101099 }, { I_MOUNT, -16412, -4135, -15544 },
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
static const char *INGR[] = { "", "cheese", "tomatoes", "sauce", "pasta", "pepperoni" };

static int all_blinds(void) { int a = 1; for (int i = 0; i < 11; i++) a &= G.blinds[i]; return a; }
static int food_seq(void) { return seq == 4 || seq == 6; }

static const char *spot_label(int id)
{
    static char b[40];
    if (id >= I_BLIND) return G.blinds[id - I_BLIND] ? "Open shutter" : "Close shutter";
    switch (id) {
    case I_GEN:
        if (seq == 7 && G.power_out) return G.gas_hand ? "Fill the generator" : "Start generator";
        return G.gen ? NULL : "Start generator";
    case I_SHED: return "Light switch";
    case I_SWITCH: return "Light switch";
    case I_DOOR: return G.locked ? "Unlock door" : G.door_open ? "Close door" : "Open door";
    case I_WOOD: return G.can_pick_wood && !G.wood_hand && !G.wood_in_stove ? "Pick up firewood" : NULL;
    case I_STOVE: return G.stove_open && G.wood_hand ? "Place wood in stove" : G.stove_open ? "Close stove" : "Open stove";
    case I_BED:
        if (G.cass_hand && G.cass_cooked) return "Eat in bed";
        if (seq == 8 && G.flash_off && !G.hidden) return "Hide under the bed";
        return "Sleep";
    case I_DESK: return "Sit down";
    case I_MATCH: return G.wood_in_stove && !G.fire ? "Light the stove" : NULL;
    case I_THERMO: return "Check thermometer";
    case I_ANEMO: return "Check anemometer";
    case I_PEE2: return seq == 2 && !G.peed ? "Pee" : NULL;
    case I_POTTY: return seq == 7 && G.gas_filled && !G.peed ? "Use the porta potty" : NULL;
    case I_FRIDGE:
        if (!food_seq() || G.eaten || G.cass_hand || G.ingr_hand) return NULL;
        if (seq == 6) return G.cass_where || G.cass_cooked ? NULL : "Take the casserole";
        if (G.ingr_in >= 5) return NULL;
        snprintf(b, sizeof b, "Take %s", INGR[G.ingr_in + 1]); return b;
    case I_CASS:
        if (seq != 4 || G.eaten || G.cass_where || G.cass_cooked) return NULL;
        if (G.ingr_hand) { snprintf(b, sizeof b, "Add %s", INGR[G.ingr_hand]); return b; }
        if (G.ingr_in >= 5 && !G.cass_hand) return "Pick up the casserole";
        return G.ingr_in ? NULL : "Casserole dish";
    case I_OVEN:
        if (seq != 4) return NULL;
        if (G.cass_hand && !G.cass_cooked) return "Put it in the oven";
        if (G.cass_where == CW_OVEN) return G.cook_t ? "Check the oven" : "Take the casserole out";
        return NULL;
    case I_MICRO:
        if (seq != 6) return NULL;
        if (G.cass_hand && !G.cass_cooked) return "Heat it in the microwave";
        if (G.cass_where == CW_MICRO) return G.cook_t ? "Check the microwave" : "Take the casserole out";
        return NULL;
    case I_SKULL: return seq == 5 && G.cult_gone && !G.skull ? "What is that?" : NULL;
    case I_GAS: return seq == 7 && G.power_out && !G.gas_hand && !G.gas_filled ? "Take the gas can" : NULL;
    case I_MOUNT: return seq == 8 && G.cult_seen && !G.flash_off ? "Take a photo" : NULL;
    }
    return NULL;
}

static void stove_convo_done(void) { }
static void on_radiostart_done(void) { G.first_radio_done = 1; yarn_set("firstRadioConversationDone", 100); }
static void on_firewood_done(void) { }
static void on_smoke_done(void) { G.ask_report = 1; }

/* yarn commands with game-side effects */
static void (*scene_cmd)(const char *c);   /* set by the 3D scenes (diner, trail) */
static void game_cmd(const char *c)
{
    if (scene_cmd) { scene_cmd(c); return; }
    if (!strncmp(c, "AnxiousConvo", 12)) { G.anxious = 1; after(200, EV_ANXIOUS); }
    else if (!strncmp(c, "StartKnocking", 13)) { G.knock = 1; after(30, EV_KNOCK); }
}

static void play_until_done(void);
static void eat(void)
{
    sfx(SFX_EAT); fade(1);
    G.cass_hand = 0; G.eaten = 1; ctrl_hint = NULL;
    sub(S("EatingInBed"), 5);
    fade(0);
    if (seq == 4 && !G.standup1) after(80, EV_S4STAND);
}

static void use_spot(int id)
{
    char b[64];
    if (id >= I_BLIND) { sfx(SFX_BLIND); G.blinds[id - I_BLIND] ^= 1; return; }
    {   /* interaction sound (the action itself may still refuse below) */
        static const short SND[I_BLIND] = { SFX_GEN_ON, SFX_SWITCH, SFX_SWITCH, -2, SFX_WOOD, SFX_STOVE, -1, -1, SFX_MATCH, -1, -1,
                                            SFX_PEE, SFX_DOOR_CLOSE, SFX_FRIDGE, SFX_PICKUP, SFX_OVEN, SFX_MIC_OPEN, SFX_PICKUP, SFX_GAS, SFX_SHUTTER };
        int s = SND[id];
        if (s == -2) s = G.locked ? SFX_KEYTURN : G.door_open ? SFX_DOOR_CLOSE : SFX_DOOR_OPEN;
        if (id == I_GEN && G.gen && !(seq == 7 && G.power_out)) s = -1;
        if (s >= 0) sfx(s);
    }
    switch (id) {
    case I_GEN:
        if (seq == 7 && G.power_out) {
            if (!G.gas_hand) { sub(S("Seq7NeededGas"), 5); break; }
            fade(1); G.gas_hand = 0; G.gas_filled = 1; G.power_out = 0; G.gen = 1; G.light = 1; ctrl_hint = NULL;
            sub(T("ep3_controls.PowerBack"), 3); fade(0);
            sub_after(40, "Seq7Drenched");
            break;
        }
        fade(1); G.gen = 1; G.shed = 1; fade(0); break;
    case I_SHED: if (G.gen) G.shed ^= 1; break;
    case I_SWITCH: if (G.gen) G.light ^= 1; else sub(S(seq == 7 ? "Seq7PowerOut" : "GeneratorOn"), 4); break;
    case I_DOOR:
        if (G.locked) { G.locked = 0; sub("*click* the door unlocked", 3); }
        else {
            if (!G.door_open && seq == 5 && G.cult_walk) { sub(S("NotComfortableOpenDoor"), 4); break; }
            G.door_open ^= 1;
            if (G.door_open && seq == 6 && G.knock && !G.billy) {
                G.billy = 1; G.knock = 0;
                dlg_begin("Seq6Billy", NULL); play_until_done();
                dlg_begin("Seq6Billy2", NULL);
            }
        }
        break;
    case I_WOOD: G.wood_hand = 1; ctrl_hint = "A: place in stove (open it)"; break;
    case I_STOVE:
        if (G.stove_open && G.wood_hand) { G.wood_hand = 0; G.wood_in_stove = 1; ctrl_hint = NULL; break; }
        if (seq == 1 && G.to_trigger_stove && !G.stove_done) {
            sub(S("NoFirewood"), 4); G.stove_done = 1; G.repeat_stove = 0; after(40, EV_STOVECONVO);
        } else if (food_seq() && !G.fire && !G.wood_in_stove && !G.can_pick_wood) {
            sub(S("NoFirewood"), 4);
        }
        G.stove_open ^= 1;
        break;
    case I_BED:
        if (G.cass_hand && G.cass_cooked) { eat(); break; }
        switch (seq) {
        case 1:
            if (!G.report_done) sub(S("ReportTonight"), 4);
            else if (!G.fire) sub(S("GettingCold"), 4);
            else if (!all_blinds()) sub(S("CloseBoards"), 5);
            else if (G.door_open) sub(S("CloseDoor"), 5);
            else G.next = 2;
            break;
        case 2:
            if (!G.peed) sub(S("StillPee"), 4);
            else if (G.door_open) sub(S("CloseDoor"), 4);
            else G.next = 3;
            break;
        case 3: sub(S("ReportTonight"), 4); break;
        case 4:
            if (!G.convo1) sub(S("Seq4Connor"), 4);
            else if (!G.eaten) sub(S("Seq4Hungry"), 4);
            else if (!G.fire) sub(S("Seq4LightFire"), 4);
            else if (!G.report_done) sub(S("Seq4Report"), 4);
            else if (!all_blinds()) sub(S("CloseBoards"), 5);
            else if (G.door_open) sub(S("CloseDoor"), 4);
            else G.next = 5;
            break;
        case 5:
            if (!G.cult_gone || !G.skull) sub(S(rand() & 1 ? "BeingWatched" : "PresenceOutside"), 4);
            else if (!G.convo1) sub(S("Seq5Connor"), 4);
            else if (G.door_open) sub(S("CloseDoor"), 4);
            else G.next = 6;
            break;
        case 6:
            if (!G.eaten) sub(S("Seq4Hungry"), 4);
            else if (!G.report_done) sub(S("Seq4Report"), 4);
            else if (!G.fire) sub(S("Seq4LightFire"), 4);
            else if (G.door_open) sub(S("CloseDoor"), 4);
            else if (!G.connor_c) sub(S("Seq6ConnnorOnLine"), 4);
            else G.next = 7;
            break;
        case 7: sub(S(G.gas_filled ? "StillPee" : "Seq7PowerOut"), 4); break;
        case 8:
            if (!G.flash_off) sub(S("Seq8CheckCamp"), 4);
            else if (!G.hidden) {   /* hide under the bed until it leaves */
                G.hidden = 1; fade(1); consoleClear(); setBrightness(1, -16);
                printf("\n\n\n   (under the bed)\n\n"); wrap_print(T("ep3_controls.Hide"));
                for (int i = 0; i < 60 * 6; i++) swiWaitForVBlank();
                wrap_print(S("Seq5VeryStrange"));
                for (int i = 0; i < 60 * 5; i++) swiWaitForVBlank();
                setBrightness(3, 0); fade(0);
                G.run = 1; sub(S("Seq8MakeARun"), 6); ctrl_hint = "R: sprint";
            } else sub(S("Seq8MakeARun"), 4);
            break;
        }
        break;
    case I_DESK:
        { static int on_seq; if (on_seq != seq) { on_seq = seq; sfx(SFX_STARTUP); } }   /* the computer boots once a night */
        fade(1); mode = M_SEAT; hud_dirty = 1;
        if (seq == 1 && !G.radio_hint) { G.radio_hint = 1; after(10, EV_RADIOHINT); }
        fade(0);
        break;
    case I_MATCH:
        G.fire = 1; G.stove_open = 0; yarn_set("fireLit", 100);
        if (seq == 1) after(50, EV_SMOKE);
        if (seq == 6 && G.badguy && !G.connor_su) after(60, EV_S6CONNORSU);
        break;
    case I_THERMO: snprintf(b, sizeof b, "%s %d.%d F", S("CheckTemp"), G.temp10 / 10, G.temp10 % 10); sub(b, 5); break;
    case I_ANEMO: snprintf(b, sizeof b, "the wind speed was %d mph", G.wind); sub(b, 5); break;
    case I_PEE2: fade(1); G.peed = 1; fade(0); sub(S("pee"), 4); break;
    case I_POTTY:
        fade(1); G.peed = 1; fade(0);
        G.next = 8; break;
    case I_FRIDGE:
        if (seq == 6) { G.cass_hand = 1; sub(S("ColdFood"), 4); ctrl_hint = "Heat it in the microwave"; break; }
        G.ingr_hand = G.ingr_in + 1;
        if (!G.cass_first) { G.cass_first = 1; sub(S("Starving"), 4); sub_after(50, "Recipe"); }
        snprintf(b, sizeof b, "Holding: %s", INGR[G.ingr_hand]); ctrl_hint = NULL; sub(b, 2);
        break;
    case I_CASS:
        if (G.ingr_hand) {
            if (!G.convo1) { sub(S("HungryButReport"), 5); break; }
            G.ingr_hand = 0; G.ingr_in++;
            if (G.ingr_in == 5) sub("The casserole is ready for the oven.", 4);
        } else if (G.ingr_in >= 5) { G.cass_hand = 1; ctrl_hint = "Holding: casserole"; }
        else sub(S("MissingIngredient1"), 5);
        break;
    case I_OVEN: case I_MICRO:
        if (G.cass_hand) {
            G.cass_hand = 0; G.cass_where = id == I_OVEN ? CW_OVEN : CW_MICRO; G.cook_t = id == I_OVEN ? 20 * 40 : 20 * 15;
            ctrl_hint = NULL; if (id == I_OVEN) sub(S("ReportMeantime"), 5);
        } else if (G.cook_t) sub(S(id == I_OVEN ? "CheckingOven" : "Seq6FoodHeating"), 4);
        else { G.cass_where = CW_NONE; G.cass_hand = 1; G.cass_cooked = 1; ctrl_hint = "Holding: casserole (eat in bed)"; }
        break;
    case I_SKULL:
        G.skull = 1; sub(S("WhatToMake"), 5); sub_after(55, "ContactConnor"); break;
    case I_GAS: G.gas_hand = 1; ctrl_hint = "Holding: gas can"; break;
    case I_MOUNT:
        fade(1); setBrightness(3, 16); for (int i = 0; i < 6; i++) swiWaitForVBlank(); fade(0);
        G.flash_off = 1; sub("*FLASH*", 2); after(30, EV_S8COME);
        break;
    }
    hud_dirty = 1;
}

static void start_radio(void)
{
    if (!G.gen) { sub(S(seq == 7 ? "Seq7PowerOut" : "GeneratorOn"), 4); return; }
    switch (seq) {
    case 1:
        if (!G.first_radio_done) { sub(S("ConnorOnRadio"), 3); return; }
        if (G.stove_done && !G.fire && !G.firewood_once) {
            G.firewood_once = 1; G.can_pick_wood = 1; dlg_begin("Firewood", on_firewood_done);
        } else if (G.ask_report) { G.ask_report = 0; dlg_begin(G.report_done ? "ReportDone" : "Report", NULL); }
        else if (G.repeat_stove) dlg_begin("Stove", NULL);
        else dlg_begin("Nothing", NULL);
        return;
    case 2:
        if (!G.convo1) { G.convo1 = 1; dlg_begin("ConnorCheck", NULL); } else dlg_begin("Nothing", NULL);
        return;
    case 3:
        if (G.standup1 && !G.campers) { G.campers = 1; dlg_begin("Campers", NULL); sub_after(30, "SmokeCampfire"); after(80, EV_S3CONFIRM); }
        else if (G.standup2 && !G.checkcampers) { G.checkcampers = 1; dlg_begin("CheckCampers", NULL); }
        else dlg_begin("Nothing", NULL);
        return;
    case 4:
        if (!G.convo1) { G.convo1 = 1; dlg_begin("Seq4Start", NULL); G.can_pick_wood = 1; }
        else if (G.standup1 && !G.convo2) { G.convo2 = 1; dlg_begin(G.report_done ? "Seq4LastReportDone" : "Seq4Last", NULL); }
        else dlg_begin("Nothing", NULL);
        return;
    case 5:
        if (!G.convo1 && G.cult_gone && G.skull) { G.convo1 = 1; dlg_begin("Seq5Start", NULL); after(20, EV_S5SUBS2); }
        else dlg_begin("Nothing", NULL);
        return;
    case 6:
        if (!G.convo1 && G.standup1) { G.convo1 = 1; dlg_begin("Hiker1", NULL); }
        else if (G.convo1 && G.anxious == 2) { G.anxious = 0; dlg_begin("Hiker2", NULL); }
        else if (G.connor_su && !G.connor_c) { G.connor_c = 1; dlg_begin("Seq6Connor", NULL); }
        else dlg_begin("Nothing", NULL);
        return;
    case 7:
        if (!G.update_done && G.gas_filled) { G.update_done = 1; dlg_begin("Seq7Update", NULL); }
        else if (!G.update_done) dlg_begin("Seq7AskAgain", NULL);
        else dlg_begin("Nothing", NULL);
        return;
    case 8:
        if (!G.convo1) { G.convo1 = 1; dlg_begin("Seq8SitDown", NULL); after(300, EV_S8STAND); }
        else if (!G.convo2) dlg_begin("Seq8SitDownLoop", NULL);
        else dlg_begin("Nothing", NULL);
        return;
    }
}

/* ---- computer: service report form (sub screen, D-pad edited) ---- */
static const char *WEATHER[] = { "Cloud", "Mist", "Clear", "Wind", "Rain", "Heat Waves", "Blizzard", "Thunderstorm", "Dust Storm" };
static int f_row, f_temp, f_wind, f_weather, f_campers;
static const char *seq_time(void)
{
    static const char *t[] = { "", "11:32 PM", "3:26 AM", "7:04 PM", "11:15 PM", "3:10 AM", "4:30 PM", "9:18 PM", "10:05 PM" };
    return t[seq];
}
static void form_show(void)
{
    consoleClear();
    printf("  IRONBARK SERVICE REPORT\n\n");
    const char *lab[] = { "Reported by", "Time", "Temperature", "Wind (mph)", "Weather", "Campers", "[ SUBMIT ]" };
    for (int r = 0; r < 7; r++) {
        printf("%c %-12s", r == f_row ? '>' : ' ', lab[r]);
        switch (r) {
        case 0: printf("Tower 11"); break;
        case 1: printf("%s", seq_time()); break;
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
        int want_w = seq == 6 ? 2 : 2;
        if (G.report_done) { sub("Report already submitted.", 3); mode = M_SEAT; }
        else if (f_temp != G.temp10 || f_wind != G.wind) { sfx(SFX_ERROR); sub(S("MistakeReport"), 4); }
        else if (f_weather != want_w) { sfx(SFX_ERROR); sub(S("RightWeatherCondition"), 4); }
        else if (f_campers < 0) { sfx(SFX_ERROR); sub(S("MistakeReport"), 4); }
        else {
            G.report_done = 1; yarn_set("reportDone", 100); sfx(SFX_FORM);
            sub("Report submitted.", 3); mode = M_SEAT;
            if (seq == 1 && G.fire) after(30, EV_REPORTDONE2);
        }
    }
    if (d & KEY_B) mode = M_PC;
    hud_dirty = 1;
}

/* returns 1 when handled; *rearm asks to retry shortly (player busy / not inside) */
static void fire_event(int id, const char *s, int ok_walk, int *rearm)
{
    switch (id) {
    case EV_SUB: sub(S(s), 5); break;
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
    case EV_S2SUBS: sub(S("Seq2FirstNight"), 5); sub_after(60, "Seq2LeftAlone"); break;
    case EV_S3CONVO: if (!ok_walk) { *rearm = 1; break; } G.standup1 = 1; dlg_begin("Seq3Start", NULL); break;
    case EV_S3CONFIRM: if (!ok_walk) { *rearm = 1; break; } G.standup2 = 1; dlg_begin("Confirm", NULL); break;
    case EV_S4STAND: if (!ok_walk) { *rearm = 1; break; } G.standup1 = 1; dlg_begin("Seq4StandUp", NULL); break;
    case EV_S5WALK: if (!ok_walk) { *rearm = 1; break; } G.cult_walk = 1; sub(S("Seq5VeryStrange"), 6); break;   /* tnpc ends the walk */
    case EV_S5GONE: G.cult_walk = 0; G.cult_gone = 1; sub(S("PresenceOutside"), 5); break;
    case EV_S5SUBS2: if (dlg_active) { *rearm = 1; break; } sub(S("ConnorDidntMakeSense"), 5); sub_after(55, "NotMuchElse"); break;
    case EV_S6FLARE: sfx(SFX_FLARE); G.flare = 20; after(40, EV_S6START); break;
    case EV_S6START: if (!ok_walk) { *rearm = 1; break; } G.standup1 = 1; dlg_begin("Seq6Start", NULL); break;
    case EV_ANXIOUS:
        if (!ok_walk && mode != M_SEAT) { *rearm = 1; break; }
        if (G.anxious == 1) { G.anxious = 2; dlg_begin("Anxious", NULL); }
        break;
    case EV_KNOCK: if (G.knock && !G.door_open) { sfx(SFX_KNOCK); sub("*knock* *knock*", 2); *rearm = 1; } break;
    case EV_S6CONNORSU: if (!ok_walk) { *rearm = 1; break; } G.connor_su = 1; dlg_begin("Seq6ConnorStandUp", NULL); break;
    case EV_S7CONNOR: mode = M_SEAT; dlg_begin("Seq7Connor", NULL); after(250, EV_S7POWER); break;
    case EV_S7POWER:
        if (dlg_active) { *rearm = 1; break; }
        sfx(SFX_POWEROUT); G.power_out = 1; G.gen = 0; if (mode != M_WALK) mode = M_SEAT;
        sub(T("ep3_controls.PowerOut"), 3); sub_after(40, "Seq7PowerOut");
        break;
    case EV_S8STAND: if (!ok_walk) { *rearm = 1; break; } G.convo2 = 1; dlg_begin("Seq8StandUp", NULL); break;
    case EV_S8SUBS: sub(S("Seq8DidntKnow"), 5); sub_after(55, "Seq8TakeEvidence"); break;
    case EV_S8COME: sub(S("BeingWatched"), 5); break;
    }
}

static int typed(const char *t)   /* typewriter text on the sub screen; 1 = Start pressed (skip) */
{
    int n = strlen(t);
    for (int c = 1; c <= n; c++) {
        char b[200]; int m = c < 199 ? c : 199; memcpy(b, t, m); b[m] = 0;
        consoleClear(); printf("\n\n"); wrap_print(b);
        swiWaitForVBlank(); swiWaitForVBlank(); scanKeys();
        if (keysDown() & KEY_A) break;
        if (keysDown() & KEY_START) return 1;
    }
    consoleClear(); printf("\n\n"); wrap_print(t); printf("\n\n            [A]");
#ifdef AUTOPLAY
    for (int w = 0; w < 50; w++) swiWaitForVBlank();   /* recording build: advance on its own */
    return 0;
#endif
    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_START)));
    return (keysDown() & KEY_START) != 0;
}
static void story(const char *const *keys, int n)
{
    setBrightness(1, -16); setBrightness(2, 0);
    for (int i = 0; i < n; i++) if (typed(T(keys[i]))) break;
}
static void card(const char *a, const char *b)
{
    setBrightness(1, -16); setBrightness(2, 0);
    consoleClear();
    printf("\n\n\n\n\n\n     %s\n\n     %s\n", a, b);
    for (int i = 0; i < 150; i++) swiWaitForVBlank();
    consoleClear();
}

static void intro(void)
{
    char k[40];
    setBrightness(1, -16);
    for (int i = 1; i <= 17; i++) {
        snprintf(k, sizeof k, "ep4_intro.IntroText%d", i);
        if (typed(T(k))) break;
    }
    consoleClear();
    printf("\n\n\n\n\n     %s\n\n     %s\n", T("ep4_intro.ni"), T("ep4_intro.IronbarkLookout"));
    for (int i = 0; i < 150; i++) swiWaitForVBlank();
}

/* blocking dialogue on the sub screen (vignettes, back-to-back conversations) */
static void play_until_done(void)
{
    while (dlg_active) { swiWaitForVBlank(); scanKeys(); dlg_input(keysDown()); }
}
static void vignette(const char *title, const char *const *nodes, int n)
{
    card(title, "");
    for (int i = 0; i < n; i++) { dlg_begin(nodes[i], NULL); play_until_done(); }
}
#include "../inc/world.inc"
#include "../inc/walk.inc"
#include "../inc/rv.inc"
#include "../inc/diner.inc"
#include "../inc/firstscene.inc"
#include "../inc/trail.inc"
#include "../inc/camp.inc"
#include "../inc/trailend.inc"
#include "../inc/tnpc.inc"
static int load_tower(void)
{
    if (!L_in.data && (!load_level(&L_in, "nitro:/tower.bin") || !load_level(&L_out, "nitro:/outside.bin"))) return 0;
    if (!L_door.data) load_level(&L_door, "nitro:/door.bin");
    L_in.bud = L_out.bud = 1;
    return 1;
}
static void prologue(void)
{
    walk_free();   /* the tower's collision boxes and floor must not follow us into the 3D scenes */
#ifndef TRAIL_TEST
    first_scene();
#endif
    trail_scene();
    load_tower();
    if (!nboxes) load_col("nitro:/tower.col");
    if (!flr) load_floor("nitro:/tower.flr");
}
static void campsite(void)
{
    static const char *const s4[] = { "ep4_intro.Seq4Intro0", "ep4_intro.Seq4Intro1", "ep4_intro.Seq4Intro2", "ep4_intro.Seq4Intro3",
                                      "ep4_intro.Seq4Intro4", "ep4_intro.Seq4Intro5", "ep4_intro.Seq4Intro6" };
    card(T("ep3_controls.SmokeWoods"), "Lacey Trail");
    camp_scene();
    story(s4, 7);
    load_tower();
    if (!nboxes) load_col("nitro:/tower.col");
    if (!flr) load_floor("nitro:/tower.flr");
}
static void ending(void)
{
    char k[40];
    tnpc_free();
    trail_end_scene();
    setBrightness(3, 0);
    for (int i = 1; i <= 15; i++) { snprintf(k, sizeof k, "ep4_intro.Seq8Outro%d", i); if (typed(T(k))) break; }
    typed(T("ep4_intro.PleaseBeSafe"));
    music_set(SFX_CREDITS);
    consoleClear();
    printf("\n\n\n\n\n     %s\n\n     %s\n\n\n   Thanks for playing.\n\n   Press A", T("ep4_intro.ni"), T("ep4_intro.IronbarkLookout"));
    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & KEY_A));
    music_set(-1);
}

static const char *chapter(void)
{
    static const char *k[] = { "", "FirstNight", "FirstNight", "SmokeWoods", "ReportCampsite", "SomethingStrange", "LostHiker", "PowerOut", "Abandoned" };
    static char b[40]; snprintf(b, sizeof b, "ep3_controls.%s", k[seq]); return T(b);
}

static void hud_draw(int px, int fy, int pz)
{
    consoleClear();
    printf("%s  %s\n", chapter(), seq_time());
    printf("--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[12;0H");
    if (mode == M_SEAT) printf("A: Computer   X: Radio\nB: Get up\n");
    else if (prompt) printf("A: %s\n", prompt);
    printf("\x1b[18;0H");
    if (ctrl_hint) printf("%s\n", ctrl_hint);
    printf("Pad: move  Y+pad/stylus: look\nR: run  X: flashlight %s\n", G.flash ? "(on)" : "");
    if (seq == 6 || seq == 8) printf("L: binoculars\n");
#ifdef DEBUG_POS
    printf("%d %d %d s%d m%d\npoly %d vtx %d vb %d", px >> 8, fy >> 8, pz >> 8, seq, mode, stat_p, stat_v, stat_vb);
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
#define GROUND(fy) ((fy) < -40000)
#define CULT_X 9515        /* Seq 8 cult camp (north, ~134 m) */
#define CULT_Z (-549357)

/* per-sequence setup: flags, lighting state, spawn */
/* save: the night reached, on the flashcart's SD card (silently skipped when there is no FAT) */
#define SAVE_PATH "fat:/ironbark_lookout.sav"
static int save_read(void)
{
    FILE *f = fopen(SAVE_PATH, "rb"); int n = 0;
    if (f) { if (fread(&n, 4, 1, f) != 1 || n < 1 || n > 8) n = 0; fclose(f); }
    return n;
}
static void save_write(int n)
{
    if (n <= save_read()) return;
    FILE *f = fopen(SAVE_PATH, "wb");
    if (f) { fwrite(&n, 4, 1, f); fclose(f); }
}

static void seq_start(int n, int *px, int *pz, int *fy, int *yaw)
{
    save_write(n);
    static const char *const s6[] = { "ep4_intro.Seq6Intro0", "ep4_intro.Seq6Intro1", "ep4_intro.Seq6Intro2", "ep4_intro.Seq6Intro3" };
    static const char *const s7[] = { "ep4_intro.Seq7Intro1", "ep4_intro.Seq7Intro2", "ep4_intro.Seq7Intro3", "ep4_intro.Seq7Intro4" };
    static const char *const s8[] = { "ep4_intro.Seq8Intro1", "ep4_intro.Seq8Intro2", "ep4_intro.Seq8Intro3", "ep4_intro.Seq8Intro4", "ep4_intro.Seq8Intro5" };
    tnpc_free();
    seq = n;
    memset(&G, 0, sizeof G); memset(evq, 0, sizeof evq);
    G.temp10 = 440 + rand() % 31; G.wind = 17 + rand() % 4; G.flash = 1; G.door_ang = DOOR_CLOSED;
    f_row = 0; f_temp = -1; f_wind = -1; f_weather = 0; f_campers = -1;
    mode = M_WALK; ctrl_hint = NULL; subbuf[0] = 0; sub_t = 0;
    if (n > 1) { G.gen = G.shed = 1; G.fire = G.wood_in_stove = 1; G.report_done = 1; G.first_radio_done = 1; G.light = 1; }
    int lim = 0;
    *yaw = 0;
    switch (n) {
    case 1:
        intro(); prologue();
        card(T("ep3_controls.FirstNight"), T("ep4_intro.Night1"));
        G.locked = 1; after(50, EV_LONGHIKE);
        *px = -14 * 4096; *pz = -10 * 4096; *yaw = -11378; lim = 0x7FFFFFF;   /* end of the trail, facing the tower */
        break;
    case 2:
        card(T("ep4_intro.Night2"), T("ep4_intro.TimeIntro2"));
        for (int i = 0; i < 11; i++) G.blinds[i] = 1;
        G.light = 0;
        *px = 5440; *pz = 8353;
        dlg_begin("Unintellegible", NULL); after(20, EV_S2SUBS);
        break;
    case 3:
        card(T("ep3_controls.SmokeWoods"), seq_time());
        G.light = 0; G.report_done = 0;
        *px = -17709; *pz = -16729; *yaw = 8192;
        sub_after(50, "TimeMelts"); after(150, EV_S3CONVO);
        break;
    case 4:
        card(T("ep3_controls.ReportCampsite"), seq_time());
        G.fire = G.wood_in_stove = 0; G.report_done = 0; G.light = 1;
        *px = -12970; *pz = 11000; lim = -50000;
        sub(S("ConnorReportCampsite"), 6);
        break;
    case 5:
        card(T("ep3_controls.SomethingStrange"), seq_time());
        for (int i = 0; i < 11; i++) G.blinds[i] = 1;
        G.light = 0;
        *px = 5440; *pz = 8353; *yaw = 8192;
        sub(S("EyesOpen"), 5); after(120, EV_S5WALK);
        break;
    case 6:
        story(s6, 4);
        card(T("ep3_controls.LostHiker"), seq_time());
        G.fire = G.wood_in_stove = 0; G.report_done = 0;
        *px = -17709; *pz = 16038; *yaw = 8192;
        break;
    case 7:
        story(s7, 4);
        card(T("ep4_intro.2NightsLater"), T("ep4_intro.Seq7Time"));
        mode = M_SEAT; *px = 5600; *pz = -3000; *yaw = SEAT_YAW;
        after(10, EV_S7CONNOR);
        break;
    case 8:
        story(s8, 5);
        card(T("ep3_controls.Abandoned"), seq_time());
        for (int i = 0; i < 11; i++) G.blinds[i] = 1;
        *px = 5440; *pz = 8353;
        dlg_begin("Seq8Connor", NULL);
        break;
    }
    *fy = floor_at(*px, *pz, lim);
    if (*fy == NOFLOOR) *fy = lim ? -60000 : -5800;
    tnpc_load(n);
    setBrightness(1, 0);
    hud_dirty = 1;
}

static void run_game(int start)
{
    sfx_flush();
    if (start > 1) music_set(-1);   /* night 1 keeps the menu music through the intro letter */
    if (!nboxes) load_col("nitro:/tower.col");
    if (!flr) load_floor("nitro:/tower.flr");
    srand(REG_VCOUNT ^ (TIMER0_DATA << 3));
    int px, pz, fy, py, yaw, pitch = 0;
    seq_start(start, &px, &pz, &fy, &yaw);
    touchPosition t0, t1; int wasTouch = 0;
    const char *last_prompt = NULL;

    while (1) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        int s = sinLerp(yaw), c = cosLerp(yaw);
        int in = INSIDE(px, fy, pz);
        int bino = (seq == 6 || seq == 8) && mode == M_WALK && !dlg_active && (k & KEY_L);
        if (dlg_active) {
            dlg_input(kd);
            if (exit_radio) { exit_radio = 0; }
        } else if (mode == M_FORM) {
            form_input(kd, k);
            if (mode == M_FORM && hud_dirty) { form_show(); hud_dirty = 0; }
        } else if (mode == M_PC) {
            if (kd & KEY_A) {
                mode = M_FORM; hud_dirty = 1;
                if (seq == 1 && !G.connor_started) { G.connor_started = 1; after(50, EV_CONNOR); }
            } else if (kd & KEY_B) { mode = M_SEAT; hud_dirty = 1; }
            else if (hud_dirty) { pc_show(); hud_dirty = 0; }
        } else if (mode == M_SEAT) {
            if (kd & KEY_A) { if (G.gen) { mode = M_PC; hud_dirty = 1; } else sub(S(seq == 7 ? "Seq7PowerOut" : "GeneratorOn"), 4); }
            else if (kd & KEY_X) start_radio();
            else if (kd & KEY_B) { fade(1); mode = M_WALK; px = 5600; pz = -3000; yaw = SEAT_YAW; hud_dirty = 1; fade(0); }
        } else {
            int sp = (k & KEY_R) ? 900 : 450;
            int fwd = 0, str = 0, lk = bino ? 120 : 500;
            if (k & KEY_Y || bino) {   /* Y held + D-pad = look (binoculars: pad looks, slowly) */
                yaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * lk;
                pitch += (((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0)) * (lk * 3 / 5);
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
            {   /* footsteps: one every ~0.7 m walked */
                static int stepd, stepi;
                stepd += abs(px - ox) + abs(pz - oz);
                if (stepd > 2900) { stepd = 0; stepi = (stepi + 1) & 3; static const short W[] = { SFX_STEP_WOOD1, SFX_STEP_WOOD2, SFX_STEP_WOOD3, SFX_STEP_WOOD4 }, GR[] = { SFX_STEP_GRASS1, SFX_STEP_GRASS2, SFX_STEP_GRASS3, SFX_STEP_GRASS4 };
                                     sfx_play(GROUND(fy) ? GR[stepi] : W[stepi], 110); }
            }
            if (kd & KEY_SELECT) {   /* debug: cycle teleport spots */
                /* generator, outside the door, inside the door, stove, desk, bed, thermometer */
                static const int tp[][4] = {{-12970, 11000, 0, -50000}, {-17500, 4354, -8192, 0}, {-9000, 4354, 8192, 0}, {-10300, 5500, 0, 0},
                                            {6000, -2000, 0, 0}, {6500, 6612, -8192, 0}, {2256, -8500, 0, 0}};
                static int ti = 0;
                px = tp[ti][0]; pz = tp[ti][1]; yaw = tp[ti][2];
                fy = floor_at(px, pz, tp[ti][3]); if (fy == NOFLOOR) fy = -5800;
                ti = (ti + 1) % 7;
                hud_dirty = 1;
            }
            if (kd & KEY_X) { G.flash ^= 1; sfx(G.flash ? SFX_FLASH_ON : SFX_FLASH_OFF); hud_dirty = 1; }
#ifdef DEBUG_POS
            if ((kd & KEY_L) && seq == 6) { G.eaten = G.report_done = 1; G.knock = 1; px = WX(306.5); pz = WZ(311.5); fy = WY(27.2); yaw = yaw_to(WX(306) - px, WZ(305.5) - pz); }
            else if ((kd & KEY_L) && seq == 8) { G.convo2 = 1; for (int i = 0; i < 11; i++) G.blinds[i] = 0; px = WX(317.3); pz = WZ(329.4); fy = WY(43.72); yaw = yaw_to(CULT_X - px, CULT_Z - pz); pitch = 250; }
            else if (kd & KEY_L) { px = WX(310.9); pz = WZ(323.2); fy = WY(43.72); yaw = yaw_to(WX(312) - px, WZ(330) - pz); }
#endif
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
            /* whole-area actions on the ground below the tower */
            int area = 0;
            if (best < 0 && GROUND(fy)) {
                if (seq == 3 && G.checkcampers) { prompt = "Hike to the smoke"; area = 1; }
                else if (seq == 8 && G.run) { prompt = "Run for the truck"; area = 2; }
            }
            if (prompt != last_prompt) { last_prompt = prompt; hud_dirty = 1; }
            if ((kd & KEY_A) && best >= 0) use_spot(SPOTS[best].id);
            else if ((kd & KEY_A) && area) G.next = area == 1 ? 4 : 9;
            if (in && !G.inside_first) {
                G.inside_first = 1;
                if (seq == 1) { sub(S("Seq1Cabin"), 6); after(60, EV_HOME); }
            }
            if (GROUND(fy) && !G.ground_first) {
                G.ground_first = 1;
                if (seq == 6 && G.eaten && G.report_done && !G.badguy) {
                    G.badguy = 1; G.can_pick_wood = 1;
                    dlg_begin("Seq6BadGuy", NULL); sub_after(10, "CreepyVibes");
                    if (G.fire) after(60, EV_S6CONNORSU);
                } else if (seq == 6 && !G.eaten) sub(S("Seq6Hungry"), 4);
                else if (seq == 6 && !G.report_done) sub(S("Seq6Report"), 4);
            }
            if (!GROUND(fy)) G.ground_first = 0;
            if (seq == 6 && bino && !G.bino_used) { G.bino_used = 1; after(50, EV_S6FLARE); }
            if (seq == 8 && bino && G.convo2 && !G.cult_seen) {   /* looking north at the cult camp */
                int dx = CULT_X - px, dz = CULT_Z - pz;
                long long dot = (long long)dx * (-s) + (long long)dz * (-c);
                long long len = (long long)(abs(dx) + abs(dz));
                if (dot * 10 > len * 4096LL * 8) { sfx(SFX_SCARE); sfx(SFX_CULT_NOTICED); G.cult_seen = 1; after(10, EV_S8SUBS); }
            }
        }
        if ((k & KEY_TOUCH) && mode == M_WALK && !dlg_active) {
            touchRead(&t1);
            if (wasTouch) { yaw -= (t1.px - t0.px) * (bino ? 25 : 90); pitch += (t1.py - t0.py) * (bino ? 20 : 70); }
            t0 = t1; wasTouch = 1;
        } else wasTouch = 0;
        if (pitch > 4000) pitch = 4000; if (pitch < -4000) pitch = -4000;

        /* timers */
        tnpc_update(px, pz);
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        if (G.cook_t && --G.cook_t == 0) sub(S(G.cass_where == CW_OVEN ? "SmellsGood" : "Seq6FoodHeating"), 4);
        if (G.flare) G.flare--;
        int ok_walk = in && mode == M_WALK && !dlg_active;
        for (int i = 0; i < 12; i++) if (evq[i].id && --evq[i].t <= 0) {
            int id = evq[i].id, rearm = 0; const char *es = evq[i].s; evq[i].id = 0;
            if (dlg_active && id != EV_SUB && id != EV_LONGHIKE && id != EV_HOME && id != EV_RADIOHINT && id != EV_KNOCK) rearm = 1;
            else fire_event(id, es, ok_walk, &rearm);
            if (rearm) { evq[i].id = id; evq[i].s = es; evq[i].t = id == EV_KNOCK ? 50 : 10; }
        }
        int target = G.door_open ? DOOR_OPEN : DOOR_CLOSED;   /* 0.5 s swing */
        if (G.door_ang != target) { int d = target - G.door_ang, st = 850; G.door_ang += d > st ? st : d < -st ? -st : d; }

        /* lighting per time of day; flashlight is a view-space light; cabin light when powered */
        int cabin = in && G.gen && G.light;
        u32 sun = RGB15(6, 7, 11), amb = RGB15(4, 4, 6);
        if (seq == 3) { sun = RGB15(28, 19, 11); amb = RGB15(11, 8, 7); sky(20, 12, 8); }
        else if (seq == 6) { sun = RGB15(28, 28, 25); amb = RGB15(13, 13, 13); sky(15, 20, 27); }
        else if (G.flare) { sun = RGB15(31, 8, 6); amb = RGB15(12, 3, 3); sky(14, 3, 2); }
        else sky(2, 2, 3);
        if (seq == 7) { sun = RGB15(4, 5, 8); amb = RGB15(3, 3, 5); sky(3, 3, 4); }
        sky_use(seq == 6 || G.flare ? NULL : seq == 3 ? "evening" : seq == 2 ? "cloud" : seq == 4 || seq == 7 ? "moonmid" : "moon");
        g_amb = cabin ? RGB15(13, 12, 10) : amb;
        /* ambience loop + generator hum (louder when close to it) */
        amb_set(seq == 3 ? SFX_AMB_EVENING : seq == 7 ? SFX_AMB_RAIN : seq == 8 && G.cult_seen ? SFX_AMB_DRONE : seq == 6 ? SFX_AMB_WIND : SFX_AMB_NIGHT);
        amb2_set(seq == 7 || seq == 6 || seq == 3 ? -1 : SFX_CRICKETS);
        {
            int dx = (px + 12970) >> 12, dz = (pz - 11000) >> 12, d = abs(dx) + abs(dz) + (in ? 6 : 0);
            gen_snd(G.gen, d > 40 ? 20 : 180 - d * 4);
        }
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FOG | (G.flash ? POLY_FORMAT_LIGHT1 : 0);
        if (bino) glDisable(GL_FOG); else glEnable(GL_FOG);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(bino ? 14 : 70, 256.0 / 192.0, 0.05, bino ? 200 : 80);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLight(1, RGB15(22, 21, 17), 0, 0, floattov10(-0.99));
        int cx = px, cz = pz, cyaw = yaw, cpitch = pitch;
        if (mode != M_WALK) { cx = SEAT_X; cz = SEAT_Z; cyaw = SEAT_YAW; cpitch = 300; }
        glRotateXi(cpitch);
        glRotateYi(-cyaw);
        sky_dome(bino ? 200 : 80);
        py = mode != M_WALK ? SEAT_Y : fy + EYE;
        glTranslatef32(-cx, -py, -cz);
        glLight(0, cabin ? RGB15(31, 29, 24) : sun, floattov10(0.4), floattov10(-0.8), floattov10(-0.3));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        in = INSIDE(cx, py - EYE, cz);
        for (int pass = 0; pass < 2; pass++) {
            if (in) draw_level(&L_in, pass);
            draw_level(&L_out, pass);
            if (L_door.data) draw_level_rot(&L_door, pass, G.door_ang);
            tnpc_draw(pass);
            if (!pass) glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        }
        while (GFX_STATUS & BIT(27)) ;
        stat_v = GFX_VERTEX_RAM_USAGE; stat_p = GFX_POLYGON_RAM_USAGE;
        glFlush(0);

        /* fixed 20 fps: frame work is absorbed into the 3-vblank budget instead of added to it */
        stat_vb = vb_count; if (stat_vb < 3) { while (vb_count < 3) swiWaitForVBlank(); }
        vb_count = 0;
#ifdef DEBUG_POS
        { static int t; if (++t >= 20) { t = 0; hud_dirty = 1; } }
#endif
        if (hud_dirty && !dlg_active && (mode == M_WALK || mode == M_SEAT)) { hud_draw(px, fy, pz); hud_dirty = 0; }
        if (G.next && !dlg_active) {
            int nx = G.next;
            fade(1); amb_set(-1); gen_snd(0, 0); vo_play(-1);
            if (nx == 4) campsite();
            if (nx == 9) { ending(); break; }
            setBrightness(3, 0);
            seq_start(nx, &px, &pz, &fy, &yaw); pitch = 0; last_prompt = NULL;
            fade(0);
        }
        if ((keysDown() & KEY_START) && !dlg_active) break;
    }
    amb_set(-1); gen_snd(0, 0); vo_play(-1);
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

/* some flashcart menus launch homebrew without argv, so NitroFS can't learn where the ROM is:
   look for it on the SD card (root and one folder deep), matched against the header the loader left in RAM */
#include <fat.h>
#include <dirent.h>
static int nitro_nds;   /* .nds files looked at, for the error message */
static int nitro_try(const char *path)
{
    if (!nitroFSInit(path)) return 0;
    nitro_nds++;
    FILE *f = fopen("nitro:/rv.bin", "rb");   /* it's this game if its filesystem has our files */
    if (f) { fclose(f); return 1; }
    nitroFSExit(); return 0;
}
static int nitro_scan(const char *dir, int depth)
{
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; char p[256]; int ok = 0;
    while (!ok && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s%s", dir, e->d_name);
        int n = strlen(e->d_name);
        if (e->d_type == DT_DIR) { if (depth && strcasecmp(e->d_name, "moonshl2") && strcasecmp(e->d_name, "R4iMenu")) { strcat(p, "/"); ok = nitro_scan(p, depth - 1); } }
        else if (n > 4 && !strcasecmp(e->d_name + n - 4, ".nds")) ok = nitro_try(p);
    }
    closedir(d); return ok;
}
static int nitro_find(void)
{
    if (!fatInitDefault()) { printf("SD card not readable (DLDI)\n"); return 0; }
    static const char *const guess[] = { "fat:/IronbarkLookout.nds", "fat:/rom.nds", "fat:/ironbark.nds" };
    for (int i = 0; i < 3; i++) if (nitro_try(guess[i])) return 1;
    if (nitro_scan("fat:/", 1)) return 1;
    printf("checked %d .nds files\n", nitro_nds); return 0;
}

int main(void)
{
    init_hw();
    irqSet(IRQ_VBLANK, on_vblank); irqEnable(IRQ_VBLANK);
    if (!nitroFSInit(NULL) && !nitro_find()) { printf("nitroFS init failed\nput the .nds in the card's root\n"); while (1) swiWaitForVBlank(); }
    mmInitDefault("nitro:/soundbank.bin");
    if (!yarn_load("nitro:/story.bin")) { printf("story.bin missing\n"); while (1) swiWaitForVBlank(); }
    { long sz; txt = (char *)load_file("nitro:/text.bin", &sz); ntxt = txt ? ((u32 *)txt)[1] : 0; }
    int sel = 0;
#ifdef CAMP_TEST
    campsite();
#endif
#if defined(TRAIL_WALK) || defined(TRAIL_DRIVE)
    trail_scene();
#endif
#ifdef END_TEST
    ending();
#endif
    for (;;) {
        music_set(SFX_MENU_MUSIC);
        int saved = save_read();
        consoleClear();
        printf("FEARS TO FATHOM\nIRONBARK LOOKOUT (DS)\n\n%c New game\n%c Chapter select\n%c Read story nodes\n", sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ', sel == 2 ? '>' : ' ');
        if (saved > 1) printf("%c Continue (Night %d)\n", sel == 3 ? '>' : ' ', saved);
        int nsel = saved > 1 ? 4 : 3;
        do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
        u32 d = keysDown();
        if (d & KEY_UP) sel = (sel + nsel - 1) % nsel;
        if (d & KEY_DOWN) sel = (sel + 1) % nsel;
        if (d & KEY_A) {
            int start = 1;
            if (sel == 2) { story_reader(); continue; }
            if (sel == 3) start = saved;
            if (sel == 1) {
                start = saved > 1 ? saved : 2;
                for (;;) {
                    consoleClear(); printf("CHAPTER SELECT\n\n  < Night %d >\n\nLeft/Right: pick  A: start  B: back\n", start);
                    do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
                    u32 e = keysDown();
                    if (e & KEY_LEFT) start = start > 1 ? start - 1 : 8;
                    if (e & KEY_RIGHT) start = start < 8 ? start + 1 : 1;
                    if (e & KEY_B) { start = 0; break; }
                    if (e & KEY_A) break;
                }
                if (!start) continue;
            }
            if (!load_tower()) { printf("no level\n"); continue; }
            run_game(start);
        }
    }
}
