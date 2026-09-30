/* First Scene: the night drive to Roseburg in the RV, the diner stop. Included from main.c.
   Positions are DS world units (unity x, y, -z; 20.12). */

#define U(v) ((int)((v) * 4096))
/* angle (32768 per turn) of the vector (x = cos part, y = sin part) */
static int iatan2(int y, int x)
{
    if (!x && !y) return 0;
    if (abs(x) >= abs(y)) { int a = atanLerp((s32)(((long long)y << 12) / x)); return x > 0 ? a : a + 16384; }
    int a = atanLerp((s32)(((long long)x << 12) / y)); return y > 0 ? 8192 - a : -8192 - a;
}
/* timed subtitles (frames, ep4_subs key, seconds shown) */
static struct { int t; const char *k; int secs; } sq[8];
static void sq_add(int frames, const char *k, int secs)
{
    for (int i = 0; i < 8; i++) if (!sq[i].k) { sq[i].t = frames; sq[i].k = k; sq[i].secs = secs; return; }
}
static void sq_tick(void)
{
    for (int i = 0; i < 8; i++) if (sq[i].k && --sq[i].t <= 0) { sub(S(sq[i].k), sq[i].secs); sq[i].k = NULL; }
}

typedef struct { int x, z, hx, hz; u8 id, once, in; } Trig;   /* axis box: centre, half sizes (DS units) */
static int trig_update(Trig *t, int n, int x, int z, int *exited)
{
    int hit = -1; *exited = -1;
    for (int i = 0; i < n; i++) {
        int inside = abs(x - t[i].x) < t[i].hx && abs(z - t[i].z) < t[i].hz;
        if (inside && !t[i].in) { t[i].in = 1; if (t[i].once != 2) { hit = i; if (t[i].once) t[i].once = 2; } }
        else if (!inside && t[i].in) { t[i].in = 0; *exited = i; }
    }
    return hit;
}

enum { TR_DRIVESUB = 1, TR_NOTIF1, TR_LOVEJOB, TR_NOTIF2, TR_LOOKDINER, TR_SLOW, TR_CLOSE, TR_TRANSITION, TR_WENTBACK, TR_WENTBACK2 };

static Level L_rv;
static const char *drive_hint;

static int dbg_x, dbg_y, dbg_z;
static void drive_hud(int speed)
{
    consoleClear();
    printf("On the road to Roseburg\n--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[12;0H");
    if (prompt) printf("X: %s\n", prompt);
    printf("\x1b[17;0H");
    if (drive_hint) printf("%s\n", drive_hint);
    printf("Up/A: gas  Down/B: brake\nLeft/Right: steer  Y+pad: look\n%3d km/h", speed);
#ifdef DEBUG_POS
    printf("  ch %d vb %d\n%d %d %d", w_drawn, stat_vb, dbg_x >> 12, dbg_y >> 12, dbg_z >> 12);
#endif
}

static void first_scene(void)
{
    /* free the tower data: this scene needs the texture memory */
    level_free(&L_in); level_free(&L_out); level_free(&L_door);
    glResetTextures();
    consoleClear(); printf("\n\n\n   Loading...\n");
    if (!world_load("nitro:/drive1.wld") || !load_model(&L_rv, "nitro:/rv.bin")) { printf("drive data missing\n"); for (int i = 0; i < 120; i++) swiWaitForVBlank(); return; }
    Trig tr[] = {
        { U(644.4), U(-500.2), U(14.3), U(3), TR_DRIVESUB, 1, 0 },
        { U(611.0), U(-390.8), U(14.3), U(3), TR_NOTIF1, 1, 0 },
        { U(772.0), U(-729.7), U(14.3), U(3), TR_LOVEJOB, 1, 0 },
        { U(724.2), U(-865.4), U(14.3), U(3), TR_NOTIF2, 1, 0 },
        { U(1084.2), U(-974.8), U(3), U(35), TR_LOOKDINER, 1, 0 },
        { U(1158.6), U(-976.7), U(120), U(144), TR_SLOW, 0, 0 },
        { U(1116.5), U(-963.5), U(27), U(65), TR_CLOSE, 0, 0 },
        { U(1122.1), U(-976.7), U(55), U(144), TR_TRANSITION, 0, 0 },
        { U(584.6), U(-163.1), U(3), U(24), TR_WENTBACK, 1, 0 },
        { U(593.7), U(-219.0), U(3), U(24), TR_WENTBACK2, 1, 0 },
    };
    const int NT = sizeof tr / sizeof tr[0];
#ifndef YAW0
#define YAW0 0
#endif
    int x = U(608.8), z = U(-274.4), yaw = YAW0, y = 0, spd = 0, steer = 0, pitch_rv = 0;
    int lookyaw = 0, lookpitch = 250, maxspd = U(16.0) / 20;
    int close = 0, look_t = 0, diner_done = 0;
    touchPosition t0, t1; int wasTouch = 0;
    memset(sq, 0, sizeof sq);
    sub_t = 0; prompt = NULL;
    drive_hint = "Drive to Roseburg";
    w_far = U(72);
    sky(5, 4, 5);
    int hud_t = 0, lastkmh = -1; hud_dirty = 1;
    fade(0);
    for (;;) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        if (dlg_active) dlg_input(kd);
        int s = sinLerp(yaw), c = cosLerp(yaw);
        /* ---- controls */
        int gas = !dlg_active && (k & (KEY_UP | KEY_A)) && !(k & KEY_Y);
        int brk = !dlg_active && (k & (KEY_DOWN | KEY_B)) && !(k & KEY_Y);
        int st = dlg_active || (k & KEY_Y) ? 0 : ((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0);
        if (k & KEY_Y) {
            lookyaw += (((k & KEY_LEFT) ? 1 : 0) - ((k & KEY_RIGHT) ? 1 : 0)) * 400;
            lookpitch += (((k & KEY_DOWN) ? 1 : 0) - ((k & KEY_UP) ? 1 : 0)) * 300;
        } else if (!(k & KEY_TOUCH)) { lookyaw -= lookyaw / 8; }
        if (k & KEY_TOUCH) {
            touchRead(&t1);
            if (wasTouch) { lookyaw -= (t1.px - t0.px) * 90; lookpitch += (t1.py - t0.py) * 70; }
            t0 = t1; wasTouch = 1;
        } else wasTouch = 0;
        if (lookyaw > 12000) lookyaw = 12000; if (lookyaw < -12000) lookyaw = -12000;
        if (lookpitch > 3000) lookpitch = 3000; if (lookpitch < -4000) lookpitch = -4000;
        /* ---- RV physics: speed (DS units / frame), bicycle-model steering */
        int acc = U(3.0) / 400, dec = U(8.0) / 400, drag = U(0.8) / 400;
        if (gas) spd += acc; else if (brk) spd -= spd > 0 ? dec : acc / 2; else spd -= spd > drag ? drag : spd < -drag ? -drag : spd;
        if (spd > maxspd) spd -= dec; if (spd < -U(3.0) / 20) spd = -U(3.0) / 20;
        steer += st * 110; if (!st) steer -= steer / 5;
        { int ms = 1800 - (spd > U(6.0) / 20 ? 700 : 0);  /* ~10 deg, ~6 deg above 22 km/h */
          if (steer > ms) steer = ms; if (steer < -ms) steer = -ms; }
        /* yaw rate = v * tan(steer) / wheelbase(5 m) */
        yaw += (int)((long long)spd * tanLerp(steer) * 5215 / (4096LL * 4096 * 5));
        int ox = x, oz = z;
        x += (-s * spd) >> 12; z += (-c * spd) >> 12;
        /* three circles along the body */
        for (int e = -1; e <= 1; e++) {
            int bx = x + ((-s * e * U(2.6)) >> 12), bz = z + ((-c * e * U(2.6)) >> 12), qx = bx, qz = bz;
            world_collide(&qx, &qz, y, y + U(3), U(1.3));
            if (qx != bx || qz != bz) { x += qx - bx; z += qz - bz; spd = spd * 3 / 4; }
        }
        int f = world_floor(x, z);
        if (f == NOFLOOR) { x = ox; z = oz; spd = 0; f = world_floor(x, z); if (f == NOFLOOR) f = y; }
        y += (f - y) / 2;
        {   /* body pitch from the ground 3 m ahead / behind */
            int ff = world_floor(x + ((-s * U(3)) >> 12), z + ((-c * U(3)) >> 12)), fb = world_floor(x - ((-s * U(3)) >> 12), z - ((-c * U(3)) >> 12));
            int tgt = (ff != NOFLOOR && fb != NOFLOOR) ? ((ff - fb) * 16384 / U(6) / 10) : 0;   /* small-angle: rad*~5215 */
            if (tgt > 1500) tgt = 1500; if (tgt < -1500) tgt = -1500;
            pitch_rv += (tgt - pitch_rv) / 4;
        }
        /* ---- triggers */
        int ex, h = trig_update(tr, NT, x, z, &ex);
        if (h >= 0) switch (tr[h].id) {
            case TR_DRIVESUB: sub(S("SolitaryLife"), 7); sq_add(140, "MemoriesAtBay", 6); break;
            case TR_LOVEJOB: sub(S("LovedOutdoors"), 5); sq_add(100, "NatureGaveHappiness", 6); sq_add(220, "WoodsIsHome", 6); break;
            case TR_NOTIF1: case TR_NOTIF2: sfx_play(SFX_RADIO_BEEP, 200); sub("Kayla Nelson: 1 Text Message", 5); break;
            case TR_LOOKDINER: look_t = 60; sub(S("GetFood"), 5); break;
            case TR_SLOW: maxspd = U(7.0) / 20; break;
            case TR_CLOSE: close = 1; break;
            case TR_TRANSITION: if (diner_done) goto out; break;
            case TR_WENTBACK: sub(S("SecondGuess"), 5); break;
            case TR_WENTBACK2: sub(S("SecondGuess"), 5); break;
        }
        if (ex >= 0) {
            if (tr[ex].id == TR_SLOW) { maxspd = U(16.0) / 20; if (!diner_done && !close) sub(S("MissedDiner"), 5); }
            if (tr[ex].id == TR_CLOSE) close = 0;
        }
        const char *p = (close && abs(spd) < U(1) / 20) ? "Get up" : NULL;
        if (p != prompt) { prompt = p; hud_dirty = 1; }
        if ((kd & KEY_X) && prompt && !dlg_active) {
            /* the diner stop */
            fade(1);
            { static const char *const diner[] = { "DinerStart", "DinerOrder", "AfterEating", "Check", "AfterCheck", "Bad_Guy", "ParkingLotGuy" };
              vignette("Rosebourg Diner", diner, 7); }
            setBrightness(1, 0);
            diner_done = 1; close = 0; prompt = NULL; spd = 0;
            drive_hint = "Keep driving";
            fade(0);
        }
        sq_tick();
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        int kmh = abs(spd) * 20 * 36 / 10 / 4096;
        if (++hud_t >= 10) { hud_t = 0; if (kmh != lastkmh) hud_dirty = 1; lastkmh = kmh; }
        /* ---- render */
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
        g_amb = RGB15(5, 4, 5);
        glEnable(GL_FOG);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        gluPerspective(70, 256.0 / 192.0, 0.1, 80);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glLight(1, RGB15(24, 22, 16), 0, floattov10(-0.15), floattov10(-0.98));      /* headlights */
        /* driver's eye in RV space: (-0.57, 2.05, -2.99) */
        int ex_ = U(-0.57), ez_ = U(-2.99);
        int camx = x + ((ex_ * c + ez_ * s) >> 12), camz = z + ((-ex_ * s + ez_ * c) >> 12), camy = y + U(2.05);
        int cyaw = yaw + lookyaw, cpitch = lookpitch;
        if (look_t > 0) {   /* LookAtDiner: turn toward the diner sign for 3 s */
            look_t--;
            int dx = U(1114.91) - camx, dz = U(-960.99) - camz;
            int want = iatan2(-dx, -dz);   /* yaw whose forward (-sin,-cos) points at (dx,dz) */
            int d = (short)(want - yaw); lookyaw += (d - lookyaw) / 6;
        }
        glRotateXi(cpitch - pitch_rv);
        glRotateYi(-cyaw);
        glTranslatef32(-camx, -camy, -camz);
        glLight(0, RGB15(8, 6, 6), floattov10(0.3), floattov10(-0.9), floattov10(-0.3));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        world_draw(camx, camz, cyaw);
        for (int pass = 0; pass < 2; pass++) {
            glPushMatrix();
            glTranslatef32(x, y, z); glRotateYi(yaw); glRotateXi(-pitch_rv);
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(1));
            draw_level(&L_rv, pass);
            glPopMatrix(1);
        }
        glFlush(0);
        stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
        vb_count = 0;
        dbg_x = x; dbg_y = y; dbg_z = z;
        if (hud_dirty && !dlg_active) { drive_hud(kmh); hud_dirty = 0; }
        if ((kd & KEY_START) && !dlg_active) break;
    }
out:
    fade(1);
    level_free(&L_rv); world_free(); glResetTextures();
    setBrightness(3, 0);
}
