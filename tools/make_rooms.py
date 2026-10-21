"""Writes the first draft of the four rooms as Tiled maps (.tmj) + tilesets (.tsj).

The maps are plain Tiled JSON: open assets/rooms/*.tmj in Tiled to edit them,
and the running game hot-reloads on save. This script only produced the
starting layout; after that the .tmj files are the source of truth.

    python tools/make_rooms.py            # (re)write assets/rooms + tilesets
    python tools/make_rooms.py --preview  # also render docs/rooms/*.png
"""
import json
import os
import random
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets")
T = 16

TILESETS = [  # name, image, columns, rows
    ("tinytown", "tinytown.png", 12, 11),
    ("tinydungeon", "tinydungeon.png", 12, 11),
    ("urban", "urban.png", 27, 18),
]
FIRST = {}
g = 1
for name, _, cols, rows in TILESETS:
    FIRST[name] = g
    g += cols * rows


def TT(i): return FIRST["tinytown"] + i
def TD(i): return FIRST["tinydungeon"] + i
def UR(i): return FIRST["urban"] + i


class Room:
    def __init__(self, rid, w, h):
        self.id, self.w, self.h = rid, w, h
        self.layers = {}  # name -> (data, foreground)
        self.props, self.hotspots, self.walk, self.spawns = [], [], [], []

    def layer(self, name, fill=0, foreground=False):
        self.layers[name] = ([fill] * (self.w * self.h), foreground)
        return self.layers[name][0]

    def put(self, layer, x, y, gid):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.layers[layer][0][y * self.w + x] = gid

    def row(self, layer, x, y, gids):
        for i, gid in enumerate(gids):
            self.put(layer, x + i, y, gid)

    def prop(self, name, gid, tx, ty, visible_if=""):
        # tile objects are anchored bottom-left
        self.props.append(dict(name=name, gid=gid, x=tx * T, y=(ty + 1) * T, visible_if=visible_if))

    def hotspot(self, hid, label, x, y, w, h, walk, face="up", look="", visible_if=""):
        self.hotspots.append(dict(name=hid, x=x, y=y, w=w, h=h, label=label, walk=walk, face=face,
                                  look=look, visible_if=visible_if))

    def walkable(self, pts, hole=False):
        self.walk.append(("hole" if hole else "outer", pts))

    def spawn(self, name, x, y):
        self.spawns.append((name, x, y))

    def to_tmj(self):
        oid = [1]

        def nid():
            oid[0] += 1
            return oid[0] - 1

        def props(d):
            return [dict(name=k, type=("int" if isinstance(v, int) else "string"), value=v)
                    for k, v in d.items() if v != ""]

        layers, lid = [], 1
        for name, (data, fg) in self.layers.items():
            layer = dict(id=lid, name=name, type="tilelayer", x=0, y=0, width=self.w, height=self.h,
                         opacity=1, visible=True, data=data)
            if fg:
                layer["properties"] = [dict(name="foreground", type="bool", value=True)]
            layers.append(layer)
            lid += 1

        def group(name, objects):
            nonlocal lid
            layers.append(dict(id=lid, name=name, type="objectgroup", draworder="topdown", opacity=1,
                               visible=True, x=0, y=0, objects=objects))
            lid += 1

        group("props", [dict(id=nid(), name=p["name"], type="", gid=p["gid"], x=p["x"], y=p["y"], width=T,
                             height=T, rotation=0, visible=True,
                             properties=props({"visible_if": p["visible_if"]})) for p in self.props])
        group("hotspots", [dict(id=nid(), name=h["name"], type="", x=h["x"], y=h["y"], width=h["w"],
                                height=h["h"], rotation=0, visible=True,
                                properties=props({"label": h["label"], "look": h["look"],
                                                  "walk_x": h["walk"][0], "walk_y": h["walk"][1],
                                                  "face": h["face"], "visible_if": h["visible_if"]}))
                           for h in self.hotspots])
        group("walkable", [dict(id=nid(), name=kind, type="", x=0, y=0, width=0, height=0, rotation=0,
                                visible=True, polygon=[dict(x=x, y=y) for x, y in pts])
                           for kind, pts in self.walk])
        group("spawns", [dict(id=nid(), name=n, type="", x=x, y=y, width=0, height=0, rotation=0,
                              visible=True, point=True) for n, x, y in self.spawns])
        return dict(type="map", version="1.10", tiledversion="1.11.0", orientation="orthogonal",
                    renderorder="right-down", width=self.w, height=self.h, tilewidth=T, tileheight=T,
                    infinite=False, nextlayerid=lid, nextobjectid=oid[0],
                    tilesets=[dict(firstgid=FIRST[n], source="../tilesets/%s.tsj" % n) for n, *_ in TILESETS],
                    layers=layers)


def grass(room, seed):
    rnd = random.Random(seed)
    data = room.layer("ground")
    for i in range(len(data)):
        data[i] = TT(rnd.choice([0, 0, 0, 0, 1, 2]))


def road(room, y0, x0=0, x1=None):
    x1 = room.w if x1 is None else x1
    for x in range(x0, x1):
        room.put("ground", x, y0, TT(13))
        room.put("ground", x, y0 + 1, TT(25))
        room.put("ground", x, y0 + 2, TT(37))


# ---------------------------------------------------------------- village
def village():
    r = Room("village", 40, 11)
    grass(r, 1)
    road(r, 7)
    r.layer("buildings")
    # cottage (cols 3-7, rows 2-4), door at col 5
    r.row("buildings", 3, 2, [TT(52), TT(53), TT(53), TT(53), TT(54)])
    r.row("buildings", 3, 3, [TT(64), TT(65), TT(65), TT(65), TT(66)])
    r.row("buildings", 3, 4, [TT(72), TT(73), TT(74), TT(73), TT(75)])
    # Tobin's house (cols 15-18)
    r.row("buildings", 15, 2, [TT(52), TT(53), TT(53), TT(54)])
    r.row("buildings", 15, 3, [TT(64), TT(65), TT(65), TT(66)])
    r.row("buildings", 15, 4, [TT(84), TT(85), TT(73), TT(75)])
    # crypt (cols 32-37): stone, gate at cols 34-35
    r.row("buildings", 32, 1, [TT(48), TT(49), TT(49), TT(49), TT(49), TT(50)])
    r.row("buildings", 32, 2, [TT(60), TT(61), TT(61), TT(61), TT(61), TT(62)])
    r.row("buildings", 32, 3, [TT(126), TT(126), TT(126), TT(126), TT(126), TT(126)])
    r.row("buildings", 32, 4, [TT(126), TT(125), TT(123), TT(123), TT(125), TT(126)])
    r.prop("gate_closed_l", TT(111), 34, 4, "gate_open=0")
    r.prop("gate_closed_r", TT(112), 35, 4, "gate_open=0")
    r.prop("gate_open_l", TT(113), 34, 4, "gate_open=1")
    r.prop("gate_open_r", TT(114), 35, 4, "gate_open=1")
    # fence, trees, well, sign, tobin
    for x in range(8, 14):
        r.put("buildings", x, 4, TT(45))
    for x, y, t in [(0, 1, 4), (1, 2, 16), (10, 1, 28), (12, 2, 4), (20, 1, 16), (22, 2, 5), (26, 1, 28),
                    (28, 3, 4), (39, 2, 16), (24, 3, 28)]:
        r.prop("tree", TT(t), x, y)
    r.prop("well", TT(104), 11, 5)
    r.prop("sign", TT(83), 1, 5)
    r.prop("tobin", UR(6 * 27 + 24), 19, 5)
    for x, y in [(8, 10), (9, 10), (21, 10), (30, 10), (31, 10)]:
        r.prop("bush", TT(5), x, y)

    r.hotspot("exit_orchard", "path to the orchard", 0, 96, 12, 64, (6, 136), face="left")
    r.hotspot("sign", "signpost", 16, 80, 16, 16, (24, 104), look="West: the orchard. East: the old crypt.")
    r.hotspot("cottage_door", "my cottage", 80, 64, 16, 16, (88, 88), look="Home, sweet home.")
    r.hotspot("well", "well", 176, 80, 16, 16, (184, 104), face="up")
    r.hotspot("tobin", "Tobin", 304, 80, 16, 16, (296, 104), face="right")
    r.hotspot("tobin_house", "Tobin's house", 240, 32, 64, 48, (272, 88),
              look="Tobin's house. The door is shut; he's out here anyway.")
    r.hotspot("crypt_gate", "crypt gate", 544, 64, 32, 16, (560, 88))
    # walkable: everything from just below the buildings to the bottom, minus the well and Tobin
    r.walkable([(0, 86), (640, 86), (640, 172), (0, 172)])
    r.walkable([(172, 92), (196, 92), (196, 100), (172, 100)], hole=True)
    r.walkable([(302, 92), (322, 92), (322, 100), (302, 100)], hole=True)
    r.spawn("start", 88, 120)
    r.spawn("cottage", 88, 92)
    r.spawn("west", 12, 136)
    r.spawn("crypt", 560, 96)
    return r


# ---------------------------------------------------------------- cottage
def interior(r, wall, floor):
    r.layer("ground")
    for y in range(r.h):
        for x in range(r.w):
            r.put("ground", x, y, TD(wall) if y < 4 else TD(floor))


def cottage():
    r = Room("cottage", 20, 11)
    interior(r, 40, 48)
    r.layer("decor")
    r.put("decor", 3, 2, TD(29))
    r.put("decor", 16, 2, TD(29))
    r.prop("bed_shelf", TD(63), 2, 3)
    r.prop("shelf", TD(75), 6, 3)
    r.prop("table", TD(72), 10, 5)
    r.prop("stool", TD(73), 11, 6)
    r.prop("barrel", TD(82), 16, 4)
    r.prop("chest", TD(89), 14, 4)
    r.prop("bucket", TT(130), 4, 7, "bucket_taken=0")
    r.hotspot("shelf", "shelf", 96, 48, 16, 16, (104, 88), look="Jars of pickles. Nothing useful.")
    r.hotspot("table", "table", 160, 80, 16, 16, (168, 104), look="My kitchen table.")
    r.hotspot("chest", "chest", 224, 64, 16, 16, (232, 88), look="My clothes chest.")
    r.hotspot("barrel", "barrel", 256, 64, 16, 16, (264, 88), look="Rainwater.")
    r.hotspot("bucket", "bucket", 64, 112, 16, 16, (72, 132), face="left", look="A sturdy wooden bucket.",
              visible_if="bucket_taken=0")
    r.hotspot("door", "door", 144, 164, 32, 12, (160, 166), face="down")
    r.walkable([(16, 84), (304, 84), (304, 168), (16, 168)])
    r.walkable([(156, 94), (180, 94), (180, 100), (156, 100)], hole=True)
    r.spawn("door", 160, 160)
    return r


# ---------------------------------------------------------------- orchard
def orchard():
    r = Room("orchard", 20, 11)
    grass(r, 7)
    road(r, 7, 12, 20)
    r.layer("buildings")
    # shed, door at col 6
    r.row("buildings", 4, 2, [TT(52), TT(53), TT(54)])
    r.row("buildings", 4, 3, [TT(64), TT(65), TT(66)])
    r.row("buildings", 4, 4, [TT(72), TT(86), TT(75)])
    for x, y, t in [(1, 1, 3), (0, 4, 27), (2, 6, 15), (11, 1, 3), (13, 2, 15), (15, 1, 27), (17, 3, 3),
                    (9, 4, 15), (1, 9, 27), (12, 5, 3)]:
        r.prop("tree", TT(t), x, y)
    r.prop("mushroom", TT(29), 10, 8, "mushroom_taken=0")
    r.prop("branch", TT(106), 5, 5, "branch_cleared=0")
    r.hotspot("shed", "shed door", 80, 64, 16, 16, (88, 96))
    r.hotspot("branch", "fallen branch", 80, 80, 16, 16, (88, 104), visible_if="branch_cleared=0")
    r.hotspot("mushroom", "mushroom", 160, 128, 16, 16, (152, 140), face="right",
              look="A plump red mushroom.", visible_if="mushroom_taken=0")
    r.hotspot("trees", "apple trees", 176, 16, 96, 48, (224, 92), look="Tobin's apple trees.")
    r.hotspot("exit_village", "path to the village", 308, 96, 12, 64, (314, 136), face="right")
    r.walkable([(0, 84), (320, 84), (320, 172), (0, 172)])
    r.spawn("east", 306, 136)
    return r


# ---------------------------------------------------------------- crypt
def crypt():
    r = Room("crypt", 20, 11)
    interior(r, 57, 0)
    r.layer("decor")
    for x in (4, 15):
        r.put("decor", x, 2, TD(29))
    r.prop("tomb_a", TD(65), 4, 5)
    r.prop("tomb_b", TD(65), 15, 5)
    r.prop("cross_a", TD(64), 2, 7)
    r.prop("cross_b", TD(64), 17, 7)
    r.prop("chest_closed", TD(89), 9, 4, "chest_open=0")
    r.prop("chest_open", TD(91), 9, 4, "chest_open=1")
    r.hotspot("chest", "old chest", 144, 64, 16, 16, (152, 92))
    r.hotspot("tombs", "tombstones", 64, 80, 16, 16, (72, 104), look="Names worn away by time.")
    r.hotspot("door", "way out", 128, 164, 64, 12, (160, 166), face="down")
    r.walkable([(16, 84), (304, 84), (304, 168), (16, 168)])
    r.walkable([(66, 90), (82, 90), (82, 96), (66, 96)], hole=True)
    r.walkable([(242, 90), (258, 90), (258, 96), (242, 96)], hole=True)
    r.spawn("entry", 160, 160)
    return r


def main():
    os.makedirs(os.path.join(ROOT, "rooms"), exist_ok=True)
    for name, image, cols, rows in TILESETS:
        ts = dict(type="tileset", version="1.10", tiledversion="1.11.0", name=name, image=image,
                  imagewidth=cols * T, imageheight=rows * T, tilewidth=T, tileheight=T, columns=cols,
                  tilecount=cols * rows, margin=0, spacing=0)
        with open(os.path.join(ROOT, "tilesets", name + ".tsj"), "w", newline="\n") as f:
            json.dump(ts, f, indent=1)
            f.write("\n")
    rooms = [village(), cottage(), orchard(), crypt()]
    for r in rooms:
        with open(os.path.join(ROOT, "rooms", r.id + ".tmj"), "w", newline="\n") as f:
            json.dump(r.to_tmj(), f, indent=1)
            f.write("\n")
    if "--preview" in sys.argv:
        preview(rooms)


def preview(rooms):
    from PIL import Image, ImageDraw
    sheets = {n: Image.open(os.path.join(ROOT, "tilesets", img)).convert("RGBA") for n, img, *_ in TILESETS}

    def tile(gid):
        for n, _, cols, rows in reversed(TILESETS):
            if gid >= FIRST[n]:
                i = gid - FIRST[n]
                return sheets[n].crop(((i % cols) * T, (i // cols) * T, (i % cols) * T + T, (i // cols) * T + T))

    out = os.path.join(ROOT, "..", "docs", "rooms")
    os.makedirs(out, exist_ok=True)
    for r in rooms:
        im = Image.new("RGBA", (r.w * T, r.h * T), (0, 0, 0, 255))
        for data, _ in r.layers.values():
            for i, gid in enumerate(data):
                if gid:
                    t = tile(gid)
                    im.paste(t, ((i % r.w) * T, (i // r.w) * T), t)
        for p in sorted(r.props, key=lambda p: p["y"]):
            if p["visible_if"].endswith("=1"):
                continue
            t = tile(p["gid"])
            im.paste(t, (p["x"], p["y"] - T), t)
        d = ImageDraw.Draw(im)
        for kind, pts in r.walk:
            d.polygon(pts, outline=(255, 0, 0, 255) if kind == "outer" else (255, 128, 0, 255))
        for h in r.hotspots:
            d.rectangle([h["x"], h["y"], h["x"] + h["w"] - 1, h["y"] + h["h"] - 1], outline=(0, 255, 255, 255))
            d.ellipse([h["walk"][0] - 2, h["walk"][1] - 2, h["walk"][0] + 2, h["walk"][1] + 2], fill=(255, 255, 0, 255))
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(out, r.id + ".png"))


if __name__ == "__main__":
    main()
