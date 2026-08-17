--
-- geometry2d polypartition test
--
display.setStatusBar(display.HiddenStatusBar)

local Geometry2D = require("plugin.geometry2d")
local PP = Geometry2D.polypartition

local W, H   = display.contentWidth, display.contentHeight
local CX, CY = display.contentCenterX, display.contentCenterY

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
    }, { mesh = true })

    PrintResult("earcut(mesh holey)", #meshData.indices / 3)
    OutlinePolygon({0,0, 100,0, 100,100, 0,100}, ox, oy, 1, 1, 1)
    OutlinePolygon({30,30, 70,30, 70,70, 30,70}, ox, oy, 1, 0.3, 0.3)

    meshData.mode = "indexed"
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
    }, { mesh = true })

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
