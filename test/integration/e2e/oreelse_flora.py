"""Ore Else's flora driven through its command port: the frondstalks, ashcups and quillbulbs of a
run register in the tile grid, none stays on a deposit's tile and no two share a tile; a wall
placed over a plant sinks it out of sight and demolishing the wall brings it back; after a
restart the same holds on the new map, whose flora starts from its own plants and deposits.

Every case runs in a fresh apps/oreelse process with its default scene and the fixed session
seed of oreelse_buildings.py, whose helpers place and demolish the walls (it describes the setup:
a 640x360 window without shadows, a fixed delta of 1/60 s per frame, waits that count frames).
"""
from oreelse_buildings import (  # noqa: F401 (ARGS)
    ARGS, SETTLE, STATE, building_at, buildings, click, count, demolish, deposits, key,
    rows_of, select, set_inventory, set_state, start)

APP = "apps/oreelse"

# The plants of a run (scenery.flecs): 96 frondstalks, 200 ashcups and 84 quillbulbs, fewer
# once those on deposits and on taken tiles are gone
PLANTS = 96 + 200 + 84

# The tile grid (cfg.x0, cfg.z0, cfg.tile, cfg.rows, cfg.cols)
X0 = -47
Z0 = -47
TILE = 2
ROWS = 48
COLS = 48

# A plant under a building sinks this far below the ground (cfg.floraSink)
SINK = 8


def plants(app):
    """The plants of the run: path -> (row, col) of their tile, (-1, -1) off the grid."""
    return {path: (f["row"], f["col"]) for path, f in rows_of(app, "oreelse.OreFlora").items()}


def positions(app):
    """The positions of the plants of the run: path -> Position."""
    return rows_of(app, "box.transform.Position, oreelse.OreFlora")


def tile_of(p):
    """The tile a position lies in, (-1, -1) off the grid."""
    col = int((p["x"] - X0) / TILE + 0.5 + 1000) - 1000
    row = int((p["z"] - Z0) / TILE + 0.5 + 1000) - 1000
    if 0 <= row < ROWS and 0 <= col < COLS:
        return row, col
    return -1, -1


def check_map(app):
    """Check the flora of the run against its deposits: every plant on the grid holds its own
    tile, the tile of its position, and none is on a deposit's tile. Returns the plants."""
    found = plants(app)
    kinds = {path.split(".")[-1].split("_")[0] for path in found}
    app.check(kinds == {"frondstalk", "ashcup", "quillbulb"},
              "the flora has the kinds %s" % sorted(kinds))
    app.check(100 < len(found) <= PLANTS, "the run has %d plants" % len(found))

    on_grid = {path: t for path, t in found.items() if t != (-1, -1)}
    app.check(on_grid, "no plant registered on the grid")
    app.check(len(set(on_grid.values())) == len(on_grid),
              "two plants share a tile: %d plants on %d tiles" %
              (len(on_grid), len(set(on_grid.values()))))

    for path, p in positions(app).items():
        app.check(tile_of(p) == found[path],
                  "the plant %s at %s registered on the tile %s" % (path, p, found[path]))

    taken = {(d["row"], d["col"]) for d in deposits(app).values()}
    app.check(taken, "the map has no deposits")
    on_deposit = sorted(path for path, t in on_grid.items() if t in taken)
    app.check(not on_deposit, "plants on deposit tiles: %s" % on_deposit)

    return found


def pick(app, found):
    """A plant on the grid away from its edges, on a tile without a building: (path, tile)."""
    built = set(buildings(app).values())
    for path in sorted(found):
        row, col = found[path]
        if 2 <= row < ROWS - 2 and 2 <= col < COLS - 2 and (row, col) not in built:
            return path, (row, col)
    app.check(False, "no plant to build over")


def height(app, path):
    return float(app.exec("get %s Position.y" % path))


def wait_height(app, path, y, what):
    app.wait_for("get %s Position.y" % path, lambda out: abs(float(out) - y) < 1e-4,
                 frames=SETTLE * 4, what=what)


def place_wall(app, path, tile):
    """Place a wall on the tile of a plant: the plant sinks out of sight, where it stood."""
    before = app.get(path, "Position")
    set_inventory(app, [("Wall", 1)])
    select(app, "1", "Wall")
    click(app, *tile)
    app.check(building_at(app, tile[0], tile[1], "Wall") is not None,
              "no wall on the plant's tile %s" % (tile,))
    wait_height(app, path, -SINK, "the plant %s %g under the wall" % (path, SINK))
    after = app.get(path, "Position")
    app.check(after["x"] == before["x"] and after["z"] == before["z"],
              "the plant moved from %s to %s" % (before, after))


def check_cover(app, found):
    """Wall over a plant hides it, demolishing the wall shows it again."""
    path, tile = pick(app, found)
    app.check(abs(height(app, path)) < 1e-4, "the plant %s is not on the ground" % path)
    place_wall(app, path, tile)

    demolish(app, *tile)
    app.check(building_at(app, *tile) is None, "the wall on %s was not demolished" % (tile,))
    app.check(count(app, "Wall") == 1, "the demolished wall is not in the inventory")
    wait_height(app, path, 0, "the plant %s back on the ground" % path)


def test_no_flora_on_deposits(app):
    start(app)
    check_map(app)

    # Every plant on the grid stands on the ground
    for path, p in positions(app).items():
        app.check(p["y"] == 0, "the plant %s stands at %s" % (path, p))


def test_wall_hides_flora(app):
    start(app)
    found = check_map(app)
    check_cover(app, found)

    # The other plants did not move
    for path, p in positions(app).items():
        app.check(p["y"] == 0, "the plant %s stands at %s" % (path, p))


def test_restart(app):
    start(app)
    before = check_map(app)

    # A wall left standing over a plant at the end of the run
    path, tile = pick(app, before)
    place_wall(app, path, tile)

    set_state(app, "Lost")
    key(app, "R")
    app.wait_for(STATE % "Playing", "true")
    app.wait_frames(SETTLE)

    after = check_map(app)
    app.check(after != before, "the new run has the flora of the previous one")
    app.check(not buildings(app), "the wall of the previous run stands")

    # The grid of the new run is not the old one's: no plant is sunk under the gone wall
    for name, p in positions(app).items():
        app.check(p["y"] == 0, "the plant %s stands at %s after the restart" % (name, p))

    check_cover(app, after)
