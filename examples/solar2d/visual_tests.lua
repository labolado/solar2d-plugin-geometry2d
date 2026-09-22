-- geometry2d visual display tests.
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

-- Render a fringe mesh with the plugin's AA alpha baked into standard
-- Solar2D fill vertex colors (packed for buffer output, Lua values otherwise).
local function FringeMesh(data, ox, oy, r, g, b)
    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    if data.fillVertexColors then
        mesh:setFillColor(r, g, b)
    else
        for i = 1, mesh.fillVertexCount do
            mesh:setFillVertexColor(i, r, g, b, data.alphas[i])
        end
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
    }, { aaWidth = 3.0, join = "miter", miterLimit = 2.4 })

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
    }, { aaWidth = 1.0, join = "round", refine = false })

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
    }, { aaWidth = 5.0, join = "round" })

    PrintResult("util.meshFillGroups(3)", #data.indices / 3)
    FringeMesh(data, ox, oy, 0.9, 0.6, 0.2)

    local t = display.newText({
        text = "util.meshFillGroups", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 22: util.meshDistance — signed-distance mesh for shader-based AA
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(2.2, 4)
    local DISTANCE = 5

    local data = Geometry2D.util.meshDistance({
        { 0,0,  80,0,  80,40,  60,40,  60,80,  0,80 }, -- reflex outer corner
        { 20,20,  20,60,  40,60,  40,20 },   -- CCW on screen (hole)
    }, {method="partition", innerRange = DISTANCE, outerRange = 2, output = "buffers" })

    PrintResult("util.meshDistance v2", data.stats.triangles)
    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    local shader = require("sdf_shader")
    shader.attach(mesh)
    local frames = 0
    local function initialize()
        frames = frames + 1
        if frames < 2 then return end
        Runtime:removeEventListener("enterFrame", initialize)
        shader.write(mesh, data, 3)
    end
    Runtime:addEventListener("enterFrame", initialize)

    local t = display.newText({
        text = "util.meshDistance / reflex + hole", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 23: util.meshDistanceGroups — several SDF shape groups in ONE mesh
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(3, 4)
    local DISTANCE = 5

    local data = Geometry2D.util.meshDistanceGroups({
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
    }, {method="partition", innerRange = DISTANCE, outerRange = 2, mode = "triangles", output = "buffers" })

    PrintResult("util.meshDistanceGroups(3)", data.stats.triangles)

    local mesh = display.newMesh(data)
    mesh.x, mesh.y = ox, oy
    mesh:translate(mesh.path:getVertexOffset())
    local shader = require("sdf_shader")
    shader.attach(mesh)
    local frames = 0
    local function initialize()
        frames = frames + 1
        if frames < 2 then return end
        Runtime:removeEventListener("enterFrame", initialize)
        shader.write(mesh, data, 2)
    end
    Runtime:addEventListener("enterFrame", initialize)

    local t = display.newText({
        text = "util.meshDistanceGroups", x = ox + 50, y = oy - 10,
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
    }, { aaWidth = 5.0, mode = "triangles", join = "round" })

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
-- Test 25: path.meshStroke — solid and dashed Bezier strokes
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(5.4, 4)
    local path = {
        {"M", 0, 20},
        {"C", 25, -15, 75, 55, 100, 20},
    }

    local solid = Geometry2D.path.meshStroke(path, 8, {
        aaWidth = 2,
        cap = "round",
        join = "round",
    })
    PrintResult("path.meshStroke(solid)", #solid.indices / 3)
    FringeMesh(solid, ox, oy, 0.3, 0.9, 1.0)

    local dashed = Geometry2D.path.meshStroke(path, 2, {
        aaWidth = 1,
        cap = "round",
        join = "round",
        dashPattern = {12, 12},
        dashOffset = -4,
    })
    PrintResult("path.meshStroke(dash)", #dashed.indices / 3)
    FringeMesh(dashed, ox, oy + 55, 1.0, 0.55, 0.2)

    local t = display.newText({
        text = "path.meshStroke solid / dash", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 26: path.meshFill — Clipper2 self-intersection resolution
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(6, 2)
    local bowTie = {
        {"M", 0, 0}, {"L", 90, 80}, {"L", 0, 80},
        {"L", 90, 0}, {"Z"},
    }
    local resolved, err = Geometry2D.path.meshFill(bowTie, {
        intersections = "resolve",
        fillRule = "evenOdd",
        aaWidth = 2,
        join = "round",
    })
    assert(resolved, err)
    FringeMesh(resolved, ox, oy, 0.35, 0.9, 0.7)

    local t = display.newText({
        text = "Clipper2 / resolved bow-tie", x = ox + 45, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Test 27: path.meshStroke — dashed five-point star
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(6.5, 4)
    local bezierPath = {
        {"M", 0, 20},
        {"C", 25, -15, 75, 55, 100, 20},
    }
    local starPath = {
        {"M", 50.00 * 2,  0.00 * 2},
        {"L", 60.58 * 2, 25.44 * 2},
        {"L", 88.04 * 2, 27.64 * 2},
        {"L", 67.12 * 2, 45.56 * 2},
        {"L", 73.51 * 2, 72.36 * 2},
        {"L", 50.00 * 2, 58.00 * 2},
        {"L", 26.49 * 2, 72.36 * 2},
        {"L", 32.88 * 2, 45.56 * 2},
        {"L", 11.96 * 2, 27.64 * 2},
        {"L", 39.42 * 2, 25.44 * 2},
        {"Z"},
    }

    local dashed = Geometry2D.path.meshStroke(starPath, 1, {
        aaWidth = 1,
        cap = "butt",
        join = "miter",
        dashPattern = {10, 10},
        dashOffset = -2,
        mode = "triangles",
        output = "buffers"
    })
    PrintResult("path.meshStroke(star dash)", dashed.vertices.count / 3)
    FringeMesh(dashed, ox, oy, 1, 1, 0.2)

    local t = display.newText({
        text = "meshStroke / dashed star", x = ox + 50, y = oy - 10,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- -------------------------------------------------------------------
-- Footer
-- -------------------------------------------------------------------
do
    local ox, oy = Pos(6, 0)
    local shape = Geometry2D.path.newShape()
    shape:moveTo(0, 45)
        :cubicTo(25, -15, 85, 105, 115, 45)
        :fill(false)
        :strokeWidth(3)
        :strokeFill(0.35, 0.85, 1)
        :strokeCap("round")
        :strokeJoin("round")
        :strokeDash({14, 8}, -3)

    local view, err = shape:newView()
    assert(view, err)
    view.group.x, view.group.y = ox, oy

    local phase = 0
    timer.performWithDelay(120, function()
        if not view.group.removeSelf then return end
        phase = phase + 0.22
        shape:setCommand(2, "C", 25, -15 + math.sin(phase) * 12,
            85, 105 + math.sin(phase) * 12, 115, 45)
        local updated, updateError = shape:updateView(view)
        if not updated then error(updateError, 0) end
    end, 0)

    local t = display.newText({
        text = "retained Shape / mutable dashed curve", x = ox + 58, y = oy - 18,
        fontSize = 10,
    })
    t:setFillColor(1, 1, 1)
end

-- Small retained ribbon AA gallery; numerical coverage lives in
-- tests/ribbon_simulator/pixels.lua, including the same side/cap/join cases.
do
    graphics.defineEffect({category = "generator", group = "geometry2d", name = "ribbonGallery",
        fragment = [[
            P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
                P_UV float coverage = clamp(1.0 - max(uv.y, 0.0) / max(fwidth(uv.y), 0.00001), 0.0, 1.0);
                return CoronaColorScale(vec4(coverage));
            }
        ]],
    })
    for i, mode in ipairs({"indexed", "triangles"}) do
        local r = Geometry2D.ribbon.new({mode = mode, width = 5, aaWidth = 4, minDistance = 0, miterLimit = 1.5})
        r:addPoint(0, 0, 0); r:addPoint(35, 0, 1); r:addPoint(15, 20, 2)
        local v = assert(r:newView({effect = "generator.geometry2d.ribbonGallery"}))
        v.group.x, v.group.y = CX + (i - 1.5) * 90, H - 75
        v.mesh:setFillColor(0.3, 0.8, 1)
    end
end

-- Compare capability levels using the same original outline and material UVs.
do
    local shader=require('sdf_shader')
    for i,label in ipairs{'fill','vertex AA','distance outer','partition','local'} do
        local distance=i>=3
        local options=distance and {innerRange=i==3 and 0 or 8,method=i==5 and 'local' or 'partition',output='mesh'}
            or {aa=i==1 and 'none' or 'vertex',output='mesh'}
        local m,a=(distance and Geometry2D.util.meshDistance or Geometry2D.util.meshFill)(
            {-22,-22,22,-22,22,0,0,0,0,22,-22,22},options)
        assert(m,a);m.x=CX+(i-3.5)*90;m.y=H-155
        if not distance then m:setFillColor(.2,.6,1,.5)
        else
            shader.attach(m)
            local frames=0
            local function ready()
                frames=frames+1
                if frames<2 then return end
                Runtime:removeEventListener('enterFrame',ready)
                shader.write(m,a,i==3 and 0 or 3)
            end
            Runtime:addEventListener('enterFrame',ready)
        end
        display.newText{text=label,x=m.x,y=H-120,fontSize=10}
    end
end

local instructions = display.newText({
    text = "Visual tests — inspect the rendered geometry",
    x = CX, y = H - 30, fontSize = 12,
})
instructions:setFillColor(0.7, 0.7, 0.7)

print("=============================================")
print(" geometry2d visual display tests             ")
print("=============================================")
