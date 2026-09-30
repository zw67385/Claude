/* Lacey Trail campsite (Camping scene: CampfireManager / CultistCamp). Follow the smoke to an
   abandoned campfire, put it out with the water pot from the creek; someone is watching from the trees.
   Included from main.c. World frame: DS x = ux, z = -uz (TX / TZ from trail.inc). */
#define CULT_F0 7937                        /* the baked cultist faces DS yaw +7937 (unity yaw -87.2) */

static Level C_cult, C_pot;
static struct {
    int fired[9];                           /* one-shot triggers */
    int pot, filled, fire_on, next_on, seen, convo, cult_run, cx, cz, cyaw, whistle_t, texts_t, done, flash_on;
} CS;
static Walker cw;

/* the scene's thin trigger boxes (1.9 m x 25.5 m, long side along unity yaw th) */
enum { CT_SCREAM, CT_SIGNAL0, CT_TEXTS, CT_WHISTLE, CT_LOWDEEP, CT_RUN, CT_NEXT, CT_EXIT, CT_NEAREXIT };
static const float CTRIG[9][3] = {
    { 44.4f, 59.9f, -66.1f }, { 61.4f, 85.3f, -40.2f }, { 74.4f, 97.0f, -40.2f }, { 79.8f, 99.0f, -23.0f },
    { 90.1f, 101.5f, 7.8f }, { 92.5f, 101.2f, 7.8f }, { 50.9f, 70.0f, -63.2f }, { 39.7f, 40.9f, -72.1f }, { 61.3f, 83.6f, -72.1f },
};
static int camp_in_trig(int i)
{
    float px = cw.x / 4096.0f - CTRIG[i][0], pz = -cw.z / 4096.0f - CTRIG[i][1];
    int a = (int)(CTRIG[i][2] * 32768 / 360);
    float s = sinLerp(a) / 4096.0f, c = cosLerp(a) / 4096.0f;
    float along = px * s + pz * c, across = px * c - pz * s;
    return along > -12.75f && along < 12.75f && across > -0.95f && across < 0.95f;
}
/* the creek bank north of the fire pit (unity 76..90, 107..113), facing the water */
static int camp_shore(void)
{
    int ux = cw.x >> 12, uz = -cw.z >> 12;
    return ux >= 75 && ux <= 90 && uz >= 106 && uz <= 114 && abs((short)cw.yaw) < 9000;
}
/* the taped-off fence east of the site (x 92.5, unity z 95..106), facing it */
static int camp_at_fence(void)
{
    int ux = cw.x >> 12, uz = -cw.z >> 12;
    return ux >= 89 && ux <= 93 && uz >= 94 && uz <= 106 && abs((short)(cw.yaw + 8192)) < 7000;
}
static void camp_exit_sub(void)
{
    if (!CS.seen) sub(S("CheckSource"), 5);
    else if (CS.fire_on) sub(S("PutOutCampfire"), 5);
}

/* Kayla's texts arrive in a burst once there is signal */
static const char *const CTEXTS[] = { "SorryGotHome", "Attachment", "ReachYet", "YouOkay", "ReplyBack" };
static char ctext_buf[160];
static void camp_text(int n)
{
    char k[40]; ctext_buf[0] = 0;
    for (int i = 0; i < n; i++) {
        snprintf(k, sizeof k, "ep4_chats.%s", CTEXTS[i]);
        strncat(ctext_buf, "Kayla: ", sizeof ctext_buf - strlen(ctext_buf) - 1);
        strncat(ctext_buf, T(k), sizeof ctext_buf - strlen(ctext_buf) - 1);
        strncat(ctext_buf, "\n", sizeof ctext_buf - strlen(ctext_buf) - 1);
    }
    hud_dirty = 1;
}

static void camp_cmd(const char *c)
{
    if (!strncmp(c, "ExitConversationCamp", 20)) CS.convo = 2;
}

static void camp_hud(void)
{
    consoleClear();
    printf("Lacey Trail campsite\n--------------------------------\n\n");
    if (sub_t > 0) { wrap_print(subbuf); printf("\n"); }
    if (CS.texts_t > 0) printf("%s", ctext_buf);
    printf("\x1b[14;0H");
    if (prompt) printf("A: %s\n", prompt);
    if (CS.pot) printf("%s\n", CS.filled ? "(pot full of water)" : "(carrying the pot)");
    printf("\x1b[18;0H");
    printf("Pad: walk  R: run\nY+pad / stylus: look\nX: flashlight %s", CS.flash_on ? "(on)" : "");
#ifdef DEBUG_POS
    printf("\nvb %d %d %d", stat_vb, cw.x >> 12, -cw.z >> 12);
#endif
}

/* flickering flames: camera-facing tongues of fire over the pit. Each has its own polygon ID: the DS does not
   blend translucent polygons over others with the same ID, which made the old crossed triangles read as one. */
static void camp_flames(void)
{
    static int t; t++;
    static const s16 ang[7] = { 0, 9000, 18500, 27000, 4500, 14000, 23000 };
    static const u8 rad[7] = { 0, 30, 34, 28, 14, 16, 18 }, hgt[7] = { 60, 34, 30, 36, 46, 44, 40 }, wid[7] = { 18, 12, 11, 12, 14, 14, 13 };
    int rx = cosLerp(cw.yaw), rz = -sinLerp(cw.yaw);        /* camera right: the tongues face the viewer */
    glBindTexture(0, 0);
    glPushMatrix();
    glTranslatef32(TX(80.69), U(50.0), TZ(98.84));
    for (int i = 0; i < 7; i++) {
        int cx = (cosLerp(ang[i]) * rad[i] * U(0.01)) >> 12, cz = (sinLerp(ang[i]) * rad[i] * U(0.01)) >> 12;
        int h = hgt[i] * U(0.01) + ((sinLerp(t * (900 + i * 170) + i * 7000) * U(0.12)) >> 12);
        int w = wid[i] * U(0.01) / 2, sw = (sinLerp(t * 1300 + i * 5000) * U(0.06)) >> 12;
        glPolyFmt(POLY_ALPHA(i ? 16 : 22) | POLY_CULL_NONE | POLY_ID(8 + i));
        glBegin(GL_TRIANGLES);
        glColor3b(255, 70, 0);   glVertex3v16(cx - ((rx * w) >> 12), 0, cz - ((rz * w) >> 12));
        glColor3b(255, 70, 0);   glVertex3v16(cx + ((rx * w) >> 12), 0, cz + ((rz * w) >> 12));
        glColor3b(255, 225, 110); glVertex3v16(cx + ((rx * sw) >> 12), h, cz + ((rz * sw) >> 12));
        glEnd();
    }
    glPopMatrix(1);
}

static void camp_draw(int pass)
{
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
    if (CS.cult_run < 3) {
        int f = world_floor(CS.cx, CS.cz); if (f == NOFLOOR) f = U(50.88);
        draw_model(&C_cult, CS.cx, f, CS.cz, CS.cyaw - CULT_F0, pass);
    }
    if (!CS.pot) draw_model(&C_pot, TX(80.05), U(49.99), TZ(98.06), 0, pass);
}

static void camp_scene(void)
{
    level_free(&L_in); level_free(&L_out); level_free(&L_door);
    glResetTextures();
    consoleClear(); printf("\n\n\n   Loading...\n");
    int ok = world_load("nitro:/camp.wld") && load_model(&C_cult, "nitro:/camp/cultist.bin") && load_model(&C_pot, "nitro:/camp/pot.bin");
    if (!ok) { printf("camp data missing\n"); for (int i = 0; i < 120; i++) swiWaitForVBlank(); goto done; }
    memset(&CS, 0, sizeof CS); memset(&cw, 0, sizeof cw);
    CS.fire_on = 1; CS.flash_on = 1;
    CS.cx = TX(104.29); CS.cz = TZ(101.53); CS.cyaw = CULT_F0;
    cw.x = TX(40.24); cw.z = TZ(45.61); cw.yaw = -1702; cw.grass = 1;
    cw.fy = world_floor(cw.x, cw.z); if (cw.fy == NOFLOOR) cw.fy = U(56.9);
    walk_world = 1;
    amb_set(SFX_AMB_EERIE); amb2_set(SFX_CRICKETS);
    scene_cmd = camp_cmd;
    prompt = NULL; sub_t = 0; hud_dirty = 1;
    sub(S("ScentWoodFire"), 5);
    const char *last = NULL;
    int fire_snd = 0; mm_sfxhand fire_h = 0;
    fade(0);
    while (!CS.done) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        if (dlg_active) dlg_input(kd);
#ifdef TRAIL_TEST
        { static int at; if (dlg_active && ++at > 12) { at = 0; dlg_input(KEY_A); } }
#endif
#ifdef DEBUG_POS
        if ((kd & KEY_L) && !(k & KEY_R) && !dlg_active) { cw.x = TX(78.5); cw.z = TZ(97.5); cw.yaw = -8192; cw.fy = world_floor(cw.x, cw.z); }
        if ((kd & KEY_L) && (k & KEY_R)) { CS.fire_on = 0; CS.next_on = 1; cw.x = TX(56); cw.z = TZ(76); cw.yaw = 12200; cw.fy = world_floor(cw.x, cw.z); }
#endif
        if (!dlg_active) walker_update(&cw, k);
        else {   /* the conversation at the fence: look into the trees where he stood */
            walker_update(&cw, 0);
            int dx = TX(104.3) - cw.x, dz = TZ(101.41) - cw.z;
            int d = (short)(yaw_to(dx, dz) - cw.yaw); cw.yaw += d / 8;
            int dist = abs(dx) > abs(dz) ? abs(dx) + abs(dz) / 2 : abs(dz) + abs(dx) / 2;
            int want = -iatan2(U(52.35) - (cw.fy + EYE), dist < U(1) ? U(1) : dist);
            cw.pitch += (want - cw.pitch) / 8;
        }
        if (!dlg_active && (kd & KEY_X)) { CS.flash_on ^= 1; sfx(CS.flash_on ? SFX_FLASH_ON : SFX_FLASH_OFF); hud_dirty = 1; }

        /* ---- triggers */
        for (int i = 0; i < 9; i++) {
            int in = camp_in_trig(i);
            if (i == CT_EXIT || i == CT_NEAREXIT) {
                if (in && !CS.fired[i] && (i == CT_EXIT || CS.seen)) camp_exit_sub();
                CS.fired[i] = in; continue;
            }
            if (!in || CS.fired[i]) continue;
            if (i == CT_NEXT && !CS.next_on) continue;
            CS.fired[i] = 1;
            switch (i) {
            case CT_SCREAM: sfx_play(SFX_SHOUT, 127); sting(SFX_VIOLIN); sfx(SFX_HEARTBEAT); break;
            case CT_TEXTS: CS.seen = 1; CS.texts_t = 1; break;
            case CT_WHISTLE: sfx_play(SFX_WHISTLE2, 100); CS.whistle_t = 200; break;
            case CT_LOWDEEP: sting(SFX_LOWDEEP); break;
            case CT_RUN: if (!CS.cult_run) CS.cult_run = 1; break;
            case CT_NEXT: CS.done = 1; break;
            }
        }
        if (CS.texts_t > 0) {   /* SorryGotHome, Attachment +0.5 s, ReachYet +0.4, YouOkay +0.5, ReplyBack +0.4 */
            static const int at[] = { 1, 11, 19, 29, 37 };
            CS.texts_t++;
            for (int i = 0; i < 5; i++) if (CS.texts_t == at[i]) { camp_text(i + 1); sfx(SFX_NOTIF2); }
            if (CS.texts_t > 240) { CS.texts_t = 0; hud_dirty = 1; }
        }
        if (CS.whistle_t && --CS.whistle_t == 100) sfx_play(SFX_WHISTLE2, 80);

        /* ---- the cultist slips behind a tree once you get close to the fence */
        if (CS.cult_run == 1) {
            if (npc_step(&CS.cx, &CS.cz, &CS.cyaw, TX(106.98), TZ(107.54), U(4.0) / 20)) CS.cult_run = 2;
        }
        if (CS.cult_run == 2) {
            int d = (short)(yaw_to(TX(104.3) - CS.cx, TZ(52.35) - CS.cz) - CS.cyaw); CS.cyaw += d / 4;
            /* gone once you look away from him */
            int v = (short)(yaw_to(CS.cx - cw.x, CS.cz - cw.z) - cw.yaw);
            if (abs(v) > 7000 && CS.convo == 2) CS.cult_run = 3;
        }

        /* ---- hotspots */
        enum { H_FIRE = 1, H_POT, H_WATER, H_BARRIER };
        Hot H[] = {
            { TX(80.69), U(50.3), TZ(98.84), CS.fire_on ? (CS.filled ? "Put out the fire" : "Campfire") : NULL, H_FIRE },
            { TX(80.05), U(50.2), TZ(98.06), CS.pot ? NULL : "Pick up the pot", H_POT },
            { cw.x - sinLerp(cw.yaw) / 4, cw.fy + U(0.6), cw.z - cosLerp(cw.yaw) / 4,
              CS.pot && !CS.filled && camp_shore() ? "Fill the pot" : NULL, H_WATER },
            { cw.x - sinLerp(cw.yaw) / 4, cw.fy + U(1.0), cw.z - cosLerp(cw.yaw) / 4, camp_at_fence() ? "Fence" : NULL, H_BARRIER },
        };
        int best = dlg_active ? -1 : hot_pick(&cw, H, 4, U(2.2));
        prompt = best >= 0 ? H[best].label : NULL;
        if (prompt != last) { last = prompt; hud_dirty = 1; }
        if ((kd & KEY_A) && best >= 0) {
            switch (H[best].id) {
            case H_FIRE:
                if (CS.filled) {
                    fade(1); sfx(SFX_SPLASH); sfx(SFX_SIZZLE); for (int i = 0; i < 40; i++) swiWaitForVBlank();
                    CS.fire_on = 0; CS.filled = 0; CS.next_on = 1; fade(0);
                } else sub(S(CS.pot ? "GetWater" : "PutOutFire"), 4);
                break;
            case H_POT: CS.pot = 1; sfx(SFX_PICKUP); break;
            case H_WATER: fade(1); sfx(SFX_FILL); for (int i = 0; i < 30; i++) swiWaitForVBlank(); CS.filled = 1; fade(0); break;
            case H_BARRIER:
                if (!CS.convo) { CS.convo = 1; dlg_begin("Campfire", NULL); }
                else if (CS.convo == 2) sub(S("AreaClosed"), 4);
                break;
            }
            hud_dirty = 1;
        }
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;

        /* crackling fire: loud near the pit */
        {
            int dx = cw.x - TX(80.69), dz = cw.z - TZ(98.84), d = (abs(dx) + abs(dz)) >> 12;
            int want = CS.fire_on && d < 25;
            if (want && !fire_snd) { fire_h = sfx_play(SFX_FIRE, 100); fire_snd = 1; }
            if (!want && fire_snd) { mmEffectCancel(fire_h); fire_snd = 0; }
            if (fire_snd) mmEffectVolume(fire_h, 127 - d * 5);
        }

        /* ---- render: night, flashlight, the fire lights up the site */
        sky(1, 1, 3);
        glEnable(GL_FOG);
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
        {
            int dx = cw.x - TX(80.69), dz = cw.z - TZ(98.84), d = (abs(dx) + abs(dz)) >> 12;
            int f = CS.fire_on && d < 16 ? (16 - d) : 0;
            g_amb = RGB15(4 + f * 3 / 4, 4 + f / 3, 5);
        }
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glLight(1, CS.flash_on ? RGB15(24, 23, 18) : RGB15(2, 2, 3), 0, floattov10(-0.15), floattov10(-0.98));   /* flashlight (view space) */
        cam_setup(cw.x, cw.fy + EYE, cw.z, cw.yaw, cw.pitch, 48);
        glLight(0, RGB15(5, 6, 9), floattov10(0.4), floattov10(-0.8), floattov10(-0.4));                          /* moonlight */
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
        w_far = U(44);
        world_draw(cw.x, cw.z, cw.yaw);
        for (int pass = 0; pass < 2; pass++) camp_draw(pass);
        if (CS.fire_on) camp_flames();
        glFlush(0);
        stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
        vb_count = 0;
#ifdef DEBUG_POS
        { static int t; if (++t >= 20) { t = 0; hud_dirty = 1; } }
#endif
        if (hud_dirty && !dlg_active) { camp_hud(); hud_dirty = 0; }
    }
done:
    fade(1);
    scene_cmd = NULL; prompt = NULL; amb_set(-1);
    level_free(&C_cult); level_free(&C_pot);
    world_free(); walk_free(); glResetTextures(); sfx_flush();
    setBrightness(3, 0);
}
