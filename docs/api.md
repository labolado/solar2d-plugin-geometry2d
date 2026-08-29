# geometry2d Plugin API

A Solar2D native plugin for geometry computation and antialiased mesh generation. Load:

```lua
local Geometry2D = require("plugin.geometry2d")
```

Six submodules:

| Module | Purpose |
|--------|---------|
| `Geometry2D.polypartition` | Polygon triangulation / convex partition / hole removal (polypartition) |
| `Geometry2D.earcut` | Fast robust triangulation with native hole support (mapbox/earcut) |
| `Geometry2D.fringe` | Edge-AA fringe meshes (ported from NanoVG's expandFill/expandStroke) |
| `Geometry2D.util` | Composite helpers: complete `display.newMesh`-ready meshes in one call |
| `Geometry2D.path` | Adaptive Bezier flattening and fill/SDF/stroke mesh generation |
| `Geometry2D.clipper2` | Clipper2 path boolean, offset, clipping, Minkowski, and path utilities |

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
-- meshSDF variants name the value array `distances` instead of `alphas`
```

All `util` and `path` mesh functions accept `opts.output`:

| Value | Result |
|-------|--------|
| `"table"` | Default. Existing Lua number tables; fully backward-compatible. |
| `"buffers"` | Mesh data whose `vertices`, `indices`, and `alphas` / `distances` are owning `CoronaMemory` descriptors. Alpha meshes also include packed `fillVertexColors`. |
| `"mesh"` | Returns `displayMesh, attributes`; alpha meshes pass packed vertex colors directly to `display.newMesh()`, and the second result retains all packed auxiliary attributes. |

`output="mesh"` does not translate by `mesh.path:getVertexOffset()`. For fringe
alpha output it creates white packed RGBA8 vertex colors, passes them to
`display.newMesh()`, and leaves the mesh tintable with `mesh:setFillColor()`.
SDF distance output does not bind a paint, shader, or vertex extension.

Packed SDF output can be written directly with Solar2D's bulk custom-attribute API:

```lua
local data = Geometry2D.util.meshSDF(poly, {
    output = "buffers",
    distanceSign = "outsidePositive",
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
| `distanceSign` | `"outsideNegative"` | SDF only: `"outsideNegative"` or `"outsidePositive"` |
| `legacyUVs` | `false` | With buffer/mesh output, include normalized UVs for Solar2D builds from before the missing-UV buffer fix |

`legacyUVs=true` requires `output="buffers"` or `output="mesh"`. Current
Solar2D builds synthesize normalized UVs when `uvs` is absent, so the default
avoids generating and copying a redundant buffer.

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

Complete "solid fill + AA fringe" meshes in one call.

```lua
-- Vertex-alpha version (no shader needed)
local data = Geometry2D.util.meshFill(poly[, opts])
-- opts: {fringe=1, join=..., miterLimit=..., tessTol=..., refine=...,
--        mode=..., output=...}
-- Returns {vertices, alphas, indices, mode}

local data = Geometry2D.util.meshFillGroups(groups[, opts])
-- groups: an array of meshFill inputs, merged into ONE mesh

-- SDF version (for exact 1px AA via fwidth() in a fragment shader)
local data = Geometry2D.util.meshSDF(poly[, opts])
-- opts: {distance=5, join=..., miterLimit=..., tessTol=..., refine=...,
--        mode=..., output=..., distanceSign=...}
-- Returns {vertices, distances, indices, mode}
--   distances: 0 on the body/boundary, 0 → -distance across the band
--   (negative outside); body vertices are the boundary vertices — no
--   interior values needed, the AA ramp is alpha 1 for every d >= 0

local data = Geometry2D.util.meshSDFGroups(groups[, opts])
```

SDF fragment shader recipe:

```glsl
float d = vDistance;                      // interpolated distance from the mesh
float w = fwidth(d);                      // distance change across one screen pixel
float a = smoothstep(-w, 0.0, d);         // opaque at the boundary, exact 1px fade
```

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
local data = Geometry2D.path.meshSDF(path[, opts])
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

`configure()` accepts only `fringe`, `miterLimit`, `tessTol`, `refine`, `mode`,
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
local mesh, attributes = Geometry2D.path.meshSDF(path, {
    output = "mesh",
    mode = "triangles",
    distanceSign = "outsidePositive",
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
| `path.flatten` | `tessTol`, `maxCurvePoints` |
| `util.meshFill` | `fringe`, `join`, `miterLimit`, `tessTol`, `refine`, `mode`, `output`, `legacyUVs` |
| `path.meshFill` | `fringe`, `join`, `miterLimit`, `tessTol`, `refine`, `mode`, `output`, `legacyUVs`, `fillRule`, `intersections`, `clipperPrecision` |
| `util.meshSDF` | `distance`, `distanceSign`, `join`, `miterLimit`, `tessTol`, `refine`, `mode`, `output`, `legacyUVs` |
| `path.meshSDF` | `distance`, `distanceSign`, `join`, `miterLimit`, `tessTol`, `refine`, `mode`, `output`, `legacyUVs`, `fillRule`, `intersections`, `clipperPrecision` |
| `path.meshStroke` | `fringe`, `cap`, `join`, `miterLimit`, `tessTol`, `closed`, `dashPattern`, `dashOffset`, `maxDashSegments`, `mode`, `output`, `legacyUVs` |

The three `path.mesh*` functions additionally accept `maxCurvePoints`.

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
-- Complete AA mesh for a polygon with a hole (one mesh, one draw call)
local data = Geometry2D.util.meshFill({
    { 0,0,  100,0,  100,100,  0,100 },      -- outer (clockwise on screen)
    { 30,30,  30,70,  70,70,  70,30 },      -- hole (counterclockwise on screen)
}, { fringe = 2, join = "round" })

local mesh = display.newMesh(data)
for i = 1, mesh.fillVertexCount do
    mesh:setFillVertexColor(i, 0.3, 0.8, 1, data.alphas[i])
end
```
