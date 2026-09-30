/* Chunked outdoor worlds (WLD1, see tools/bake_world.py). Included from main.c.
   World frame: DS x = unity x, y = unity y, z = -unity z; 20.12 fixed. */

typedef struct { u32 w, h, npal, paloff, texoff; } WTex;
typedef struct { s32 cx, cy, cz, scale; u32 ng, grpoff, flooroff, radius; } WChunk;
typedef struct { s32 tex; u32 colalpha, dlwords, dloff; } WGroup;
typedef struct {
    u8 *d; u32 ntex, nchunk, nbox; s32 cs, gx0, gz0; u32 gnx, gnz;
    u16 *grid; WTex *tex; WChunk *ch; Box *box;
    int texid[192];
    u8 *blob;                           /* group dl offsets are relative to the blob after the box table */
} World;
static World W;
static int w_far = 72 * 4096;          /* draw distance (fog end) */
static int w_drawn;                    /* chunks drawn last frame */

static void world_free(void)
{
    if (!W.d) return;
    for (u32 i = 0; i < W.ntex && i < 192; i++) if (W.texid[i]) glDeleteTextures(1, &W.texid[i]);
    free(W.d); memset(&W, 0, sizeof W);
}

static int world_load(const char *path)
{
    world_free();
    u8 *d = load_file(path, NULL);
    if (!d || memcmp(d, "WLD1", 4)) { free(d); return 0; }
    u32 *h = (u32 *)d;
    W.d = d; W.ntex = h[1]; W.nchunk = h[2]; W.nbox = h[3]; W.cs = h[4]; W.gx0 = h[5]; W.gz0 = h[6]; W.gnx = h[7]; W.gnz = h[8];
    W.grid = (u16 *)(d + h[9]); W.tex = (WTex *)(d + h[10]); W.ch = (WChunk *)(d + h[11]); W.box = (Box *)(d + h[12]);
    W.blob = d + h[12] + W.nbox * sizeof(Box);
    for (u32 i = 0; i < W.ntex && i < 192; i++) {
        WTex *t = &W.tex[i];
        glGenTextures(1, &W.texid[i]);
        glBindTexture(0, W.texid[i]);
        u32 fl = TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
        if (t->npal >> 16) fl |= GL_TEXTURE_COLOR0_TRANSPARENT;
        if (!glTexImage2D(0, 0, GL_RGB256, texsize_enum(t->w), texsize_enum(t->h), 0, fl, d + t->texoff)) { W.texid[i] = 0; continue; }
        glColorTableEXT(0, 0, t->npal & 0xFFFF, 0, 0, (u16 *)(d + t->paloff));
    }
    return 1;
}

static WChunk *world_chunk_at(int x, int z, int *ci, int *cj)
{
    int ux = x, uz = -z;
    int i = (ux - W.gx0) / W.cs, j = (uz - W.gz0) / W.cs;
    if (ux < W.gx0 || uz < W.gz0 || i >= (int)W.gnx || j >= (int)W.gnz) return NULL;
    u16 k = W.grid[j * W.gnx + i];
    if (k == 0xFFFF) return NULL;
    if (ci) { *ci = i; *cj = j; }
    return &W.ch[k];
}

/* ground height (20.12) at DS x,z; NOFLOOR outside the baked area */
static int world_floor(int x, int z)
{
    int i, j;
    WChunk *c = world_chunk_at(x, z, &i, &j);
    if (!c) return NOFLOOR;
    int lx = (x - (W.gx0 + i * W.cs)) >> 13, lz = (-z - (W.gz0 + j * W.cs)) >> 13;   /* 2 m cells */
    if (lx < 0) lx = 0; if (lx > 15) lx = 15; if (lz < 0) lz = 0; if (lz > 15) lz = 15;
    s16 hgt = ((s16 *)(W.d + c->flooroff))[lz * 16 + lx];
    return hgt == -32768 ? NOFLOOR : hgt * 128;
}

static void world_collide(int *px, int *pz, int feet, int head, int rad)
{
    for (int it = 0; it < 2; it++)
        for (u32 i = 0; i < W.nbox; i++) {
            Box *b = &W.box[i];
            if (b->y1 <= feet + 1200 || b->y0 >= head) continue;
            if (*px + rad <= b->x0 || *px - rad >= b->x1 || *pz + rad <= b->z0 || *pz - rad >= b->z1) continue;
            int l = *px + rad - b->x0, r = b->x1 - (*px - rad), d = *pz + rad - b->z0, u = b->z1 - (*pz - rad);
            int m = l; if (r < m) m = r; if (d < m) m = d; if (u < m) m = u;
            if (m == l) *px -= l; else if (m == r) *px += r; else if (m == d) *pz -= d; else *pz += u;
        }
}

/* draw every chunk in range and roughly inside the view cone (camera at cx,cz looking along yaw) */
static void world_draw(int cx, int cz, int yaw)
{
    if (!W.d) return;
    int s = sinLerp(yaw), c = cosLerp(yaw);
    int ux = cx, uz = -cz;
    int r = w_far / W.cs + 1;
    int ci = (ux - W.gx0) / W.cs, cj = (uz - W.gz0) / W.cs;
    static u16 vis[96]; static int vd[96]; int nv = 0;
    for (int j = cj - r; j <= cj + r; j++) {
        if (j < 0 || j >= (int)W.gnz) continue;
        for (int i = ci - r; i <= ci + r; i++) {
            if (i < 0 || i >= (int)W.gnx) continue;
            u16 k = W.grid[j * W.gnx + i];
            if (k == 0xFFFF) continue;
            WChunk *ch = &W.ch[k];
            int dx = (ch->cx - cx) >> 8, dz = (ch->cz - cz) >> 8, rad = ch->radius >> 8;     /* 1/16 m */
            int far = w_far >> 8;
            if (dx * dx + dz * dz > (far + rad) * (far + rad)) continue;
            int along = (dx * -s + dz * -c) >> 12, side = (dx * c - dz * s) >> 12;
            if (along < -rad) continue;
            if (abs(side) > along + rad + (rad >> 1)) continue;      /* ~90 deg horizontal view */
            if (nv < 96) { vd[nv] = dx * dx + dz * dz; vis[nv++] = k; }
        }
    }
    w_drawn = nv;
    /* near to far: past the hardware's 2048-polygon list the last ones sent are dropped, so let that be the fogged distance */
    for (int a = 1; a < nv; a++) {
        int d = vd[a]; u16 v = vis[a], b = a;
        while (b > 0 && vd[b - 1] > d) { vd[b] = vd[b - 1]; vis[b] = vis[b - 1]; b--; }
        vd[b] = d; vis[b] = v;
    }
    for (int pass = 0; pass < 2; pass++)
        for (int n = 0; n < nv; n++) {
            WChunk *ch = &W.ch[vis[n]];
            WGroup *g = (WGroup *)(W.d + ch->grpoff);
            glPushMatrix();
            glTranslatef32(ch->cx, ch->cy, ch->cz);
            glScalef32(ch->scale, ch->scale, ch->scale);
            for (u32 q = 0; q < ch->ng; q++, g++) {
                int a = g->colalpha >> 16;
                if ((a < 31) != pass) continue;
                int cut = g->tex >= 0 && (W.tex[g->tex].npal >> 16);
                glPolyFmt(POLY_ALPHA(a) | (cut || a < 31 ? POLY_CULL_NONE : POLY_CULL_BACK) | g_lights | POLY_ID(a < 31 ? 2 + (q & 31) : 1));
                glBindTexture(0, g->tex >= 0 ? W.texid[g->tex] : 0);
                glMaterialf(GL_DIFFUSE, (g->colalpha & 0x7FFF) | BIT(15));
                glMaterialf(GL_AMBIENT, g_amb);
                glCallList((u32 *)(W.blob + g->dloff));
            }
            glPopMatrix(1);
        }
}

/* ---- models: LVL1 files drawn at a transform (RV, NPCs, props). Geometry is relative to the bake pivot. */
static int load_model(Level *L, const char *path)
{
    if (!load_level(L, path)) return 0;
    L->tx = L->ty = L->tz = 0;
    return 1;
}
static void level_free(Level *L)
{
    if (!L->data) return;
    for (u32 i = 0; i < L->n && i < 160; i++) if (L->tex[i]) glDeleteTextures(1, &L->tex[i]);
    free(L->data); memset(L, 0, sizeof *L);
}
static void draw_model(Level *L, int x, int y, int z, int yaw, int pass)
{
    glPushMatrix();
    glTranslatef32(x, y, z);
    glRotateYi(yaw);
    draw_level(L, pass);
    glPopMatrix(1);
}
