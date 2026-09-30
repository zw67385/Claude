#!/usr/bin/env python3
"""Convert the episode's audio clips to small mono 8-bit WAVs for Maxmod (rom/audio/).
SFX / ambience come from a fixed list (loops trimmed to a few seconds with a crossfade and a 'smpl'
loop chunk so they fit in DS RAM); radio voice-over comes from the RadioVoiceOver arrays in
Watchtower.unity (VO_SEQn index -> clip), named vo<seq>_<i> (hiker = seq 9).
also writes rom/source/vo_map.h: per-set list of soundbank ids."""
import os, re, struct, sys
import numpy as np, soundfile as sf
import unity
AC = os.path.join(unity.ROOT, "AudioClip")
OUT = sys.argv[1] if len(sys.argv) > 1 else "../rom/audio"
RATE = 11025

SFX = [  # name, file, loop seconds (0 = one-shot), max seconds, gain
    ("amb_night", "Ambient_42__Trees_Are_the_Thing__Loopable", 9, 0, 0.8),
    ("amb_wind", "wind-outside-sound-ambient-141989", 8, 0, 0.8),
    ("amb_evening", "evening amb", 9, 0, 0.8),
    ("amb_rain", "Rain 03", 7, 0, 0.8),
    ("amb_drone", "ambient_49__Bone_Drone_", 9, 0, 0.8),
    ("gen_on", "Generator turn on", 0, 4, 1),
    ("gen_run", "Generator Running", 3, 0, 0.8),
    ("door_open", "Door Open 2", 0, 2, 1), ("door_close", "Door Close 2", 0, 2, 1),
    ("static", "Radio Static", 3, 0, 0.6), ("radio_beep", "Radio Beep2", 0, 2, 1), ("beep", "Beep", 0, 1.5, 1),
    ("stove", "Stove Door 2", 0, 2, 1), ("fire", "Fire", 4, 0, 0.9), ("match", "Match Stick Lit 2", 0, 2.5, 1),
    ("wood", "Dropping wood", 0, 2, 1), ("knock", "Knock1", 0, 2, 1), ("powerout", "Computer Power Out Beep", 0, 2.5, 1),
    ("flare", "Flare Gun Shot", 0, 4, 1), ("shutter", "Camera Shutter", 0, 1.5, 1), ("blind", "Blind down", 0, 1.5, 1),
    ("fridge", "Fridge Open", 0, 1.5, 1), ("micro_end", "Microwave End", 0, 2, 1), ("oven", "Oven Open", 0, 1.5, 1),
    ("eat", "Eating MainFood", 0, 4, 1), ("pee", "Pee Outside", 0, 4, 1), ("click", "Click 1", 0, 1, 1),
    ("scare", "Ambient_50__Seen_By_Evil_", 0, 7, 1), ("harp", "Harp_Noise_Number_2", 0, 4, 1), ("gasp", "Gasp_instant", 0, 2, 1),
    ("gas", "Gas Can Hit", 0, 1.5, 1), ("pickup", "Keys catch", 0, 1, 1), ("typing", "KeyHit1", 0, 0.5, 0.7),
    ("shout", "Man Shout", 0, 3, 1), ("whistle", "Whistle Amb", 0, 5, 0.9), ("splash", "Water Splash", 0, 2, 1), ("sizzle", "Fire Sizzle", 0, 3, 1),
    ("crack", "Cracking snapping", 0, 3, 1),
    # First Scene: RV engine / start, diner jukebox, menu music, till
    ("rv_engine", "van_engine", 1.5, 0, 0.7), ("rv_start", "RV Start", 0, 3.4, 1),
    ("diner_music", "DedLighter Diner_Music_version_1", 45, 0, 0.7), ("menu_music", "menu music", 45, 0, 0.7),
    ("coins", "diner change sound", 0, 1, 1),
    # RV radio playlists (Radio.cs: First Scene plays clips 1,2,0 in turn; Trail Start one song), chase, credits, stingers
    ("rad1", "Master Distant Final (1)", 0, 0, 0.7, 8000), ("rad2", "Miss Me", 0, 0, 0.7, 8000), ("rad3", "come home", 0, 0, 0.7, 8000),
    ("rad_tue", "tuesday (108bpm)", 0, 0, 0.7, 8000), ("chase", "GET_TO_THE_CAR_VERSION_3", 0, 0, 0.8, 8000),
    ("credits", "Nostalgic Ending Credits 4", 0, 0, 0.7, 8000), ("knocks", "hard knocks outside rv", 0, 0, 1),
    ("tgate", "Trail gate open", 0, 0, 1), ("handbrake", "handbreak", 0, 0, 1), ("notif", "notif", 0, 0, 1),
    # scene-accurate extras (clips taken from each scene's AudioSource wiring)
    ("pop", "dialogue pop", 0, 1, 0.8), ("crickets", "circkets_tyops", 6, 0, 0.7),
    ("amb_bell", "Ambient_36__culling_but_with_bell_", 9, 0, 0.8), ("amb_eerie", "eerie atmosphere", 9, 0, 0.8),
    ("creepies", "creepies", 0, 8, 1, 8000), ("haunt", "scary haunting cinematic", 0, 8, 1, 8000),
    ("tension", "subtle tension", 0, 8, 1, 8000), ("violin_s", "scary violin fade out CC0 short", 0, 8, 1, 8000),
    ("violin", "scary violin fade out CC0", 0, 10, 1, 8000), ("eerie", "eerie_strings", 0, 10, 1, 8000),
    ("lowdeep", "low_and_deep_horror_tone", 0, 8, 1, 8000), ("mystery", "mystery builder", 0, 10, 1, 8000),
    ("siren", "Spooky siren", 0, 10, 1, 8000), ("rviolin", "Seq6 Park Ranger Violin", 0, 10, 1, 8000),
    ("jumpscare", "tommy jumpscare 2", 0, 4, 1), ("heartbeat", "heartbeat non fatal", 0, 8, 1, 8000),
    ("fheart", "futureistic heartbeat", 0, 6, 1, 8000), ("glitch", "Glitch Before Quit", 0, 3, 1),
    ("stab", "knife stab 1", 0, 2, 1), ("scream", "man scream", 0, 3, 1), ("hunt", "Hunt Far", 0, 8, 1, 8000),
    ("riser", "Post Riser Violin", 0, 10, 1, 8000), ("run_grass", "running on the grass", 2.5, 0, 0.9),
    ("realize", "cultist_realization", 0, 4, 1), ("question", "question", 0, 2, 1), ("plate", "Plate_put_down", 0, 1.5, 1),
    ("toilet", "peeing toilet", 0, 4, 1), ("flush", "flush", 0, 4, 1), ("handwash", "handwash at diner", 0, 3, 1),
    ("idoor_open", "Inside Door Open 3", 0, 2, 1), ("idoor_shut", "Inside Door Shut 3", 0, 2, 1), ("jammed", "Door Jammed", 0, 2, 1),
    ("juke2", "Before the End of Time (Mixed)", 45, 0, 0.7, 8000), ("fill", "Fill Bucket", 0, 3, 1),
    ("whistle2", "Whistle placeholder 2", 0, 5, 0.9), ("notif2", "notification sound new", 0, 2, 1), ("msgsent", "MessageSent1", 0, 2, 1),
    ("wolf", "wolf howl", 0, 6, 1), ("switch", "switch", 0, 1, 1), ("mic_open", "Microwave Open", 0, 1.5, 1),
    ("keyturn", "key turning", 0, 1.5, 1), ("startup", "Startup sound", 0, 4, 1), ("form", "form submitted", 0, 2, 1),
    ("error", "Error", 0, 2, 1), ("walkie1", "Walkie_Talkie_Sound_2_instant", 0, 2, 1), ("walkie2", "Walkie Talkie Sound 2", 0, 2, 1),
    ("cult_noticed", "That_is_a_cult_for_sure", 0, 4, 1), ("slam", "BigSlam1", 0, 2, 1), ("rv_accel", "RV Accelerate", 0, 3, 0.8),
    ("fridge_close", "Fridge Close", 0, 1.5, 1), ("drink", "Drinking Drink", 0, 3, 1),
    ("flash_on", "flashlight on", 0, 0, 1), ("flash_off", "flashlight off", 0, 0, 1), ("horn", "Horn", 0, 2, 1),
]
STEPS = [("step_wood%d" % i, "Carpet-%02d" % i) for i in (1, 2, 3, 4)] + [("step_grass%d" % i, "Grass_%02d" % i) for i in (1, 2, 3, 4)]

def find(stem):
    for ext in (".ogg", ".wav"):
        p = os.path.join(AC, stem + ext)
        if os.path.exists(p): return p
    for f in os.listdir(AC):
        if os.path.splitext(f)[0].lower() == stem.lower() and not f.endswith(".meta"): return os.path.join(AC, f)
    raise FileNotFoundError(stem)

def load(path, gain=1.0, rate=RATE):
    d, sr = sf.read(path, always_2d=True)
    d = d.mean(1)
    n = int(len(d) * rate / sr)
    d = np.interp(np.linspace(0, len(d) - 1, n), np.arange(len(d)), d)   # plain resample (low-passed by 8-bit anyway)
    pk = np.abs(d).max() or 1
    return d / pk * 0.95 * gain

def write(name, d, loop=False, rate=RATE):
    pcm = np.clip(d * 127 + 128, 0, 255).astype(np.uint8).tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if loop:   # before 'data': mmutil stops reading chunks once it has the samples
        smpl = struct.pack("<9I", 0, 0, 1000000000 // RATE, 60, 0, 0, 0, 1, 0) + struct.pack("<6I", 0, 0, 0, len(pcm) - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(pcm)) + pcm + (b"\0" if len(pcm) & 1 else b"")
    open(os.path.join(OUT, name + ".wav"), "wb").write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)
    return len(pcm)

def main():
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for name, stem, loop, mx, gain, *rt in SFX:
        rate = rt[0] if rt else RATE
        d = load(find(stem), gain, rate)
        if loop:   # take 'loop' seconds from the middle, crossfade the tail into the head
            L = int(loop * RATE); X = RATE // 2
            st = 0 if loop >= 20 else max(0, len(d) // 2 - (L + X) // 2); seg = d[st:st + L + X]   # music: from the top
            if len(seg) < L + X: seg = np.resize(d, L + X)
            fade = np.linspace(0, 1, X)
            out = seg[:L].copy(); out[:X] = seg[:X] * fade + seg[L:L + X] * (1 - fade)
            total += write(name, out, True)
        else:
            d = d[:int(mx * RATE)] if mx else d
            d[-min(200, len(d)):] *= np.linspace(1, 0, min(200, len(d)))
            total += write(name, d, rate=rate)
    for name, stem in STEPS:
        total += write(name, load(find(stem), 0.5)[:RATE // 2])
    # radio voice-over
    txt = open(os.path.join(unity.ROOT, "Scenes", "Watchtower.unity"), encoding="utf-8", errors="ignore").read()
    i = txt.index("  seq1RadioClips:")
    block = txt[i:txt.index("\n---", i)]
    G = unity.guid_map()
    sets = {}; cur = None
    for line in block.split("\n"):
        m = re.match(r"  (\w+)RadioClips:", line)
        if m: cur = m.group(1); sets[cur] = []; continue
        m = re.search(r"guid: (\w+)", line)
        if m and cur: sets[cur].append(G.get(m.group(1)))
    order = ["seq1", "seq3", "seq4", "seq5", "seq6", "seq7", "seq8", "hiker"]
    hdr = ["/* generated by tools/pack_audio.py: VO_<set> index -> audio file */"]
    for si, sname in enumerate(order):
        names = []
        for j, p in enumerate(sets.get(sname, [])):
            nm = "vo%s_%d" % (sname.replace("seq", ""), j)
            if p:
                total += write(nm, load(os.path.join(unity.ROOT, p), 1.0) * 1.0)
                names.append("SFX_" + nm.upper())
            else:
                names.append("-1")
        hdr.append("static const short VO_%s[] = { %s };" % (sname.upper(), ", ".join(names) or "-1"))
    open("../rom/source/vo_map.h", "w").write("\n".join(hdr) + "\n")
    print("audio bytes", total)

main()
