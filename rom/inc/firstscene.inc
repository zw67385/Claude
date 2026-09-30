/* First Scene: the night drive to Roseburg in the RV, the diner stop. Included from main.c.
   Positions are DS world units (unity x, y, -z; 20.12). */

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

static const char *drive_hint, *drive_title = "On the road to Roseburg";

static int dbg_x, dbg_y, dbg_z;
static void drive_hud(int speed)
{
    consoleClear();
    printf("%s\n", drive_title); printf("--------------------------------\n\n");
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
#ifdef DINER_TEST
    diner_scene();
#endif
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
    RV r; rv_place(&r, U(608.8), U(-274.4), YAW0);
    int close = 0, look_t = 0, diner_done = 0;
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
        rv_update(&r, k, dlg_active);
        /* ---- triggers */
        int ex, h = trig_update(tr, NT, r.x, r.z, &ex);
        if (h >= 0) switch (tr[h].id) {
            case TR_DRIVESUB: sub(S("SolitaryLife"), 7); sq_add(140, "MemoriesAtBay", 6); break;
            case TR_LOVEJOB: sub(S("LovedOutdoors"), 5); sq_add(100, "NatureGaveHappiness", 6); sq_add(220, "WoodsIsHome", 6); break;
            case TR_NOTIF1: case TR_NOTIF2: sfx_play(SFX_RADIO_BEEP, 200); sub("Kayla Nelson: 1 Text Message", 5); break;
            case TR_LOOKDINER: look_t = 60; sub(S("GetFood"), 5); break;
            case TR_SLOW: r.maxspd = U(7.0) / 20; break;
            case TR_CLOSE: close = 1; break;
            case TR_TRANSITION: if (diner_done) goto out; break;
            case TR_WENTBACK: sub(S("SecondGuess"), 5); break;
            case TR_WENTBACK2: sub(S("SecondGuess"), 5); break;
        }
        if (ex >= 0) {
            if (tr[ex].id == TR_SLOW) { r.maxspd = U(16.0) / 20; if (!diner_done && !close) sub(S("MissedDiner"), 5); }
            if (tr[ex].id == TR_CLOSE) close = 0;
        }
        const char *p = (close && abs(r.spd) < U(1) / 20) ? "Get up" : NULL;
        if (p != prompt) { prompt = p; hud_dirty = 1; }
        if ((kd & KEY_X) && prompt && !dlg_active) {
            /* the diner stop: park, walk the diner in 3D, come back to the RV */
            fade(1);
            level_free(&L_rv); world_free(); glResetTextures();
            diner_scene();
            consoleClear(); printf("\n\n\n   Loading...\n");
            world_load("nitro:/drive1.wld"); load_model(&L_rv, "nitro:/rv.bin");
            int ml = r.maxspd; rv_place(&r, U(1107.25), U(-960.9), -8192); r.maxspd = ml;
            setBrightness(1, 0);
            diner_done = 1; close = 0; prompt = NULL; hud_dirty = 1;
            drive_hint = "Keep driving";
            fade(0);
        }
        sq_tick();
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        int kmh = rv_kmh(&r);
        if (++hud_t >= 10) { hud_t = 0; if (kmh != lastkmh) hud_dirty = 1; lastkmh = kmh; }
        if (look_t > 0) {   /* LookAtDiner: turn toward the diner sign for 3 s */
            int cx, cy, cz; rv_eye(&r, &cx, &cy, &cz);
            look_t--;
            int d = (short)(iatan2(-(U(1114.91) - cx), -(U(-960.99) - cz)) - r.yaw); r.lookyaw += (d - r.lookyaw) / 6;
        }
        rv_render(&r, RGB15(5, 4, 5), NULL);
        dbg_x = r.x; dbg_y = r.y; dbg_z = r.z;
        if (hud_dirty && !dlg_active) { drive_hud(kmh); hud_dirty = 0; }
        if ((kd & KEY_START) && !dlg_active) break;
    }
out:
    fade(1);
    level_free(&L_rv); world_free(); glResetTextures();
    setBrightness(3, 0);
}
