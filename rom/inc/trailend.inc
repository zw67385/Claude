/* Trail End: back at the trailhead at night, run for the RV with the cultist on your heels
   (TrailEndManager / CultistTrailEnd). Reuses the Trail Start world and guard house models.
   Included from main.c after trail.inc and camp.inc. */

enum { TE_WALK, TE_LOOK, TE_CHASE, TE_STARE, TE_HUNT, TE_SCARE, TE_SAFE };
static struct { int st, t, cx, cz, cyaw, knocks, yaw0; } TE;

static int te_dist(void) { int dx = TE.cx - tw.x, dz = TE.cz - tw.z; return abs(dx) > abs(dz) ? abs(dx) + abs(dz) / 2 : abs(dz) + abs(dx) / 2; }

static void te_hud(void)
{
    consoleClear();
    printf("Ironbark Trailhead\n--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[14;0H");
    if (prompt) printf("A: %s\n", prompt);
    printf("\x1b[18;0H");
    printf("Pad: walk  R: sprint\nY+pad / stylus: look\nX: flashlight %s", TS.flash_on ? "(on)" : "");
#ifdef DEBUG_POS
    printf("\nvb %d %d %d st%d d%d\nfy %d blk %d rv %d %d", stat_vb, tw.x >> 12, -tw.z >> 12, TE.st, te_dist() >> 12, tw.fy >> 8, ts_block(tw.x, tw.z), TS.rvy >> 8, TS.rvz >> 12);
#endif
}

/* returns 1 when the player made it into the RV, 0 when caught */
static int trail_end_run(void)
{
    memset(&TS, 0, sizeof TS); memset(&tw, 0, sizeof tw); memset(&TE, 0, sizeof TE);
    TS.rs = R_HIDE; TS.gate = -8192; TS.flash = 1; TS.flash_on = 1;
    TS.rvx = TX(622.29); TS.rvz = TZ(412.13); TS.rvyaw = 8192;
    TS.rvy = world_floor(TS.rvx, TS.rvz); if (TS.rvy == NOFLOOR) TS.rvy = 0;
    tw.x = TX(651.48); tw.z = TZ(436.83); tw.yaw = 8066; tw.grass = 1;
    tw.fy = wfloor(tw.x, tw.z, U(50)); if (tw.fy == NOFLOOR) tw.fy = U(0.88);
    TE.cx = TX(659.91); TE.cz = TZ(436.35); TE.cyaw = -8192;
    walk_world = 1; walk_block = ts_block;
    amb_set(SFX_AMB_NIGHT);
    prompt = NULL; sub_t = 0; hud_dirty = 1;
    sub(S("Seq8MakeARun"), 4);
    const char *last = NULL;
    fade(0);
    for (;;) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        int frozen = TE.st == TE_LOOK || TE.st == TE_SCARE || TE.st == TE_SAFE;
        walker_update(&tw, frozen ? (k & KEY_TOUCH) : k);
        if ((kd & KEY_X) && !frozen) { TS.flash_on ^= 1; sfx(TS.flash_on ? SFX_FLASH_ON : SFX_FLASH_OFF); hud_dirty = 1; }
        TS.inside = ts_in_house(tw.x, tw.z);
#ifdef DEBUG_POS
        if (kd & KEY_L) { tw.x = TX(621.0); tw.z = TZ(415.2); tw.yaw = 16384; TE.st = TE_CHASE; TE.cx = TX(645); TE.cz = TZ(436); }
#endif

        switch (TE.st) {
        case TE_WALK:   /* the chase trigger west of the guard house */
            if (tw.x < TX(629.6) && abs(tw.z - TZ(436.8)) < U(6.15)) {
                TE.st = TE_LOOK; TE.t = 0; TE.yaw0 = tw.yaw; amb_set(-1); music_set(SFX_CHASE);
                sfx_play(SFX_SCARE, 127);
            }
            break;
        case TE_LOOK: {  /* the camera turns to him for a moment */
            int dx = TE.cx - tw.x, dz = TE.cz - tw.z;
            int d = (short)(yaw_to(dx, dz) - tw.yaw); tw.yaw += d / 6; tw.pitch -= tw.pitch / 6;
            npc_step(&TE.cx, &TE.cz, &TE.cyaw, tw.x, tw.z, U(3.6) / 20);
            if (++TE.t > 40) { TE.st = TE_CHASE; tw.yaw = TE.yaw0; tw.pitch = 0; sub("R: sprint to the RV", 3); }   /* back to where you were heading */
            break; }
        case TE_CHASE:
            npc_step(&TE.cx, &TE.cz, &TE.cyaw, tw.x, tw.z, U(3.6) / 20);
            if ((++TE.t & 7) == 0) { static const short GR[] = { SFX_STEP_GRASS1, SFX_STEP_GRASS2, SFX_STEP_GRASS3, SFX_STEP_GRASS4 }; sfx_play(GR[(TE.t >> 3) & 3], 100); }
            if (te_dist() < U(5.5)) { TE.st = TE_STARE; TE.t = 0; }
            break;
        case TE_STARE:   /* he stops, stares, then draws the sickle */
            TE.cyaw = yaw_to(tw.x - TE.cx, tw.z - TE.cz);
            if (++TE.t == 20) sfx_play(SFX_HARP, 127);
            if (TE.t > 20) TE.st = TE_HUNT;
            break;
        case TE_HUNT:
            npc_step(&TE.cx, &TE.cz, &TE.cyaw, tw.x, tw.z, U(6.0) / 20);
            if (te_dist() < U(1.5)) { TE.st = TE_SCARE; TE.t = 0; sfx_play(SFX_SCARE, 127); }
            break;
        case TE_SCARE: {
            int dx = TE.cx - tw.x, dz = TE.cz - tw.z;
            int d = (short)(yaw_to(dx, dz) - tw.yaw); tw.yaw += d / 2;
            tw.pitch += (-600 - tw.pitch) / 3;
            setBrightness(1, (TE.t & 2) ? 8 : -4);
            if (++TE.t > 60) { setBrightness(1, 0); fade(1); goto out; }
            break; }
        case TE_SAFE:
            if (++TE.t == 20 || TE.t == 34 || TE.t == 44) sfx_play(SFX_KNOCK, 127);
            for (int i = 0; i < 3; i++) swiWaitForVBlank();
            if (TE.t > 60) goto out;
            continue;
        }

        /* ---- hotspots: the RV door; the guard house is locked now */
        enum { H_RV = 1, H_GDOOR };
        int sR = sinLerp(TS.rvyaw), cR = cosLerp(TS.rvyaw);
        Hot H[] = {
            { TS.rvx + ((-sR * U(1.35) + cR * U(1.3)) >> 12), tw.fy + EYE, TS.rvz + ((-cR * U(1.35) - sR * U(1.3)) >> 12), "Get in the RV", H_RV },
            { TX(633.56), U(1.4), TZ(440.29), "Door", H_GDOOR },
        };
        int best = frozen || TE.st == TE_STARE ? -1 : hot_pick(&tw, H, 2, U(3.2));
        prompt = best >= 0 ? H[best].label : NULL;
        if (prompt != last) { last = prompt; hud_dirty = 1; }
        if ((kd & KEY_A) && best >= 0) {
            if (H[best].id == H_GDOOR) sub(S("ItWasLocked"), 3);
            else if (TE.st == TE_CHASE || TE.st == TE_WALK) {   /* in, door slammed and locked */
                sfx(SFX_DOOR_CLOSE); sfx(SFX_KNOCKS); music_set(-1); TE.st = TE_SAFE; TE.t = 0; prompt = NULL;
                amb_set(-1); fade(1);   /* the outro fades in the dark while he pounds on the door */
            }
            hud_dirty = 1;
        }
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;

        /* ---- render: night at the trailhead */
        sky(1, 1, 3);
        glEnable(GL_FOG);
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
        g_amb = TE.st == TE_SCARE && (TE.t & 4) ? RGB15(20, 2, 2) : RGB15(4, 4, 5);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glLight(1, TS.flash_on ? RGB15(24, 23, 18) : RGB15(2, 2, 3), 0, floattov10(-0.15), floattov10(-0.98));
        {
            int shake = TE.st == TE_HUNT || TE.st == TE_SCARE ? ((rand() & 255) - 128) * 2 : 0;
            cam_setup(tw.x, tw.fy + EYE, tw.z, tw.yaw + shake, tw.pitch + shake / 2, 48);
        }
        glLight(0, RGB15(5, 6, 9), floattov10(0.4), floattov10(-0.8), floattov10(-0.4));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        w_far = U(44);
        world_draw(tw.x, tw.z, tw.yaw);
        for (int pass = 0; pass < 2; pass++) {
            ts_draw(pass);
            draw_model(&L_rv, TS.rvx, TS.rvy, TS.rvz, TS.rvyaw, pass);
            if (TE.st != TE_WALK && TE.st != TE_SAFE) {
                int f = world_floor(TE.cx, TE.cz); if (f == NOFLOOR) f = tw.fy;
                u32 a0 = g_amb; g_amb = RGB15(12, 11, 10);   /* his lantern (the scene's light beam) keeps him visible */
                glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(2));
                draw_model(&C_cult, TE.cx, f, TE.cz, TE.cyaw - CULT_F0, pass);
                g_amb = a0;
            }
        }
        glFlush(0);
        stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
        vb_count = 0;
#ifdef DEBUG_POS
        { static int t; if (++t >= 20) { t = 0; hud_dirty = 1; } }
#endif
        if (hud_dirty) { te_hud(); hud_dirty = 0; }
    }
out:
    amb_set(-1); music_set(-1);
    return TE.st == TE_SAFE;
}

static void trail_end_scene(void)
{
    level_free(&L_in); level_free(&L_out); level_free(&L_door);
    glResetTextures();
    consoleClear(); printf("\n\n\n   Loading...\n");
    int ok = world_load("nitro:/trail.wld") && load_model(&L_rv, "nitro:/rv.bin")
          && load_model(&T_guard, "nitro:/trail/guard.bin") && load_model(&T_door, "nitro:/trail/gdoor.bin")
          && load_model(&T_gate, "nitro:/trail/gate.bin") && load_model(&C_cult, "nitro:/camp/cultist.bin")
          && load_wal("nitro:/trail/trail.wal") && load_floor("nitro:/trail/trail.flr");
    if (ok) while (!trail_end_run()) {
        consoleClear(); printf("\n\n\n\n\n\n\n\n        YOU DIED\n\n\n     A: try again\n");
        setBrightness(3, 0);
        do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & KEY_A));
    }
    prompt = NULL;
    level_free(&T_guard); level_free(&T_door); level_free(&T_gate); level_free(&C_cult);
    level_free(&L_rv); world_free(); walk_free(); glResetTextures();
}
