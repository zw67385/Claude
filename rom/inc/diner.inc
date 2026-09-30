/* Twin River diner, Roseburg (First Scene): park, walk in, order, pee, eat, pay, leave.
   Follows DinerManager / Waitress / BadGuy. Diner frame: unity (1115, 0, 955) is the origin,
   DS x = ux - 1115, z = 955 - uz (20.12). Included from main.c. */
#define DX(ux) U((ux) - 1115.0)
#define DZ(uz) U(955.0 - (uz))
#define AISLE DX(1117.0)

static Level D_lvl, D_door, D_wait, D_bad, D_badsit, D_food, D_plg, D_rv, D_npc[4];
enum { W_IDLE, W_GO, W_TALK, W_AWAY };
enum { WN_NONE, WN_START, WN_ORDER, WN_SERVE, WN_AFTEREAT, WN_CHECK, WN_KITCHEN };
static struct {
    int in, seated, started, ordered, peed, served, eaten, paid, plg_done, leave;
    int wx, wz, wyaw, wstate, wnode, wt, px[3], pz[3], np, wander;
    int bstate, bx, bz, byaw, bt, bpx[3], bpz[3], bnp;
    int booth, food_t, ae_t, seat_t, pee_t, pee_nag, door, talks[4];
} DN;
static Walker dw;
/* booths: seat (unity x, z), facing yaw, "Waitress Here" (x, z), food tray z */
static const struct { float sx, sz; int yaw; float wx, wz, tz; } BOOTH[4] = {
    { 1115.25, 960.46, 16384, 1115.72, 959.57, 959.86 }, { 1115.32, 952.57, 0, 1115.79, 953.08, 953.14 },
    { 1115.32, 949.54, 0, 1115.78, 950.13, 950.15 }, { 1115.32, 948.56, 16384, 1115.92, 947.81, 947.91 } };

/* route along the aisle so the NPCs don't cut through booths and the counter */
static int route(int *px, int *pz, int x, int z, int tx, int tz)
{
    int n = 0;
    if (abs(x - AISLE) > U(0.3)) { px[n] = AISLE; pz[n++] = z; }
    if (abs(tx - AISLE) > U(0.3)) { px[n] = AISLE; pz[n++] = tz; }
    px[n] = tx; pz[n++] = tz;
    return n;
}
static void w_goto(int tx, int tz, int node)
{
    if (DN.wstate == W_AWAY) { DN.wx = DX(1119.63); DN.wz = DZ(954.41); }   /* out through the bar doors */
    DN.np = route(DN.px, DN.pz, DN.wx, DN.wz, tx, tz);
    DN.wstate = W_GO; DN.wnode = node;
}
static void w_to_player(int node)   /* "Waitress Here" beside booth 1, or next to the player when standing */
{
    if (DN.seated) w_goto(DX(BOOTH[DN.booth].wx + 0.2), DZ(BOOTH[DN.booth].wz), node);
    else w_goto(dw.x + (dw.x > AISLE ? -U(0.8) : U(0.8)), dw.z, node);
}

static void dn_after_check(void)
{
    /* FadePayCheck: while the screen is dark the bad guy slips out to hide in the RV */
    fade(1); DN.bstate = 3; for (int i = 0; i < 20; i++) swiWaitForVBlank(); fade(0);
    dlg_begin("AfterCheck", NULL);
}
static void diner_cmd(const char *c)
{
    const char *a = strchr(c, ' ');
    const char *arg = a ? strchr(a + 1, ' ') : NULL;
    if (arg) arg++;
    if (!strncmp(c, "ExitConversation ", 17)) {   /* Waitress [state] */
        DN.wstate = W_IDLE; DN.wt = 0; DN.wander = 0;
        if (!arg) return;
        if (!strcmp(arg, "take_order")) DN.wt = 200;                 /* back in 10 s */
        else if (!strcmp(arg, "kitchen") || !strcmp(arg, "get_check")) {
            DN.np = route(DN.px, DN.pz, DN.wx, DN.wz, DX(1119.63), DZ(954.41));
            DN.wstate = W_GO; DN.wnode = WN_KITCHEN; DN.wt = !strcmp(arg, "get_check") ? 80 : 0;
        } else if (!strcmp(arg, "Check_paid")) { DN.paid = 1; sub(S("LookAtParking"), 4); }
    }
    else if (!strncmp(c, "SetOrder", 8)) { DN.ordered = 1; DN.food_t = 200; }
    else if (!strncmp(c, "HeStared", 8)) { sub(S("HeStared"), 4); sfx_play(SFX_SCARE, 90); }
    else if (!strncmp(c, "PayCheck", 8)) dlg_done = dn_after_check;
}

static void dn_hud(void)
{
    consoleClear();
    printf("Twin River Diner\n--------------------------------\n\n");
    if (sub_t > 0) wrap_print(subbuf);
    printf("\x1b[13;0H");
    if (prompt) printf("A: %s\n", prompt);
    printf("\x1b[18;0H");
    if (DN.seated) printf("B: get up\n");
    printf("Pad: walk  R: run\nY+pad / stylus: look");
#ifdef DEBUG_POS
    printf("\nvb %d %d %d o%d p%d s%d e%d $%d w%d b%d", stat_vb, dw.x >> 10, dw.z >> 10, DN.ordered, DN.peed, DN.served, DN.eaten, DN.paid, DN.wstate, DN.bstate);
#endif
}

enum { H_DOOR, H_SEAT, H_SEAT2, H_SEAT3, H_SEAT4, H_TOILET, H_WAIT, H_FOOD, H_SUIT, H_TR1, H_TR2, H_BAD, H_PLG, H_RV };
static int dn_walk_to(int x, int z, int yaw) { dw.x = x; dw.z = z; dw.yaw = yaw; dw.pitch = 0; dw.fy = floor_at(x, z, U(3)); if (dw.fy == NOFLOOR) dw.fy = U(0.45); return 0; }

static void diner_use(int id)
{
    switch (id) {
    case H_DOOR:
        if (!DN.in) {
            fade(1); DN.in = 1; dn_walk_to(DX(1116.58), DZ(962.88), 16384); amb_set(-1); sfx(SFX_DOOR_OPEN);
            if (!DN.started) { DN.started = 1; DN.wx = DX(1117.0); DN.wz = DZ(958.7); w_goto(DX(1116.6), DZ(962.0), WN_START); }
            fade(0);
        } else if (DN.paid) {
            fade(1); DN.in = 0; dn_walk_to(DX(1116.58), DZ(964.6), 0); sfx(SFX_DOOR_CLOSE); fade(0);
        } else sub(S("GetFood"), 3);
        break;
    case H_SEAT: case H_SEAT2: case H_SEAT3: case H_SEAT4:
        fade(1); DN.seated = 1; DN.booth = id - H_SEAT; DN.seat_t = 40; dw.yaw = BOOTH[DN.booth].yaw; dw.pitch = 1400; fade(0);
        if (!DN.peed && !DN.pee_nag) { DN.pee_nag = 1; DN.pee_t = 400; sub_after(30, "HabitPee"); }
        break;
    case H_TOILET:
        if (DN.peed) { sub(S("IWasDoneWithIt"), 3); break; }
        fade(1); sfx(SFX_PEE); for (int i = 0; i < 180; i++) swiWaitForVBlank();
        DN.peed = 1; fade(0);
        break;
    case H_WAIT: DN.wstate = W_TALK; dlg_begin(DN.started ? "General" : "DinerStart", NULL); DN.started = 1; break;
    case H_FOOD:
        fade(1); sfx(SFX_EAT); for (int i = 0; i < 150; i++) swiWaitForVBlank();
        DN.eaten = 1; DN.bt = 40; DN.ae_t = 80; fade(0);   /* FinishEating: bad guy gets up in 2 s, waitress in 4 s */
        break;
    case H_SUIT: { static const char *const n[] = { "SuitGuy_1", "SuitGuy_2", "SuitGuy_3" }; dlg_begin(n[DN.talks[0]], NULL); if (DN.talks[0] < 2) DN.talks[0]++; break; }
    case H_TR1: { static const char *const n[] = { "Trucker1_1", "Trucker1_2" }; dlg_begin(n[DN.talks[1]], NULL); DN.talks[1] = 1; break; }
    case H_TR2: dlg_begin("Trucker2_1", NULL); break;
    case H_BAD: dlg_begin("Bad_Guy", NULL); break;
    case H_PLG: DN.plg_done = 1; dlg_begin("ParkingLotGuy", NULL); break;
    case H_RV: if (DN.paid) DN.leave = 1; else sub(S("GetFood"), 3); break;
    }
    hud_dirty = 1;
}

static void diner_scene(void)
{
    consoleClear(); printf("\n\n\n   Loading...\n");
    static const char *const npc[4] = { "nitro:/diner/trucker1.bin", "nitro:/diner/trucker2.bin", "nitro:/diner/suitguy.bin", "nitro:/diner/chef.bin" };
    int ok = load_model(&D_lvl, "nitro:/diner/diner.bin") && load_model(&D_door, "nitro:/diner/wdoor.bin")
          && load_model(&D_wait, "nitro:/diner/waitress.bin") && load_model(&D_bad, "nitro:/diner/badguy.bin")
          && load_model(&D_badsit, "nitro:/diner/badguy_sit.bin") && load_model(&D_food, "nitro:/diner/food.bin")
          && load_model(&D_plg, "nitro:/diner/plg.bin") && load_model(&D_rv, "nitro:/rv.bin")
          && load_wal("nitro:/diner/diner.wal") && load_floor("nitro:/diner/diner.flr");
    for (int i = 0; i < 4; i++) ok = ok && load_model(&D_npc[i], npc[i]);
    if (!ok) { printf("diner data missing\n"); for (int i = 0; i < 120; i++) swiWaitForVBlank(); }
    memset(&DN, 0, sizeof DN); memset(&dw, 0, sizeof dw);
    DN.wx = DX(1117.0); DN.wz = DZ(958.7); DN.wyaw = 0;
    DN.bx = DX(1114.9); DN.bz = DZ(955.6);
    const int RVX = DX(1107.25), RVZ = DZ(960.9), RVYAW = -8192;
    int rvy = floor_at(RVX, RVZ, U(3)); if (rvy == NOFLOOR) rvy = U(0.27);
    dn_walk_to(DX(1108.6), DZ(959.2), -8192);   /* out of the RV door, facing the diner */
#ifdef DINER_TEST
    dn_walk_to(DX(1116.6), DZ(964.8), 16384);
#endif
    scene_cmd = diner_cmd;
    prompt = NULL; sub_t = 0; hud_dirty = 1;
    const char *last = NULL;
    fade(0);
    while (ok && !DN.leave) {
        scanKeys();
        u32 k = keysHeld(), kd = keysDown();
        dw.grass = !DN.in;
        int talking = dlg_active;   /* the A press that ends a conversation must not also use a hotspot */
        if (dlg_active) dlg_input(kd);
#ifdef DINER_TEST
        { static int at; if (dlg_active && ++at > 12) { at = 0; dlg_input(KEY_A); } }
#endif
        if (dlg_active) ;
        else if (DN.seated) {
            walker_update(&dw, k & (KEY_Y | KEY_TOUCH | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT) & ((k & KEY_Y) ? ~0u : KEY_TOUCH));
            dw.x = DX(BOOTH[DN.booth].sx); dw.z = DZ(BOOTH[DN.booth].sz);
            if (kd & KEY_B) { fade(1); DN.seated = 0; dn_walk_to(DX(1116.36), DZ(BOOTH[DN.booth].sz), -8192); fade(0); hud_dirty = 1; }
        } else {
            int ox = dw.x, oz = dw.z;
            walker_update(&dw, k);
            if (!DN.in && abs(dw.x - RVX) < U(4.3) && abs(dw.z - RVZ) < U(1.5)) { dw.x = ox; dw.z = oz; }   /* parked RV */
        }
#ifdef DEBUG_POS
        if ((kd & KEY_L) && !DN.seated) {   /* washroom facing the toilet / beside booth 1 / by the RV */
            static int ti;
            if (ti == 0) dn_walk_to(DX(1120.0), DZ(951.4), -6550);
            else if (ti == 1) dn_walk_to(DX(1116.4), DZ(960.46), 8192);
            else { DN.in = 0; dn_walk_to(DX(1110.0), DZ(958.0), 8192); }
            ti = (ti + 1) % 3;
        }
#endif
        /* ---- hotspots */
        int eye = DN.seated ? U(1.76) : dw.fy + EYE;
        Hot H[] = {
            { DX(1116.6), U(1.5), DZ(963.6), DN.in ? "Leave" : "Enter the diner", H_DOOR },
            { DX(1115.25), U(1.1), DZ(960.46), DN.in && !DN.seated ? "Sit down" : NULL, H_SEAT },
            { DX(1115.32), U(1.1), DZ(952.57), DN.in && !DN.seated ? "Sit down" : NULL, H_SEAT2 },
            { DX(1115.32), U(1.1), DZ(949.54), DN.in && !DN.seated ? "Sit down" : NULL, H_SEAT3 },
            { DX(1115.32), U(1.1), DZ(948.56), DN.in && !DN.seated ? "Sit down" : NULL, H_SEAT4 },
            { DX(1121.41), U(0.9), DZ(951.86), DN.in && !DN.seated ? "Use the toilet" : NULL, H_TOILET },
            { DN.wx, U(1.6), DN.wz, DN.in && DN.wstate == W_IDLE && !DN.wt && DN.wander > 20 && !DN.seated ? "Talk to the waitress" : NULL, H_WAIT },
            { DX(1115.36), U(1.3), DZ(BOOTH[DN.booth].tz), DN.seated && DN.served && !DN.eaten ? "Eat" : NULL, H_FOOD },
            { DX(1118.73), U(1.4), DZ(960.38), DN.in && !DN.seated ? "Talk" : NULL, H_SUIT },
            { DX(1118.35), U(1.4), DZ(956.81), DN.in && !DN.seated ? "Talk" : NULL, H_TR1 },
            { DX(1115.15), U(1.4), DZ(961.64), DN.in && !DN.seated ? "Talk" : NULL, H_TR2 },
            { DN.bx, U(1.4), DN.bz, DN.in && !DN.seated && DN.bstate == 0 ? "Talk" : NULL, H_BAD },
            { DX(1107.23), U(1.5), DZ(939.34), !DN.in && !DN.plg_done ? "Talk" : NULL, H_PLG },
            { RVX, U(1.5), RVZ, !DN.in ? "Get in the RV" : NULL, H_RV },
        };
        int best = -1;
        if (!dlg_active && !talking) {
            Walker v = dw; v.fy = eye - EYE;
            best = hot_pick(&v, H, sizeof H / sizeof *H, DN.in ? U(2.1) : U(4.5));
        }
        prompt = best >= 0 ? H[best].label : NULL;
        if (prompt != last) { last = prompt; hud_dirty = 1; }
        if ((kd & KEY_A) && best >= 0 && !dlg_active) diner_use(H[best].id);

        /* ---- waitress */
        if (DN.wstate == W_GO) {
            if (npc_step(&DN.wx, &DN.wz, &DN.wyaw, DN.px[0], DN.pz[0], U(1.8) / 20)) {
                for (int i = 1; i < DN.np; i++) { DN.px[i - 1] = DN.px[i]; DN.pz[i - 1] = DN.pz[i]; }
                if (--DN.np <= 0) {
                    int n = DN.wnode; DN.wstate = W_IDLE; DN.wnode = WN_NONE;
                    if (n == WN_KITCHEN) DN.wstate = W_AWAY;
                    else if (n == WN_SERVE) { DN.served = 1; sfx_play(SFX_PICKUP, 160); }
                    else if (n) {
                        static const char *const NODE[] = { "", "DinerStart", "DinerOrder", "", "AfterEating", "Check" };
                        DN.wstate = W_TALK; DN.wyaw = yaw_to(dw.x - DN.wx, dw.z - DN.wz);
                        dlg_begin(NODE[n], NULL);
                    }
                }
            }
        } else if (DN.wstate == W_IDLE && !dlg_active) {
            if (DN.wt && --DN.wt == 0 && DN.seated && !DN.ordered) w_to_player(WN_ORDER);
            else if (!DN.wt && ++DN.wander > 60) {   /* random walk between the counter end and her stand */
                DN.wander = 0;
                if (DN.started && !(DN.seated && !DN.ordered)) w_goto(DX(1117.0), DZ((rand() & 1) ? 958.7 : 955.4), WN_NONE);
            }
        } else if (DN.wstate == W_AWAY && DN.wt && --DN.wt == 0) w_to_player(WN_CHECK);
        if (DN.wstate == W_TALK && !dlg_active) DN.wstate = W_IDLE;
        /* the order: she asks once seated; the food comes out once you've been to the washroom */
        if (DN.seated && !DN.ordered && !DN.wt && DN.wstate != W_GO && DN.wstate != W_TALK && !dlg_active && DN.seat_t && --DN.seat_t == 0) w_to_player(WN_ORDER);
        if (DN.food_t) DN.food_t--;
        if (DN.ordered && !DN.served && DN.peed && DN.seated && !DN.food_t && DN.wstate == W_AWAY) w_goto(DX(BOOTH[DN.booth].wx + 0.2), DZ(BOOTH[DN.booth].wz), WN_SERVE);
        if (DN.ae_t && !dlg_active && DN.wstate != W_GO && --DN.ae_t == 0 && DN.seated) w_to_player(WN_AFTEREAT);
        if (DN.seated && !DN.peed && DN.pee_t && --DN.pee_t == 0) { sub(S("ReallyPee"), 4); DN.pee_t = 400; }
        /* ---- bad guy: gets up after you eat, heads to the washroom */
        if (DN.bt && --DN.bt == 0 && DN.bstate == 0) { DN.bstate = 1; DN.bnp = route(DN.bpx, DN.bpz, DX(1116.4), DN.bz, DX(1120.51), DZ(950.93)); DN.bx = DX(1116.4); }
        if (DN.bstate == 1 && npc_step(&DN.bx, &DN.bz, &DN.byaw, DN.bpx[0], DN.bpz[0], U(1.1) / 20)) {
            for (int i = 1; i < DN.bnp; i++) { DN.bpx[i - 1] = DN.bpx[i]; DN.bpz[i - 1] = DN.bpz[i]; }
            if (--DN.bnp <= 0) DN.bstate = 2;
        }
        /* washroom door swings open when someone is close to it */
        {
            int hx = DX(1118.98), hz = DZ(950.99);
            int near = (DN.in && abs(dw.x - hx) + abs(dw.z - hz) < U(1.8)) || (DN.bstate == 1 && abs(DN.bx - hx) + abs(DN.bz - hz) < U(1.8));
            int tgt = near ? 7000 : 0;
            if (DN.door != tgt) { int d = tgt - DN.door; DN.door += d > 700 ? 700 : d < -700 ? -700 : d; }
        }
        if (sub_t > 0 && --sub_t == 0) hud_dirty = 1;
        for (int i = 0; i < 12; i++) if (evq[i].id == EV_SUB && --evq[i].t <= 0) { evq[i].id = 0; sub(S(evq[i].s), 4); }
        amb_set(DN.in ? -1 : SFX_AMB_NIGHT);

        /* ---- render */
        sky(2, 2, 4);
        glEnable(GL_FOG);
        g_lights = POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_FOG;
        g_amb = DN.in ? RGB15(13, 12, 10) : RGB15(5, 5, 7);
        cam_setup(dw.x, eye, dw.z, dw.yaw, dw.pitch, 60);
        glLight(0, DN.in ? RGB15(24, 22, 18) : RGB15(7, 7, 10), floattov10(0.3), floattov10(-0.9), floattov10(-0.3));
        glLight(1, DN.in ? RGB15(8, 8, 7) : RGB15(10, 9, 6), floattov10(-0.4), floattov10(-0.5), floattov10(0.7));
        for (int pass = 0; pass < 2; pass++) {
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | g_lights | POLY_ID(1));
            draw_level(&D_lvl, pass);
            draw_model(&D_door, DX(1118.98), 0, DZ(950.99), DN.door, pass);
            if (DN.in) {
                for (int i = 0; i < 4; i++) draw_level(&D_npc[i], pass);
                draw_model(&D_wait, DN.wstate == W_AWAY ? U(50) : DN.wx, U(0.45), DN.wz, DN.wyaw, pass);
                if (DN.bstate == 0) draw_level(&D_badsit, pass);
                else if (DN.bstate == 1) draw_model(&D_bad, DN.bx, U(0.45), DN.bz, DN.byaw, pass);
                if (DN.served && !DN.eaten) draw_model(&D_food, DX(1115.36), U(1.24), DZ(BOOTH[DN.booth].tz), BOOTH[DN.booth].yaw, pass);
            } else {
                draw_model(&D_rv, RVX, rvy, RVZ, RVYAW, pass);
                draw_model(&D_plg, DX(1107.2), U(0.27), DZ(939.3), 0, pass);
            }
        }
        while (GFX_STATUS & BIT(27)) ;
        stat_v = GFX_VERTEX_RAM_USAGE; stat_p = GFX_POLYGON_RAM_USAGE;
        glFlush(0);
        stat_vb = vb_count; while (vb_count < 3) swiWaitForVBlank();
        vb_count = 0;
#ifdef DEBUG_POS
        { static int t; if (++t >= 20) { t = 0; hud_dirty = 1; } }
#endif
        if (hud_dirty && !dlg_active) { dn_hud(); hud_dirty = 0; }
    }
    fade(1);
    scene_cmd = NULL; prompt = NULL; amb_set(-1);
    level_free(&D_lvl); level_free(&D_door); level_free(&D_wait); level_free(&D_bad); level_free(&D_badsit);
    level_free(&D_food); level_free(&D_plg); level_free(&D_rv);
    for (int i = 0; i < 4; i++) level_free(&D_npc[i]);
    walk_free(); glResetTextures();
}
