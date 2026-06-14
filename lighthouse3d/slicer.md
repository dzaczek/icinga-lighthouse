# Printing guide — Bambu Studio (lighthouse parts)

How to slice the lantern parts (especially `04_wieza_gorna.stl`, the upper tower with the
platform, corbel and cable funnel) cleanly. The whole job comes down to **one setting** —
the support **Threshold angle** — plus a note on the screw-insert bosses.

## TL;DR

| Setting | Value |
| :--- | :--- |
| Orientation | **default** — frame/ring flat on the plate, platform up. **Do not flip.** |
| Parameter mode | **Advanced / Expert** (to see the support sub-settings) |
| Layer height | 0.2 mm |
| Walls | 3 wall loops |
| Support | **On**, type **`normal(auto)`** |
| **Support → Threshold angle** | **25°** (drop to 20° if any support still shows under the platform) |
| On build plate only | **OFF** |
| Part cooling (overhang) | PLA ~100 % · PETG ~40–60 % |
| Overhang slow-down | ON, steepest band ~20–30 mm/s |
| Brim | none (the ring gives a big first layer); 3–5 mm only if an edge lifts |

---

## 1. Import & orient

Import `04_wieza_gorna.stl` (or the STEP). **Leave it as-is**: split/frame ring on the plate
at z = 0, platform + railing pointing up. Don't rotate or flip — that orientation is what makes
the funnel self-supporting once the threshold is set.

## 2. Show all options

Top-right, set the parameter mode to **Advanced** (or **Expert**) so the support sub-settings
are visible.

## 3. Filament & quality

Pick your filament (PETG or PLA). 0.2 mm layer height, **3 wall loops**. Infill to taste
(15–20 % gyroid is plenty for an enclosure).

## 4. Support — the key step

Support tab → **Support: On**, **Type: `normal(auto)`**.

**Threshold angle: `25°`.**

> **Why.** Bambu generates support for a surface when its slope **to the bed** is *below* the
> threshold. The funnel/corbel under the platform sit at **~31° from the bed** (≈59° from
> vertical) — right on top of the **~30° default**. Because a curved cone is a mesh of facets,
> some land just under 30° and some just over, so you get **flaky, partial** support smeared
> over the cone. Setting the threshold to **25°** puts the *entire* cone safely above it, so it
> is left support-free, while genuinely flat spots (window-top lintels, platform outer rim,
> railing tops — all ~0° from the bed) still get supported.

- Leave **Top Z distance / XY distance** at the profile defaults.
- **Do not** tick **“On build plate only”** — the small support under the platform rim needs to
  rest on the corbel below it, not the plate.

## 5. Overhang cooling

So the 59°-from-vertical funnel prints clean without support:

- **Cooling:** part-cooling fan high for overhang regions — **PLA ~100 %**, **PETG ~40–60 %**
  (keep PETG moderate so you don't kill layer adhesion).
- **Overhang slow-down:** enable it; put the steepest band around **20–30 mm/s**.
- Optional: raise *min layer time* so thin upper layers get more cooling.

## 6. Slice & verify (don't skip)

Slice → **Preview** → switch to the support/overhang view, then check:

- ✅ **No support columns under the platform** (the funnel/corbel cone). If any remain, lower
  **Threshold angle to 20°**.
- ✅ Support appears **only** at: window-top lintels, the platform outer rim, and railing tops —
  all reachable from outside/top, nothing trapped inside.
- ✅ Scrub the layer slider through the funnel region (≈ z 135–165) and confirm it slices as a
  clean, self-supporting cone.

## 7. Manual cleanup (optional)

- Unwanted auto-support → **Support painting → eraser**, or drop a **Support blocker** over it.
- A flat spot got missed → **Support enforcer**.

## 8. Screw-insert bosses — and dissolvable support

The threshold trick removes support from the funnel; the *remaining* supports are all
**external** (lintels, rim, railing) and snap off by hand — **no soluble filament needed there.**

### Fasteners in this model

The model already uses print-friendly fixing, so **most nut/screw features need no support**:

| Feature | Where | Geometry | Printing |
| :--- | :--- | :--- | :--- |
| **M3 captive-nut slots** | tower↔base tabs, tower-section tabs | horizontal slot **5.75 × 2.8 mm**, nut **slides in** from the bolt hole | **No support** — the 2.8 mm-tall slot bridges only 5.75 mm. Print, then slide the M3 nut in sideways. |
| **M3 heat-set inserts** | upper tower-section tab (Ø4 hole) | straight cylindrical hole | **No support** — melt the brass insert in afterwards. |
| **M6 hex nut pockets** | platform underside, ×3 (lamp mount) | Ø12 hex × **5.6 mm deep**, opens **downward**, with a central Ø6.6 through-hole | Roof is a thin **~2.7 mm annular ledge** (not a full 12 mm disc), so it's only a small overhang. At threshold 25° it picks up a little support there — but the pocket **opens downward and is shallow**, so just poke it out from the opening. |

So in practice you **don't** need dissolvable filament for the nuts either — the slots bridge,
the inserts are straight holes, and the M6 pockets clear from their open (downward) side.

**Only if an M6 pocket is blocked from below (corbel in the way) or you want it perfect:**
- Drop a **Support enforcer** on that ledge and clear it from the downward opening, **or**
- Use a **breakaway / PVA interface** (multi-material) on those three pockets only, **or**
- **CAD fix (cleanest):** chamfer the hex-pocket roof into a ~45° cone down to the Ø6.6 hole in
  [`latarnia_cadquery.py`](latarnia_cadquery.py) — then the ledge is self-supporting and prints
  with no support at the default threshold. Say the word and I'll regenerate the STL.


Soluble/breakaway support is only worth it if a **screw boss traps support inside a blind/deep
cavity** you can't reach. First decide which case you have:

| Insert type | Hole geometry | Needs support? |
| :--- | :--- | :--- |
| **Heat-set threaded insert** (brass, melted in) | straight cylindrical hole, no roof | **No** — print it open, no support, no soluble filament |
| **Captive nut pocket** (hex pocket with a roof) | overhanging roof over a cavity | Yes — and it's hard to clear → soluble/breakaway helps |
| **Self-tapping screw straight into plastic** | plain pilot hole | No |

**Recommended order (cheapest/easiest first):**

1. **Design it out.** Use heat-set inserts in straight holes (zero overhang → zero support), or
   make captive-nut roofs a self-supporting chamfer / teardrop, or open the pocket to one side.
2. **Breakaway interface.** On a multi-material setup (AMS / dual), set only the **support
   interface** to a breakaway material (Bambu *“Support for PLA/PETG”*) and the support **body**
   to your main filament. It releases cleanly from a pocket without water.
3. **Water-soluble (PVA)** — true “dissolve it out”. Use it **only as the support interface**
   (top/bottom interface layers), main filament for the body, to save the (expensive,
   hygroscopic) PVA. Then soak the part in warm water until it dissolves.

**If you go PVA / soluble:**

- Needs a **multi-material printer** (AMS + a printer that allows soluble support, e.g. X1/P1
  with the AMS) — a single-extruder print can't mix two materials.
- Support tab → enable **Support interface**, set **“Interface filament” = PVA** (or breakaway),
  **“Support base filament” = main**. A few interface layers is enough.
- PVA is **hygroscopic** — dry it before use and keep it sealed, or it strings/clogs.
- Temperatures: PVA prints ~**190–215 °C**. It pairs easily with **PLA**; with **PETG** the temp
  gap is larger, so prefer the **breakaway** interface for PETG parts, or print PVA at the low
  end and slow.
- Dissolving is **slow** (warm water, often hours, agitation/replacing water speeds it up).

> Bottom line: for this lantern you most likely **don't** need soluble filament — the funnel is
> handled by the threshold angle, and screw fixing is cleanest with **heat-set inserts in
> straight holes** (no support at all). Reserve PVA/breakaway for genuinely trapped pockets.

## 9. Bed adhesion

The frame ring gives a large first-layer contact area, so adhesion is solid — **no brim needed**
unless an edge lifts, then add a **3–5 mm brim**.

## 10. Alternative — fix it in CAD instead of the slicer

If you'd rather not touch slicer settings on every print, lower `PLUG_BOT_Z` in
[`latarnia_cadquery.py`](latarnia_cadquery.py) so the funnel becomes ~50° from vertical —
self-supporting even at the **default 30°** threshold — and regenerate the STL. Likewise, model
screw bosses as straight holes / teardrop pockets so they never need support.
