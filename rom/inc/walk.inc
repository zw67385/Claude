/* First-person walking for the 3D story scenes (diner, trail, camp). Included from main.c.
   Walls: WAL1 blocked-cell bitmap (0.25 m cells); floor: FLR1 heightfield (floor_at). */

#define EYE 6300        /* eye height 1.54 m */
#define STEPUP 1843     /* 0.45 m */
#define U(v) ((int)((v) * 4096))
/* angle (32768 per turn) of the vector (x = cos part, y = sin part) */
static int iatan2(int y, int x)
{
    if (!x && !y) return 0;
    if (abs(x) >= abs(y)) { int a = atanLerp((s32)(((long long)y << 12) / x)); return x > 0 ? a : a + 16384; }
    int a = atanLerp((s32)(((long long)x << 12) / y)); return y > 0 ? 8192 - a : -8192 - a;
}
/* yaw whose forward (-sin, -cos) points along (dx, dz) */
static int yaw_to(int dx, int dz) { return iatan2(-dx, -dz); }

static u8 *wal; static s32 wgx0, wgz0; static u32 wnx, wnz;
static int load_wal(const char *path)
{
    if (!(wal = load_file(path, NULL)) || memcmp(wal, "WAL1", 4)) return 0;
    s32 *h = (s32 *)wal; wgx0 = h[1]; wgz0 = h[2]; wnx = h[3]; wnz = h[4];
    return 1;
}
static int wall_at(int x, int z)
{
    if (!wal) return 0;
    int ix = (x - wgx0) >> 10, iz = (z - wgz0) >> 10;
    if (ix < 0 || iz < 0 || ix >= (int)wnx || iz >= (int)wnz) return 1;
    int b = iz * wnx + ix;
    return (wal[20 + (b >> 3)] >> (b & 7)) & 1;
}
#define WR 700   /* body radius against walls, 0.17 m */
static int wall_hit(int x, int z)
{
    return wall_at(x - WR, z - WR) || wall_at(x + WR, z - WR) || wall_at(x - WR, z + WR) || wall_at(x + WR, z + WR);
}
static void walk_free(void)
{
    free(wal); wal = NULL;
    free(flr); flr = NULL;
    free(boxes); boxes = NULL; nboxes = 0;
}

typedef struct { int x, z, fy, yaw, pitch; int stepd, stepi, grass, wasTouch; touchPosition t0; } Walker;
/* D-pad walks/strafes, Y+pad or stylus looks, R runs. Returns 1 if the player moved. */
static int walker_update(Walker *w, u32 k)
{
    int s = sinLerp(w->yaw), c = cosLerp(w->yaw);
    int fwd = 0, str = 0;
    if (k & KEY_Y) {
        w->yaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * 500;
        w->pitch += (((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0)) * 300;
    } else {
        fwd = ((k & KEY_UP) ? 1 : 0) - ((k & KEY_DOWN) ? 1 : 0);
        str = ((k & KEY_RIGHT) ? 1 : 0) - ((k & KEY_LEFT) ? 1 : 0);
    }
    if (k & KEY_TOUCH) {
        touchPosition t; touchRead(&t);
        if (w->wasTouch) { w->yaw -= (t.px - w->t0.px) * 90; w->pitch += (t.py - w->t0.py) * 70; }
        w->t0 = t; w->wasTouch = 1;
    } else w->wasTouch = 0;
    if (w->pitch > 4000) w->pitch = 4000; if (w->pitch < -4000) w->pitch = -4000;
    if (!fwd && !str) return 0;
    int sp = (k & KEY_R) ? 800 : 420;
    int ox = w->x, oz = w->z;
    int nx = ox + (((-s * fwd + c * str) * sp) >> 12), nz = oz + (((-c * fwd - s * str) * sp) >> 12);
    /* walls: slide along whichever axis is free */
    if (wall_hit(nx, nz)) {
        if (!wall_hit(nx, oz)) nz = oz; else if (!wall_hit(ox, nz)) nx = ox; else { nx = ox; nz = oz; }
    }
    if (nboxes) collide(&nx, &nz, w->fy, w->fy + 6600);
    if (wall_hit(nx, nz) && !wall_hit(ox, oz)) { nx = ox; nz = oz; }
    int f = floor_at(nx, nz, w->fy + STEPUP);
    if (f == NOFLOOR || f < w->fy - 2900) {
        int fx = floor_at(nx, oz, w->fy + STEPUP), fz = floor_at(ox, nz, w->fy + STEPUP);
        if (fx != NOFLOOR && fx >= w->fy - 2900) { nz = oz; f = fx; }
        else if (fz != NOFLOOR && fz >= w->fy - 2900) { nx = ox; f = fz; }
        else { nx = ox; nz = oz; f = floor_at(nx, nz, w->fy + STEPUP); }
    }
    w->x = nx; w->z = nz;
    if (f != NOFLOOR) { if (f > w->fy) w->fy = f; else { w->fy -= 800; if (w->fy < f) w->fy = f; } }
    w->stepd += abs(nx - ox) + abs(nz - oz);
    if (w->stepd > 2900) {
        static const short WD[] = { SFX_STEP_WOOD1, SFX_STEP_WOOD2, SFX_STEP_WOOD3, SFX_STEP_WOOD4 },
                           GR[] = { SFX_STEP_GRASS1, SFX_STEP_GRASS2, SFX_STEP_GRASS3, SFX_STEP_GRASS4 };
        w->stepd = 0; w->stepi = (w->stepi + 1) & 3;
        sfx_play(w->grass ? GR[w->stepi] : WD[w->stepi], 110);
    }
    return 1;
}

/* interactable points: nearest one in reach and in front of the camera */
typedef struct { int x, y, z; const char *label; int id; } Hot;
static int hot_pick(const Walker *w, const Hot *h, int n, int reach)
{
    int s = sinLerp(w->yaw), c = cosLerp(w->yaw), best = -1;
    long long bd = (long long)reach * reach;
    for (int i = 0; i < n; i++) {
        if (!h[i].label) continue;
        long long dx = h[i].x - w->x, dy = h[i].y - (w->fy + EYE), dz = h[i].z - w->z;
        long long d2 = dx * dx + dy * dy / 4 + dz * dz;
        if (d2 >= bd || dx * (-s) + dz * (-c) <= 0) continue;
        best = i; bd = d2;
    }
    return best;
}

/* camera at (x, y, z) looking along yaw / pitch */
static void cam_setup(int x, int y, int z, int yaw, int pitch, float far)
{
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.05, far);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glRotateXi(pitch);
    glRotateYi(-yaw);
    glTranslatef32(-x, -y, -z);
}
/* smooth approach of an NPC toward (tx, tz) at speed sp per frame; returns 1 when there */
static int npc_step(int *x, int *z, int *yaw, int tx, int tz, int sp)
{
    int dx = tx - *x, dz = tz - *z, d = abs(dx) > abs(dz) ? abs(dx) + abs(dz) / 2 : abs(dz) + abs(dx) / 2;
    if (d <= sp) { *x = tx; *z = tz; return 1; }
    *x += (int)((long long)dx * sp / d); *z += (int)((long long)dz * sp / d);
    int want = yaw_to(dx, dz), dy = (short)(want - *yaw);
    *yaw += dy / 4;
    return 0;
}
