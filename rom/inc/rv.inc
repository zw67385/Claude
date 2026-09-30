/* RV driving shared by the story drives (First Scene, Trail Start). Included from main.c after world.inc.
   Positions are DS world units (unity x, y, -z; 20.12). */
typedef struct {
    int x, z, y, yaw, spd, steer, pitch_rv, lookyaw, lookpitch, maxspd, wasTouch;
    touchPosition t0;
} RV;
static Level L_rv;

static void rv_place(RV *r, int x, int z, int yaw)
{
    memset(r, 0, sizeof *r);
    r->x = x; r->z = z; r->yaw = yaw; r->lookpitch = 1100; r->maxspd = U(16.0) / 20;
    r->y = world_floor(x, z); if (r->y == NOFLOOR) r->y = 0;
}
/* controls + physics for one frame (lock = dialogue up: no driving input) */
static void rv_update(RV *r, u32 k, int lock)
{
    int s = sinLerp(r->yaw), c = cosLerp(r->yaw);
    int gas = !lock && (k & (KEY_UP | KEY_A)) && !(k & KEY_Y);
    int brk = !lock && (k & (KEY_DOWN | KEY_B)) && !(k & KEY_Y);
    int st = lock || (k & KEY_Y) ? 0 : ((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0);
    if (k & KEY_Y) {
        r->lookyaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * 400;
        r->lookpitch += (((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0)) * 300;
    } else if (!(k & KEY_TOUCH)) { r->lookyaw -= r->lookyaw / 8; }
    if (k & KEY_TOUCH) {
        touchPosition t1; touchRead(&t1);
        if (r->wasTouch) { r->lookyaw -= (t1.px - r->t0.px) * 90; r->lookpitch += (t1.py - r->t0.py) * 70; }
        r->t0 = t1; r->wasTouch = 1;
    } else r->wasTouch = 0;
    if (r->lookyaw > 12000) r->lookyaw = 12000; if (r->lookyaw < -12000) r->lookyaw = -12000;
    if (r->lookpitch > 3000) r->lookpitch = 3000; if (r->lookpitch < -4000) r->lookpitch = -4000;
    /* speed (DS units / frame), bicycle-model steering */
    int acc = U(3.0) / 400, dec = U(8.0) / 400, drag = U(0.8) / 400, spd = r->spd;
    if (gas) spd += acc; else if (brk) spd -= spd > 0 ? dec : acc / 2; else spd -= spd > drag ? drag : spd < -drag ? -drag : spd;
    if (spd > r->maxspd) spd -= dec; if (spd < -U(3.0) / 20) spd = -U(3.0) / 20;
    r->steer += st * 110; if (!st) r->steer -= r->steer / 5;
    { int ms = 1800 - (spd > U(6.0) / 20 ? 700 : 0);  /* ~10 deg, ~6 deg above 22 km/h */
      if (r->steer > ms) r->steer = ms; if (r->steer < -ms) r->steer = -ms; }
    /* yaw rate = v * tan(steer) / wheelbase(5 m) */
    r->yaw += (int)((long long)spd * tanLerp(r->steer) * 5215 / (4096LL * 4096 * 5));
    int ox = r->x, oz = r->z;
    r->x += (-s * spd) >> 12; r->z += (-c * spd) >> 12;
    /* three circles along the body */
    for (int e = -1; e <= 1; e++) {
        int bx = r->x + ((-s * e * U(2.6)) >> 12), bz = r->z + ((-c * e * U(2.6)) >> 12), qx = bx, qz = bz;
        world_collide(&qx, &qz, r->y, r->y + U(3), U(1.3));
        if (qx != bx || qz != bz) { r->x += qx - bx; r->z += qz - bz; spd = spd * 3 / 4; }
    }
    /* suspension: ground averaged over the body length (5 points, -4..+4 m), pitch from the
       front pair against the back pair. Averages in space, not time, so there is no lag on hills */
    int hs[5], nh = 0, sum = 0;
    for (int e = 0; e < 5; e++) {
        int d = (e - 2) * U(2);
        hs[e] = world_floor_smooth(r->x + ((-s * d) >> 12), r->z + ((-c * d) >> 12));
        if (hs[e] != NOFLOOR) { sum += hs[e]; nh++; }
    }
    int f = nh ? sum / nh : NOFLOOR;
    if (f == NOFLOOR && world_floor(ox, oz) == NOFLOOR) f = r->y;   /* spawned on an unbaked cell: drive out of it */
    if (f == NOFLOOR) { r->x = ox; r->z = oz; spd = 0; f = r->y; }
    r->y += (f - r->y) / 2;
    {
        int ok = nh == 5;
        int tgt = ok ? ((hs[3] + hs[4] - hs[0] - hs[1]) / 2) * 16384 / U(6) / 10 : 0;   /* small-angle, as before */
        if (tgt > 1500) tgt = 1500; if (tgt < -1500) tgt = -1500;
        r->pitch_rv += (tgt - r->pitch_rv) / 4;
    }
    r->spd = spd;
#ifdef SWEEP
    { static int t; t++; r->lookyaw = sinLerp(t * 150) * 12000 >> 12; r->lookpitch = 1100 + (cosLerp(t * 90) * 1500 >> 12); }
#endif
}
static int rv_kmh(const RV *r) { return abs(r->spd) * 20 * 36 / 10 / 4096; }
/* driver's eye in RV space: (-0.57, 2.05, -2.99) */
static void rv_eye(const RV *r, int *cx, int *cy, int *cz)
{
    int s = sinLerp(r->yaw), c = cosLerp(r->yaw), ex = U(-0.57), ez = U(-2.99);
    *cx = r->x + ((ex * c + ez * s) >> 12); *cz = r->z + ((-ex * s + ez * c) >> 12); *cy = r->y + U(2.05);
}
/* camera from the driver's seat, world + RV (+ extra models via draw(pass)) drawn, frame flushed */
/* sun colour and direction for the drive (world space, set per scene) */
static u16 rv_sun = RGB15(8, 6, 6); static v10 rv_sdx = floattov10(0.3), rv_sdy = floattov10(-0.9), rv_sdz = floattov10(-0.3);
static u16 rv_fill = RGB15(6, 6, 9), rv_camb = RGB15(7, 7, 8);   /* cab bounce light and ambient */
static void rv_render(RV *r, u16 amb, void (*draw)(int pass))
{
    int camx, camy, camz; rv_eye(r, &camx, &camy, &camz);
    radio_tick(keysDown());
    g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
    g_amb = amb;
    glEnable(GL_FOG);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 80);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glLight(1, RGB15(24, 22, 16), 0, floattov10(-0.15), floattov10(-0.98));      /* headlights */
    int cyaw = r->yaw + r->lookyaw;
    glRotateXi(r->lookpitch - r->pitch_rv);
    glRotateYi(-cyaw);
    sky_dome(80);
    glTranslatef32(-camx, -camy, -camz);
    glLight(0, rv_sun, rv_sdx, rv_sdy, rv_sdz);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
    /* the cab goes first so a polygon-list overflow can only ever cost distant scenery */
    for (int pass = 0; pass < 2; pass++) {
        if (pass) world_draw(camx, camz, cyaw);
        glPushMatrix();
        glTranslatef32(r->x, r->y, r->z); glRotateYi(r->yaw); glRotateXi(-r->pitch_rv);
        /* the RV body is a single-sided shell facing out: from the driver seat both sides must draw */
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(1));
        /* cab lighting in RV space: the sky's light falls in through the windscreen onto the dash top,
           a soft cool bounce comes from behind the seats, so nothing in the cab reads as a flat slab */
        glLight(0, rv_sun, 0, floattov10(-0.75), floattov10(0.66));
        glLight(1, rv_fill, floattov10(0.2), floattov10(-0.35), floattov10(-0.9));
        u32 a0 = g_amb; g_amb = rv_camb;
        draw_level(&L_rv, pass);
        g_amb = a0;
        glPopMatrix(1);
        glLight(0, rv_sun, rv_sdx, rv_sdy, rv_sdz);
        glLight(1, RGB15(24, 22, 16), 0, floattov10(-0.15), floattov10(-0.98));
        if (draw) draw(pass);
    }
    while (GFX_STATUS & BIT(27)) ;
    stat_v = GFX_VERTEX_RAM_USAGE; stat_p = GFX_POLYGON_RAM_USAGE;
    glFlush(GL_WBUFFERING);   /* linear depth: distant walls and their trim no longer z-fight */
    stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
    vb_count = 0;
}
