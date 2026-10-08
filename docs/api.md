# geometry2d Plugin API

A Solar2D native plugin for geometry computation and antialiased mesh generation. Load:

```lua
local Geometry2D = require("plugin.geometry2d")
```

Seven submodules:

| Module | Purpose |
|--------|---------|
| `Geometry2D.polypartition` | Polygon triangulation / convex partition / hole removal (polypartition) |
| `Geometry2D.earcut` | Fast robust triangulation with native hole support (mapbox/earcut) |
| `Geometry2D.fringe` | Edge-AA fringe meshes (ported from NanoVG's expandFill/expandStroke) |
| `Geometry2D.util` | Composite helpers: complete `display.newMesh`-ready meshes in one call |
| `Geometry2D.path` | Adaptive Bezier flattening and fill/SDF/stroke mesh generation |
| `Geometry2D.clipper2` | Clipper2 path boolean, offset, clipping, Minkowski, and path utilities |
| `Geometry2D.ribbon` | Prototype stateful ribbon trail, reusable native storage and retained mesh views |

## Stateful ribbon prototype

`ribbon` is separate from the stateless `path`/`util` output API. Its borrowed
buffers have a shorter lifetime than the owning buffers returned by those APIs.
The standalone project `tests/ribbon_simulator` exercises the prototype; consult
its README for the current validation status before using this in production.

```lua
local trail = Geometry2D.ribbon.new{
    width = 20, aaWidth = 2, minDistance = 5,
    color = {0.2, 0.7, 1, 1}, alpha = 0.8,
}
-- Define the shader in tests/ribbon_simulator/shader.lua before creating a view.
local view = assert(trail:newView{effect = "generator.geometry2d.ribbonPrototype"})
parent:insert(view.group)
-- All coordinates are local to view.group. time, now and maxAge use the same unit.
trail:addPoint(x, y, now)
trail:expire(now, 500)
assert(trail:updateView(view))
-- Cleanup: the display view and the generator have independent lifecycles.
display.remove(view.group)
trail:destroy()
```

Constructor options (unknown keys raise Lua errors):

| Option | Default | Meaning |
|--------|---------|---------|
| `width` | `28` | Positive full core width, in local coordinate units |
| `aaWidth` | `2` | Non-negative outward band extent; `0` omits the AA geometry |
| `minDistance` | `5` | Accept only points farther than this from the last accepted point |
| `reverseDot` | `-0.9` | Split a join if consecutive normalized directions have a smaller dot product; range `[-1,1]` |
| `miterLimit` | `2.4` | Maximum outer miter length divided by core half-width; finite and at least `1`; sharper joins are beveled |
| `mode` | `"indexed"` | `"indexed"` or non-indexed `"triangles"`; fixed for the generator lifetime |
| `initialPointCapacity` | `min(16,maxPoints)` | Initially allocated queue slots, integer `2..maxPoints` |
| `maxPoints` | `4096` | Accepted-point limit, integer `2..8191` |
| `capacityTiers` | `true` | Retain power-of-two vertex/triangle capacity tiers, capped by the indexed vertex limit |
| `color` | `{1,1,1,1}` | Dense `{r,g,b[,a]}` array; components in `[0,1]` |
| `alpha` | `1` | Overall alpha in `[0,1]`, multiplied by color alpha |
| `timestampMode` | `"monotonic"` | Timestamp/expiry policy: `"monotonic"` or opt-in business compatibility `"legacyCount"`; fixed for the generator lifetime |

Methods:

| Call | Result / behavior |
|------|-------------------|
| `trail:addPoint(x,y,time)` | `true` when accepted; `false` for a nearby/duplicate point; `nil,message` if a resource/geometry limit is exceeded |
| `trail:expire(now,maxAge)` | Number removed under `timestampMode` (see below); `maxAge >= 0` |
| `trail:clear()` | Returns self; empties points and retains allocated queue/work/output capacity |
| `trail:reservePoints(count)` | Returns self; preallocates queue slots only; `2..maxPoints` |
| `trail:setWidth(width)` / `trail:setAAWidth(width)` | Returns self; schedules geometry regeneration only when the value changes |
| `trail:setColor(r,g,b[,a])` / `trail:setAlpha(alpha)` | Returns self; style-only updates do not regenerate or upload geometry |
| `trail:pointCount()` | Current accepted point count |
| `trail:snapshot([mode])` | `mode="buffers"` (default) or `"table"`; `nil,message` if fewer than two points or geometry generation fails |
| `trail:newView([{effect=name}])` | A retained view table, or `nil,message`; empty trails create an empty group |
| `trail:updateView(view)` | `view,replaced,updated`; `replaced` indicates mesh creation/removal/replacement; unchanged state returns `view,false,false` |
| `trail:getStats()` | Builds pending geometry and returns counts, revisions, timings and native array capacity bytes; may return `nil,message` |
| `trail:destroy()` | `true` on first destruction, `false` subsequently; immediately releases native arrays and invalidates borrowed buffers |

Point timestamps must always be finite. Under the default `timestampMode="monotonic"`,
they must also be non-decreasing relative to the last accepted point; `expire()`
removes only the expired prefix (`time <= now-maxAge`). Future points cannot be
removed before their own expiry in this default mode. Rejected nearby points do
not refresh the last point's timestamp. Coordinates and geometric
parameters must be finite and representable as float32. Bad types, enums, ranges,
timestamp order in monotonic mode and unknown options raise Lua errors. Native allocation,
geometric limits and display construction/update failures return `nil,message`.

For the existing business `addPointsEvenly()` / `trailDisappearing()` behavior,
opt in when constructing the generator:

```lua
local trail = Geometry2D.ribbon.new{timestampMode = "legacyCount"}
trail:addPoint(0, 0, 2000)  -- a future point
trail:addPoint(20, 0, 1000) -- subsequently append a current/earlier timestamp
local removed = trail:expire(1500, 500) -- 1: removes the FIRST point (time=2000)
-- The second point (time=1000) remains until another expire() call.
```

`legacyCount` preserves timestamps and insertion/path order without clamping or
sorting. Each `expire()` call scans all current points once, counts those satisfying
`now-time >= maxAge`, then removes exactly that count from the queue front. This
intentionally reproduces the business rule: a future point can be removed while
an already-expired point remains. Repeated calls with the same `now` can therefore
remove more points. It does not delete individual points from the middle or
reconnect across them. The scan is O(pointCount), with no extra work array;
default monotonic expiry keeps its existing prefix-only behavior. Buffer
invalidation, distance fade and view updates follow the actual removed prefix.
Keep the business draw-before-expire order in the adapter if that timing is
required; the plugin does not schedule expiry or redraw automatically.

Snapshots contain `vertices`, `uvs`, `pathDistances`,
`contourDistances`, `mode`, `logicalVertexCount`, `logicalIndexCount`,
`vertexCapacity`, `indexCapacity`, `logicalTriangleCount`, `triangleCapacity`,
`tailLength`, `headLength`, `activeLength`,
`bufferRevision` and `bufferValidity`. Table snapshots are independent copies
with 1-based `indices` in indexed mode. Buffer snapshots use float32 coordinates/attributes and,
in indexed mode, zero-based uint16 `indices` with `zeroBasedIndices=true`. Descriptor `count` is
the vertex count for vertices/UVs/attributes, or the index count for indices;
the two scalar distance descriptors also set `componentCount=1`.

```lua
local trail = Geometry2D.ribbon.new{
    mode = "triangles", miterLimit = 2.4,
    timestampMode = "legacyCount", -- optional business expiry compatibility
}
-- snapshot("table"), snapshot("buffers") and newView/updateView all use this mode.
```

Triangle-list snapshots and mesh descriptors omit `indices` and
`zeroBasedIndices`. Logical and capacity vertex counts are multiples of three;
`logicalIndexCount` and `indexCapacity` are zero, while triangle counts equal the
corresponding vertex counts divided by three. Indexed output remains limited to
65535 vertices; triangle-list output is generated directly without an internal
uint16 index bottleneck and is capped at 3,145,728 vertices. Exceeding either
limit returns `nil,message`. Capacity-tier padding in triangle mode adds whole
degenerate triangles at an existing vertex. It duplicates shared vertices and
can use more storage/upload bandwidth; it does not enable Solar2D batching.
The point-count safety limit remains independent of output mode.

UV.x contains accumulated path distance; UV.y contains the contour-distance
coordinate: zero on core vertices and positive on the exterior, up to `aaWidth`,
interpolated across the outer strip. It is the maximum positive distance to the
segment cell's silhouette planes. This is an approximate boundary-distance field near
joins, not an exact signed distance to the union of overlapping geometry. The
two scalar buffers expose the same independent attributes for custom consumers.
At ordinary turns the two neighboring segments share an angle-bisector
partition. Each segment's inner side is clipped to its cell. Outer joins use a
bounded miter, with a shared bevel plane when `1/cos(turnAngle/2) > miterLimit`.
This replaces the old propagated offset-line intersections and can change the
outline, vertex order/count and distance interpolation around turns.

The AA region is the difference between the expanded cell and its core. It is
decomposed into disjoint convex pieces by the dominant silhouette-distance
plane, using the same join partitions as the core. Each piece has an affine
contour-distance field, so triangle interpolation preserves the side/cap AA
ramp instead of spreading corner distances across an entire strip.
Consequently adjacent segment cells, core and AA do not intentionally
cover each other at an ordinary join; clipping does not add an AA seam along
their internal shared boundary. Path-distance coordinates agree at the shared
join, and the core width on straight portions remains unchanged.

Flat caps at endpoints and near-reverse splits are retained. Non-adjacent
self-crossings, tightly folded paths where non-adjacent segments overlap, and
separate reversal runs may still overlap and alpha-blend. This is local join
partitioning, not a global union of the trail or AA region.

The shader computes `clamp((pathDistance-tailLength)/(headLength-tailLength),0,1)`
independently from outward AA coverage based on `fwidth(contourDistance)`.
It multiplies both by the mesh tint/global alpha. The core is not narrowed.
`aaWidth` must cover the desired pixel footprint at the current transform;
too narrow a band can clip the AA transition at small scales. UVs are occupied
by distance data, so ordinary image texture mapping is not supported by this
prototype shader. No vertex extension or deferred extended-attribute write is
needed: UVs are supplied in the mesh constructor, and the effect/tint are assigned
before `newView()` returns. A view without `effect` is a solid geometry diagnostic,
with neither distance fade nor shader AA.

`view.group` is stable across mesh rebuilds; transform/insert that group.
`view.mesh` may change when capacity grows, or become nil after clear/expiry.
The view owns mesh offset compensation, unlike stateless `output="mesh"`.
Its `mode` and logical/capacity triangle counts are also reported; mode cannot
be changed through an existing view.
Do not modify the child mesh geometry or underscore-prefixed view bookkeeping.
Updating a view from a different generator is an error. Reentrant access to the
generator during its display callbacks is rejected, protecting borrowed arrays.
After destroying the generator, existing display meshes retain their last
uploaded state but cannot be updated through it; remove their groups separately.
Views retain the generator until released, unless `destroy()` is called explicitly.

Buffers are readable borrowed views of generator-owned arrays. **Consume them
synchronously before the next successful geometry mutation, clear or destruction.**
Style changes and no-op operations do not invalidate them. Old buffer acquisition
raises an expired-buffer error; buffers do not keep the generator alive. Keep a
reference to the generator, or use `snapshot("table")` for persistent copies.
Do not cache an acquired native pointer across updates. Clearing retains storage;
destroying or garbage-collecting the generator releases it even if stale buffer
descriptors remain. This avoids Lua numeric-table conversion and reuses native
storage; Solar2D still copies/updates its own geometry and GPU resources. It is
not end-to-end zero-copy, and descriptor tables/userdata still allocate.

Capacity padding repeats an existing vertex and uses degenerate triangles, so it
does not enlarge the bounds. Submitted triangle counts include padding even
though its area is zero. `getStats()` reports `logicalTriangleCount`,
`triangleCapacity`, queue capacity, `nativeCapacityBytes` (vector capacities,
excluding allocator overhead, descriptors and engine/GPU copies), `buildCount`,
`noOpBuildCount`, `lastBuildMilliseconds`, `totalBuildMilliseconds`,
`meshCreateCount` and `meshUpdateCount`, in addition to geometry/style revisions
and lengths, plus the selected `timestampMode`, `mode` and `miterLimit`.
Reading stats builds pending geometry; it is not a free per-frame
instrumentation call. Timings cover native geometry generation, not full rendering.

Distance attributes are currently float32 accumulated lengths; very long-lived
trails can lose distance precision. Start a fresh trail/clear when appropriate.
The prototype updates endpoint shader parameters without recomputing alpha
tables, but adding/removing points still rebuilds and uploads geometry. It does
not yet implement partial-range uploads or cross-trail batching.

## Conventions

### Coordinates & winding

- Coordinates are Solar2D display coordinates (**y axis points down**).
- Ring orientation follows the signed shoelace area: **outer = clockwise on screen (positive area), hole = counterclockwise on screen (negative area)** — the screen-space equivalent of polypartition's "CCW outer / CW hole" in y-up.
- earcut detects winding automatically — orientation does not matter there.
- An explicit `{points=..., hole=true}` flag overrides the winding convention (`fringe.fill`).

### Polygon input formats

| Format | Example |
|--------|---------|
| Flat table | `{x1,y1, x2,y2, ...}` |
| Array of rings | `{{x1,y1,...}, {hx1,hy1,...}}` (outer + holes) |
| Array of tagged rings | `{{points=..., hole=false}, {points=..., hole=true}}` |
| Named format (earcut) | `{poly={...}, holes={{...}, ...}}` |
| Packed coordinates | `{bytes=data, type="float32"}` (`"float64"` / `"int32"`; optional `hole=true`) |

Packed coordinate types are explicit because byte length alone cannot reliably
distinguish float, double, and integer arrays. Values use the platform's native
byte order. `data` may be a Lua string or a readable `CoronaMemory` object. A
bare bytes value is not accepted; the typed descriptor is required.

### Mesh output format

The `fringe`, `util`, and `path` mesh functions return data ready for
`display.newMesh`:

```lua
{ vertices = {x1,y1, ...}, alphas = {a1, ...}, indices = {i1, ...}, mode = "indexed" }
-- meshDistance variants name the value array `distances` instead of `alphas`
```

All `util` and `path` mesh functions accept `opts.output`:

| Value | Result |
|-------|--------|
| `"table"` | Default. Lua number tables; distance semantics are specified below. |
| `"buffers"` | Mesh data whose `vertices`, `indices`, and `alphas` / `distances` are owning `CoronaMemory` descriptors. Alpha meshes also include packed `fillVertexColors`. |
| `"mesh"` | Returns `displayMesh, attributes`; alpha meshes pass packed vertex colors directly to `display.newMesh()`, and the second result retains all packed auxiliary attributes. |

`output="mesh"` does not translate by `mesh.path:getVertexOffset()`. For fringe
alpha output it creates white packed RGBA8 vertex colors, passes them to
`display.newMesh()`, and leaves the mesh tintable with `mesh:setFillColor()`.
SDF distance output does not bind a paint, shader, or vertex extension.

Packed SDF output can be written directly with Solar2D's bulk custom-attribute API:

```lua
local data = Geometry2D.util.meshDistance(poly, {
    output = "buffers",
    innerRange = 8, outerRange = 3,
})
local mesh = display.newMesh(data)
mesh.fillExtension = "MegaMechanicalData"
local frames = 0
local function writeDistancesAfterGeometryCreation()
    frames = frames + 1
    if frames < 2 then return end -- allow one render to finish
    Runtime:removeEventListener("enterFrame", writeDistancesAfterGeometryCreation)
    mesh.fillExtendedData:setAttributeValues("geom", data.distances)
end
Runtime:addEventListener("enterFrame", writeDistancesAfterGeometryCreation)
```

The `distances` descriptor already contains `buffer`, `count`, and
`componentCount=1`. Solar2D expands the scalar source into the first component
of the destination attribute and clears the remaining components.

The current Solar2D branch validates the bulk `count` against
`Geometry::GetVerticesUsed()`. A newly constructed mesh does not populate that
render geometry until its first render, so an immediate bulk write is rejected
even though the extension storage is already allocated. The one-shot
second-`enterFrame` write above is required under the confirmed "no Solar2D source changes"
boundary. Existing meshes that have already rendered can be updated directly.

With table output, `alphas` remains a Lua number array and can be applied with
the original per-vertex API:

```lua
local data = Geometry2D.path.meshStroke(path, 8, {output = "table"})
local mesh = display.newMesh(data)
mesh.x, mesh.y = ox, oy
mesh:translate(mesh.path:getVertexOffset())
for i = 1, mesh.fillVertexCount do
    mesh:setFillVertexColor(i, r, g, b, data.alphas[i])
end
```

With buffer output, `alphas` is a float32 descriptor and is not Lua-indexable.
The plugin additionally returns `fillVertexColors`, containing one white RGBA8
color per vertex with the AA value in its alpha byte:

```lua
local data = Geometry2D.path.meshStroke(path, 8, {output = "buffers"})
local mesh = display.newMesh(data)
mesh:setFillColor(r, g, b)
```

Solar2D multiplies these white per-vertex colors by the fill paint, so changing
the mesh fill color preserves the AA ramp. `output="mesh"` passes the same
descriptor to `display.newMesh()` and returns it as
`attributes.fillVertexColors`; it does not perform a second `path:update()`.

### Common options

| Option | Default | Description |
|--------|---------|-------------|
| `join` | `"miter"` | Corner style: `"miter"` / `"bevel"` / `"round"` |
| `miterLimit` | `2.4` | Miter validity threshold |
| `tessTol` | `0.25` | Arc tessellation tolerance for round joins/caps |
| `refine` | `false` | Delaunay refinement after earcut |
| `mode` | `"indexed"` | Mesh layout: `"indexed"` (shared vertex pool + indices) or `"triangles"` (raw triangle list, no indices table) |
| `output` | `"table"` | `"table"`, `"buffers"`, or `"mesh"` |
| `legacyUVs` | `false` | With buffer/mesh output, include normalized UVs for Solar2D builds from before the missing-UV buffer fix |

`legacyUVs=true` requires `output="buffers"` or `output="mesh"`. Current
Solar2D builds synthesize normalized UVs when `uvs` is absent, so the default
avoids generating a redundant stroke UV buffer. Fill and distance outputs always
supply UVs referenced to original contour bounds; distance rejects legacyUVs.
Fill accepts legacyUVs only for buffers/mesh but already has UVs, so no extra buffer is made.

Options are strictly validated per function. Unknown names, invalid enum values,
wrong types, non-finite numbers, and options irrelevant to that function raise a
Lua error. In particular, the corner option is named `join`, not `joint`.

### Failure handling

Geometry generation follows the usual Lua `result, error` convention. A valid
call whose geometry cannot be triangulated, partitioned, expanded, or represented
in the selected mesh mode returns `nil, message` instead of raising an error:

```lua
local meshData, err = Geometry2D.util.meshFill(poly, opts)
if not meshData then
    print("mesh generation skipped:", err)
    return
end
```

Calling-contract errors still raise normally: wrong argument types, malformed
path commands, unknown options, invalid enum values, and non-finite coordinates.
Empty polygon/group arrays and well-formed rings with fewer than three points
return `nil, message` consistently for fill and distance entries. Malformed
coordinates still raise even in a short ring.
For `output="mesh"`, success returns `displayMesh, attributes`; failure returns
`nil, message`.

## polypartition

Accepts a single polygon or a ring list (with hole tags); returns an array of flat
polygon tables on success, or `nil, message` when the selected algorithm rejects
the geometry.

| Function | Description | Input |
|----------|-------------|-------|
| `triangulate_EC(poly)` | Ear-clipping triangulation, O(n²) | single or list |
| `triangulate_MONO(poly)` | Monotone triangulation, O(n log n) | single or list |
| `triangulate_OPT(poly)` | Minimum-weight triangulation, O(n³) | single |
| `convexPartition_HM(poly[, opts])` | Hertel-Mehlhorn convex partition | single or list |
| `convexPartition_OPT(poly[, opts])` | Optimal convex partition, O(n³) | single |
| `removeHoles(polyList)` | Merge holes into the outer ring | list |
| `monotonePartition(polyList)` | Monotone partition | list |

`opts`: `{maxVertices=N}` caps each convex piece at ≤ N vertices (smart diagonal cuts on convex pieces — no degenerate triangulation).

> Note: `removeHoles` is not robust when holes share vertices or hug the boundary; use earcut for such inputs.

## earcut

```lua
local tris = Geometry2D.earcut.triangulate(poly[, opts])
-- Returns an array of flat triangle tables: {{x1,y1,x2,y2,x3,y3}, ...}

local meshData = Geometry2D.earcut.triangulate(poly, { result = "indexed" })
-- Returns {vertices={...}, indices={1-based ...}, mode="indexed"}
```

`opts`: `{refine=true, result="triangles"}`. `result` is either
`"triangles"` (default) or `"indexed"`.

## fringe

Edge-AA fringes based on NanoVG's `nvg__expandFill` / `nvg__expandStroke`.

```lua
local skirt = Geometry2D.fringe.fill(poly[, opts])
-- opts: {fringe=1, join="miter", miterLimit=2.4, tessTol=0.25}
-- Returns {vertices, alphas, indices, mode="indexed"}
--   alphas: 1 on the outline, fading linearly to 0 over `fringe` px outward
--   hole skirts point into the hole (the non-fill side)

local strip = Geometry2D.fringe.stroke(poly, width[, opts])
-- opts: {fringe=1, cap="butt", join="miter", miterLimit=2.4, tessTol=0.25, closed=false}
--   cap: "butt" / "square" / "round"
-- Returns a banded mesh: opaque stroke core (alpha 1) with a linear
-- fade over `fringe` px on each side
```

## util

Choose the requested product, not a backend/geometry combination:

| Entry | Result | Default |
|---|---|---|
| `util.meshFill(poly[, opts])` | Filled mesh, optionally vertex-alpha AA | `aa="vertex", topology="direct"` |
| `util.meshFillGroups(groups[, opts])` | Same, multiple groups in one result | Same |
| `util.meshDistance(poly[, opts])` | Fill plus signed-distance bands, always `distances` | `method="local", innerRange=8, outerRange=2` |
| `util.meshDistanceGroups(groups[, opts])` | Same, multiple groups | Same |
| `path.meshStroke(path, width[, opts])` | Fixed-width centered stroke, optionally vertex-alpha AA | `aa="vertex"` |

Path variants `path.meshFill` / `path.meshDistance` accept absolute Bezier commands
and the existing flatten/fill-rule/intersection policy. No legacy `meshSDF` aliases
remain. `backend` and `geometry` are not accepted by these public mesh functions.

### Fill and centered stroke AA

```lua
local fill = assert(Geometry2D.util.meshFill(poly, {
    aa = "none",             -- or "vertex" (default)
    topology = "direct",     -- or "normalize"
    mode = "indexed",
    output = "buffers",
}))
local stroke = assert(Geometry2D.path.meshStroke(path, 8, {
    aa = "vertex", aaWidth = 1,
    join = "round", cap = "round", output = "buffers",
}))
```

`aa="none"` emits no `alphas`, `fillVertexColors`, or distances. `aa="vertex"`
emits float alpha coverage; buffers/mesh also include packed RGBA8 colors.
`aaWidth` is a positive finite local-coordinate width (default 1), accepted only
with vertex AA. High-level mesh functions no longer accept `fringe`; the low-level
`fringe.fill/stroke` module keeps its existing `fringe` option unchanged.

Fill with no AA rejects `join` and `miterLimit`; stroke still uses these for its
solid joins. `tessTol` controls round geometry or path flattening; util fill
with no AA rejects tessTol. `refine` independently enables earcut refinement.
`maxVertices` defaults to 1,000,000 and cannot exceed that
value; both input fill points and final output are bounded. It is not a peak
memory or time budget. Indexed output still has the 65,535 vertex engine limit.
Round subdivision fails with `nil, message` if the requested tolerance cannot
be represented safely or exceeds the subdivision/output limit; it is not silently
coarsened. Increase `tessTol` or simplify the geometry explicitly.

`topology="direct"` trusts polygon groups and directly triangulates them.
`topology="normalize"` uses Clipper2 at six decimal places to subtract each
group's holes, then union the groups before the SAME fill/fringe construction.
An empty normalized region returns `nil, message`. Ring roles come from group
structure, not winding; islands belong in separate groups. Inputs are not mutated.
Normalization resolves fill regions, **not** global overlap of external AA bands.
No-AA normalized fill is the replacement for the former normalized SDF fill.

For path input, intersection policy runs FIRST. `topology="normalize"` does not
bypass `intersections="error"`; explicitly use `intersections="resolve"` for
crossing paths. `clipperPrecision` controls that path preprocessing, independently
of the six-decimal polygon-group normalization. Avoid enabling both normalization
steps when the second is unnecessary.

Fill always supplies material UVs referenced to the original unpadded input
bounds, plus `kind="fill"`, `aa`, optional `aaWidth`, `topology`, `uvBounds` and
`stats`. This includes table/buffers and the second return of `output="mesh"`.
For path inputs (including retained fills), these bounds come from the flattened
path BEFORE fill-rule/intersection normalization, even when contours cancel.
Path distance meshes use the same convention.
Stroke retains its ordinary mesh/alpha output and does not produce distances.
Stroke UVs are left to Solar2D unless `legacyUVs=true` is requested.

Centered stroke expands half the width on either side. It is NOT a fill-clipped
inside stroke or a globally unioned translucent stroke. Local AA bands can
overlap at distant edges, tight contours or different groups. Fill and stroke
rendered separately (including retained views) do not promise unified opacity.

### Distance meshes

```lua
local data = assert(Geometry2D.util.meshDistance(poly, {
    method = "partition",    -- explicit global method; default is "local"
    innerRange = 8,
    outerRange = 2,
    distanceTolerance = 0.1,  -- partition only
    maxWork = 2000000,
    maxVertices = 1000000,
    mode = "indexed", output = "buffers",
}))
```

All successful distance calls return `distances`, no `alphas` or vertex colors.
All three distance entries default to `method="local"`, including calls without
an options table. Explicitly select `method="partition"` for `innerRange=0`,
`distanceTolerance`, or `distanceTransform`; these options do not select a method
automatically. Default local output has `approximate=true` and its limitations below.
Metadata: `kind="distance"`, `method`, `approximate` (true for local),
`innerRange`, `outerRange`, original `uvBounds`, and `stats`.
The old `backend`, `geometry`, and `sdfVersion` output fields are removed.
The mesh encodes distances; the caller's shader decides the appearance.
Nothing automatically installs a shader or vertex extension.

`method="partition"` retains the global distance-envelope implementation.
`innerRange=0` explicitly requests an exterior-only distance band with body value
zero. It still returns distances, never implicitly switches to vertex-alpha or
plain fill. Use the example shader's one-sided AA branch for this case; a centered
distance-zero transition would make the entire zero-valued body half covered.
An exterior-only mesh cannot implement an internal stroke.

`method="local"` retains local miter inner/outer bands plus earcut core.
It requires positive innerRange and outerRange; `innerRange=0` is rejected.
It accepts `miterLimit` (default 2.4), not distanceTolerance/distanceTransform.
It rejects core collapse, local crossings, excess miter/work/output limits;
it does not repair split/disappearing cores or union overlapping groups/bands.
Its interpolated distance is approximate without a uniform error bound.
It can reuse the distance shader but is not an equivalent-accuracy replacement.

Neither method accepts `aa`, `aaWidth`, `fringe`, `join`, `cap`, `refine`,
`topology`, or `legacyUVs`. The util distance entries reject `tessTol`; path entries
accept it strictly for curve flattening. No automatic fallback or lossy repair
is enabled. Float32 collapse can occur in deep core triangulation, not only in
AA bands; these precision failures remain unresolved. Dropping failed triangles
or applying unverified local repair is not a supported fallback.

### Partition distance field

The following refers to method="partition" with positive innerRange:
The original contour is distance **zero**, the filled region is **negative**,
and the empty region (including holes) is **positive**. Distances are in local
coordinate units unless `distanceTransform` is provided. Interior values saturate
at `-innerRange`; exterior geometry stops at `outerRange`. The shader, not this
mesh boundary, limits exterior coverage to AA. Do not use `outerRange` as stroke width.

`innerRange` must be non-negative and `outerRange` positive; both must be finite floats. `distanceTolerance`
is a positive absolute distance error budget, at least `0.0001`. There is no
`join` option: these are approximated Euclidean nearest-boundary distances, not
independent miter/bevel strips. `path.meshDistance` additionally uses `tessTol` for
curve flattening; that error is separate from `distanceTolerance`.

The tessellator constructs a piecewise-affine approximation to each boundary
segment's distance, partitions by the lower envelope of ALL relevant segments,
clips it into the filled/empty regions, and triangulates the cells. Thus meeting
bands, concave corners, small holes and neighboring exterior bands compete for
coverage rather than drawing on top of one another. Deep fill is triangulated
once without an area-dependent sampling grid. Circular endpoint distance uses
adaptive angular facets: maximum error is controlled by `distanceTolerance`
and the larger range, not a fixed high segment count. Shared scalar/position/UV
vertices are indexed; triangles mode only expands the SAME result.
If earcut cannot triangulate a weakly-simple touching cell with matching area,
that cell is split into trapezoids at existing vertex Y events. This bounded
fallback does not add a uniform grid or change the distance plane. Ordinary
deep fill still uses its compact polygon triangulation.

Group roles are structural, independent of winding. Each group is its outer
region minus the union of its holes; groups are unioned before distance analysis.
Put an island inside a hole in a separate group. Overlapping groups therefore
form one filled region, not separately layered translucent shapes. A hole outside
its own outer ring fails with `nil, message`. Input arrays are not mutated.
For self-intersecting path commands, the existing `intersections="error"` default
still applies; explicitly request `"resolve"` to normalize them. Direct util
contours use non-zero boolean normalization; zero-area/degenerate rings fail.

Precision and resource boundaries:

- Boolean coordinates are quantized to `1e-6` in distance space; float32 output
  adds rounding error. Do not rely on sub-grid features. Input edges or distinct
  near-touching boundaries below an 8-grid-unit guard fail explicitly. Generated
  cells/triangles within the few-grid-unit uncertainty envelope are removed;
  non-negligible float32 flips, inconsistent
  shared distances, or failed triangulation return `nil, message`.
- Transformed coordinate magnitude plus range is limited to `1e7`; excessive
  float32 mapping error also fails. Recenter/rescale large coordinates. The
  angular approximation alone is bounded by `distanceTolerance`; this is NOT
  an exact Euclidean SDF or an exact-real-arithmetic topology guarantee.
- At most 256 angular facets; finer requested accuracy fails rather than being
  silently weakened. `maxWork` defaults to 2,000,000 (maximum 20,000,000), and
  `maxVertices` to 1,000,000 (maximum 1,000,000). Work is an operation budget,
  not milliseconds or a byte allocator limit. Dense adjacent contours can incur
  quadratic work; this implementation prioritizes bounded, correct cached output,
  not guaranteed per-frame rebuilding of arbitrary complex paths.
- Indexed output fails above 65,535 vertices with a triangles/splitting suggestion.
  Triangle-list output is also bounded by `maxVertices`; nothing is truncated.
  Split independent regions only if their AA supports do not meet; otherwise
  splitting restores overdraw and is not a safe fallback. A rejected difficult
  input can be simplified explicitly by the caller or rendered as fill-only
  without AA; the plugin does not silently change its appearance.

UVs are always `(localX-minX)/(maxX-minX), (localY-minY)/(maxY-minY)` using the
UNPADDED original input bounds, returned as `uvBounds={minX,minY,maxX,maxY}`.
They are not based on the AA mesh bounding box; exterior UVs can exceed `[0,1]`.
For a different painting/atlas rectangle, remap via the original local position
`uvBounds.xy + uv * (uvBounds.zw-uvBounds.xy)`. Clamping/wrapping belongs to the
material. Do not repurpose UV.x for distance if the material needs those UVs.
The independent `distances` descriptor has `componentCount=1` when present
(all distance outputs). All distance methods retain the same material UV convention.

`stats` reports native `prepareMs`, `partitionMs`, `triangulateMs`, `totalMs`,
`inputEdges`, `cells`, `work`, `uniqueVertices`, `outputVertices`, `indices`,
`triangles`, and `outputBytes`. Native timing excludes Lua parsing, descriptor
copies, engine upload and rendering. `outputBytes` counts float32 position/UV/
distance plus uint16 output indices, NOT Lua table overhead or GPU allocation.
All stateless descriptors own their bytes and survive later calls. This is not
end-to-end zero-copy; indexed native arrays are moved into the common output,
then copied to owning userdata and synchronously consumed/copied by the engine.

### SDF shader and transforms

Runnable example: `examples/solar2d/sdf_shader.lua`; regression project:
`python3 tests/run_simulator.py tests/sdf_simulator`.
Use `shader.attach(mesh, true)` for its textured filter variant after assigning
an image/gradient paint; the default generator variant uses solid colors.
It keeps material UVs, transports distance through a one-float vertex extension,
mixes premultiplied fill/stroke colors once, and applies one combined coverage.
Use `params={strokeWidth,ready,innerRange,opacity}` and `fillColor`/`strokeColor`.
Its saturated deep fill is explicitly treated as fill, not a stroke edge.
Reserve `innerRange > maximumStrokeWidth + half of the largest AA footprint`
and `outerRange > half of that footprint`. Width changes inside this budget only
update shader uniforms. The shader does not know the missing unsaturated field
beyond `innerRange`; setting width at/beyond saturation is unsupported.

```glsl
float d = vDistance;
float w = max(fwidth(d), 0.00001);
float coverage = clamp(0.5 - d/w, 0.0, 1.0);
float strokeMix = clamp(0.5 + (d + strokeWidth)/w, 0.0, 1.0);
// Mix premultiplied RGBA, then multiply by coverage and overall opacity.
```

`fwidth` produces an orientation-dependent pixel footprint (L1 gradient), not
an analytic pixel-area integral. Very thin subpixel structures have limited
coverage. Keep enough exterior range at the smallest scale: at scale 0.2,
an axis-aligned one-pixel footprint is 5 local units; use at least 2.5 plus margin
outside, more for diagonal edges. The default outer range of 2 is for near-unit
scale, not a promise for every transform.

Translation, rotation, reflection and uniform scale can reuse geometry if the
range still covers AA. Local-width strokes scale with the object; uniform
screen-width strokes can compensate width by scale, within the reserved range.
Nonuniform scaling of a cached local-distance mesh preserves its local metric,
NOT equal screen-space width. For screen-space widths pass
`distanceTransform={a,b,c,d,tx,ty}`: compute distance after
`X=a*x+c*y+tx, Y=b*x+d*y+ty`, then inverse-map positions back into local space.
Use the actual local-to-screen-pixel transform (including content-to-pixel scale),
not merely the object's own xScale/yScale when parents are transformed. Singular
or ill-conditioned transforms fail. Rebuild when its linear metric changes;
rigid screen motions need not rebuild. Distance units then are screen pixels.
For coordinates relative to a display group, a translation-free pixel metric
can be obtained without changing the input path:

```lua
local x0,y0 = group:localToContent(0,0)
local x1,y1 = group:localToContent(1,0)
local x2,y2 = group:localToContent(0,1)
local pixelTransform = {
    (x1-x0)/display.contentScaleX, (y1-y0)/display.contentScaleY,
    (x2-x0)/display.contentScaleX, (y2-y0)/display.contentScaleY, 0, 0,
}
-- opts.distanceTransform = pixelTransform
-- Translation is omitted intentionally: it does not change distances.
```

Curve flattening still precedes this transform, so tighten `tessTol` for strong
magnification. With a cached local metric, `distanceTolerance` also scales into
screen space; choose it for the largest intended magnification. With a pixel
distance transform it is already a pixel error budget. Cached transformed UVs
still map to the original local material.

On this engine, a new mesh's bulk extension buffer is writable only AFTER its
first completed render. Attach the example shader immediately (`ready=0`), wait
for that render, write `distances`, then set `ready=1`. The pending mesh is
transparent, not white. When replacing an on-screen mesh, retain the old object
until the replacement is ready, then swap; do not delete it before preparing the
replacement. This is a documented one-render preparation delay, not synchronous
first-frame attribute initialization. No Solar2D source changes are required.

## path

Bezier paths use an array of absolute command tables. Uppercase short commands
and the exact lower-camel-case long names are accepted:

```lua
local path = {
    {"M", 0, 0},
    {"L", 80, 0},
    {"Q", 120, 40, 80, 80},
    {"C", 55, 105, 25, 105, 0, 80},
    {"Z"},
}

local contours = Geometry2D.path.flatten(path, {
    tessTol = 0.25,
    maxCurvePoints = 262144,
})

local data = Geometry2D.path.meshFill(path[, opts])
local data = Geometry2D.path.meshDistance(path[, opts])
local data = Geometry2D.path.meshStroke(path, width[, opts])
```

Fill and SDF calls reject self-intersections and intersections between contours
by default. This avoids silently feeding ambiguous rings to earcut:

```lua
local data, err = Geometry2D.path.meshFill(path, {
    intersections = "resolve", -- "error" (default) or "resolve"
    fillRule = "nonZero",      -- "nonZero" (default) or "evenOdd"
    clipperPrecision = 4,       -- decimal precision, integer from -8 through 8
})
```

`intersections="resolve"` unions the flattened closed contours with Clipper2
before triangulation. `fillRule="evenOdd"` also performs this normalization so
nested contours do not need opposite winding. With `intersections="error"`, an
actual crossing still returns `nil, message`, including under the even-odd rule.
Clipper2 operates after Bezier flattening, so `tessTol` still controls the curve
approximation that is normalized.

Dashed strokes are generated as one combined mesh:

```lua
local data = Geometry2D.path.meshStroke(path, 8, {
    dashPattern = {12, 6},
    dashOffset = 0,
    maxDashSegments = 4096,
    cap = "round",
})
```

`dashPattern` is an alternating array of visible and gap lengths in path/local
units. Every value must be a positive finite number. An odd-length array is
repeated once so that the visible/gap alternation remains stable across pattern
cycles. `dashOffset` may be positive or negative and is wrapped by the pattern
period. The phase restarts for each subpath. On a closed subpath, visible pieces
on both sides of the closing point are merged so the seam uses a join rather
than two caps.

Dash lengths are measured on the adaptively flattened polyline, so curved-path
accuracy follows `tessTol`. Every visible dash adds two caps; short patterns,
especially with `cap="round"`, can therefore increase the vertex count quickly.
All pieces still belong to one returned mesh. `maxDashSegments` limits the total
number of visible pieces generated by one call (default `4096`); exceeding it
returns `nil, message`.

Commands:

| Command | Arguments |
|---------|-----------|
| `M` / `moveTo` | `x, y` |
| `L` / `lineTo` | `x, y` |
| `Q` / `quadraticTo` | `controlX, controlY, x, y` |
| `C` / `cubicTo` | `control1X, control1Y, control2X, control2Y, x, y` |
| `Z` / `close` | none |

Lowercase single-letter commands such as `"m"` and `"c"` are rejected rather
than being misinterpreted as absolute coordinates. Relative commands are not
currently supported. Command tables must contain exactly the documented number
of arguments.

Quadratic curves are converted to cubic curves and all curves are adaptively
subdivided using `tessTol`. Each cubic is capped at 10 subdivision levels, and
`maxCurvePoints` limits the points in one contour.

### Retained mutable shapes

`path.newShape()` keeps the path commands and styles in native memory and
provides ThorVG-like chained editing without exposing a ThorVG paint or canvas:

```lua
local shape = Geometry2D.path.newShape()
shape:moveTo(199, 34)
    :lineTo(253, 143)
    :lineTo(374, 160)
    :lineTo(287, 244)
    :lineTo(307, 365)
    :lineTo(199, 309)
    :lineTo(97, 365)
    :lineTo(112, 245)
    :lineTo(26, 161)
    :lineTo(146, 143)
    :close()
    :fill(0.59, 0.59, 1)
    :strokeWidth(3)
    :strokeFill(0, 0, 1)
    :strokeJoin("round")
    :strokeCap("round")
    :strokeDash({10, 10})

local view, err = shape:newView()
assert(view, err)
view.group.x, view.group.y = display.contentCenterX, display.contentCenterY

shape:setCommand(2, "L", 260, 150)
local updated, childReplaced = shape:updateView(view)
assert(updated, childReplaced)
```

`newShape(pathTable)` may import the same absolute command table accepted by
`path.flatten()`. Path-building and style methods return the shape, so calls may
be chained. Colors use Solar2D's normalized `0..1` range. Fill defaults to
opaque white; stroke defaults to width `0` (disabled). `fill(false)` and
`strokeFill(false)` disable a part, while `strokeDash(nil)` restores a solid
stroke.

| Method | Purpose |
|--------|---------|
| `moveTo(x,y)`, `lineTo(x,y)` | Append an absolute path command |
| `quadraticTo(cx,cy,x,y)` | Append a quadratic command |
| `cubicTo(c1x,c1y,c2x,c2y,x,y)` | Append a cubic command |
| `close()` / `clear()` | Close the current contour / remove all commands |
| `setCommand(index, name, ...)` | Replace one command; `name` is an accepted short or long absolute command |
| `removeCommand(index)` / `commandCount()` | Remove or count commands |
| `fill(r,g,b[,a])` / `fill(false)` | Set or disable fill color |
| `fillJoin(join)` | Set the fill fringe join |
| `strokeWidth(width)` | Set stroke width; zero disables stroke geometry |
| `strokeFill(r,g,b[,a])` / `strokeFill(false)` | Set or disable stroke color |
| `strokeJoin(join)` / `strokeCap(cap)` | Set stroke corner and cap styles |
| `strokeDash(pattern[,offset])` | Set a positive dense dash array and phase; pass `nil` to clear |
| `configure(opts)` | Merge geometry options into the current configuration |
| `newView()` | Create a retained view table |
| `updateView(view)` | Synchronize one view after shape changes |

`configure()` accepts only `aa`, `aaWidth`, `miterLimit`, `tessTol`, `refine`, `mode`,
`maxCurvePoints`, `maxDashSegments`, `fillRule`, `intersections`, and
`clipperPrecision`. Omitted fields retain their current values. Fill and stroke
join styles and the stroke cap/dash are deliberately set by their named methods.
Retained views always use packed buffers internally; there is no `output` option.

A view owns a stable `view.group`, with current internal display meshes exposed
as `view.fillMesh` and `view.strokeMesh`. Geometry edits always rebuild the
native tessellation cache. If the new mesh has compatible mode, vertex count,
and index count, `updateView()` writes packed vertices, indices, and vertex
colors through `mesh.path:update()` and preserves the child mesh object. If the
topology is incompatible, only that internal child is replaced; `view.group`
remains stable. The second success result is `true` when any child was added,
removed, or replaced, otherwise `false`.

Color-only edits call `setFillColor()` and do not tessellate. Display transforms
belong on `view.group` and likewise do not tessellate. A view retains its shape;
remove `view.group` when the display is no longer needed. Do not replace the
view's documented fields or reuse it with another shape.

Contract violations raise Lua errors. Tessellation/display creation failures
from `newView()` return `nil, message`; failures from `updateView()` also return
`nil, message`.

Fill/SDF functions treat every subpath as closed. Clockwise screen-space
subpaths are outer contours; counterclockwise subpaths are holes, assigned to
the smallest containing outer contour. Stroke closure follows `Z`; setting
`opts.closed=true` closes every stroke contour.

With direct mesh output, auxiliary values are returned separately:

```lua
local mesh, attributes = Geometry2D.path.meshDistance(path, {
    output = "mesh",
    mode = "triangles",
    innerRange = 8, outerRange = 3,
})

mesh.fillExtension = "MegaMechanicalData"
-- For a newly created mesh, wait for one completed render as shown in the
-- packed-output example above.
mesh.fillExtendedData:setAttributeValues("geom", attributes.distances)
```

Indexed buffer output uses packed zero-based `uint16` indices and is limited to
65535 vertices. Use `mode="triangles"` for larger results; this increases vertex
count and only participates in Solar2D triangle-list batching when that optional
renderer feature is enabled and the render state is compatible.

### Mesh option scope

| Function | Accepted options |
|----------|------------------|
| `path.flatten` | tessTol, maxCurvePoints |
| `util.meshFill` / `util.meshFillGroups` | aa, aaWidth, topology, join, miterLimit, tessTol, refine, maxVertices, mode, output, legacyUVs |
| `path.meshFill` | Fill options plus maxCurvePoints, fillRule, intersections, clipperPrecision |
| `util.meshDistance` / `util.meshDistanceGroups` | method, innerRange, outerRange, maxWork, maxVertices, mode, output; partition: distanceTolerance, distanceTransform; local: miterLimit |
| `path.meshDistance` | Distance options plus tessTol, maxCurvePoints, fillRule, intersections, clipperPrecision |
| `path.meshStroke` | aa, aaWidth, cap, join, miterLimit, tessTol, closed, dashPattern, dashOffset, maxDashSegments, maxCurvePoints, maxVertices, mode, output, legacyUVs |

Options irrelevant to the selected mode are errors, as described above.

## clipper2

`Geometry2D.clipper2` is an independent Lua sublibrary inside
`plugin.geometry2d`. Its name deliberately differs from the existing Solar2D
Marketplace `plugin.clipper`, which wraps the older Clipper generation. This
module binds Clipper2 2.x and exposes `clipper2.version`; it is not a separate
`require("plugin.clipper2")` entry point.

Clipper2 paths are dense flat coordinate arrays, and path collections are dense
arrays of paths:

```lua
local C = Geometry2D.clipper2
local subject = {{0,0, 100,0, 100,100, 0,100}}
local clip = {{50,50, 150,50, 150,150, 50,150}}

local overlap = C.intersection(subject, clip, {
    fillRule = "nonZero",
    precision = 2,
})
local outline = C.union({subject[1], clip[1]})
local cut = C.difference(subject, clip)
local toggled = C.xor(subject, clip)
```

Boolean options are `fillRule` (`"evenOdd"`, `"nonZero"`, `"positive"`, or
`"negative"`), `precision` (`-8..8`), `preserveCollinear`, and
`reverseSolution`. `union()` accepts one combined subject collection;
`intersection()`, `difference()`, and `xor()` accept separate subject and clip
collections.

The lower-level boolean entry point also supports open subjects and hierarchy:

```lua
local result = C.booleanOp("intersection", subjects, clips, {
    openSubjects = {{-20,50, 120,50}},
    polyTree = true,
})
-- result.tree: root nodes {polygon, isHole, children}
-- result.open: clipped open paths
```

Without `polyTree=true`, `booleanOp()` returns `{closed=..., open=...}`. Its clip
type is `"intersection"`, `"union"`, `"difference"`, or `"xor"`. Pass an empty
table or `nil` for `clips` when a union has no separate clip collection.

The remaining mature geometry operations are available as stateless functions:

| Function | Purpose / options |
|----------|-------------------|
| `inflate(paths, delta[, opts])` | Offset closed or open paths; `delta` may be a number or nested per-vertex arrays; `joinType`, `endType`, `miterLimit`, `arcTolerance`, `precision`, `preserveCollinear`, `reverseSolution`, `polyTree` |
| `rectClip(rect, paths[, opts])` | Clip closed paths to `{left,top,right,bottom}` |
| `rectClipLines(rect, lines[, opts])` | Clip open lines to a rectangle |
| `minkowskiSum(pattern, path[, opts])` / `minkowskiDiff(...)` | Minkowski operations; `closed`, `precision` |
| `simplify(paths, epsilon[, opts])` | Clipper2 simplify; `closed` defaults true |
| `ramerDouglasPeucker(paths, epsilon)` | RDP path reduction |
| `trimCollinear(path[, opts])` | Remove collinear vertices; `closed`, `precision` |
| `stripDuplicates(path[, opts])` / `stripNearEqual(path, distance[, opts])` | Remove repeated or near-equal vertices |
| `translate(paths, dx, dy)` / `reverse(paths)` | Transform path collections |
| `area(paths)` / `isPositive(path)` / `length(path[, closed])` | Measurements and orientation |
| `pointInPolygon(point, path)` | Returns `"inside"`, `"outside"`, or `"on"` |
| `getBounds(paths)` | Returns both `{left,top,right,bottom}` and named fields, or `nil` for no paths |
| `pathContains(inner, outer)` / `closestPoint(point, a, b)` | Containment and nearest point helpers |
| `ellipse(cx, cy, rx[, ry][, opts])` | Create an ellipse path; `opts.steps` controls tessellation |

`joinType` is `"square"`, `"bevel"`, `"round"`, or `"miter"`. `endType` is
`"polygon"`, `"joined"`, `"butt"`, `"square"`, or `"round"`. All coordinate
tables are strictly dense and contain finite-float values. Contract violations
raise; Clipper2 range or algorithm failures return `nil, message`.

Clipper2's experimental triangulation implementation is deliberately neither
linked nor exposed as Lua API. Use `Geometry2D.earcut` or the path mesh APIs
for triangulation.

For a variable offset, pass one dense delta array per input path, with one value
per path vertex. Variable deltas expose Clipper2's mature delta-callback offset
without calling Lua once per generated corner. They currently return flat paths;
combining variable deltas with `polyTree=true` is a contract error.

## Example

```lua
-- Complete AA mesh for a polygon with a hole (one mesh; renderer costs vary)
local data = Geometry2D.util.meshFill({
    { 0,0,  100,0,  100,100,  0,100 },      -- outer (clockwise on screen)
    { 30,30,  30,70,  70,70,  70,30 },      -- hole (counterclockwise on screen)
}, { aaWidth = 2, join = "round" })

local mesh = display.newMesh(data)
for i = 1, mesh.fillVertexCount do
    mesh:setFillVertexColor(i, 0.3, 0.8, 1, data.alphas[i])
end
```
