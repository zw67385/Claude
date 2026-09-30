/* Trail Start: drive to the Ironbark trailhead, get the bag, meet ranger Billy at the guard house
   (TrailStartManager / ParkRangerTrailStart). Included from main.c. World frame: DS x = ux, z = -uz. */
#define TX(u) U(u)
#define TZ(u) U(-(u))
#define RANGER_F0 7790                      /* the baked ranger faces DS yaw +7790 */

enum { R_HIDE, R_RUSH, R_TALK, R_GOCHAIR, R_SIT, R_GOPLAYER, R_GOGATE, R_GATE, R_STAND, R_GOCHAIRFINAL, R_SITFINAL };
static Level T_guard, T_door, T_gate, T_ranger, T_rsit, T_bagguy;
static struct {
    int rs, rx, rz, ryaw, rt, stage;       /* ranger state, position, facing, timer, which talk comes next */
    int px[4], pz[4], np;                  /* ranger path */
    int door, door_open, gate, gate_t;     /* guard house door / gate angles (DS yaw units) */
    int scared, bag, flash, flash_on, bad_t, done, look_t, serious_t, inside;
    int rvx, rvz, rvyaw, rvy;
} TS;
static Walker tw;

static int ts_in_house(int x, int z) { return x > TX(630.54) && x < TX(635.87) && z < TZ(440.30) && z > TZ(443.42); }
static int ts_block(int x, int z)
{
    const int R = 900;
    /* closed guard house door: hinge (634.13, 440.29), leaf 1.14 m toward -x */
    if (TS.door > -4000 && x > TX(633.0) - R && x < TX(634.13) + R && abs(z - TZ(440.29)) < R) return 1;
    /* closed gate leaf between the posts */
    if (TS.gate > -4000 && abs(x - TX(637.51)) < R && z < TZ(433.68) + R && z > TZ(439.6) - R) return 1;
    /* the parked RV */
    int s = sinLerp(TS.rvyaw), c = cosLerp(TS.rvyaw), dx = x - TS.rvx, dz = z - TS.rvz;
    int along = (int)(((long long)dx * -s + (long long)dz * -c) >> 12), side = (int)(((long long)dx * c - (long long)dz * s) >> 12);
    return abs(along) < U(4.3) && abs(side) < U(1.3);
}
/* ranger path to (tx, tz), through the guard house door when it has to go in or out */
static void ts_route(int tx, int tz)
{
    int a = ts_in_house(TS.rx, TS.rz), b = ts_in_house(tx, tz);
    TS.np = 0;
    if (a && !b) { TS.px[TS.np] = TX(633.55); TS.pz[TS.np++] = TZ(440.9); TS.px[TS.np] = TX(633.55); TS.pz[TS.np++] = TZ(439.6); }
    if (!a && b) { TS.px[TS.np] = TX(633.55); TS.pz[TS.np++] = TZ(439.6); TS.px[TS.np] = TX(633.55); TS.pz[TS.np++] = TZ(440.9); }
    TS.px[TS.np] = tx; TS.pz[TS.np++] = tz;
}
static int ts_walk(int sp)   /* follow the path; 1 at the end */
{
    if (TS.np <= 0) return 1;
    if (npc_step(&TS.rx, &TS.rz, &TS.ryaw, TS.px[0], TS.pz[0], sp)) {
        for (int i = 1; i < TS.np; i++) { TS.px[i - 1] = TS.px[i]; TS.pz[i - 1] = TS.pz[i]; }
        if (--TS.np <= 0) return 1;
    }
    return 0;
}
static void ts_face_player(void) { TS.ryaw = yaw_to(tw.x - TS.rx, tw.z - TS.rz); }
static void ts_talk(const char *node) { TS.rs = R_TALK; ts_face_player(); dlg_begin(node, NULL); }

static void ts_after_keys(void) { fade(1); sfx(SFX_PICKUP); for (int i = 0; i < 20; i++) swiWaitForVBlank(); fade(0); ts_talk("RangerAfterKeys"); }
static void ts_after_flash(void) { fade(1); sfx(SFX_PICKUP); for (int i = 0; i < 20; i++) swiWaitForVBlank(); fade(0); ts_talk("RangerAfterFlashLight"); }
static void trail_cmd(const char *c)
{
    const char *a = strrchr(c, ' ');
    const char *arg = a ? a + 1 : "";
    if (!strncmp(c, "ExitConversationPR", 18)) {
        if (!strcmp(arg, "CheckComputer")) { TS.rs = R_GOCHAIR; ts_route(TX(633.63), TZ(442.48)); TS.stage = 0; }
        else if (!strcmp(arg, "OpenGate")) { TS.rs = R_GOGATE; ts_route(TX(636.63), TZ(433.91)); TS.stage = 1; }
        else if (!strcmp(arg, "End")) { TS.rs = R_STAND; TS.rt = 40; TS.flash = 1; TS.stage = 2; sub("X: use the flashlight", 4); }
        else if (!strcmp(arg, "End2")) { TS.rs = R_GOCHAIRFINAL; ts_route(TX(633.63), TZ(442.48)); TS.serious_t = 40; TS.stage = 3; }
    }
    else if (!strncmp(c, "TakeKeys", 8)) dlg_done = ts_after_keys;
    else if (!strncmp(c, "TakeFlashLight", 14)) dlg_done = ts_after_flash;
    else if (!strncmp(c, "EnableBadGuy", 12)) TS.bad_t = 400;               /* the bag guy stands out back for 20 s */
}

static void trail_hud(void)
{
    consoleClear();
    printf("Ironbark Trailhead\n--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[14;0H");
    if (prompt) printf("A: %s\n", prompt);
    printf("\x1b[18;0H");
    printf("Pad: walk  R: run\nY+pad / stylus: look\n");
    if (TS.flash) printf("X: flashlight %s", TS.flash_on ? "(on)" : "");
#ifdef DEBUG_POS
    printf("\nvb %d %d %d r%d s%d", stat_vb, tw.x >> 12, -tw.z >> 12, TS.rs, TS.stage);
#endif
}

static void ts_draw(int pass)
{
    /* the guard house shell is single-sided (roof, some walls): draw both faces */
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | g_lights | POLY_ID(1));
    draw_model(&T_guard, TX(633), 0, TZ(440), 0, pass);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
    draw_model(&T_door, TX(634.13), 0, TZ(440.29), TS.door, pass);
    draw_model(&T_gate, TX(637.51), 0, TZ(433.68), TS.gate, pass);
    if (TS.rs == R_SIT || TS.rs == R_SITFINAL) draw_model(&T_rsit, TX(632.95), U(0.33), TZ(442.42), -8154, pass);
    else if (TS.rs != R_HIDE) {
        int f = floor_at(TS.rx, TS.rz, U(1.5)); if (f == NOFLOOR) f = world_floor(TS.rx, TS.rz); if (f == NOFLOOR) f = 0;
        draw_model(&T_ranger, TS.rx, f, TS.rz, TS.ryaw - RANGER_F0, pass);
    }
    if (TS.bad_t) { int f = world_floor(TX(633.63), TZ(453.16)); draw_model(&T_bagguy, TX(633.63), f == NOFLOOR ? 0 : f, TZ(453.16), 0, pass); }
}

/* the drive from the highway turn-off to the trailhead lot; returns with the RV parked */
static void trail_drive(void)
{
    RV r; rv_place(&r, U(584.2), U(-153.0), -2731);
    const int PX = TX(621.2), PZ = TZ(420.47);
    sub_t = 0; prompt = NULL; drive_hint = "Park at the trailhead"; drive_title = "Ironbark Trailhead";
    w_far = U(72); hud_dirty = 1;
    int hud_t = 0, lastkmh = -1;
    radio_start(RAD_TRAIL, RAD_TRAIL_LEN, 1);
    fade(0);
    for (;;) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
#ifdef AUTOPLAY
        bot_drive_wp(&r, &k, &kd, 0, trailwp, sizeof trailwp / sizeof *trailwp);
#endif
        rv_update(&r, k, 0);
        int near = abs(r.x - PX) < U(16) && abs(r.z - PZ) < U(16);
        const char *p = near && abs(r.spd) < U(1) / 20 ? "Get up" : NULL;
        if (p != prompt) { prompt = p; hud_dirty = 1; }
        if (near && !drive_hint[0]) ;
        if ((kd & KEY_X) && prompt) break;
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        int kmh = rv_kmh(&r);
        if (++hud_t >= 10) { hud_t = 0; if (kmh != lastkmh) hud_dirty = 1; lastkmh = kmh; }
        sky(9, 8, 10); sky_use("cold");
        rv_sun = RGB15(10, 12, 17); rv_sdx = floattov10(0.4); rv_sdy = floattov10(-0.8); rv_sdz = floattov10(-0.4); rv_fill = RGB15(6, 7, 12); rv_camb = RGB15(6, 6, 8);
        rv_render(&r, RGB15(7, 7, 9), ts_draw);
        dbg_x = r.x; dbg_y = r.y; dbg_z = r.z;
        if (hud_dirty) { drive_hud(kmh); hud_dirty = 0; }
    }
    sfx(SFX_HANDBRAKE); radio_stop();
    fade(1);
    TS.rvx = r.x; TS.rvz = r.z; TS.rvyaw = r.yaw; TS.rvy = r.y;
}

static void trail_scene(void)
{
    level_free(&L_in); level_free(&L_out); level_free(&L_door);
    glResetTextures();
    consoleClear(); printf("\n\n\n   Loading...\n");
    int ok = world_load("nitro:/trail.wld") && load_model(&L_rv, "nitro:/rv.bin")
          && load_model(&T_guard, "nitro:/trail/guard.bin") && load_model(&T_door, "nitro:/trail/gdoor.bin")
          && load_model(&T_gate, "nitro:/trail/gate.bin") && load_model(&T_ranger, "nitro:/trail/ranger.bin")
          && load_model(&T_rsit, "nitro:/trail/ranger_sit.bin") && load_model(&T_bagguy, "nitro:/trail/bagguy.bin")
          && load_wal("nitro:/trail/trail.wal") && load_floor("nitro:/trail/trail.flr");
    if (!ok) { printf("trail data missing\n"); for (int i = 0; i < 120; i++) swiWaitForVBlank(); goto done; }
    memset(&TS, 0, sizeof TS); memset(&tw, 0, sizeof tw);
    TS.rx = TX(635.28); TS.rz = TZ(441.40); TS.ryaw = RANGER_F0;
    walk_world = 1; walk_block = ts_block;
    amb_set(SFX_AMB_BELL); amb2_set(SFX_CRICKETS);
#ifdef TRAIL_WALK
    TS.rvx = TX(621.2); TS.rvz = TZ(420.47); TS.rvyaw = -7163; TS.rvy = world_floor(TS.rvx, TS.rvz);
#else
    trail_drive();
#endif
    {   /* out of the RV's side door */
        int s = sinLerp(TS.rvyaw), c = cosLerp(TS.rvyaw);
        tw.x = TS.rvx + ((-s * U(1.35) + c * U(1.8)) >> 12); tw.z = TS.rvz + ((-c * U(1.35) - s * U(1.8)) >> 12);
        tw.yaw = TS.rvyaw - 8192; tw.fy = wfloor(tw.x, tw.z, U(50)); if (tw.fy == NOFLOOR) tw.fy = TS.rvy;
        tw.grass = 1;
    }
#ifdef TRAIL_WALK
    tw.x = TX(633.55); tw.z = TZ(438.8); tw.yaw = 0; tw.fy = wfloor(tw.x, tw.z, U(3)); TS.bag = 1;
#endif
    scene_cmd = trail_cmd;
    prompt = NULL; sub_t = 0; hud_dirty = 1;
    sub(S("GetBag"), 4);
    const char *last = NULL;
    fade(0);
    while (!TS.done) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        int talking = dlg_active;
        if (dlg_active) dlg_input(kd);
#if defined(TRAIL_TEST) || defined(AUTOPLAY)
        { static int at; if (dlg_active && ++at > (40)) { at = 0; dlg_input(KEY_A); } }
#endif
#ifdef AUTOPLAY
        if (!dlg_active) {   /* recording bot: bag, knock at the guard house, let the ranger do his rounds, then the trail */
            static int st, t, lx, lz, still;
            int s = sinLerp(TS.rvyaw), c = cosLerp(TS.rvyaw), tx = 0, tz = 0, walk = 0;
            k = 0; kd = 0;
            switch (st) {
            case 0: if (++t > 40) { t = 0; st++; } break;
            case 1: tx = TS.rvx + ((-s * U(1.35) + c * U(1.3)) >> 12); tz = TS.rvz + ((-c * U(1.35) - s * U(1.3)) >> 12);
                    if (TS.bag) st++; else if (abs(tw.x - tx) + abs(tw.z - tz) < U(1.2)) { if (bot_face(&tw, tx, tz) && prompt && ++t > 15) { t = 0; kd = KEY_A; } }
                    else walk = 1; break;
            case 2: tx = TX(633.55); tz = TZ(438.9); walk = 1; break;
            case 3: bot_face(&tw, TX(633.56), TZ(441.5)); if (prompt && !strcmp(prompt, "Open the door") && ++t > 15) { t = 0; kd = KEY_A; }
                    if (TS.door_open && ++t > 20) { t = 0; st = 10; } break;
            case 10: tx = TX(633.6); tz = TZ(441.6); walk = 1; break;   /* step inside: the ranger bursts in */
            case 11: st = 4; break;
            case 4: if (TS.flash && TS.gate <= -8192 && ++t > 60) { t = 0; st++; } break;   /* keys, flashlight, gate */
            case 5: tx = TX(640); tz = TZ(436); walk = 1; break;
            case 6: tx = TX(662); tz = TZ(436); walk = 1; break;
            }
            if (walk) {
                if (bot_walk(&tw, &k, tx, tz) || still > 30) { st++; still = 0; }
                else if (abs(tw.x - lx) + abs(tw.z - lz) < 40) still++; else still = 0;
                lx = tw.x; lz = tw.z;
            }
        }
#endif
#ifdef DEBUG_POS
        if ((kd & KEY_L) && !dlg_active) {   /* past the open gate with everything, facing the trail */
            tw.x = TX(640); tw.z = TZ(436); tw.yaw = -8192; tw.fy = wfloor(tw.x, tw.z, U(50));
            TS.flash = TS.bag = TS.scared = 1; TS.gate = -8192; TS.rs = R_SITFINAL;
        }
#endif
        int locked = dlg_active || TS.rs == R_RUSH;
        if (!locked) walker_update(&tw, k);
        else walker_update(&tw, k & KEY_TOUCH);
        if (TS.flash && !dlg_active && (kd & KEY_X)) { TS.flash_on ^= 1; sfx(TS.flash_on ? SFX_FLASH_ON : SFX_FLASH_OFF); hud_dirty = 1; }
        TS.inside = ts_in_house(tw.x, tw.z);
        tw.grass = !TS.inside;
        /* look at the ranger's face while he talks (and during the jumpscare) */
        if (locked && TS.rs != R_HIDE) {
            int dx = TS.rx - tw.x, dz = TS.rz - tw.z;
            int d = (short)(yaw_to(dx, dz) - tw.yaw); tw.yaw += d / 4;
            int dist = abs(dx) > abs(dz) ? abs(dx) + abs(dz) / 2 : abs(dz) + abs(dx) / 2;
            int rf = floor_at(TS.rx, TS.rz, U(1.5)); if (rf == NOFLOOR) rf = tw.fy;
            int want = -iatan2(rf + U(1.65) - (tw.fy + EYE), dist < U(0.6) ? U(0.6) : dist);
            tw.pitch += (want - tw.pitch) / 4;
        }
        /* ---- hotspots */
        enum { H_BAG = 1, H_DOOR };
        int sR = sinLerp(TS.rvyaw), cR = cosLerp(TS.rvyaw);
        Hot H[] = {
            { TS.rvx + ((-sR * U(1.35) + cR * U(1.3)) >> 12), TS.rvy + U(1.3), TS.rvz + ((-cR * U(1.35) - sR * U(1.3)) >> 12), TS.bag ? NULL : "Take your bag", H_BAG },
            { TX(633.56), U(1.4), TZ(440.29), TS.door_open ? "Close the door" : "Open the door", H_DOOR },
        };
        int best = -1;
        if (!dlg_active && !talking && TS.rs != R_RUSH) best = hot_pick(&tw, H, 2, U(2.2));
        prompt = best >= 0 ? H[best].label : NULL;
        if (prompt != last) { last = prompt; hud_dirty = 1; }
        if ((kd & KEY_A) && best >= 0) {
            if (H[best].id == H_BAG) { sfx(SFX_PICKUP); TS.bag = 1; sub("Got the bag.", 2); }
            else { TS.door_open ^= 1; sfx(TS.door_open ? SFX_IDOOR_OPEN : SFX_IDOOR_SHUT); }
            hud_dirty = 1;
        }
        /* ---- jumpscare: stepping into the guard house */
        if (!TS.scared && TS.inside) {
            TS.scared = 1; TS.rs = R_RUSH; TS.rt = 0; sfx(SFX_JUMPSCARE); sfx(SFX_GASP); sting(SFX_HEARTBEAT);
            TS.np = 0; TS.px[TS.np] = TX(634.57); TS.pz[TS.np++] = TZ(441.83);
        }
        switch (TS.rs) {
        case R_RUSH:
            if (TS.np > 0) ts_walk(U(3.0) / 20);
            else {
                int dx = tw.x - TS.rx, dz = tw.z - TS.rz, d = abs(dx) + abs(dz);
                if (d > U(1.3)) npc_step(&TS.rx, &TS.rz, &TS.ryaw, tw.x, tw.z, U(5.0) / 20);
                ts_face_player();
                if (++TS.rt > 50) ts_talk("TrailStart");
            }
            break;
        case R_TALK: if (!dlg_active && !dlg_done) TS.rs = R_STAND; break;
        case R_GOCHAIR: case R_GOCHAIRFINAL:
            if (ts_walk(U(1.4) / 20)) { TS.rs = TS.rs == R_GOCHAIR ? R_SIT : R_SITFINAL; TS.rt = 200; if (TS.rs == R_SITFINAL) TS.door_open = 0; }
            break;
        case R_SIT:
            if (--TS.rt <= 0) { TS.rx = TX(633.80); TS.rz = TZ(442.43); TS.rs = R_GOPLAYER; TS.rt = 20; ts_route(tw.x, tw.z); }
            break;
        case R_GOPLAYER: {
            if (TS.rt > 0) { TS.rt--; break; }
            TS.px[TS.np - 1] = tw.x; TS.pz[TS.np - 1] = tw.z;       /* follow the player */
            int dx = tw.x - TS.rx, dz = tw.z - TS.rz;
            if (abs(dx) + abs(dz) < U(1.4) && TS.np <= 1) ts_talk(TS.stage == 0 ? "RangerKeys" : TS.stage == 1 ? "RangerFlashLight" : "RangerAfterEnd");
            else ts_walk(U(1.4) / 20);
            break; }
        case R_GOGATE:
            if (ts_walk(U(1.4) / 20)) { TS.rs = R_GATE; TS.rt = 0; TS.ryaw = 16384; }
            break;
        case R_GATE:
            if (++TS.rt == 10) sfx(SFX_DOOR_OPEN);
            if (TS.rt > 10 && TS.gate > -8192) { if (TS.gate == 0) sfx(SFX_TGATE); TS.gate -= 8192 / 30; }
            if (TS.rt > 50) { TS.rs = R_GOPLAYER; TS.rt = 0; ts_route(tw.x, tw.z); }
            break;
        case R_STAND:
            if (TS.stage == 2 && !dlg_active) {
                if (TS.rt > 0) TS.rt--;
                else {   /* he waits until you look away, then calls you back */
                    int d = (short)(yaw_to(TS.rx - tw.x, TS.rz - tw.z) - tw.yaw);
                    if (abs(d) > 5500) ts_talk("RangerAfterEnd");
                }
            }
            break;
        }
        /* he opens the guard house door when he walks through it */
        if ((TS.rs == R_GOCHAIR || TS.rs == R_GOPLAYER || TS.rs == R_GOGATE || TS.rs == R_GOCHAIRFINAL) && !TS.door_open
            && abs(TS.rx - TX(633.55)) < U(1.2) && abs(TS.rz - TZ(440.29)) < U(0.9)) { TS.door_open = 1; sfx(SFX_IDOOR_OPEN); }
        { int tgt = TS.door_open ? -8192 : 0, d = tgt - TS.door; TS.door += d > 1400 ? 1400 : d < -1400 ? -1400 : d; }
        if (TS.bad_t) TS.bad_t--;
        if (TS.serious_t && !dlg_active && --TS.serious_t == 0) sub(S("LookedSerious"), 5);
        /* ---- the trail past the gate: on to the lookout once you have both */
        if (tw.x > TX(652.9) && tw.x < TX(701.6) && tw.z < TZ(389) && tw.z > TZ(462) && !dlg_active) {
            if (TS.flash && TS.bag) TS.done = 1;
            else if (!sub_t) sub(S(TS.bag ? "GetBag" : "BagRV"), 3);
        }
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        for (int i = 0; i < 12; i++) if (evq[i].id == EV_SUB && --evq[i].t <= 0) { evq[i].id = 0; sub(S(evq[i].s), 4); }

        /* ---- render: dusk outside, a lamp in the guard house */
        sky(9, 8, 10); sky_use("cold");
        glEnable(GL_FOG);
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
        g_amb = TS.inside ? RGB15(11, 10, 8) : RGB15(7, 7, 9);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glLight(1, TS.flash_on ? RGB15(26, 25, 20) : RGB15(4, 4, 5), 0, floattov10(-0.15), floattov10(-0.98));   /* flashlight (view space) */
        cam_setup(tw.x, tw.fy + EYE, tw.z, tw.yaw, tw.pitch, 64);
        glLight(0, TS.inside ? RGB15(18, 14, 10) : RGB15(11, 13, 18), floattov10(0.4), floattov10(-0.8), floattov10(-0.4));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        w_far = U(60);
        world_draw(tw.x, tw.z, tw.yaw);
        for (int pass = 0; pass < 2; pass++) {
            ts_draw(pass);
            draw_model(&L_rv, TS.rvx, TS.rvy, TS.rvz, TS.rvyaw, pass);
        }
        glFlush(0);
        stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
        vb_count = 0;
#ifdef DEBUG_POS
        { static int t; if (++t >= 20) { t = 0; hud_dirty = 1; } }
#endif
        if (hud_dirty && !dlg_active) { trail_hud(); hud_dirty = 0; }
    }
done:
    fade(1);
    scene_cmd = NULL; prompt = NULL; amb_set(-1);
    level_free(&T_guard); level_free(&T_door); level_free(&T_gate); level_free(&T_ranger); level_free(&T_rsit); level_free(&T_bagguy);
    level_free(&L_rv); world_free(); walk_free(); glResetTextures(); sfx_flush();
    setBrightness(3, 0);
}
