# -*- coding: utf-8 -*-
"""
LATARNIA MORSKA v2 - parametryczna maketa pod lampe OSRAM LIGHTsignal HAL
(70W 24V, podstawa fi150, 3x M6 na okregu fi130 - wg PDF G15099283)

Czesci do druku:
  01_podstawa        - 230x230, skaly, sciezka, komora elektroniki od spodu
  02_pokrywa_dolna   - pokrywa komory z wentylacja (wkrety 4x)
  03_wieza_dolna     - sekcja 0..185 (drzwi, okna, pokoik)
  04_wieza_gorna     - sekcja 185..386 (okna, pokoik, korbel, balkon, platforma lampy)
  05_drzwi           - drzwi nakladane, zawias: pin z filamentu 1.75mm

Konwencja katow: ang = azymut w stopniach (0=+X, 90=+Y, 180=-X, 270=-Y=front).
Sciezka i drzwi sa z przodu (-Y).

Jednostki: mm.  Master do Fusion 360: latarnia_assembly.step
"""

import math
import random
import os
import cadquery as cq

random.seed(42)

# ============================================================
# PARAMETRY
# ============================================================

# --- podstawa: cokol techniczny ukryty pod skalami ---
BASE_W       = 206.0   # cokol; komora 170x170x44 (zasilacz 160x95x40 + luz)
BASE_D       = 206.0
BASE_H       = 50.0    # przeswit komory 44 mm
BASE_DECK_T  = 6.0
BASE_WALL    = 18.0
COVER_T      = 3.0
FOOT_MAX     = 228.0   # twardy limit stolu (230) z marginesem

# --- skalny pagorek ---
TOWER_SEAT   = 64.0    # wieza siedzi na kopcu, nie na cokole (+14 mm)
MOUND_R_TOP  = 86.0    # plaskowyz pod wieza (srodek przesuniety w +Y -> lagodny skos)
MOUND_R_BOT  = 106.0
MOUND_Z_BOT  = 46.0
MOUND_OFF_Y  = 8.0
PASS_W       = 96.0    # przelot komora->wieza: prostokat zaokraglony
PASS_D       = 76.0    # (zasilacz 99x82x30 wchodzi na plasko, lekko po skosie)
PASS_OFF_Y   = 6.0
PASS_RC      = 14.0    # promien naroznikow

# --- skrecanie wieza<->podstawa: 3x M3x16 + insert w plaskowyzu ---
BTAB_AZ      = (0.0, 90.0, 180.0)   # 270 wolne (drzwi/schody)
BTAB_W       = 16.0
BSCREW_R     = 58.0

# --- wieza ---
TOWER_OD_BOT = 150.0
TOWER_OD_TOP = 104.0
TOWER_H      = 350.0
WALL_T       = 7.0
SPLIT_Z      = 185.0
LIP_H        = 8.0
LIP_T        = 3.0
LIP_CLR      = 0.25

# --- platforma / galeria ---
PLAT_D       = 200.0
PLAT_T       = 10.0
CORBEL_H     = 26.0
RAIL_H       = 26.0
RAIL_POSTS   = 16
RAIL_POST_D  = 4.0
RAIL_RING_D  = 4.0

# --- lampa OSRAM (PDF) ---
LAMP_BCD     = 130.0   # 3x M6 na okregu fi130
LAMP_HOLE_D  = 6.6     # przelot M6
LAMP_NUT_AC  = 12.0    # kieszen hex pod nakretke M6 (na wcisk, srednica po rogach)
LAMP_NUT_T   = 5.6     # glebokosc kieszeni
PIN_D        = 2.1     # otwory na kolki pasujace z filamentu 1.75

# --- skrecanie sekcji wiezy: 3x M3x16 + insert wtapiany ---
TAB_AZ       = (0.0, 120.0, 240.0)   # azymuty wspornikow (z dala od okien 90/270)
TAB_W        = 16.0                  # szerokosc lapy
TAB_RO       = 58.2                  # zewn. krawedz (zatopiona w scianie)
TAB_RI       = 41.5                  # wewn. krawedz
SCREW_R      = 48.0                  # promien osi sruby
INSERT_D     = 4.0                   # otwor pod insert M3 (OD~4.6); bez insertu daj 2.8
                       # (alternatywa: 6.6 przelot + nakretka, albo 8.0 pod insert)
CABLE_HOLE_D = 16.0

# --- drzwi ---
DOOR_W       = 38.0    # szerokosc otworu
DOOR_H       = 64.0    # szczyt luku otworu (od z=0)
DOOR_SILL    = 6.0     # prog
DOOR_T       = 3.2
FRAME_T      = 4.0
DOOR_ANG     = 270.0   # front
HINGE_PIN_D  = 2.0     # otwor na pin = kawalek filamentu 1.75

# --- okna ---
WIN_W        = 17.0
WIN_H        = 30.0
WIN_FRAME    = 3.0
WIN_LEVELS   = [80.0, 150.0, 240.0, 285.0]
WIN_PER_LVL  = [3, 2, 4, 2]
WIN_ROT0     = [30, 90, 0, 90]     # azymut pierwszego okna na poziomie
                                    # wszystkie poziomy symetryczne do osi drzwi (270)
ROOM_LEVELS  = [0, 2]               # pokoiki w srodku
FLOOR_HOLE_D = 44.0                 # otwor w podlodze pokoiku (kable+wentylacja)

# --- detale ---
BAND_ZS      = [40.0, 115.0, 185.0, 276.5, 330.0]
BAND_T       = 4.0
BAND_PROUD   = 2.2

EPS = 0.01


def r_out(z):
    t = z / TOWER_H
    return (TOWER_OD_BOT / 2) * (1 - t) + (TOWER_OD_TOP / 2) * t


def r_in(z):
    return r_out(z) - WALL_T


def cone(r1, r2, h, z0=0.0):
    return cq.Solid.makeCone(r1, r2, h, pnt=cq.Vector(0, 0, z0))


def rot(s, ang):
    return s.rotate((0, 0, 0), (0, 0, 1), ang)


def arch_profile(wp, w, h):
    """Luk: prostokat + polkole, wycentrowany w X, od 0 w gore."""
    straight = h - w / 2
    return (wp.moveTo(-w / 2, 0)
              .lineTo(-w / 2, straight)
              .threePointArc((0, h), (w / 2, straight))
              .lineTo(w / 2, 0)
              .close())


def radial_cutter(w, h, z_sill, ang, depth_out=15.0):
    """Pryzma lukowa do wycinania otworow; azymut = ang."""
    length = depth_out + WALL_T + 35.0
    sk = (arch_profile(cq.Workplane("XZ").workplane(offset=-(r_out(z_sill) + depth_out)), w, h)
          .extrude(length)
          .translate((0, 0, z_sill)))
    return rot(sk, ang - 90)


def radial_block(w, h, t, z_sill, ang, r_center):
    """Lukowy klocek 'naklejony' radialnie; azymut = ang."""
    sk = (arch_profile(cq.Workplane("XZ").workplane(offset=-(r_center + t / 2)), w, h)
          .extrude(t)
          .translate((0, 0, z_sill)))
    return rot(sk, ang - 90)


def front_box(w, depth, hgt, r_center, z_center, ang, x_off=0.0):
    """Box zwrocony radialnie (lokalnie budowany na -Y); azymut = ang."""
    b = (cq.Workplane("XY").box(w, depth, hgt)
         .translate((x_off, -r_center, z_center)))
    return rot(b, ang - 270)


# ============================================================
# WIEZA
# ============================================================

tower = cq.Workplane(obj=cone(TOWER_OD_BOT / 2, TOWER_OD_TOP / 2, TOWER_H))

# gzymsy
for bz in BAND_ZS:
    rb = r_out(bz) + BAND_PROUD
    tower = tower.union(cq.Workplane(
        obj=cq.Solid.makeCone(rb, rb - 1.0, BAND_T, pnt=cq.Vector(0, 0, bz - BAND_T / 2))))

# --- okna: specyfikacja ---
window_specs = []
for lvl, z in enumerate(WIN_LEVELS):
    n = WIN_PER_LVL[lvl]
    for i in range(n):
        window_specs.append((lvl, z, (WIN_ROT0[lvl] + i * 360.0 / n) % 360.0))

frames, cutters, sills, mullions, rooms = [], [], [], [], []

for lvl, z, ang in window_specs:
    rc = r_out(z + WIN_H / 2)
    # rama
    frames.append(radial_block(WIN_W + 2 * WIN_FRAME, WIN_H + 2 * WIN_FRAME, 8.0,
                               z - WIN_FRAME, ang, rc + 0.5))
    # parapet
    sills.append(front_box(WIN_W + 2 * WIN_FRAME + 5, 8.0, 2.5,
                           rc + 1.0, z - WIN_FRAME - 1.25, ang))
    # otwor
    cutters.append(radial_cutter(WIN_W, WIN_H, z, ang))
    # szprosy
    mullions.append(front_box(2.2, WALL_T + 2, WIN_H, rc - 2.0, z + WIN_H / 2, ang))
    mullions.append(front_box(WIN_W + 2, WALL_T + 2, 2.2, rc - 2.0, z + WIN_H * 0.45, ang))
    # pokoik: polka + tylna scianka tylko za oknem - srodek wiezy zostaje
    # otwarty (dostep reka do srub sekcji od dolu + komin wentylacyjny)
    if lvl in ROOM_LEVELS:
        ri = r_in(z)
        inner_region = cq.Workplane(obj=cone(r_in(0) - 0.1, r_in(TOWER_H) - 0.1, TOWER_H))
        # polka jako wspornik 45 stopni (drukuje sie bez podpor)
        shelf = (cq.Workplane("XZ")
                 .polyline([(ri - 26.0, z), (ri + 2.0, z),
                            (ri + 2.0, z - 31.0), (ri - 26.0, z - 3.0)])
                 .close().extrude(WIN_W + 30)
                 .translate((0, (WIN_W + 30) / 2, 0))
                 .rotate((0, 0, 0), (0, 0, 1), ang))
        backwall = front_box(WIN_W + 30, 3.0, WIN_H + 8, ri - 26.0,
                             z + (WIN_H + 8) / 2 - 2.0, ang)
        rooms.append(shelf.intersect(inner_region))
        rooms.append(backwall.intersect(inner_region))

# --- drzwi: otwor + rama + kwadry + zawiasy ---
H_OPEN = DOOR_H - DOOR_SILL
rc_door = r_out(DOOR_SILL + H_OPEN / 2)

frames.append(radial_block(DOOR_W + 2 * FRAME_T, H_OPEN + FRAME_T, 12.0,
                           DOOR_SILL - 1.0, DOOR_ANG, rc_door - 0.5))

# kwadry kamienne po bokach
qz = DOOR_SILL + 2.0
for qw, qh in [(10, 7), (7, 7), (10, 7), (7, 7), (10, 7)]:
    for side in (-1, 1):
        frames.append(front_box(qw, 12.0, qh, rc_door - 2.5,
                                qz + qh / 2, DOOR_ANG,
                                x_off=side * (DOOR_W / 2 + FRAME_T + qw / 2 - 2)))
    qz += qh + 2.0

cutters.append(radial_cutter(DOOR_W, H_OPEN, DOOR_SILL, DOOR_ANG))

# zawiasy ramy: 2 knykcie z otworem fi2 (pin = filament 1.75, wsuwany od gory)
HINGE_X   = -(DOOR_W / 2 + FRAME_T + 3.5)   # os zawiasu, lewa strona (-26.5)
HINGE_R   = rc_door + 5.0                   # os w plaszczyznie lica ramy
KNUCKLE_D = 7.0
hinge_knuckles = []
hinge_holes = []
for zc in (DOOR_SILL + 12.0, DOOR_SILL + 46.0):
    k = (cq.Workplane("XY").cylinder(10.0, KNUCKLE_D / 2)
         .translate((HINGE_X, -HINGE_R, zc)))
    # zebro laczace knykiec z rama drzwi
    rib = (cq.Workplane("XY").box(8.0, 6.0, 10.0)
           .translate((HINGE_X + 1.5, -(HINGE_R - 2.0), zc)))
    hinge_knuckles.append(rot(k.union(rib), DOOR_ANG - 270))
# otwor na pin: przelotowy przez oba knykcie i rurke drzwi
hinge_holes.append(rot(
    cq.Workplane("XY").cylinder(60.0, HINGE_PIN_D / 2)
    .translate((HINGE_X, -HINGE_R, DOOR_SILL + 29.0)), DOOR_ANG - 270))

# --- skladanie wiezy ---
print("[*] wieza: ramy/gzymsy/parapety ...")
for f in frames:
    tower = tower.union(f)
for s in sills:
    tower = tower.union(s)
for k in hinge_knuckles:
    tower = tower.union(k)

print("[*] wieza: drazenie wnetrza ...")
tower = tower.cut(cq.Workplane(obj=cone(r_in(0), r_in(TOWER_H), TOWER_H - EPS)))

print("[*] wieza: okna/drzwi ...")
for c in cutters:
    tower = tower.cut(c)
for h in hinge_holes:
    tower = tower.cut(h)

print("[*] wieza: szprosy/pokoiki ...")
for m in mullions:
    tower = tower.union(m)
for rsolid in rooms:
    tower = tower.union(rsolid)

# ============================================================
# KORBEL + PLATFORMA + BARIERKA
# ============================================================

print("[*] platforma / galeria ...")
corbel = cq.Workplane(obj=cone(r_out(TOWER_H - CORBEL_H), PLAT_D / 2 - 4.0,
                               CORBEL_H, z0=TOWER_H - CORBEL_H))
corbel = corbel.cut(cq.Workplane(obj=cone(r_in(TOWER_H - CORBEL_H),
                                          PLAT_D / 2 - 4.0 - WALL_T,
                                          CORBEL_H, z0=TOWER_H - CORBEL_H)))
tower = tower.union(corbel)

plat = (cq.Workplane("XY").cylinder(PLAT_T, PLAT_D / 2)
        .translate((0, 0, TOWER_H + PLAT_T / 2)))
plat = plat.edges(">Z").chamfer(1.2)

# 3x M6 na fi130: przelot + kieszen hex na nakretke od spodu + przepust kabla
for k in range(3):
    a = math.radians(90 + k * 120)
    hx, hy = (LAMP_BCD / 2) * math.cos(a), (LAMP_BCD / 2) * math.sin(a)
    plat = plat.cut(cq.Workplane("XY").cylinder(PLAT_T + 2, LAMP_HOLE_D / 2)
                    .translate((hx, hy, TOWER_H + PLAT_T / 2)))
# kieszenie hex na wciskane nakretki M6 (od spodu platformy)
for k in range(3):
    a = math.radians(90 + k * 120)
    plat = plat.cut(cq.Workplane("XY")
                    .polygon(6, LAMP_NUT_AC).extrude(LAMP_NUT_T)
                    .translate(((LAMP_BCD / 2) * math.cos(a),
                                (LAMP_BCD / 2) * math.sin(a), TOWER_H)))

plat = plat.cut(cq.Workplane("XY").cylinder(PLAT_T + 2, CABLE_HOLE_D / 2)
                .translate((0, 0, TOWER_H + PLAT_T / 2)))

# szczeliny wentylacyjne poza obrysem lampy (fi150)
for k in range(8):
    slot = (cq.Workplane("XY").box(14, 4, PLAT_T + 2)
            .translate((0, (PLAT_D / 2 + 150 / 2) / 2 - 2, TOWER_H + PLAT_T / 2)))
    plat = plat.cut(rot(slot, k * 45 + 22.5))

tower = tower.union(plat)

# ============================================================
# KOREK POD PLATFORMA LAMPY  (drukowalnosc + przepust kabla)
# Problem: wnetrze pod platforma bylo zamkniete -> podpor druku nie ma
# jak wyjac, a kabel nie mial czystej drogi. Rozwiazanie: zalewamy gore
# na pelno (rdzen stozka wiezy + wnetrze korbla). Spod korka to stozek
# samonosny (~58 st od pionu) opadajacy do sciany w PLUG_BOT_Z, wiec
# drukuje sie bez podpor i bez wiszacych "sufitow". Srodkiem pionowy
# przepust na kabel. Sruby M6 + kieszenie nakretek + okna serwisowe
# wycinamy PONOWNIE, juz po wypelnieniu (korek je zalewa).
# Granica PLUG_BOT_Z jest nad oknami poziomu z=285 (ich rama siega ~318).
# ============================================================
print("[*] wieza: korek pod platforma (pelne wypelnienie + przepust) ...")

PLUG_BOT_Z  = 320.0                 # spod korka (nad gornymi oknami)
PLUG_APEX_Z = TOWER_H - 1.0         # wierzcholek stozka spodu tuz pod platforma
PLUG_CABLE_D = CABLE_HOLE_D + 2.0   # przepust kabla nieco luzniejszy do przewleczenia
_r_plug_bot = r_in(PLUG_BOT_Z)

# 1) pelna bryla wnetrza od PLUG_BOT_Z w gore (rdzen wiezy + wnetrze korbla)
_inner_fill = cq.Workplane(obj=cone(r_in(0), r_in(TOWER_H), TOWER_H))
_inner_fill = _inner_fill.union(cq.Workplane(obj=cone(
    r_in(TOWER_H - CORBEL_H), PLAT_D / 2 - 4.0 - WALL_T, CORBEL_H,
    z0=TOWER_H - CORBEL_H)))
_inner_fill = _inner_fill.intersect(
    cq.Workplane("XY").box(800.0, 800.0, (TOWER_H + 60.0) - PLUG_BOT_Z)
    .translate((0, 0, (PLUG_BOT_Z + TOWER_H + 60.0) / 2)))

# 2) wytnij stozek (szeroki u dolu -> ostry u gory) => samonosny spod korka
_funnel = cq.Workplane(obj=cq.Solid.makeCone(
    _r_plug_bot + 1.0, 0.5, PLUG_APEX_Z - PLUG_BOT_Z,
    pnt=cq.Vector(0, 0, PLUG_BOT_Z)))
tower = tower.union(_inner_fill.cut(_funnel))

# 3) przepust kabla: pionowy kanal srodkiem (platforma -> wnetrze wiezy)
tower = tower.cut(cq.Workplane("XY").cylinder((TOWER_H + 8.0) - PLUG_BOT_Z,
                                              PLUG_CABLE_D / 2)
                  .translate((0, 0, (PLUG_BOT_Z + TOWER_H + 8.0) / 2)))

# 4) ponowne mocowanie lampy (korek je zalal): przelot M6 + kieszen hex
for k in range(3):
    a = math.radians(90 + k * 120)
    hx, hy = (LAMP_BCD / 2) * math.cos(a), (LAMP_BCD / 2) * math.sin(a)
    tower = tower.cut(cq.Workplane("XY").cylinder(28.0, LAMP_HOLE_D / 2)
                      .translate((hx, hy, TOWER_H + 4.0 - 14.0)))     # przelot + luz na grot
    tower = tower.cut(cq.Workplane("XY")
                      .polygon(6, LAMP_NUT_AC).extrude(LAMP_NUT_T + 0.4)
                      .translate((hx, hy, TOWER_H - LAMP_NUT_T)))     # kieszen nakretki M6
    # sfazowanie stropu kieszeni: plaski pierscien (Ø6.6->Ø12) -> stozek ~48 st,
    # czyli samonosny strop drukowany od dolu (bez podpor wewnatrz kieszeni).
    tower = tower.cut(cq.Workplane(obj=cq.Solid.makeCone(
        LAMP_NUT_AC / 2, LAMP_HOLE_D / 2, 3.0,
        pnt=cq.Vector(hx, hy, TOWER_H + LAMP_NUT_T))))               # 6.0 -> 3.3 mm na h=3.0

# okna serwisowe w korbelu - dostep z dolu do kieszeni nakretek
for k in range(3):
    a = math.radians(90 + k * 120)
    tower = tower.cut(cq.Workplane("XY").cylinder(22.0, 6.5)
                      .translate(((LAMP_BCD / 2) * math.cos(a),
                                  (LAMP_BCD / 2) * math.sin(a),
                                  TOWER_H - 0.2 - 11.0)))

# barierka
rail_r = PLAT_D / 2 - RAIL_POST_D / 2 - 1.0
for k in range(RAIL_POSTS):
    a = math.radians(k * 360.0 / RAIL_POSTS)
    tower = tower.union(cq.Workplane("XY").cylinder(RAIL_H, RAIL_POST_D / 2)
                        .translate((rail_r * math.cos(a), rail_r * math.sin(a),
                                    TOWER_H + PLAT_T + RAIL_H / 2)))
tower = tower.union(cq.Workplane(obj=cq.Solid.makeTorus(
    rail_r, RAIL_RING_D / 2, pnt=cq.Vector(0, 0, TOWER_H + PLAT_T + RAIL_H))))
tower = tower.union(cq.Workplane(obj=cq.Solid.makeTorus(
    rail_r, RAIL_RING_D / 2 - 0.8, pnt=cq.Vector(0, 0, TOWER_H + PLAT_T + RAIL_H * 0.55))))

# ============================================================
# PODZIAL NA SEKCJE + ZAMEK
# ============================================================

# lapy do skrecenia wiezy z podstawa (3x M3x16, inserty w plaskowyzu)
for azd in BTAB_AZ:
    bt = (cq.Workplane("XY").box(18.2, BTAB_W, 12.0)
          .translate(((51.0 + 69.2) / 2, 0, 6.0)))
    bgus = (cq.Workplane("XZ")
            .polyline([(51.0, 12.0), (69.2, 12.0), (69.2, 26.0)])
            .close().extrude(BTAB_W).translate((0, BTAB_W / 2, 0)))
    bt = bt.union(bgus)
    bt = bt.cut(cq.Workplane("XY").cylinder(20.0, 1.7)
                .translate((BSCREW_R, 0, 5.0)))            # przelot 3.4
    bt = bt.cut(cq.Workplane("XY").cylinder(14.0, 3.3)
                .translate((BSCREW_R, 0, 7.0 + 7.0)))      # tunel leb/klucz od gory
    tower = tower.union(bt.rotate((0, 0, 0), (0, 0, 1), azd))

# otwory na kolki pasujace (filament 1.75) - ciete PRZED podzialem,
# wiec obie polowki dostaja idealnie wspolosiowe gniazda
rc_pin_split = r_out(SPLIT_Z) - 2.0
for ang in (45, 135, 225, 315):
    a = math.radians(ang)
    tower = tower.cut(cq.Workplane("XY").cylinder(16.0, PIN_D / 2)
                      .translate((rc_pin_split * math.cos(a),
                                  rc_pin_split * math.sin(a), SPLIT_Z)))

# 3x kolek orientujacy wieza<->podstawa (asymetrycznie = drzwi zawsze na schody)
rc_pin_seat = (r_in(0) + r_out(0)) / 2.0
SEAT_PIN_ANGS = (90, 210, 325)
for ang in SEAT_PIN_ANGS:
    a = math.radians(ang)
    tower = tower.cut(cq.Workplane("XY").cylinder(16.0, PIN_D / 2)
                      .translate((rc_pin_seat * math.cos(a),
                                  rc_pin_seat * math.sin(a), 0)))

print("[*] podzial ...")
big = 700.0
below = cq.Workplane("XY").box(big, big, SPLIT_Z * 2)
above = cq.Workplane("XY").box(big, big, 2000).translate((0, 0, SPLIT_Z + 1000))

tower_lower = tower.intersect(below)
tower_upper = tower.intersect(above)

r_split_in = r_in(SPLIT_Z)
lip = (cq.Workplane("XY").cylinder(LIP_H, r_split_in)
       .translate((0, 0, SPLIT_Z + LIP_H / 2)))
lip = lip.cut(cq.Workplane("XY").cylinder(LIP_H + 2, r_split_in - LIP_T)
              .translate((0, 0, SPLIT_Z + LIP_H / 2)))
# przerwy w lipie na wsporniki srubowe gornej sekcji
for azd in TAB_AZ:
    lip = lip.cut(cq.Workplane("XY").box(15.0, TAB_W + 8.0, LIP_H + 6.0)
                  .translate((56.0, 0, SPLIT_Z + LIP_H / 2))
                  .rotate((0, 0, 0), (0, 0, 1), azd))
tower_lower = tower_lower.union(lip)

tower_upper = tower_upper.cut(
    cq.Workplane("XY").cylinder(LIP_H + LIP_CLR, r_split_in + LIP_CLR)
    .translate((0, 0, SPLIT_Z + (LIP_H + LIP_CLR) / 2)))

# wsporniki srubowe M3x16 (skrecanie sekcji zamiast klejenia)
# dolna lapa: przelot 3.4 + tunel na leb/klucz od dolu; gorna: kieszen insertu M3
for azd in TAB_AZ:
    # --- dolna sekcja ---
    tab_l = (cq.Workplane("XY")
             .box(TAB_RO - TAB_RI, TAB_W, 12.75)
             .translate(((TAB_RO + TAB_RI) / 2, 0, SPLIT_Z - 0.25 - 12.75 / 2)))
    gus = (cq.Workplane("XZ")
           .polyline([(TAB_RI, SPLIT_Z - 13.0), (TAB_RO, SPLIT_Z - 13.0),
                      (TAB_RO, SPLIT_Z - 27.0)])
           .close().extrude(TAB_W).translate((0, TAB_W / 2, 0)))
    tab_l = tab_l.union(gus)
    tab_l = tab_l.cut(cq.Workplane("XY").cylinder(20.0, 1.7)
                      .translate((SCREW_R, 0, SPLIT_Z - 6.0)))          # przelot 3.4
    tab_l = tab_l.cut(cq.Workplane("XY").cylinder(15.0, 3.2)
                      .translate((SCREW_R, 0, SPLIT_Z - 8.5 - 7.5)))    # tunel leb/klucz
    tower_lower = tower_lower.union(tab_l.rotate((0, 0, 0), (0, 0, 1), azd))

    # --- gorna sekcja ---
    tab_u = (cq.Workplane("XY")
             .box(TAB_RO - TAB_RI, TAB_W, 13.0)
             .translate(((TAB_RO + TAB_RI) / 2, 0, SPLIT_Z + 13.0 / 2)))
    tab_u = tab_u.cut(cq.Workplane("XY").cylinder(16.0, 1.7)
                      .translate((SCREW_R, 0, SPLIT_Z + 6.5)))          # przelot 3.4
    tab_u = tab_u.cut(cq.Workplane("XY").box(12.0, 5.75, 2.8)
                      .translate((45.5, 0, SPLIT_Z + 5.0)))             # slot nakretki
    tower_upper = tower_upper.union(tab_u.rotate((0, 0, 0), (0, 0, 1), azd))

# ============================================================
# DRZWI (nakladane, osobny print, lezace plasko)
# ============================================================

print("[*] drzwi ...")
DW = DOOR_W + 5.0       # nakladka: szersze niz otwor
DH = H_OPEN + 3.0
door = arch_profile(cq.Workplane("XY"), DW, DH).extrude(DOOR_T)
# deski
for i in range(1, 4):
    door = door.cut(cq.Workplane("XY").box(1.0, DH * 2, 1.2)
                    .translate((-DW / 2 + i * DW / 4, DH / 2, DOOR_T)))
# okucia
for fz in (DH * 0.20, DH * 0.60):
    door = door.union(cq.Workplane("XY").box(DW - 3, 5, 1.2)
                      .translate((0, fz, DOOR_T + 0.6)))
# klamka
door = door.union(cq.Workplane("XY").cylinder(3.0, 2.2)
                  .translate((DW / 2 - 6, DH * 0.42, DOOR_T + 1.5)))
# knykiec drzwi: rurka wzdluz wysokosci, wchodzi miedzy knykcie ramy
# os zawiasu: x = HINGE_X, radialnie = lico ramy => z_local = -0.5
tube_y0, tube_y1 = 18.5, 38.5
TUBE_ZC = -0.5
tube = (cq.Workplane("XZ").workplane(offset=-tube_y1)
        .center(HINGE_X, TUBE_ZC)
        .circle(KNUCKLE_D / 2 - 0.4).extrude(tube_y1 - tube_y0))
tube = tube.cut(cq.Workplane("XZ").workplane(offset=-(tube_y1 + 5))
                .center(HINGE_X, TUBE_ZC)
                .circle(HINGE_PIN_D / 2 + 0.1).extrude(tube_y1 - tube_y0 + 10))
web = (cq.Workplane("XY").box(8.0, tube_y1 - tube_y0, DOOR_T)
       .translate((HINGE_X + 4.0, (tube_y0 + tube_y1) / 2, DOOR_T / 2)))
door = door.union(web).union(tube)

# ============================================================
# PODSTAWA
# ============================================================

print("[*] podstawa ...")

# cokol techniczny (komora elektroniki) - docelowo niewidoczny
base = cq.Workplane("XY").box(BASE_W, BASE_D, BASE_H).translate((0, 0, BASE_H / 2))
base = base.edges("|Z").fillet(10.0)

cav = (cq.Workplane("XY")
       .box(BASE_W - 2 * BASE_WALL, BASE_D - 2 * BASE_WALL, BASE_H - BASE_DECK_T)
       .translate((0, 0, (BASE_H - BASE_DECK_T) / 2 - EPS)))
cav = cav.edges("|Z").fillet(4.0)

# skalny pagorek: stozek z przesunietym wierzcholkiem (lagodny skos w strone frontu)
mound = cq.Workplane(obj=cq.Solid.makeCone(
    MOUND_R_BOT, MOUND_R_TOP, TOWER_SEAT + 4.0 - MOUND_Z_BOT,
    pnt=cq.Vector(0, MOUND_OFF_Y, MOUND_Z_BOT)))
base = base.union(mound)


def mound_h(x, y):
    """przyblizona wysokosc terenu kopca w punkcie (x, y)"""
    d = math.hypot(x, y - MOUND_OFF_Y)
    if d <= MOUND_R_TOP:
        return TOWER_SEAT + 4.0
    if d >= MOUND_R_BOT:
        return BASE_H
    t = (d - MOUND_R_TOP) / (MOUND_R_BOT - MOUND_R_TOP)
    return (TOWER_SEAT + 4.0) * (1 - t) + MOUND_Z_BOT * t


# skaly - helper; kazda skala przycinana od razu do obrysu stolu i z>=0
# (globalny intersect na ~50 nakladajacych sie brylach bywa zawodny w OCC)
CLIP_BASE = (cq.Workplane("XY").box(FOOT_MAX, FOOT_MAX, 300)
             .translate((0, 0, 150)))


def make_rock(x, y, r, zc, cuts=4, clip=None):
    # wielościan: blok ścinany płaszczyznami -> spękana, kanciasta skała
    sx = random.uniform(0.78, 1.32)
    sy = random.uniform(0.78, 1.32)
    sz = random.uniform(0.85, 1.45) if r < 16 else random.uniform(0.8, 1.2)
    rock = cq.Workplane("XY").box(2 * r * sx, 2 * r * sy, 2 * r * sz)
    rock = rock.rotate((0, 0, 0), (0, 0, 1), random.uniform(0, 360))
    ta = random.uniform(0, 2 * math.pi)
    rock = rock.rotate((0, 0, 0), (math.cos(ta), math.sin(ta), 0),
                       random.uniform(-10, 10))
    rock = rock.translate((x, y, zc))
    for ci in range(cuts + 4):
        na = random.uniform(0, 2 * math.pi)
        # pierwsze ciecie zawsze od gory - zadnych masztow z naroznikow bloku
        nz = random.uniform(0.7, 0.95) if ci == 0 else random.uniform(-0.15, 0.95)
        hx = math.cos(na) * (1 - abs(nz))
        hy = math.sin(na) * (1 - abs(nz))
        n = cq.Vector(hx, hy, nz).normalized()
        ext = r * (sx * abs(n.x) + sy * abs(n.y) + sz * abs(n.z))
        rock = rock.cut(cq.Workplane(cq.Plane(
            origin=cq.Vector(x, y, zc) + n * (ext * random.uniform(0.45, 0.8)),
            normal=n))
            .box(4 * r, 4 * r, 3 * r, centered=(True, True, False)))
    if clip is not None:
        rock = rock.intersect(clip)
    return rock


ROCK_POOL = []


def add_rock(target, x, y, r, zc, cuts=4, clip=None):
    rock = make_rock(x, y, r, zc, cuts=cuts, clip=clip)
    if rock.solids().vals():
        ROCK_POOL.append(rock)
    return target


def merge_rock_pool(target):
    pool = list(ROCK_POOL)
    ROCK_POOL.clear()
    while len(pool) > 1:
        nxt = []
        for i in range(0, len(pool) - 1, 2):
            nxt.append(pool[i].union(pool[i + 1]))
        if len(pool) % 2:
            nxt.append(pool[-1])
        pool = nxt
    return target.union(pool[0]) if pool else target


def on_stairs(x, y):
    return abs(x) < 20 and y < -58


# 1) duze skaly strukturalne wokol cokolu - bryla terenu, nie posypka
for ang in range(0, 360, 17):
    a = math.radians(ang + random.uniform(-6, 6))
    ca, sa = math.cos(a), math.sin(a)
    scale = (BASE_W / 2 - 4.0) / max(abs(ca), abs(sa))
    r = random.uniform(16.0, 30.0)
    lim = FOOT_MAX / 2 - r
    cx = max(-lim, min(lim, ca * scale * random.uniform(0.96, 1.04)))
    cy = max(-lim, min(lim, sa * scale * random.uniform(0.96, 1.04)))
    if on_stairs(cx, cy) or (abs(cx) < 22 + r and cy < -70):
        continue
    zc = random.uniform(0.45 * r, 46.0 - 0.3 * r)
    base = add_rock(base, cx, cy, r, zc, cuts=5, clip=CLIP_BASE)

# 1b) masywy w naroznikach
for sx in (-1, 1):
    for sy in (-1, 1):
        for _ in range(4):
            r = random.uniform(15.0, 27.0)
            lim = FOOT_MAX / 2 - r
            cx = max(-lim, min(lim, sx * (BASE_W / 2 - random.uniform(-4, 10))))
            cy = max(-lim, min(lim, sy * (BASE_D / 2 - random.uniform(-4, 10))))
            base = add_rock(base, cx, cy, r, random.uniform(10.0, 42.0), cuts=5, clip=CLIP_BASE)

# 1c) glazy flankujace schody (duze, czesc terenu)
for fx in (-1, 1):
    for fy, fr, fz in ((-100, 15.0, 14), (-88, 13.0, 30)):
        r = fr + random.uniform(-1.5, 1.5)
        lim = FOOT_MAX / 2 - r
        cx = fx * (22.0 + r * 0.75)
        cx = max(-lim, min(lim, cx))
        cy = max(-lim, min(lim, fy + random.uniform(-2, 2)))
        base = add_rock(base, cx, cy, r, fz + random.uniform(-2, 2), cuts=5, clip=CLIP_BASE)

# 2) skalne lawice na zboczu kopca - zlewaja sie z terenem
for ang in range(0, 360, 13):
    a = math.radians(ang + random.uniform(-5, 5))
    rr = random.uniform(MOUND_R_TOP - 6, MOUND_R_BOT + 2)
    x, y = rr * math.cos(a), rr * math.sin(a) + MOUND_OFF_Y
    if on_stairs(x, y):
        continue
    r = random.uniform(12.0, 20.0)
    lim = FOOT_MAX / 2 - r
    x = max(-lim, min(lim, x))
    y = max(-lim, min(lim, y))
    base = add_rock(base, x, y, r, mound_h(x, y) - 0.35 * r, cuts=5, clip=CLIP_BASE)

# 2b) dywan skal na calym zboczu - zadnej gladkiej powierzchni stozka
for rr0, rr1, step, rmin, rmax in ((84.0, 97.0, 17, 8.0, 14.0),
                                   (95.0, 105.0, 21, 7.0, 12.0)):
    for ang in range(0, 360, step):
        a = math.radians(ang + random.uniform(-step / 3.0, step / 3.0))
        rr = random.uniform(rr0, rr1)
        x, y = rr * math.cos(a), rr * math.sin(a) + MOUND_OFF_Y
        if on_stairs(x, y):
            continue
        r = random.uniform(rmin, rmax)
        lim = FOOT_MAX / 2 - r
        x = max(-lim, min(lim, x))
        y = max(-lim, min(lim, y))
        base = add_rock(base, x, y, r, mound_h(x, y) - 0.3 * r, cuts=5, clip=CLIP_BASE)

# 3) kilka srednich glazow przy stopie wiezy (rim plaskowyzu)
for ang in (15, 75, 130, 200, 250, 320):
    a = math.radians(ang + random.uniform(-8, 8))
    rr = random.uniform(79.0, 84.0)
    x, y = rr * math.cos(a), rr * math.sin(a)
    if on_stairs(x, y):
        continue
    base = add_rock(base, x, y, random.uniform(6.5, 10.0),
                    TOWER_SEAT + 2.0 - random.uniform(1.0, 2.5),
                    cuts=4, clip=CLIP_BASE)

# scal wszystkie skaly drzewkowo i dolacz do podstawy (szybciej niz po jednej)
base = merge_rock_pool(base)

# kamienne schody do drzwi (front, -Y); szczyt 2 mm pod dolem drzwi
STAIR_W = 30.0
step_tops = [60.0, 64.0, 68.0]
for i, top in enumerate(step_tops):
    y_out = -(100.0 - i * 7.0)
    w = STAIR_W + random.uniform(-2.0, 2.0)
    st = (cq.Workplane("XY")
          .box(w, abs(y_out) - 76.0 + 6.0, top - 30.0)
          .translate((random.uniform(-1.2, 1.2),
                      (y_out - 70.0) / 2.0, (top + 30.0) / 2.0)))
    base = base.union(st.edges(">Z").chamfer(0.7))

# rowek osadczy pod wieze (zatopiona w skale, maskuje styk)
seat_groove = (cq.Workplane("XY").cylinder(40.0, r_out(0) + 0.8)
               .translate((0, 0, TOWER_SEAT + 20.0)))
seat_groove = seat_groove.cut(cq.Workplane("XY").cylinder(44.0, r_in(0) - 0.5)
                              .translate((0, 0, TOWER_SEAT + 20.0)))
base = base.cut(seat_groove)

# komora + przelot kabli / komin
base = base.cut(cav)
passage = (cq.Workplane("XY").box(PASS_W, PASS_D, 60.0)
           .translate((0, PASS_OFF_Y, 28.0 + 30.0)))
passage = passage.edges("|Z").fillet(PASS_RC)
base = base.cut(passage)

# pierscien centrujacy wieze (na plaskowyzu)
cring = (cq.Workplane("XY").cylinder(6.0, r_in(0) - 0.3)
         .translate((0, 0, TOWER_SEAT + 3.0)))
cring = cring.cut(cq.Workplane("XY").cylinder(8.0, r_in(0) - 3.5)
                  .translate((0, 0, TOWER_SEAT + 3.0)))
base = base.union(cring)

# notche w obrzezu plaskowyzu i cringu na lapy wiezy + kieszenie insertow M3
for azd in BTAB_AZ:
    notch = (cq.Workplane("XY").box(21.0, BTAB_W + 5.0, 7.0)
             .translate(((49.0 + 69.5) / 2, 0, TOWER_SEAT + 3.2))
             .rotate((0, 0, 0), (0, 0, 1), azd))
    base = base.cut(notch)
    # przelot na wylot + poziomy slot na nakretke M3 (wsuwana od strony przelotu)
    vhole = (cq.Workplane("XY").cylinder(18.0, 1.7)
             .translate((BSCREW_R, 0, TOWER_SEAT - 6.0))
             .rotate((0, 0, 0), (0, 0, 1), azd))
    nslot = (cq.Workplane("XY").box(22.0, 5.75, 2.8)
             .translate((52.0, 0, TOWER_SEAT - 7.5))
             .rotate((0, 0, 0), (0, 0, 1), azd))
    base = base.cut(vhole).cut(nslot)


# stojak wlacznika (inter BAR 3628): sztywna rama portalowa na mocne wciskanie
# panel 6 mm + lokalna kieszen 2.5 mm wokol wyciecia (zatrzaski wymagaja 2-3 mm)
SW_Y = -44.5           # lico panelu
sw = (cq.Workplane("XY").box(38.0, 6.0, 52.0)
      .translate((0, SW_Y + 3.0, TOWER_SEAT + 4.0 + 26.0)))      # panel z 68..120
sw = sw.cut(cq.Workplane("XY").box(27.0, 3.6, 30.0)
            .translate((0, SW_Y + 6.0 - 1.8, TOWER_SEAT + 34.0)))  # kieszen -> 2.4mm
sw = sw.cut(cq.Workplane("XY").box(19.6, 14.0, 22.0)
            .translate((0, SW_Y + 3.0, TOWER_SEAT + 34.0)))      # cutout, srodek z=98
for rx in (-1, 1):                                               # pelne sciany boczne
    sw = sw.union(cq.Workplane("XY").box(4.0, 11.0, 52.0)
                  .translate((rx * 17.0, SW_Y + 5.5, TOWER_SEAT + 4.0 + 26.0)))
sw = sw.union(cq.Workplane("XY").box(38.0, 11.0, 8.0)            # poprzeczka u gory
              .translate((0, SW_Y + 5.5, TOWER_SEAT + 52.0)))
sw = sw.union(cq.Workplane("XY").box(44.0, 11.5, 10.0)           # rozszerzona stopa
              .translate((0, SW_Y + 5.75, TOWER_SEAT + 9.0)))
base = base.union(sw)


def boss(x, y, h=10.0, pilot=2.4, r=4.0):
    b = (cq.Workplane("XY").cylinder(h, r)
         .translate((x, y, BASE_H - BASE_DECK_T - h / 2)))
    return b.cut(cq.Workplane("XY").cylinder(h + 1, pilot / 2)
                 .translate((x, y, BASE_H - BASE_DECK_T - h / 2)))


# TTGO na slupkach (prawy bok); LRS-100-24 stoi na pokrywie (otwory w pokrywie)
for x, y in ((56, 6), (76, 6), (56, 26), (76, 26)):
    base = base.union(boss(x, y, h=8.0, pilot=2.0))

# pozycje otworow montazowych LRS-100-24 (Case 238A: dno 3x M3, wkrecenie max 5mm)
PSU_C = (-16.0, -10.0)                      # srodek zasilacza 129x97 na pokrywie
PSU_HOLES = [(PSU_C[0] - 64.5 + hx, PSU_C[1] - 48.5 + hy)
             for hx, hy in ((4.5, 9.5), (122.5, 9.5), (122.5, 87.5))]
FAN_C = (-30.0, 60.0)   # wiatrak 40x40x10 24V: otwor fi38 + 4x M3 w rastrze 32x32
FOOT_POS = [(sx * 93.0, sy * 93.0) for sx in (-1, 1) for sy in (-1, 1)]
FOOT_H = 8.0            # nozki: przeswit na zasysanie powietrza od dolu

# gniazda kolkow orientujacych wieze (dno rowka osadczego)
for ang in (90, 210, 325):
    a = math.radians(ang)
    rc = (r_in(0) + r_out(0)) / 2.0
    base = base.cut(cq.Workplane("XY").cylinder(9.0, PIN_D / 2)
                    .translate((rc * math.cos(a), rc * math.sin(a),
                                TOWER_SEAT - 4.5 + 0.3)))

# gniazda nozek (wcisk fi8) w dnie cokolu
for px, py in FOOT_POS:
    base = base.cut(cq.Workplane("XY").cylinder(12.0, 4.0)
                    .translate((px, py, 6.0 - 6.0)))

# rowek na kabel 230V (tylna sciana, przy ziemi) - na wylot do komory
base = base.cut(cq.Workplane("XY").box(14.0, 48.0, 9.0)
                .translate((0, BASE_D / 2 - 2.0, 4.5 - EPS)))

# gniazda kolkow dostawki schodow (czolo cokolu, y=-100)
for px in (-9.0, 9.0):
    base = base.cut(cq.Workplane("XY").box(4.4, 11.0, 4.4)
                    .translate((px, -(BASE_D / 2) - 5.5 + 11.0, 20.0)))

# pokrywa dolna + slupki narozne
cover = (cq.Workplane("XY")
         .box(BASE_W - 2 * BASE_WALL - 0.8, BASE_D - 2 * BASE_WALL - 0.8, COVER_T)
         .translate((0, 0, COVER_T / 2)))
cover = cover.edges("|Z").fillet(4.0)
for gx in range(-3, 4):
    for gy in (-2, -1, 0, 1, 2):
        sx, sy = gx * 19, gy * 26
        if any(abs(sx - px) < 12 and abs(sy - py) < 8.5 for px, py in PSU_HOLES):
            continue
        if abs(sx - FAN_C[0]) < 27 and abs(sy - FAN_C[1]) < 27:
            continue
        cover = cover.cut(cq.Workplane("XY").box(14, 5, COVER_T + 2)
                          .translate((sx, sy, COVER_T / 2)))
cover = cover.cut(cq.Workplane("XY").box(14.0, 10.0, COVER_T + 2)
                  .translate((0, BASE_D / 2 - BASE_WALL - 0.4, COVER_T / 2)))
cover = cover.cut(cq.Workplane("XY").cylinder(COVER_T + 2, 19.0)
                  .translate((FAN_C[0], FAN_C[1], COVER_T / 2)))
for fx in (-1, 1):
    for fy in (-1, 1):
        cover = cover.cut(cq.Workplane("XY").cylinder(COVER_T + 2, 1.7)
                          .translate((FAN_C[0] + fx * 16, FAN_C[1] + fy * 16,
                                      COVER_T / 2)))
for px, py in PSU_HOLES:
    cover = cover.cut(cq.Workplane("XY").cylinder(COVER_T + 2, 1.8)
                      .translate((px, py, COVER_T / 2)))
    cover = cover.cut(cq.Workplane(obj=cq.Solid.makeCone(
        3.3, 1.8, 1.8, pnt=cq.Vector(px, py, 0))))
for sx in (-1, 1):
    for sy in (-1, 1):
        bx = sx * (BASE_W / 2 - BASE_WALL - 8)
        by = sy * (BASE_D / 2 - BASE_WALL - 8)
        base = base.union(boss(bx, by, h=BASE_H - BASE_DECK_T - COVER_T - 0.4,
                               pilot=3.4, r=5.4))
        base = base.cut(cq.Workplane("XY").polygon(6, 6.8).extrude(3.0)
                        .translate((bx, by, COVER_T + 0.4 - EPS)))
        cover = cover.cut(cq.Workplane("XY").cylinder(COVER_T + 2, 1.7)
                          .translate((bx, by, COVER_T / 2)))
        cover = cover.cut(cq.Workplane(obj=cq.Solid.makeCone(
            3.2, 1.7, 1.8, pnt=cq.Vector(bx, by, 0))))   # stozek pod leb M3

# ============================================================
# DOSTAWKA: SCHODY DO GRUNTU (osobna, doczepiana czesc)
# ============================================================

print("[*] dostawka schodow ...")
EXT_W   = 30.0
EXT_Y0  = -(BASE_D / 2) + 0.4          # tyl dostawki (0.4 luzu od czola cokolu)
ext_tops = [56.0 - i * 5.82 for i in range(11)]  # 56 .. -2.2; grunt na -8 (nozki)

stairs_ext = None
for i, top in enumerate(ext_tops):
    y_front = EXT_Y0 - 7.0 * (i + 1)
    w = EXT_W + random.uniform(-1.5, 1.5)
    st = (cq.Workplane("XY")
          .box(w, abs(y_front - EXT_Y0), top + FOOT_H)
          .translate((random.uniform(-1.0, 1.0),
                      (y_front + EXT_Y0) / 2.0, (top - FOOT_H) / 2.0)))
    st = st.edges(">Z").chamfer(0.7)
    stairs_ext = st if stairs_ext is None else stairs_ext.union(st)

# glazy po bokach dostawki - przedluzenie krajobrazu
for fx in (-1, 1):
    for fy, fr, fz in ((-112, 10.0, 0), (-130, 12.0, -1), (-152, 10.0, -2), (-172, 8.0, -3)):
        r = fr + random.uniform(-1.0, 1.0)
        stairs_ext = stairs_ext.union(make_rock(
            fx * (EXT_W / 2 + r * 0.55), fy + random.uniform(-3, 3),
            r, fz + random.uniform(-1.5, 1.5), cuts=5))

# kolki (kwadratowe - drukuja sie czysto na lezaco)
for px in (-9.0, 9.0):
    stairs_ext = stairs_ext.union(
        cq.Workplane("XY").box(3.6, 10.0, 3.6)
        .translate((px, EXT_Y0 + 4.6, 20.0)))

# obrys wlasny + poziom zero + docinka bryla podstawy (idealne spasowanie do skal)
stairs_ext = stairs_ext.intersect(
    cq.Workplane("XY").box(54.0, 102.0, 86.0)
    .translate((0, EXT_Y0 - 40.0, 35.0)))
stairs_ext = stairs_ext.union(
    cq.Workplane("XY").box(3.6, 10.0, 3.6).translate((-9.0, EXT_Y0 + 4.6, 20.0)))
stairs_ext = stairs_ext.union(
    cq.Workplane("XY").box(3.6, 10.0, 3.6).translate((9.0, EXT_Y0 + 4.6, 20.0)))
stairs_ext = stairs_ext.cut(base)

# ============================================================
# NOZKI (4 szt., wcisk w dno cokolu)
# ============================================================

def make_foot():
    f = cq.Workplane("XY").cylinder(FOOT_H, 9.0).translate((0, 0, -FOOT_H / 2))
    f = f.edges("<Z").chamfer(1.2)
    f = f.union(cq.Workplane("XY").cylinder(5.6, 3.85).translate((0, 0, 2.8)))
    return f


feet_print = None
for i in range(4):
    f = make_foot().translate((i * 26.0, 0, FOOT_H))   # lezace na stole do druku
    feet_print = f if feet_print is None else feet_print.union(f)

# ============================================================
# EXPORT
# ============================================================

print("[*] export ...")
os.makedirs("out", exist_ok=True)

# drzwi do pozycji w zlozeniu: postaw pionowo, front (-Y), zamkniete na ramie
door_closed = (door
               .rotate((0, 0, 0), (1, 0, 0), 90)
               .translate((0, -(rc_door + 5.5), DOOR_SILL - 1.0 + TOWER_SEAT)))
door_closed = door_closed.rotate((0, 0, 0), (0, 0, 1), DOOR_ANG - 270)

asm = cq.Assembly(name="latarnia_morska")
asm.add(base, name="podstawa", color=cq.Color(0.55, 0.55, 0.58))
asm.add(cover, name="pokrywa", color=cq.Color(0.3, 0.3, 0.3))
asm.add(tower_lower.translate((0, 0, TOWER_SEAT)), name="wieza_dol",
        color=cq.Color(0.92, 0.9, 0.85))
asm.add(tower_upper.translate((0, 0, TOWER_SEAT)), name="wieza_gora",
        color=cq.Color(0.85, 0.25, 0.2))
asm.add(door_closed, name="drzwi", color=cq.Color(0.45, 0.3, 0.18))
asm.add(stairs_ext, name="schody_dolne", color=cq.Color(0.5, 0.5, 0.53))
for i, (px, py) in enumerate(FOOT_POS):
    asm.add(make_foot().translate((px, py, 0)), name=f"nozka_{i}",
            color=cq.Color(0.3, 0.3, 0.32))
asm.save("out/latarnia_assembly.step")

cq.exporters.export(base,        "out/01_podstawa.stl")
cq.exporters.export(cover,       "out/02_pokrywa_dolna.stl")
cq.exporters.export(tower_lower, "out/03_wieza_dolna.stl")
cq.exporters.export(tower_upper.translate((0, 0, -SPLIT_Z)), "out/04_wieza_gorna.stl")
cq.exporters.export(door,        "out/05_drzwi.stl")
cq.exporters.export(stairs_ext.translate((0, BASE_D / 2 + 60, FOOT_H)),
                    "out/06_schody_dolne.stl")
cq.exporters.export(feet_print, "out/07_nozki.stl")

print("OK")
print(f"  calosc bez lampy: {TOWER_SEAT + TOWER_H + PLAT_T:.0f} mm (+barierka {RAIL_H:.0f})")
print(f"  z lampa (193mm):  {TOWER_SEAT + TOWER_H + PLAT_T + 193:.0f} mm")
