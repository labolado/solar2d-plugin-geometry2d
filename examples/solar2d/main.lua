--
-- geometry2d polypartition test
--
display.setStatusBar(display.HiddenStatusBar)

-- local sph = require("sph")
-- sph.setScale(1)
-- sph.setMode(2)
-- sph.x = 1024

local Geometry2D = require("plugin.geometry2d")
local PP = Geometry2D.polypartition

local W, H   = display.contentWidth, display.contentHeight
local CX, CY = display.contentCenterX, display.contentCenterY

local inspect = require("inspect")
local function _DUMP(...)
    local obj, msg, options
    local args = {...}
    if #args == 1 then
        obj = args[1]
    elseif #args == 2 then
        if type(args[1]) == 'string' then
            --print(type(args[1]))
            msg = args[1]
            obj = args[2]
        else
            obj     = args[1]
            options = args[2]
        end
    elseif #args == 3 then
        msg = args[1]
        obj = args[2]
        options = args[3]
    end

    msg = msg or ""
    options = options or {}
    local data = inspect(obj, options)
    print("[DUMP]" .. msg .. data)
end

-- -------------------------------------------------------------------
-- Helpers
-- -------------------------------------------------------------------

-- Convert our flat-pair format to Corona display-object vertices
-- (x1,y1, x2,y2, ...) → { x1,y1, x2,y2, ... }
local function PolygonVertices(poly)
    local verts = {}
    local xMin, xMax = math.huge, -math.huge
    local yMin, yMax = math.huge, -math.huge
    local numPoly = #poly
    for i = 1, numPoly, 2 do
        local px, py = poly[i], poly[i + 1]
        verts[#verts + 1] = px
        verts[#verts + 1] = py
        xMin = math.min(xMin, px)
        xMax = math.max(xMax, px)
        yMin = math.min(yMin, py)
        yMax = math.max(yMax, py)
    end
    local cx = (xMin + xMax) * 0.5
    local cy = (yMin + yMax) * 0.5
    return verts, cx, cy
end

-- Draw a filled polygon with a given fill + stroke
local function FillPolygon(poly, ox, oy, r, g, b, a)
    local verts, cx, cy = PolygonVertices(poly)
    local p = display.newPolygon(ox, oy, verts)
    p:setFillColor(r, g, b, a or 0.4)
    -- p.strokeWidth = 1
    -- p:setStrokeColor(0, 0, 0, 0.6)
    -- print(p.x, p.y, p.width, p.height, ox, oy, "FillPolygon")
    p:translate(cx, cy)
    return p
end

-- Draw the outline of a polygon
local function OutlinePolygon(poly, ox, oy, r, g, b)
    local verts = PolygonVertices(poly)
    -- display.newLine uses absolute coords: bake the offset in
    for i = 1, #verts, 2 do
        verts[i] = verts[i] + (ox or 0)
        verts[i + 1] = verts[i + 1] + (oy or 0)
    end
    -- close the loop
    verts[#verts + 1] = verts[1]
    verts[#verts + 1] = verts[2]
    local line = display.newLine(unpack(verts))
    line:setStrokeColor(r or 0, g or 0, b or 0)
    line.strokeWidth = 2
    return line
end

-- Draw a set of result polygons (triangles, convex parts) with varying colors
local COLORS = {
    { 1, 0.3, 0.3 }, { 0.3, 1, 0.3 }, { 0.3, 0.3, 1 },
    { 1, 1, 0.3 }, { 1, 0.3, 1 }, { 0.3, 1, 1 },
    { 1, 0.6, 0.3 }, { 0.6, 0.3, 1 }, { 0.3, 1, 0.6 },
    { 0.8, 0.8, 0.3 }, { 0.3, 0.8, 0.8 }, { 0.8, 0.3, 0.8 },
}
local function DrawResult(result, ox, oy, label)
    local group = display.newGroup()
    local cx, cy = ox or 0, oy or 0

    if label then
        local t = display.newText({
            text = label, x = cx, y = cy - 10,
            fontSize = 10, parent = group,
        })
        t:setFillColor(1, 1, 1)
        t:translate(t.contentWidth * 0.5, 0)
    end

    for i = 1, #result do
        local ci = ((i - 1) % #COLORS) + 1
        local c = COLORS[ci]
        FillPolygon(result[i], cx, cy, c[1], c[2], c[3], 0.5)
    end
    return group
end

-- Print a result summary
local function PrintResult(name, count)
    print(("%-22s → %d polygons"):format(name, count))
end

-- Render a fringe mesh with the per-vertex AA alpha baked in via
-- mesh:setFillVertexColor (the plugin precomputes `alphas` — the
-- NanoVG AA gradient — no custom shader needed)
local function FringeMesh(data, ox, oy, r, g, b)
    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    for i = 1, mesh.fillVertexCount do
        mesh:setFillVertexColor(i, r, g, b, data.alphas[i])
    end
    return mesh
end

-- -------------------------------------------------------------------
-- Test polygons
-- -------------------------------------------------------------------

-- Simple convex quadrilateral (CCW)
local square = { 0,0,  80,0,  80,80,  0,80 }

-- A concave "arrowhead" polygon (CCW)
local arrowhead = { 0,0,  80,0,  80,40,  40,20,  0,80 }

-- A more complex concave "L-shape" (CCW)
local lshape = { 0,0,  100,0,  100,40,  40,40,  40,100,  0,100 }

-- A star-like complex polygon (CCW)
local star = {
    52.5, 0, 67.5, 37.5, 105, 37.5, 75, 60, 90, 97.5, 52.5, 75, 15, 97.5, 30,
    60, 0, 37.5, 37.5, 37.5
}

-- -------------------------------------------------------------------
-- Layout helpers
-- -------------------------------------------------------------------
local OFFSET_Y = 50
local MARGIN = 20
local COL_W = 120
local ROW_H = 130

local function Pos(col, row)
    return MARGIN + col * COL_W, MARGIN + row * ROW_H + OFFSET_Y
end

-- ===================================================================
-- RUN TESTS
-- ===================================================================

-- Title
local title = display.newText({
    text = "geometry2d.polypartition", x = CX, y = 20,
    fontSize = 16,
})
title:setFillColor(1, 1, 1)

-------------------------------------------------------------------
-- Test 1: Triangulate_EC — square
-------------------------------------------------------------------
do
    local ox, oy = Pos(0, 0)
    local result = PP.triangulate_EC(square)
    PrintResult("Triangulate_EC(square)", #result)
    OutlinePolygon(square, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_EC(square)")
end

-- -------------------------------------------------------------------
-- Test 2: Triangulate_EC — arrowhead
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(1, 0)
    local result = PP.triangulate_EC(arrowhead)
    PrintResult("Triangulate_EC(arrow)", #result)
    OutlinePolygon(arrowhead, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_EC(arrow)")
end

-- -------------------------------------------------------------------
-- Test 3: Triangulate_MONO — L-shape
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2, 0)
    local result = PP.triangulate_MONO(lshape)
    PrintResult("Triangulate_MONO(L)", #result)
    OutlinePolygon(lshape, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_MONO")
end

-- -------------------------------------------------------------------
-- Test 4: Triangulate_EC — star
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 0)
    local result = PP.triangulate_EC(star)
    PrintResult("Triangulate_EC(star)", #result)
    OutlinePolygon(star, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_EC ★")
end

-- -------------------------------------------------------------------
-- Test 5: Triangulate_EC with holes (polygon list)
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(0, 1)

    -- Outer square (CCW) with a square hole (CW)
    local outer = { 0,0,  100,0,  100,100,  0,100 }
    local hole  = { 30,30,  30,70,  70,70,  70,30 }  -- CW for hole (required by RemoveHoles)

    local polyList = {
        { points = outer, hole = false },
        { points = hole,  hole = true },
    }

    local result = PP.triangulate_EC(polyList)
    PrintResult("Triangulate_EC(holey)", #result)
    OutlinePolygon(outer, ox, oy, 1, 1, 1)
    OutlinePolygon(hole,  ox, oy, 1, 0.3, 0.3)
    DrawResult(result, ox, oy, "Triangulate_EC w/ hole")
end

-- -------------------------------------------------------------------
-- Test 6: ConvexPartition_HM — L-shape
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(1, 1)
    local result = PP.convexPartition_HM(lshape)
    PrintResult("ConvexPartition_HM(L)", #result)
    OutlinePolygon(lshape, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "ConvexPartition_HM")
end

-- -------------------------------------------------------------------
-- Test 7: ConvexPartition_HM — star
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2, 1)
    local result = PP.convexPartition_HM(star)
    PrintResult("ConvexPartition_HM(★)", #result)
    OutlinePolygon(star, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "ConvexPartition_HM ★")
end

-- -------------------------------------------------------------------
-- Test 8: RemoveHoles — polygon list with hole
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 1)

    local outer = { 0,0,  100,0,  100,100,  0,100 }
    local hole  = { 30,30,  30,70,  70,70,  70,30 }

    local polyList = {
        { points = outer, hole = false },
        { points = hole,  hole = true },
        { points = { 10,10, 10,20, 20,20, 20,10 },  hole = true },
    }

    local noHoles = PP.removeHoles(polyList)
    PrintResult("RemoveHoles", #noHoles)
    -- Draw original outlines
    OutlinePolygon(outer, ox, oy, 1, 1, 1)
    OutlinePolygon(hole,  ox, oy, 1, 0.3, 0.3)
    -- Draw the result (merged polygon without holes)
    for i = 1, #noHoles do
        OutlinePolygon(noHoles[i], ox, oy, 0.3, 1, 0.3)
    end

    local result = PP.convexPartition_HM(polyList, {maxVertices = 8})
    DrawResult(result, ox, oy, "RemoveHoles")

    -- Label
    local t = display.newText({
        text = "RemoveHoles", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 9: Bytes input — square via packed doubles
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(0, 2)

    local pentagon = {0,0, 80,0, 80,60, 40,40, 0,60}
    local result = PP.triangulate_EC(pentagon)
    PrintResult("Triangulate_EC(pentagon)", #result)
    OutlinePolygon(pentagon, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_EC ⬠")
end

-- -------------------------------------------------------------------
-- Test 10: Triangulate_OPT (optimal) on a small polygon
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(1, 2)
    -- A small hexagon for OPT (O(n³) — keep it small)
    local hexagon = { 0,0,  50,-10,  80,20,  60,60,  20,70,  -10,40 }
    local result = PP.triangulate_OPT(hexagon)
    PrintResult("Triangulate_OPT(hex)", #result)
    OutlinePolygon(hexagon, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "Triangulate_OPT ⬡")
end

-- -------------------------------------------------------------------
-- Test 11: ConvexPartition_OPT on L-shape
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2, 2)
    local result = PP.convexPartition_OPT(lshape)
    PrintResult("ConvexPartition_OPT(L)", #result)
    OutlinePolygon(lshape, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "ConvexPartition_OPT")
end

-- -------------------------------------------------------------------
-- Test 12: MonotonePartition on L-shape
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 2)
    local lCopy = { 0,0,  100,0,  100,40,  40,40,  40,100,  0,100 }
    local result = PP.monotonePartition({ lCopy })
    PrintResult("MonotonePartition(L)", #result)
    OutlinePolygon(lCopy, ox, oy, 1, 1, 1)
    DrawResult(result, ox, oy, "MonotonePartition")
end

-- Expected geometry failures return nil + message, without requiring pcall.
do
    local function expectGeometryFailure(label, fn, ...)
        local ok, result, err = pcall(fn, ...)
        assert(ok, label .. " unexpectedly raised: " .. tostring(result))
        assert(result == nil and type(err) == "string" and #err > 0,
            label .. " did not return nil + message")
    end

    expectGeometryFailure("polypartition", PP.triangulate_EC,
        {0,0, 10,0, 20,0, 30,0})
    expectGeometryFailure("earcut", Geometry2D.earcut.triangulate,
        {0,0, 10,0, 20,0})
    expectGeometryFailure("fringe", Geometry2D.fringe.fill,
        {0,0, 10,0, 0,10}, {fringe = 0})
    expectGeometryFailure("util", Geometry2D.util.meshFill,
        {0,0, 10,0, 20,0})
    expectGeometryFailure("path fill", Geometry2D.path.meshFill, {
        {"M", 0, 0}, {"L", 0, 10}, {"L", 10, 0}, {"Z"},
    })
    expectGeometryFailure("path stroke", Geometry2D.path.meshStroke,
        {{"M", 0, 0}}, 5)
    expectGeometryFailure("path point limit", Geometry2D.path.flatten, {
        {"M", 0, 0}, {"L", 10, 0}, {"L", 20, 0}, {"L", 30, 0},
    }, {maxCurvePoints = 3})

    local newMesh = display.newMesh
    display.newMesh = function() error("forced display.newMesh failure", 0) end
    expectGeometryFailure("direct mesh", Geometry2D.util.meshFill,
        {0,0, 10,0, 0,10}, {output = "mesh"})
    display.newMesh = newMesh
    print("Geometry failure result contract: ok")
end

-- -------------------------------------------------------------------
-- Test 13: earcut — single polygon with hole (native hole support)
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(4, 2)

    -- Outer square (CCW) + triangular hole: passes directly as {poly=..., holes={...}}
    local result = Geometry2D.earcut.triangulate({
        poly  = { 0,0,  100,0,  100,100,  0,100 },
        holes = {
            { 30,30,  70,30,  70,70,  30,70 },
        },
    })
    PrintResult("earcut(holey)", #result)
    OutlinePolygon({0,0, 100,0, 100,100, 0,100}, ox, oy, 1, 1, 1)
    OutlinePolygon({30,30, 70,30, 70,70, 30,70}, ox, oy, 1, 0.3, 0.3)
    DrawResult(result, ox, oy, "earcut w/ hole")
end

-- -------------------------------------------------------------------
-- Test 14: earcut — mesh mode (vertices + 1-based indices for display.newMesh)
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(5, 2)

    local meshData = Geometry2D.earcut.triangulate({
        { 0,0,  100,0,  100,100,  0,100 },
        { 30,30,  70,30,  70,70,  30,70 },
    }, { result = "indexed" })

    PrintResult("earcut(mesh holey)", #meshData.indices / 3)
    OutlinePolygon({0,0, 100,0, 100,100, 0,100}, ox, oy, 1, 1, 1)
    OutlinePolygon({30,30, 70,30, 70,70, 30,70}, ox, oy, 1, 0.3, 0.3)

    local mesh = display.newMesh(meshData)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    mesh:setFillColor(0.3, 0.8, 1, 0.5)
    mesh.strokeWidth = 1
    mesh:setStrokeColor(0, 0, 0, 0.6)

    local t = display.newText({
        text = "earcut mesh", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)

    local packedTriangle = string.char(
        0,0,0,0, 0,0,0,0,
        0,0,200,66, 0,0,0,0,
        0,0,0,0, 0,0,200,66
    )
    local packedResult = Geometry2D.earcut.triangulate({
        bytes = packedTriangle,
        type = "float32",
    })
    assert(#packedResult == 1)
    local triangleBuffers = Geometry2D.util.meshFill(
        {0,0, 10,0, 0,10}, {fringe = 0, output = "buffers"})
    local memoryResult = Geometry2D.earcut.triangulate({
        bytes = triangleBuffers.vertices.buffer,
        type = "float32",
    })
    assert(#memoryResult == 1)
    local ok, err = pcall(Geometry2D.earcut.triangulate, packedTriangle)
    assert(not ok and err:find("Expected polygon", 1, true))
    ok, err = pcall(Geometry2D.earcut.triangulate, {0,0, 10,0, 0,10}, {mesh = true})
    assert(not ok and err:find("result='indexed'", 1, true))
end

--[[
--- Test: ConvexPartition_HM Complex
do
    local ox, oy = Pos(0, 3)
    local polyList = {
        {
            hole = false,
            points = {
                347, 180, 216, 187, 219, 241, 199, 190, 140, 192, 148, 227, 134,
                193, 70, 197, 94, 304, 62, 241, 10, 201, -95, 208, -119, 243, -115,
                340, -212, 215, -364, 225, -275, 133, -392, -16, -316, -30, -439,
                -145, -220, -79, -218, -171, -132, -118, -133, -139, -117, -108,
                -75, -83, -121, -190, -217, -202, -213, -337, 102, -254, 186, -341,
                245, -217, 413, -173, 278, -146, 282, -137, 439, -117, 325, -64,
                355, -44, 331, -34, 398, 106, 209, 49, 211, 95
            }
        },
        {hole = true, points = { -124, 96, -128, -16, -189, 45 }},
        -- {hole = true, points = {-161, -61, -210, -79, -221, -49}},
        -- {hole = true, points = {-168, 212, -136, 267, -110, 243, -120, 209}}
    }

    -- local result = PP.convexPartition_HM(polyList)
    local result = PP.convexPartition_HM(polyList, {maxVertices = 8})
    PrintResult("ConvexPartition_HM Complex", #result)
    DrawResult(result, CX, CY, "ConvexPartition_HM Complex")
end

do
    local ox, oy = Pos(5, 2)

    local meshData = Geometry2D.earcut.triangulate({
        poly = {
            347, 180, 216, 187, 219, 241, 199, 190, 140, 192, 148, 227, 134,
            193, 70, 197, 94, 304, 62, 241, 10, 201, -95, 208, -119, 243, -115,
            340, -212, 215, -364, 225, -275, 133, -392, -16, -316, -30, -439,
            -145, -220, -79, -218, -171, -132, -118, -133, -139, -117, -108,
            -75, -83, -121, -190, -217, -202, -213, -337, 102, -254, 186, -341,
            245, -217, 413, -173, 278, -146, 282, -137, 439, -117, 325, -64,
            355, -44, 331, -34, 398, 106, 209, 49, 211, 95
        },
        holes = {
            {-161, -61, -220, -79, -221, -49},
            {-168, 212, -136, 267, -119, 243, -120, 209}
        }
    }, { result = "indexed" })

    meshData.mode = "indexed"
    local mesh = display.newMesh(meshData)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    mesh:setFillColor(0.3, 0.8, 1, 0.5)
    -- mesh.strokeWidth = 1
    -- mesh:setStrokeColor(0, 0, 0, 0.6)

    local t = display.newText({
        text = "earcut mesh", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end
--]]

-- -------------------------------------------------------------------
-- Test 16: util.meshFill — earcut body + fringe AA skirt in ONE mesh
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(0, 3)

    local outer = { 0,0,  100,0,  100,100,  0,100 }
    local hole  = { 30,30,  30,70,  70,70,  70,30 }  -- CCW on screen (convention hole)

    -- One call: solid earcut fill + AA fringe skirt, merged into a single
    -- mesh. data.alphas = 1 on the body, fading 1→0 across the skirt.
    -- local data = Geometry2D.fringe.fill({
    local data = Geometry2D.util.meshFill({
        outer,
        hole,
    }, { fringe = 3.0, join = "miter", miterLimit = 2.4 })

    PrintResult("util.meshFill(holey)", #data.indices / 3)
    local mesh = FringeMesh(data, ox, oy, 1, 1, 1)
    mesh.rotation = 20

    -- OutlinePolygon(outer, ox, oy, 1, 1, 1)
    -- OutlinePolygon(hole,  ox, oy, 1, 0.3, 0.3)

    local t = display.newText({
        text = "util.meshFill (body+skirt)", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 17: fringe.stroke — open polyline, round caps/joins
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(1, 3)

    local polyline = { 0,0,  30,40,  70,10,  100,60 }
    local skirt = Geometry2D.fringe.stroke(polyline, 10.0, {
        fringe = 1.0, cap = "round", join = "round",
    })

    -- _DUMP("fringe.stroke(open)", skirt)
    PrintResult("fringe.stroke(open)", #skirt.indices / 3)
    FringeMesh(skirt, ox, oy, 0.3, 0.8, 1)

    -- draw the source polyline on top for reference
    local verts = {}
    for i = 1, #polyline, 2 do
        verts[#verts + 1] = polyline[i] + ox
        verts[#verts + 1] = polyline[i + 1] + oy
    end
    local line = display.newLine(unpack(verts))
    line:setStrokeColor(1, 0, 0)
    line.strokeWidth = 1

    local t = display.newText({
        text = "fringe.stroke round", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 18: fringe.stroke — closed polygon, miter joins, butt caps
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2, 3)

    local star = { 52.5, 0, 67.5, 37.5, 105, 37.5, 75, 60, 90, 97.5, 52.5, 75, 15, 97.5, 30,
        60, 0, 37.5, 37.5, 37.5 }
    local skirt = Geometry2D.fringe.stroke(star, 3.0, {
        fringe = 1.0, closed = true, join = "miter", miterLimit = 4.0,
    })

    -- _DUMP("fringe.stroke(closed)", skirt)
    PrintResult("fringe.stroke(closed)", #skirt.indices / 3)
    FringeMesh(skirt, ox, oy, 1, 0.6, 0.3)

    -- OutlinePolygon(star, ox, oy, 1, 1, 1)

    local t = display.newText({
        text = "fringe.stroke closed", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 19: fringe.fill — bevel + round joins on a concave polygon
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 3)

    local lshape = { 0,0,  100,0,  100,40,  40,40,  40,100,  0,100 }
    local skirt = Geometry2D.fringe.fill(lshape, { fringe = 15, join = "round" })

    PrintResult("fringe.fill(round)", #skirt.indices / 3)
    FringeMesh(skirt, ox, oy, 1, 1, 1)

    -- OutlinePolygon(lshape, ox, oy, 1, 1, 1)

    local t = display.newText({
        text = "fringe.fill round", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 20: util.meshFill — star with a hole, round joins, Delaunay refine
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(0, 4)

    local data = Geometry2D.util.meshFill({
        { 52.5,0, 67.5,37.5, 105,37.5, 75,60, 90,97.5, 52.5,75, 15,97.5, 30,60, 0,37.5, 37.5,37.5 },
        { 40,35, 40,60, 65,60, 65,35 },  -- CCW on screen (convention hole)
    }, { fringe = 1.0, join = "round", refine = false })

    PrintResult("util.meshFill(star)", #data.indices / 3)
    local mesh = FringeMesh(data, ox, oy, 0.3, 0.9, 0.4)
    mesh.rotation = 30

    -- OutlinePolygon({ 52.5,0, 67.5,37.5, 105,37.5, 75,60, 90,97.5, 52.5,75, 15,97.5, 30,60, 0,37.5, 37.5,37.5 }, ox, oy, 1, 1, 1)

    local t = display.newText({
        text = "util.meshFill ★ hole", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 21: util.meshFillGroups — several shape groups merged into ONE mesh
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(1, 4)

    local data = Geometry2D.util.meshFillGroups({
        { -- group 1: square with a hole
            { 0,0,  80,0,  80,80,  0,80 },
            { 20,20,  20,60,  60,60,  60,20 },   -- CCW on screen (hole)
        },
        { -- group 2: a triangle (flat table works too)
            90,0,  130,0,  110,40,
        },
        { -- group 3: a pentagon
            { 0,90,  40,90,  50,110,  20,130,  -10,110 },
        },
    }, { fringe = 5.0, join = "round" })

    PrintResult("util.meshFillGroups(3)", #data.indices / 3)
    FringeMesh(data, ox, oy, 0.9, 0.6, 0.2)

    local t = display.newText({
        text = "util.meshFillGroups", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 22: util.meshSDF — signed-distance mesh for shader-based AA
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2.2, 4)
    local DISTANCE = 5

    local data = Geometry2D.util.meshSDF({
        { 0,0,  80,0,  80,80,  0,80 },
        { 20,20,  20,60,  60,60,  60,20 },   -- CCW on screen (hole)
    }, { distance = DISTANCE, join = "round" })

    -- distance stats: 0 on the boundary and across the body (the AA ramp
    -- clamps to alpha 1 for d >= 0), down to -distance at the band's outer edge
    local dMin, dMax = math.huge, -math.huge
    for i = 1, #data.distances do
        dMin = math.min(dMin, data.distances[i])
        dMax = math.max(dMax, data.distances[i])
    end
    PrintResult("util.meshSDF", #data.indices / 3)
    print(("  distances: [%.1f .. %.1f]"):format(dMin, dMax))

    -- Fallback rendering without a shader: linear vertex-alpha fade across
    -- the band. The shader version replaces this with an exact 1px fwidth()
    -- smoothstep ramp: alpha = 1 - smoothstep(-fwidth(d), 0, d).
    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    for i = 1, mesh.fillVertexCount do
        mesh:setFillVertexColor(i, 0.9, 0.5, 0.9, 1 + data.distances[i] / DISTANCE)
    end

    local t = display.newText({
        text = "util.meshSDF", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 23: util.meshSDFGroups — several SDF shape groups in ONE mesh
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 4)
    local DISTANCE = 5

    local data = Geometry2D.util.meshSDFGroups({
        { -- group 1: square with a hole
            { 0,0,  80,0,  80,80,  0,80 },
            { 20,20,  20,60,  60,60,  60,20 },   -- CCW on screen (hole)
        },
        { -- group 2: a triangle
            90,0,  130,0,  110,40,
        },
        { -- group 3: a pentagon
            { 0,90,  40,90,  50,110,  20,130,  -10,110 },
        },
    }, { distance = DISTANCE, join = "round", mode = "triangles" })

    local dMin, dMax = math.huge, -math.huge
    for i = 1, #data.distances do
        dMin = math.min(dMin, data.distances[i])
        dMax = math.max(dMax, data.distances[i])
    end
    PrintResult("util.meshSDFGroups(3)", #data.vertices / 6)
    print(("  distances: [%.1f .. %.1f]"):format(dMin, dMax))

    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    for i = 1, mesh.fillVertexCount do
        mesh:setFillVertexColor(i, 0.4, 0.7, 0.9, 1 + data.distances[i] / DISTANCE)
    end

    local t = display.newText({
        text = "util.meshSDFGroups", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 24: util.meshFill with mode="triangles" — raw triangle list
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(4.2, 4)

    local data = Geometry2D.util.meshFill({
        { 0,0,  80,0,  80,80,  0,80 },
        { 20,20,  20,60,  60,60,  60,20 },
    }, { fringe = 5.0, mode = "triangles", join = "round" })

    PrintResult("util.meshFill(triangles)", #data.vertices / 6)
    print(("  mode=%s, indices=%s, #vertices=%d"):format(
        data.mode, data.indices and "present" or "absent", #data.vertices / 2))

    FringeMesh(data, ox, oy, 0.8, 0.3, 0.3)

    local t = display.newText({
        text = "util.meshFill triangles", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 25: Bezier path flattening + table/buffer/direct-mesh outputs
-- -------------------------------------------------------------------
do
    local bezier = {
        {"M", 0, 0},
        {"L", 80, 0},
        {"Q", 120, 40, 80, 80},
        {"C", 55, 105, 25, 105, 0, 80},
        {"Z"},
    }

    local contours = Geometry2D.path.flatten(bezier, {tessTol = 0.25})
    assert(#contours == 1 and contours[1].closed)
    assert(#contours[1].points > 8)
    local coarse = Geometry2D.path.flatten(bezier, {tessTol = 4})
    local fine = Geometry2D.path.flatten(bezier, {tessTol = 0.05})
    assert(#fine[1].points > #coarse[1].points)

    local holeFill = Geometry2D.path.meshFill({
        {"M", 0, 0}, {"L", 100, 0}, {"L", 100, 100}, {"L", 0, 100}, {"Z"},
        {"M", 30, 30}, {"L", 30, 70}, {"L", 70, 70}, {"L", 70, 30}, {"Z"},
    })
    assert(#holeFill.indices > 0 and #holeFill.alphas == #holeFill.vertices / 2)

    local tableData = Geometry2D.path.meshSDF(bezier, {
        distance = 5,
        distanceSign = "outsidePositive",
        mode = "triangles",
    })
    assert(tableData.indices == nil and #tableData.vertices % 6 == 0)
    for i = 1, #tableData.distances do
        assert(tableData.distances[i] >= 0)
    end

    local bufferData = Geometry2D.path.meshSDF(bezier, {
        distance = 5,
        output = "buffers",
        distanceSign = "outsidePositive",
    })
    assert(bufferData.vertices.buffer and bufferData.distances.buffer)
    assert(bufferData.uvs == nil)
    assert(bufferData.vertices.count == bufferData.distances.count)
    local bufferMesh = display.newMesh(bufferData)
    assert(bufferMesh.fillVertexCount == bufferData.vertices.count)

    local legacyBufferData = Geometry2D.path.meshSDF(bezier, {
        output = "buffers",
        legacyUVs = true,
    })
    assert(legacyBufferData.uvs.buffer)

    local ok, err = pcall(Geometry2D.path.meshStroke, bezier, 8, {joint = "round"})
    assert(not ok and err:find("use 'join'", 1, true))
    ok, err = pcall(Geometry2D.path.meshSDF, bezier, {legacyUVs = true})
    assert(not ok and err:find("requires output", 1, true))
    ok, err = pcall(Geometry2D.path.flatten, {{"m", 0, 0}})
    assert(not ok and err:find("Relative path command", 1, true))

    graphics.defineVertexExtension({
        name = "Geometry2DExampleData",
        {name = "geom", type = "float", componentCount = 4},
    })
    bufferMesh.fillExtension = "Geometry2DExampleData"
    local frames = 0
    local function writePackedDistances()
        frames = frames + 1
        if frames < 2 then return end
        Runtime:removeEventListener("enterFrame", writePackedDistances)
        bufferMesh.fillExtendedData:setAttributeValues("geom", bufferData.distances)
        bufferMesh:removeSelf()
        print("Packed distance bulk update after geometry creation: ok")
    end
    Runtime:addEventListener("enterFrame", writePackedDistances)

    local directMesh, attributes = Geometry2D.path.meshSDF(bezier, {
        distance = 5,
        output = "mesh",
        distanceSign = "outsidePositive",
    })
    assert(directMesh.fillVertexCount == attributes.vertexCount)
    assert(attributes.distances.componentCount == 1)
    directMesh:removeSelf()

    local strokeData = Geometry2D.path.meshStroke({
        {"M", 0, 0}, {"C", 20, -30, 80, 30, 100, 0},
    }, 8, {output = "buffers", cap = "round", join = "round"})
    assert(strokeData.vertices.count == strokeData.alphas.count)
    local strokeMesh = display.newMesh(strokeData)
    strokeMesh:removeSelf()

    local straightPath = {{"M", 0, 0}, {"L", 100, 0}}
    local solidStroke = Geometry2D.path.meshStroke(straightPath, 8, {
        fringe = 0, cap = "butt", mode = "triangles",
    })
    local dashedStroke = Geometry2D.path.meshStroke(straightPath, 8, {
        fringe = 0, cap = "butt", mode = "triangles",
        dashPattern = {10, 10},
    })
    assert(#solidStroke.alphas == 18)
    assert(#dashedStroke.alphas == #solidStroke.alphas * 5)

    local oddOffsetStroke = Geometry2D.path.meshStroke(bezier, 8, {
        mode = "triangles", cap = "round",
        dashPattern = {12, 5, 3}, dashOffset = -7,
    })
    assert(#oddOffsetStroke.alphas > 0)

    local closedDash = Geometry2D.path.meshStroke({
        {"M", 0, 0}, {"L", 100, 0}, {"L", 100, 100},
        {"L", 0, 100}, {"Z"},
    }, 8, {
        mode = "triangles", cap = "round", join = "round",
        dashPattern = {250, 50},
    })
    assert(#closedDash.alphas > 0)

    local limited, limitError = Geometry2D.path.meshStroke(straightPath, 8, {
        dashPattern = {10, 10}, maxDashSegments = 2,
    })
    assert(limited == nil and limitError:find("maxDashSegments", 1, true))
    ok, err = pcall(Geometry2D.path.meshStroke, straightPath, 8, {
        dashPattern = {10, 0},
    })
    assert(not ok and err:find("positive finite number", 1, true))

    print(("Dashed stroke mesh: %d vertices for five butt-cap dashes"):format(
        #dashedStroke.alphas))
    print("Bezier/table/buffers/mesh output: ok")
end

-- -------------------------------------------------------------------
-- Footer
-- -------------------------------------------------------------------
local instructions = display.newText({
    text = "All tests executed — check console for results",
    x = CX, y = H - 30, fontSize = 12,
})
instructions:setFillColor(0.7, 0.7, 0.7)

print("=============================================")
print(" geometry2d.polypartition + earcut — tests  ")
print("=============================================")
