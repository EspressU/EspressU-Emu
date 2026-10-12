"""
EspressU on-screen pad: hardware-derived GamePad geometry + responsive resolver.

Geometry source: the official Wii U GamePad front illustration, measured at
0.425 mm/px (600 px = 255 mm body, verified independently by the 6.2" screen).
Everything is then expressed in D = one face-button diameter = 10.625 mm, which is
the unit ControllerGeometry already uses.
"""
import json, math, os

D_MM = 10.625                      # face-button diameter, the layout unit
MM = 0.425                         # mm per reference pixel

# ---------------------------------------------------------------- hardware, in D
G = dict(
    faceDiameter      = 1.00000,   # 10.625 mm
    faceHalfX         = 0.96000,   # Y<->A half pitch, 10.20 mm
    faceHalfY         = 0.95000,   # X<->B half pitch, 10.09 mm
    dpadW             = 2.28000,   # 24.225 mm
    dpadH             = 2.24000,   # 23.80 mm
    dpadArm           = 0.76000,   # 8.075 mm
    stickBase         = 1.96000,   # 20.825 mm dish
    stickKnob         = 1.12000,   # 11.9 mm cap
    stickArm          = 2.79020,   # 29.646 mm, stick centre -> cluster centre
    stickArmAngleDeg  = 69.8737,   # below horizontal, outboard-and-up
    systemDiameter    = 0.68000,   # +/- , 7.225 mm
    homeDiameter      = 1.04000,   # 11.05 mm
    menuDiameter      = 0.64000,   # TV / POWER, 6.8 mm
    bodyW             = 24.00000,  # 255 mm  (exactly 24 face buttons)
    bodyH             = 12.56000,  # 133.45 mm
    screenW           = 12.92235,  # 137.3 mm, normalised to 16:9
    screenH           = 7.26588,   # 77.2 mm
    screenCentreY     = 6.28000,   # from body top, 66.725 mm
    dpadFromBodyLeft  = 3.16000,   # 33.575 mm
    abxyFromBodyRight = 3.04000,   # 32.30 mm
    clusterFromTop    = 5.66000,   # 60.1375 mm, BOTH clusters - they share a height
    homeFromBodyTop   = 11.32000,  # 120.275 mm
    tvFromHome        = 5.08047,   # 53.98 mm
    powerFromHome     = 6.28047,   # 66.73 mm
    plusFromABXY      = (-1.08000, 2.42000),   # -11.475, +25.71 mm
    minusFromABXY     = (-1.08000, 3.66000),   # -11.475, +38.89 mm
    # shoulders: not on the front face, so placed rather than measured.
    # Pill size is the one real measurement available (IMG_3278, kept from the
    # shipping layout); position is "directly above its own stick", as on the hardware.
    shoulderW         = 1.15100,
    shoulderH         = 0.87400,
    shoulderCorner    = 0.23500,
    shoulderSpread    = 0.67500,   # half the L<->ZL centre distance
    shoulderGap       = 0.20000,   # clearance above the stick dish
    # where +/- go when the hardware slot does not fit (the "elbow", up and inboard)
    systemElbow       = (1.41400, -1.41400),
)
G['shoulderDy'] = -(G['stickBase']/2 + G['shoulderGap'] + G['shoulderH']/2)   # -1.617

EDGE_M   = 0.35     # screen-edge margin, in D
CLEAR    = 0.35     # video clearance from a cluster, in D
REAL_SCREEN_MM = 137.3  # the GamePad's own 6.2" screen. Framed mode is only worth its
                        # cost when the framed picture is at least this big - otherwise
                        # the shell is nostalgia bought with a smaller picture than the
                        # hardware itself had, which is the wrong trade.
TOUCH_FLOOR_PT = 44.0
RAKE_MIN_DEG = 45.0
MIN_GAP = 0.6       # clear space the two clusters must leave between them, in D

TOP_EXTENT_FIXED = -G['shoulderDy'] + G['shoulderH']/2          # 2.054  (above the stick centre)
BOTTOM_HW  = G['minusFromABXY'][1] + G['systemDiameter']/2      # 4.000
BOTTOM_SPL = G['faceHalfY'] + G['faceDiameter']/2               # 1.450

def top_extent(theta):    return G['stickArm']*math.sin(theta) + TOP_EXTENT_FIXED
def out_extent(theta):    return max(G['dpadW']/2, G['stickArm']*math.cos(theta) + G['stickBase']/2,
                                     G['stickArm']*math.cos(theta) + G['shoulderSpread'] + G['shoulderW']/2)
def need_v(theta, hardware): return top_extent(theta) + (BOTTOM_HW if hardware else BOTTOM_SPL) + 2*EDGE_M

THETA_HW = math.radians(G['stickArmAngleDeg'])

# ---------------------------------------------------------------- the resolver

def extents(theta, hardware):
    """Outboard reach, inboard reach of each half, and the total width, all in D."""
    inb  = G['dpadW']/2 if hardware else (G['systemElbow'][0] + G['systemDiameter']/2)
    inbR = max(G['faceHalfX'] + 0.5, inb)
    outX = out_extent(theta)
    return outX, inb, inbR, (outX + inb) + (outX + inbR)

def width_fit(theta, hardware, sw):
    """The largest D at which both clusters fit side by side and still clear each other.

    The two edge margins belong in this denominator. Leaving them out is not a rounding
    error - it prescribes a NEGATIVE gap, and the clusters are then placed overlapping by
    construction, which is exactly what happened in Slide Over and in portrait."""
    return sw / (extents(theta, hardware)[3] + 2*EDGE_M + MIN_GAP)

def resolve(name, W, H, ppi_pt, insets, user_scale=1.0, external_display=False):
    l, r, b, t = insets
    sx, sy = l, t
    sw, sh = W - l - r, H - t - b
    portrait = sh > sw
    D_life = D_MM * ppi_pt / 25.4
    D0 = D_life * user_scale
    notes = []

    # In portrait the picture takes the top and the clusters get only the band below it,
    # so the picture has to be decided before the size law, not after.
    if portrait:
        vw = sw; vh = vw*9/16
        if vh > sh*0.62: vh = sh*0.62; vw = vh*16/9
        vx, vy = sx + (sw-vw)/2, sy
        availH = sy + sh - (vy + vh)
    else:
        availH = sh

    # LAW 1+2 - size, then arrangement. Two candidates, evaluated whole rather than
    # patched one after the other: keeping +/- in their hardware slot costs height but
    # makes the clusters narrower, so height and width have to be judged together.
    theta = THETA_HW
    D_hw = min(D0, width_fit(theta, True, sw))
    if D_hw <= availH / need_v(theta, True) + 1e-9:
        hardware_system, D = True, D_hw
    else:
        hardware_system = False
        D = min(D0, availH / need_v(theta, False), width_fit(theta, False, sw))
        if D < TOUCH_FLOOR_PT:
            # Raking buys height. It cannot buy width - it pushes the stick outboard - so
            # a width-bound pad is not helped by it and simply comes out small.
            k = (availH/TOUCH_FLOOR_PT - BOTTOM_SPL - 2*EDGE_M - TOP_EXTENT_FIXED) / G['stickArm']
            theta = max(math.radians(RAKE_MIN_DEG), math.asin(max(-1, min(1, k))))
            D = min(D0, availH / need_v(theta, False), width_fit(theta, False, sw))
            notes.append("raked: stick arm rotated to %.0f deg to fit the height" % math.degrees(theta))
            if D < TOUCH_FLOOR_PT:
                # Never forced back up to the floor: buttons a little small are recoverable,
                # two clusters drawn through each other are not.
                notes.append("%.0f pt buttons, under the 44 pt floor - the container is too small" % D)
    if D0 > D + .01:
        notes.append("D reduced to %.0f%% of life-size to fit" % (100*D/D_life))

    bottom_ext = BOTTOM_HW if hardware_system else BOTTOM_SPL
    outX, inb, inbR, clustersW = extents(theta, hardware_system)

    # LAW 3 - clusters pinned to the safe edges, the bezel between them is the slack.
    lx = sx + (EDGE_M + outX)*D
    rx = sx + sw - (EDGE_M + outX)*D
    gap0, gap1 = lx + (inb + CLEAR)*D, rx - (inbR + CLEAR)*D
    gapW, gapCx = max(0, gap1-gap0), (gap0+gap1)/2

    # LAW 4 - mode.
    if portrait:
        mode = "stacked"
    elif external_display:
        mode = "shell"
    else:
        fw = min(gapW, (sh - 2*CLEAR*D)*16/9)
        mode = "framed" if fw/ppi_pt*25.4 >= REAL_SCREEN_MM else "overlay"

    body_top = None
    if mode in ("framed", "shell") and G['bodyH']*D <= sh:
        body_top = sy + (sh - G['bodyH']*D)/2
        cy = body_top + G['clusterFromTop']*D
    elif mode in ("framed", "shell"):
        ext = need_v(theta, hardware_system)
        cy = sy + (sh - ext*D)/2 + (top_extent(theta) + EDGE_M)*D
    else:
        cy = sy + sh - (bottom_ext + EDGE_M)*D

    if mode == "stacked":
        notes.append("portrait: picture along the top, both clusters on the bottom corners")
    elif mode == "overlay":
        vh = min(sh, sw*9/16); vw = vh*16/9
        vx, vy = sx + (sw-vw)/2, sy + (sh-vh)/2
    else:
        vw = min(gapW, (sh - 2*CLEAR*D)*16/9); vh = vw*9/16
        vcy = (body_top + G['screenCentreY']*D) if body_top is not None else (sy + sh/2)
        vcy = min(max(vcy, sy + vh/2 + CLEAR*D), sy + sh - vh/2 - CLEAR*D)
        vx, vy = gapCx - vw/2, vcy - vh/2

    # ---- the controls
    ax, ay = G['stickArm']*math.cos(theta)*D, G['stickArm']*math.sin(theta)*D
    P = {}
    def circ(n, x, y, d): P[n] = dict(kind="circle", x=round(x,1), y=round(y,1), d=round(d*D,1))
    def pill(n, x, y):    P[n] = dict(kind="pill", x=round(x,1), y=round(y,1),
                                      w=round(G['shoulderW']*D,1), h=round(G['shoulderH']*D,1),
                                      r=round(G['shoulderCorner']*D,1))
    for side, cx, sgn in (("L", lx, -1), ("R", rx, +1)):
        stx, sty = cx + sgn*ax, cy - ay
        circ(f"stick{side}", stx, sty, G['stickBase'])
        circ(f"knob{side}",  stx, sty, G['stickKnob'])
        pill(side,           stx + sgn*G['shoulderSpread']*D, sty + G['shoulderDy']*D)
        pill("Z"+side,       stx - sgn*G['shoulderSpread']*D, sty + G['shoulderDy']*D)
    P["dpad"] = dict(kind="cross", x=round(lx,1), y=round(cy,1), w=round(G['dpadW']*D,1),
                     h=round(G['dpadH']*D,1), arm=round(G['dpadArm']*D,1))
    circ("L3", lx, cy, 0.706)
    for n, dx, dy in (("X",0,-G['faceHalfY']), ("Y",-G['faceHalfX'],0),
                      ("A",G['faceHalfX'],0), ("B",0,G['faceHalfY'])):
        circ(n, rx + dx*D, cy + dy*D, G['faceDiameter'])
    circ("R3", rx, cy, 0.706)
    if hardware_system:
        circ("plus",  rx + G['plusFromABXY'][0]*D,  cy + G['plusFromABXY'][1]*D,  G['systemDiameter'])
        circ("minus", rx + G['minusFromABXY'][0]*D, cy + G['minusFromABXY'][1]*D, G['systemDiameter'])
    else:
        ex, ey = G['systemElbow']
        circ("plus",  rx - ex*D, cy + ey*D, G['systemDiameter'])
        circ("minus", lx + ex*D, cy + ey*D, G['systemDiameter'])
        notes.append("+/- relocated to the elbow: no room for their real place below A/B/X/Y")

    hr = G['homeDiameter']/2*D
    def collides(x, y, rr):
        for i, c in P.items():
            if i.startswith('knob'): continue
            w = c.get('d', c.get('w')); h = c.get('d', c.get('h'))
            if abs(x-c['x']) < (rr + w/2) - 0.5 and abs(y-c['y']) < (rr + h/2) - 0.5: return True
        return False
    placed = False
    if body_top is not None and mode in ("framed","shell"):
        hy = max(body_top + G['homeFromBodyTop']*D, vy + vh + CLEAR*D + hr)
        if hy + hr <= sy + sh - EDGE_M*D and not collides(gapCx, hy, hr):
            circ("HOME",  gapCx, hy, G['homeDiameter'])
            circ("TV",    gapCx + G['tvFromHome']*D, hy, G['menuDiameter'])
            circ("POWER", gapCx + G['powerFromHome']*D, hy, G['menuDiameter'])
            placed = True
    if not placed:
        hx = sx + sw/2 if mode == "overlay" else gapCx
        hy = sy + sh - (EDGE_M + G['homeDiameter']/2)*D
        if not collides(hx, hy, hr):
            circ("HOME", hx, hy, G['homeDiameter'])
        else:
            notes.append("HOME joins TV/POWER in the pause menu: nothing on the pad clears")
        notes.append("TV/POWER live in the pause menu at this size, not on the pad")

    return dict(device=name, container=[W,H], safe=[round(sx,1),round(sy,1),round(sw,1),round(sh,1)],
                ppi_pt=ppi_pt, mode=mode, D=round(D,2), D_life=round(D_life,2),
                pct_life=round(100*D/D_life), theta_deg=round(math.degrees(theta),1),
                system_placement="hardware" if hardware_system else "elbow",
                mm_per_button=round(D/ppi_pt*25.4,2),
                clusters=dict(leftAnchor=[round(lx,1),round(cy,1)], rightAnchor=[round(rx,1),round(cy,1)],
                              widthUnits=round(clustersW,3), gapUnits=round((gap1-gap0)/D + 2*CLEAR,3)),
                video=[round(vx,1),round(vy,1),round(vw,1),round(vh,1)],
                video_mm=[round(vw/ppi_pt*25.4,1), round(vh/ppi_pt*25.4,1)],
                controls=P, notes=notes)

DEVICES = [
 ("iPhone SE (3rd gen)",      667, 375, 163.00, (0,0,0,0)),
 ("iPhone 13 mini",           780, 360, 158.67, (50,50,21,0)),
 ("iPhone 15 / 16",           852, 393, 153.33, (59,59,21,0)),
 ("iPhone 16 Pro",            874, 402, 153.33, (62,62,21,0)),
 ("iPhone 16 Pro Max",        956, 440, 153.33, (62,62,21,0)),
 ("iPad mini (A17 Pro)",     1133, 744, 163.00, (0,0,20,0)),
 ("iPad (A16) / Air 11\"",   1180, 820, 132.00, (0,0,20,0)),
 ("iPad Pro 11\" (M4)",      1210, 834, 132.00, (0,0,20,0)),
 ("iPad Pro 11\" (2020, A12Z)", 1194, 834, 132.00, (0,0,20,0)),
 ("iPad Pro 12.9\" (2020, A12Z)", 1366,1024, 132.00, (0,0,20,0)),
 ("iPad Pro 12.9\" (2020) portrait", 1024,1366, 132.00, (0,0,20,24)),
 ("iPad Air 13\"",           1366,1024, 132.00, (0,0,20,0)),
 ("iPad Pro 13\" (M4)",      1376,1032, 132.00, (0,0,20,0)),
 ("iPad Pro 11\" Split View 1/2", 583, 834, 132.00, (0,0,20,0)),
 ("iPad Pro 11\" Slide Over", 375, 834, 132.00, (0,0,20,0)),
 ("iPad Pro 13\" portrait",  1032,1376, 132.00, (0,0,20,24)),
 ("iPhone 16 Pro Max portrait", 440, 956, 153.33, (0,0,34,62)),
]

if __name__ == "__main__":
    out = [resolve(*d) for d in DEVICES]
    out.append(resolve("iPad Pro 13\" + external display", 1376,1032,132.0,(0,0,20,0), external_display=True))
    here = os.path.dirname(os.path.abspath(__file__))
    json.dump(dict(unit_mm=D_MM, geometry_D=G, constants=dict(edgeMargin=EDGE_M, clearance=CLEAR,
              realScreenMm=REAL_SCREEN_MM, touchFloorPt=TOUCH_FLOOR_PT, rakeMinDeg=RAKE_MIN_DEG),
              devices=out), open(os.path.join(here,"espressu_pad_layout.json"),"w"), indent=1)
    print("%-32s %-8s %6s %6s %5s %-9s %-22s %s" % ("device","mode","D pt","% life","mm","+/-","video pt","video mm"))
    for o in out:
        print("%-32s %-8s %6.1f %5d%% %5.1f %-9s %-22s %s" % (
            o['device'], o['mode'], o['D'], o['pct_life'], o['mm_per_button'],
            o['system_placement'], "x".join(str(v) for v in o['video'][2:]),
            "x".join(str(v) for v in o['video_mm'])))
