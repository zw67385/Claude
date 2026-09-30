/* Watchtower NPCs in 3D (Cultist.cs, ParkRangerTower.cs, BadGuyWatchTowerSeq6.cs, CultistCrowdSeq8.cs):
   Seq 5 the cultist walks the deck, crouches over the deer skull ritual and leaves down the stairs;
   Seq 6 ranger Billy at the door and the bad guy by the shack; Seq 8 the cult camp and the crowd.
   Tower frame: ORIGIN (315, 45.25, 327), z negated. Included from main.c after trailend.inc. */

#define WX(u) U((u) - 315)
#define WY(u) U((u) - 45.25)
#define WZ(u) U(327 - (u))
#define BAD_F0 21154                        /* the baked bad guy faces DS yaw +21154 (unity yaw 232.4) */
static Level N_skull, N_bad, N_camp;
static struct { int loaded, st, t, i, x, z, yaw, step, bx, bz, byaw, bst; } N;

static void tnpc_free(void)
{
    level_free(&C_cult); level_free(&T_ranger); level_free(&N_skull); level_free(&N_bad); level_free(&N_camp);
    N.loaded = 0;
}
static void tnpc_load(int n)
{
    tnpc_free(); memset(&N, 0, sizeof N);
    if (n == 5) N.loaded = load_model(&C_cult, "nitro:/camp/cultist.bin") && load_model(&N_skull, "nitro:/tower_npc/skull.bin");
    if (n == 6) N.loaded = load_model(&T_ranger, "nitro:/trail/ranger.bin") && load_model(&N_bad, "nitro:/tower_npc/badguy.bin");
    if (n == 8) N.loaded = load_model(&C_cult, "nitro:/camp/cultist.bin") && load_model(&N_camp, "nitro:/tower_npc/camp.bin");
    N.x = WX(318.43); N.z = WZ(329.66); N.yaw = 17858;          /* Seq 5: next to the stairs */
    N.bx = WX(305.97); N.bz = WZ(305.51); N.byaw = BAD_F0;       /* Seq 6: by the shack */
}

/* Seq 5 walk: the east deck, round the north side, to the west deck by the rail */
static const float CPATH[][2] = { { 318.62, 328.49 }, { 318.5, 330.9 }, { 311.1, 330.9 }, { 311.16, 325.96 } };
enum { N_IDLE, N_WALK, N_CROUCH, N_LEAVE, N_GONE };

/* per-frame logic; px/pz/fy is the player */
static void tnpc_update(int px, int pz)
{
    if (!N.loaded) return;
    if (seq == 5) {
        if (N.st == N_IDLE && G.cult_walk) { N.st = N_WALK; N.i = 0; }
        int moved = 0;
        if (N.st == N_WALK) {
            moved = 1;
            if (npc_step(&N.x, &N.z, &N.yaw, WX(CPATH[N.i][0]), WZ(CPATH[N.i][1]), U(1.4) / 20) && ++N.i == 4) {
                N.st = N_CROUCH; N.t = 0; N.yaw = yaw_to(WX(310.4) - N.x, WZ(326.01) - N.z);
            }
        } else if (N.st == N_CROUCH) {   /* DropAnimal(): crack, drop, the skull appears, then down the stairs */
            ++N.t;
            if (N.t == 10) sfx_play(SFX_CRACK, 110);
            if (N.t == 50) sfx_play(SFX_WOOD, 110);
            if (N.t == 290) { N.st = N_LEAVE; }
        } else if (N.st == N_LEAVE) {
            moved = 1;
            if (npc_step(&N.x, &N.z, &N.yaw, WX(311.03), WZ(330.72), U(1.4) / 20)) {
                N.st = N_GONE; G.cult_walk = 0; G.cult_gone = 1; sub(S("PresenceOutside"), 5);
            }
        }
        if (moved && ++N.step >= 11) {   /* his footsteps on the deck, fading with distance */
            static const short WD[] = { SFX_STEP_WOOD1, SFX_STEP_WOOD2, SFX_STEP_WOOD3, SFX_STEP_WOOD4 };
            int d = (abs(N.x - px) + abs(N.z - pz)) >> 12;
            N.step = 0; if (d < 20) sfx_play(WD[rand() & 3], 110 - d * 5);
        }
    }
    if (seq == 6 && G.badguy && !dlg_active && N.bst < 2) {   /* after the talk he walks off into the woods */
        N.bst = 1;
        if (npc_step(&N.bx, &N.bz, &N.byaw, WX(213.08), WZ(310.6), U(1.4) / 20)
            || (abs(N.bx - px) + abs(N.bz - pz)) > U(40)) N.bst = 2;
    }
}

static void tnpc_draw(int pass)
{
    if (!N.loaded) return;
    u32 a0 = g_amb;
    if (seq == 5) {
        if (N.st != N_IDLE && N.st != N_GONE) {
            int f = floor_at(N.x, N.z, WY(46)); if (f == NOFLOOR) f = WY(43.72);
            g_amb = RGB15(8, 8, 9);
            draw_model(&C_cult, N.x, f - (N.st == N_CROUCH ? U(0.5) : 0), N.z, N.yaw - CULT_F0, pass);
        }
        if ((N.st == N_CROUCH && N.t > 150) || N.st >= N_LEAVE) draw_model(&N_skull, 0, 0, 0, 0, pass);
    } else if (seq == 6) {
        if (G.knock || (G.billy && dlg_active)) {   /* Billy knocking at the door */
            int f = floor_at(WX(311.54), WZ(325.96), WY(46)); if (f == NOFLOOR) f = WY(43.68);
            draw_model(&T_ranger, WX(311.54), f, WZ(325.96), -8192 - RANGER_F0, pass);
        }
        if (G.eaten && G.report_done && N.bst < 2) {
            int f = floor_at(N.bx, N.bz, WY(30)); if (f == NOFLOOR) f = WY(27.09);
            draw_model(&N_bad, N.bx, f, N.bz, N.byaw - BAD_F0, pass);
        }
    } else if (seq == 8) {
        g_amb = RGB15(16, 11, 7);   /* lit by their campfire */
        draw_model(&N_camp, WX(320), WY(13), WZ(458), 0, pass);
        g_amb = a0;
        if (G.flash_off && !G.hidden) {   /* the crowd, lanterns up, staring at the tower */
            static const float CR[][3] = { { 274.86, 23.14, 338.05 }, { 268.28, 21.52, 333.81 }, { 272.25, 19.63, 348.87 }, { 262.44, 19.65, 332.13 } };
            g_amb = RGB15(12, 11, 10);
            for (int i = 0; i < 4; i++) {
                int x = WX(CR[i][0]), z = WZ(CR[i][2]);
                draw_model(&C_cult, x, WY(CR[i][1]), z, yaw_to(-x, -z) - CULT_F0, pass);
            }
        }
    }
    g_amb = a0;
}
