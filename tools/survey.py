import unity, meshio, os, sys, collections, re, yaml
import numpy as np
G = unity.guid_map()
import matio
def mat_info(guid):
    m=matio.load(guid)
    return None if not m else (m['name'], m['tex'], m['col'])
scene = unity.Scene(sys.argv[1])
roots = sys.argv[2:]
tot_tris = 0; texs = collections.Counter(); rows=[]
meshcache={}
def under(tf, root):
    while tf:
        g=scene.tf[tf]["go"]
        if scene.go[g]["name"] in roots and (not scene.tf[tf]["parent"] or scene.tf[tf]["parent"] not in scene.tf): return True
        tf=scene.tf[tf]["parent"]
        if tf not in scene.tf: break
    return False
n=0
for gf,g in scene.go.items():
    if not under(g.get("tf"), roots): continue
    if not scene.active_in_hierarchy(gf): continue
    mf = scene.comps(gf,33); mr = scene.comps(gf,23)
    if not mf or not mr: continue
    mg = mf[0][2]["m_Mesh"].get("guid")
    if mg not in G: continue
    if mg not in meshcache:
        try: meshcache[mg]=meshio.load(os.path.join(unity.ROOT,G[mg]))
        except Exception as e: meshcache[mg]=None
    m=meshcache[mg]
    if m is None: continue
    tris=sum(len(s) for s in m.subs)
    tot_tris+=tris; n+=1
    for mat in mr[0][2].get("m_Materials",[]):
        info = mat_info(mat.get("guid"))
        if info and info[1]: texs[info[1]]+=1
    rows.append((tris, scene.go[gf]["name"], m.name))
print("renderers",n,"total tris",tot_tris,"unique meshes",len(meshcache),"unique textures",len(texs))
for r in sorted(rows,reverse=True)[:25]: print(r)
sizes=[]
for t in texs:
    p=os.path.join(unity.ROOT,G[t]); 
    try:
        from PIL import Image
        im=Image.open(p); sizes.append((im.size[0]*im.size[1], im.size, os.path.basename(p)))
    except Exception as e: pass
sizes.sort(reverse=True); print("texture sizes (top):", sizes[:12]); print("total px", sum(s[0] for s in sizes))
