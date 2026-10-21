# quest-engine study sheet (about 30 minutes)

Each section explains how something works, then lists the questions you're likely to get, with
answers. File references point at the code to open if asked to walk through it.

---

## 1. The game loop: fixed timestep (`src/frontend/main.cpp`, `App::frame`)

```
acc += frame_dt (capped at 5 steps)
while acc >= 1/60: acc -= 1/60; game.tick(clicks for this tick)
render(latest state)
```

* **Why fixed?** The sim gives the same result whatever the frame rate. That is what makes replays
  and save/load deterministic, and keeps movement speed independent of the monitor (60 Hz or 144 Hz).
* **The cap ("spiral of death").** If a frame takes longer than the steps it has to simulate, the
  game falls further behind every frame. The cap makes it run in slow motion after a stall (a
  window drag, a debugger break) instead of freezing.
* **What I skipped:** interpolating the render between the last two sim states (`alpha = acc/step`).
  At 60 Hz sim on a 60 Hz display it isn't visible. On a 144 Hz display you'd see judder, and
  interpolation is the fix.
* **Input:** clicks are queued and handed to the *next* tick. The tick number is what gets recorded.

**Q: Variable vs fixed timestep?** Variable: `update(dt)`, simple, but the results depend on dt
(integration error, collisions that tunnel through at low fps), so replays don't reproduce.
Fixed: deterministic, at the cost of the accumulator and the possibility of 0 or 2 sim steps in one frame.

## 2. Pathfinding: visibility graph + A* (`src/core/pathfind.cpp`)

* The walkable area is one polygon plus holes (obstacles). Points on any edge count as walkable.
* **Key fact:** in a polygonal area, a shortest path is a series of straight segments that only
  bend at **reflex (concave) corners of the outer boundary** or **convex corners of holes**. Picture a
  string pulled tight: it can only catch on corners that stick *into* the walkable space.
* So the graph nodes are just those corners. Two nodes get an edge if the segment between them
  stays inside the area (the "visibility graph"). It is built once per room, in O(n² · edges).
* Per query: if the start can see the goal directly, walk straight. Otherwise link the start and
  the goal to the nodes they can see and run **A\*** with straight-line distance as the heuristic.
  That heuristic is admissible (it never overestimates) and consistent, so A* returns the optimal path.
* **Line of sight** is the tricky part (`lineOfSight`):
  1. any *proper* crossing with an edge means blocked;
  2. touching cases (passing exactly through a vertex, or running along an edge) are not proper
     crossings. So I split the segment at every boundary vertex lying on it and check that the
     midpoint of each piece is inside. This catches a segment that grazes a corner and goes
     outside, and allows walking along a wall.
* **A click outside the area** is clamped to the nearest point on the boundary.
* **Testing:** hand-checked cases (a U shape, a square with a hole), plus a property test: 522
  random queries over 3 polygons, compared with an exact Dijkstra that uses **all** polygon
  vertices, not just the pruned ones. All matched within 0.01 px. This tests both the "only
  concave corners matter" pruning and the A* itself.

**Q: Why not a grid / navmesh?** A grid gives zig-zag paths that need smoothing, and the
resolution trades memory for accuracy. A navmesh (triangles + a funnel algorithm) is the general
3D answer. For small 2D rooms with hand-drawn walkable shapes, the visibility graph is exact,
tiny (a handful of nodes), and needs no smoothing. It's the classic adventure-game approach
(SCUMM-era games used walk boxes; later engines used polygons).
**Q: Complexity?** Build: O(V² · E) for V candidate corners and E edges. Query: O(V · E) to link
start and goal, then A* over O(V²) edges. V is under 10 per room here.
**Q: What breaks it?** Large open maps with many obstacles (V² edges), and dynamic obstacles
(the graph has to be rebuilt). Then you'd go to navmeshes or a grid with any-angle search (Theta*).

## 3. Depth sorting, layers, camera (`src/frontend/renderer.cpp`)

* Draw order: tile layers, then props and the player **sorted by the y-coordinate where they touch
  the floor**, then foreground layers, the message box, the UI, the cursor.
* The sort is stable, with a tie-break on index, so equal baselines don't flicker from frame to frame.
* Camera: `cam += (target - cam) * 0.12` per tick (exponential smoothing), clamped to the room
  bounds, snapped when within 0.05 px, and snapped on entering a room.
* Rendering is at a 320×240 logical resolution, scaled by an integer factor
  (`SDL_RenderSetLogicalSize` + `SetIntegerScale`). Mouse coordinates arrive already in logical space.

## 4. Point-and-click flow (`src/core/game.cpp`)

* Click on a hotspot: store a *pending action* {hotspot, verb, item}, path to the hotspot's
  walk-to point, and on arrival turn to face it and run the action. Click on the floor: walk there
  and cancel any pending action.
* An action looks up the **first rule** in `game.json` that matches (room, hotspot, verb, item)
  and whose conditions hold, then applies its effects: `say`, `set`, `give`, `take`, `goto`, `talk`.
  If nothing matches you get a default line ("I can't use that.").
* Items: with Use selected, click item A (it becomes the cursor), then click item B to combine
  them, or a hotspot to use A on it.
* `goto` and `talk` must be the **last** effect in a rule (the lint enforces it), so a rule never
  has a half-applied state across a room change or a conversation.

## 5. Ink integration (`src/core/dialogue.cpp`)

* Pipeline: `.ink` → (inklecate / Inky export) → `.ink.json` → (inkcpp compiler, at load time) →
  binary → inkcpp runner. CI recompiles the `.ink` and diffs the result, so the JSON can't go stale.
* **Variable bridge:** before a conversation, push every store variable into Ink. `globals->set`
  returns false if Ink doesn't declare it, which is how I know which variables are synced. Pull
  them back after every line and choice.
* **Fresh Ink globals per conversation:** the store is the only persistent state. Consequence I
  hit: once-only `*` choices reappear in the next conversation. Fix: guard them with synced
  variables (`* {not asked_crypt} [...]`). That's the writer contract.
* **Q: Why not keep one Ink state for the whole game and save Ink's own state?** You can: inkcpp
  supports snapshots. But then progression is split across two stores. Saves would need the Ink
  snapshot too, and the solver would have to treat Ink internals as part of the game state. One
  store keeps saves, replays and the solver simple. The cost is the writer contract.
* **Q: Which Ink features did you check inkcpp supports?** Knots, stitches, labelled gathers,
  once-only and sticky choices, conditional choices and text, assignment, `INCLUDE` and tags.
  `tests/data/features.ink` + `tests/test_dialogue.cpp`. The inkcpp README claims all of Ink 1.1,
  checked with ink-proof.

## 6. The puzzle solver (`src/core/solver.cpp`)

* State = (room, all store variables). Inventory is in the store, so it's included.
* Moves = rules on visible hotspots (no item / each held item), item combinations, and every
  branch of any conversation a rule starts. For each branch it runs the real Ink story from the
  knot, re-running from the start for each choice path (DFS), de-duplicated on (variables,
  visible choices), with a depth limit.
* Forward BFS gives the reachable set and the shortest solution. A **reverse BFS from the win
  states** gives "can still win". Dead end = reachable and can't win. Each dead end is reported at
  its *entry* (a dead state whose BFS parent could still win), with the shortest script into it and
  the variables that changed on the fatal move.
* Results: 246 states, 748 transitions, 15 ms, 0 dead ends. The planted dead end ("feed the
  mushroom to the squirrels") is caught. Removing the rope + bucket combination makes it report "not completable".
* Walking is abstracted. Lint checks reachability with the real pathfinder from every spawn point.
* **Q: State explosion?** The state is a few dozen small ints, and adventure games are mostly
  monotonic (flags only flip one way), so the reachable set stays small: hundreds of states
  here, not 2^n. For a bigger game: group rooms into chapters, add a symmetry reduction, or
  search only the variables that the puzzle dependency graph says matter.
* **Q: Why does a studio care?** Accidental dead ends are a well-known adventure-game bug (an item
  used up too early, a once-only dialogue option). This finds every one, with a repro script, on every commit.

## 7. Determinism and replay (`src/core/replay.cpp`)

* Recording = (tick, click x, y). Replay = new game + the same clicks on the same ticks → the
  same state. Verified by an FNV-1a hash over tick, room, store, and the **bit patterns** of
  position and camera floats.
* **Cross-platform:** a recording made on Windows/MSVC replays to the same hash on Linux
  GCC/Clang, macOS arm64 and WebAssembly. That works because:
  * only IEEE basic operations (+ − × ÷ sqrt) are used, and these are correctly rounded everywhere;
    no `sin`/`cos`/`atan2` (libm results differ between platforms);
  * there's no x87 (x64 uses SSE2; wasm f32 is IEEE);
  * `-ffp-contract=off` stops FMA fusion on Clang/GCC (arm64 has FMA, and a fused result
    rounds once instead of twice);
  * no `-ffast-math`, no unordered-container iteration in the sim, and no wall-clock time or
    unseeded randomness.
* **Q: What would break determinism?** `-ffast-math`, libm transcendentals, iterating an
  `unordered_map` (order differs by STL), uninitialised memory, threads, time, `rand()`. Also
  floating-point state changed by a third-party library (denormal flags, x87 precision control on 32-bit).
* **Q: Lockstep networking?** Same idea: send inputs, not state. Every peer runs the same sim, and
  you compare state hashes to detect desyncs. This hash is exactly that check.

## 8. Saves and migration (`src/core/save.cpp`)

* `format` = engine layout version; `content_rev` = data version.
* `migrateSave` is a chain: v1 → v2 → … one step per version, so old saves from any version have a
  path forward without needing an "any version to latest" converter. A save with a newer format
  than the game knows is refused.
* Content renames are a per-revision map applied on load. Unknown variables are dropped, missing ones get defaults.
* Proof: `save_v1.json` was written by the v1 commit, and the v2 code loads it and finishes the
  game. Saving after every walkthrough command → load → identical re-save → finish the game.
* **Q: Binary or JSON?** JSON: diffable, easy to migrate, and small here. Binary + a schema
  (flatbuffers/protobuf) when saves are big or loads must be fast.

## 9. Performance numbers (and what they mean)

* Desktop: work p50 0.14 ms, p99 0.7–0.9 ms; browser: p50 0.1–0.2 ms, p99 0.4–0.5 ms. Both at a
  steady 60 fps (vsync), with intervals of 16.6–16.7 ms at p50.
* "Work" = sim + building the draw list, up to `SDL_RenderPresent`. SDL batches draw calls and
  flushes them in Present, so the GPU's time isn't in "work".
* **Measurement bug I hit:** the first browser numbers were all whole milliseconds (0.000/1.000).
  `SDL_GetPerformanceCounter` only had 1 ms resolution in the Emscripten build. I switched to
  `emscripten_get_now()` (`performance.now()`, which Chrome clamps to 0.1 ms on a page without
  cross-origin isolation, so the browser numbers are only good to 0.1 ms).
* Sim alone: 0.03–0.04 µs per tick. The frame cost is all rendering.

## 10. Things to say you'd do next

* Interpolated rendering for high-refresh displays; NPC walking and idle animation; audio.
* Dynamic walkable areas (a cleared obstacle opens floor): rebuild the visibility graph, which is cheap at V < 10.
* Save Ink snapshots to allow saving mid-conversation.
* A Tiled extension, or a game-side overlay that shows the solver's dead ends on the map.
