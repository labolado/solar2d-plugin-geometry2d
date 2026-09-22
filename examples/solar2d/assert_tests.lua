-- geometry2d API contract and regression assertions.
--
-- These tests may create short-lived display meshes when an API can only be
-- exercised through Solar2D, but they do not form part of the visual gallery.

local Geometry2D = require("plugin.geometry2d")
local PP = Geometry2D.polypartition

local passed = 0

local function RunTest(name, fn)
    local ok, err = xpcall(fn, debug.traceback)
    assert(ok, ("assert test '%s' failed:\n%s"):format(name, tostring(err)))
    passed = passed + 1
    print(("[PASS] %s"):format(name))
end

local function ExpectRaisedMessage(messagePart, fn, ...)
    local ok, err = pcall(fn, ...)
    assert(not ok, "expected an error containing '" .. messagePart .. "'")
    assert(type(err) == "string" and err:find(messagePart, 1, true),
        "unexpected error: " .. tostring(err))
end

local function ExpectGeometryFailure(label, fn, ...)
    local ok, result, err = pcall(fn, ...)
    assert(ok, label .. " unexpectedly raised: " .. tostring(result))
    assert(result == nil and type(err) == "string" and #err > 0,
        label .. " did not return nil + message")
end

RunTest("geometry failure result contract", function()
    ExpectGeometryFailure("polypartition", PP.triangulate_EC,
        {0,0, 10,0, 20,0, 30,0})
    ExpectGeometryFailure("earcut", Geometry2D.earcut.triangulate,
        {0,0, 10,0, 20,0})
    ExpectGeometryFailure("fringe", Geometry2D.fringe.fill,
        {0,0, 10,0, 0,10}, {fringe=0})
    ExpectGeometryFailure("util", Geometry2D.util.meshFill,
        {0,0, 10,0, 20,0})
    ExpectGeometryFailure("path fill", Geometry2D.path.meshFill, {
        {"M", 0, 0}, {"L", 0, 10}, {"L", 10, 0}, {"Z"},
    })
    ExpectGeometryFailure("path stroke", Geometry2D.path.meshStroke,
        {{"M", 0, 0}}, 5)
    ExpectGeometryFailure("path point limit", Geometry2D.path.flatten, {
        {"M", 0, 0}, {"L", 10, 0}, {"L", 20, 0}, {"L", 30, 0},
    }, {maxCurvePoints = 3})

    local originalNewMesh = display.newMesh
    display.newMesh = function()
        error("forced display.newMesh failure", 0)
    end
    local ok, err = pcall(ExpectGeometryFailure, "direct mesh",
        Geometry2D.util.meshFill, {0,0, 10,0, 0,10}, {output = "mesh"})
    display.newMesh = originalNewMesh
    assert(ok, err)
    display.newMesh = function() return nil end
    ok, err = pcall(ExpectGeometryFailure, "direct SDF mesh returning nil",
        Geometry2D.util.meshDistance, {0,0,100,0,0,100}, {output="mesh"})
    display.newMesh = originalNewMesh
    assert(ok, err)
end)

RunTest("purpose-based mesh API", function()
    require('mesh_api_tests')(Geometry2D)
end)

RunTest("earcut approximate inner stroke", function()
    local poly={0,0,120,0,120,120,0,120}
    for _,mode in ipairs{'indexed','triangles'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local m,a=Geometry2D.util.meshDistance(poly,{method="local",mode=mode,output=output,innerRange=8,outerRange=2})
            assert(m,a)
            local data=output=='mesh' and a or m
            assert(data.approximate==true and data.method=='local' and data.kind=='distance')
            assert(data.alphas==nil and data.fillVertexColors==nil and data.aaWidth ==nil)
            assert(data.innerRange==8 and data.outerRange==2 and data.stats.work>0)
            if output=='table' then
                assert(#data.distances*2==#data.vertices)
                for _,d in ipairs(data.distances) do assert(d==-8 or d==0 or d==2) end
                assert(#data.vertices==(mode=='indexed' and 24 or 108))
            else
                assert(data.distances.buffer and data.distances.componentCount==1)
                assert(data.distances.count==(output=='mesh' and a.vertexCount or m.vertices.count))
            end
            if output=='mesh' then display.remove(m) end
        end
    end
    for _,field in ipairs{'join','fringe','distanceTransform','distanceTolerance','tessTol'} do
        ExpectRaisedMessage('',Geometry2D.util.meshDistance,poly,{method="local",[field]=field=='join' and 'round' or 1})
    end
    ExpectGeometryFailure('local core collapse',Geometry2D.util.meshDistance,poly,{method="local",innerRange=70})
    ExpectGeometryFailure('local miter cap',Geometry2D.util.meshDistance,poly,{method="local",miterLimit=1})
    ExpectGeometryFailure('local work cap',Geometry2D.util.meshDistance,poly,{method="local",maxWork=1})
    local p=assert(Geometry2D.path.meshDistance({{'M',0,0},{'L',120,0},{'L',120,120},{'L',0,120},{'Z'}},{method="local",tessTol=.2}))
    assert(p.approximate and p.distances)
end)

RunTest("fill reflex corner has single coverage", function()
    for _,range in ipairs({10, 40}) do
        local m = assert(Geometry2D.util.meshDistanceGroups({{poly={0,0,200,0,200,80,80,80,80,200,0,200},holes={}}},
            {method="partition",innerRange=range,outerRange=range,mode="triangles"}))
        local count=0
        for i=1,#m.vertices,6 do
            local v=m.vertices
            local ax,ay,bx,by,cx,cy=v[i],v[i+1],v[i+2],v[i+3],v[i+4],v[i+5]
            local det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
            if math.abs(det)>1e-7 then
                local u=((by-cy)*(83-cx)+(cx-bx)*(82-cy))/det
                local w=((cy-ay)*(83-cx)+(ax-cx)*(82-cy))/det
                if u>1e-6 and w>1e-6 and 1-u-w>1e-6 then count=count+1 end
            end
        end
        assert(count==1,"reflex corner overdraw")
    end
end)

RunTest("packed string and CoronaMemory polygon input", function()
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
        {0,0, 10,0, 0,10}, {aa="none", output = "buffers"})
    local memoryResult = Geometry2D.earcut.triangulate({
        bytes = triangleBuffers.vertices.buffer,
        type = "float32",
    })
    assert(#memoryResult == 1)

    ExpectRaisedMessage("Expected polygon",
        Geometry2D.earcut.triangulate, packedTriangle)
    ExpectRaisedMessage("result='indexed'",
        Geometry2D.earcut.triangulate, {0,0, 10,0, 0,10}, {mesh = true})
end)

local bezier = {
    {"M", 0, 0},
    {"L", 80, 0},
    {"Q", 120, 40, 80, 80},
    {"C", 55, 105, 25, 105, 0, 80},
    {"Z"},
}

RunTest("Bezier table, buffer, and direct-mesh output", function()
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
    assert(#holeFill.indices > 0)
    assert(#holeFill.alphas == #holeFill.vertices / 2)

    local tableData = Geometry2D.path.meshDistance(bezier, {method="partition",
        innerRange = 5,
        mode = "triangles",
    })
    assert(tableData.indices == nil and #tableData.vertices % 6 == 0)
    for i = 1, #tableData.distances do
        assert(tableData.distances[i] >= -5 and tableData.distances[i] <= 2)
    end

    local bufferData = Geometry2D.path.meshDistance(bezier, {method="partition",
        innerRange = 5,
        output = "buffers",
    })
    assert(bufferData.vertices.buffer and bufferData.distances.buffer)
    assert(bufferData.uvs.count == bufferData.vertices.count)
    assert(bufferData.vertices.count == bufferData.distances.count)
    local bufferMesh = display.newMesh(bufferData)
    assert(bufferMesh.fillVertexCount == bufferData.vertices.count)
    bufferMesh:removeSelf()

    ExpectRaisedMessage("use 'join'",
        Geometry2D.path.meshStroke, bezier, 8, {joint = "round"})
    ExpectRaisedMessage("does not accept option",
        Geometry2D.path.meshDistance, bezier, {legacyUVs = true})
    ExpectRaisedMessage("Unknown", Geometry2D.path.meshDistance, bezier, {distance=5})
    ExpectRaisedMessage("Unknown", Geometry2D.path.meshDistance, bezier, {distanceSign="outsidePositive"})
    ExpectRaisedMessage("Relative path command",
        Geometry2D.path.flatten, {{"m", 0, 0}})

    local directMesh, attributes = Geometry2D.path.meshDistance(bezier, {method="partition",
        innerRange = 5,
        output = "mesh",
    })
    assert(directMesh.fillVertexCount == attributes.vertexCount)
    assert(attributes.distances.componentCount == 1)
    directMesh:removeSelf()

    local strokeData = Geometry2D.path.meshStroke({
        {"M", 0, 0}, {"C", 20, -30, 80, 30, 100, 0},
    }, 8, {output = "buffers", cap = "round", join = "round"})
    assert(strokeData.vertices.count == strokeData.alphas.count)
    assert(strokeData.fillVertexColors.buffer)
    assert(strokeData.fillVertexColors.count == strokeData.alphas.count)
    local strokeMesh = display.newMesh(strokeData)
    strokeMesh:removeSelf()

    local directStroke, strokeAttributes = Geometry2D.path.meshStroke({
        {"M", 0, 0}, {"C", 20, -30, 80, 30, 100, 0},
    }, 8, {output = "mesh", cap = "round", join = "round"})
    assert(directStroke.fillVertexCount == strokeAttributes.vertexCount)
    assert(strokeAttributes.fillVertexColors.buffer)
    assert(strokeAttributes.fillVertexColors.count == strokeAttributes.alphas.count)
    directStroke:setFillColor(0.2, 0.7, 1.0)
    directStroke:removeSelf()
end)

RunTest("Clipper2 path intersection policy", function()
    local bowTie = {
        {"M", 0, 0}, {"L", 100, 100}, {"L", 0, 100},
        {"L", 100, 0}, {"Z"},
    }
    local rejected, rejectError = Geometry2D.path.meshFill(bowTie, {aa="none"})
    assert(rejected == nil)
    assert(type(rejectError) == "string" and
        rejectError:find("intersect", 1, true), rejectError)

    local resolved, resolveError = Geometry2D.path.meshFill(bowTie, {
        aa="none",
        intersections = "resolve",
        fillRule = "evenOdd",
    })
    assert(resolved, resolveError)
    assert(#resolved.indices > 0)

    local sameWindingHole, sameWindingError = Geometry2D.path.meshFill({
        {"M", 0, 0}, {"L", 100, 0}, {"L", 100, 100}, {"L", 0, 100}, {"Z"},
        {"M", 25, 25}, {"L", 75, 25}, {"L", 75, 75}, {"L", 25, 75}, {"Z"},
    }, {aa="none", fillRule = "evenOdd"})
    assert(sameWindingHole, sameWindingError)
    assert(#sameWindingHole.indices > 0)

    ExpectRaisedMessage("Invalid intersections", Geometry2D.path.meshFill,
        bowTie, {intersections = "clip"})
    ExpectRaisedMessage("clipperPrecision", Geometry2D.path.meshFill,
        bowTie, {clipperPrecision = 9})
end)

RunTest("Clipper2 independent sublibrary", function()
    local C = Geometry2D.clipper2
    assert(type(C.version) == "string" and #C.version > 0)
    local a = {0,0, 100,0, 100,100, 0,100}
    local b = {50,50, 150,50, 150,150, 50,150}

    local intersection = assert(C.intersection({a}, {b}))
    assert(math.abs(C.area(intersection) - 2500) < 0.01)
    local union = assert(C.union({a, b}))
    assert(math.abs(C.area(union) - 17500) < 0.01)
    local difference = assert(C.difference({a}, {b}))
    assert(math.abs(C.area(difference) - 7500) < 0.01)
    local xorResult = assert(C.xor({a}, {b}))
    assert(math.abs(C.area(xorResult) - 15000) < 0.01)

    local generic = assert(C.booleanOp("difference", {a}, {b}, {polyTree = true}))
    assert(type(generic.tree) == "table" and type(generic.open) == "table")
    local open = assert(C.booleanOp("intersection", {}, {a}, {
        openSubjects = {{-20,50, 120,50}},
    }))
    assert(#open.closed == 0 and #open.open == 1)

    local inflated = assert(C.inflate({a}, 5, {
        joinType = "round", endType = "polygon",
    }))
    assert(math.abs(C.area(inflated)) > math.abs(C.area({a})))
    local variableInflated = assert(C.inflate({a}, {{2, 4, 6, 8}}, {
        joinType = "miter", endType = "polygon",
    }))
    assert(#variableInflated > 0)
    local clippedLine = C.rectClipLines({0,0, 100,100}, {{-20,50, 120,50}})
    assert(#clippedLine == 1 and math.abs(C.length(clippedLine[1]) - 100) < 0.01)
    assert(#C.minkowskiSum({-1,-1, 1,-1, 1,1, -1,1}, {0,0, 10,0}) > 0)

    local simplified = C.simplify({{0,0, 25,0.01, 50,0, 50,50, 0,50}}, 0.1)
    assert(#simplified[1] < 10)
    local bounds = C.getBounds({a, b})
    assert(bounds.left == 0 and bounds.top == 0 and bounds.right == 150 and bounds.bottom == 150)
    assert(C.pointInPolygon({20,20}, a) == "inside")
    assert(C.isPositive(a))
    assert(#C.ellipse(0, 0, 10, 5, {steps = 12}) == 24)
    ExpectRaisedMessage("only array entries", C.union,
        {{0,0, 10,0, 0,10, extra = true}})
end)

RunTest("stateful ribbon buffers and retained view", function()
    local ribbon = Geometry2D.ribbon.new({
        width = 20,
        aaWidth = 2,
        minDistance = 0,
        initialPointCapacity = 4,
        maxPoints = 64,
        capacityTiers = true,
        color = {0.2, 0.7, 1, 0.8},
        alpha = 0.5,
    })
    assert(ribbon:addPoint(0, 0, 100))
    assert(ribbon:addPoint(50, 0, 110))
    assert(ribbon:addPoint(100, 0, 120))
    assert(ribbon:pointCount() == 3)

    local tableData = assert(ribbon:snapshot("table"))
    assert(tableData.mode == "indexed" and tableData.logicalVertexCount > 0)
    assert(tableData.logicalIndexCount == tableData.logicalTriangleCount * 3)
    assert(tableData.vertexCapacity >= tableData.logicalVertexCount)
    assert(tableData.indexCapacity >= tableData.logicalIndexCount)
    assert(#tableData.pathDistances == tableData.vertexCapacity)
    assert(#tableData.contourDistances == tableData.vertexCapacity)
    assert(math.abs(tableData.headLength - 100) < 0.001)
    for i = tableData.logicalIndexCount + 1, tableData.indexCapacity do
        assert(tableData.indices[i] == 1, "ribbon padding index is not degenerate")
    end

    local buffers = assert(ribbon:snapshot())
    assert(buffers.vertices.buffer and buffers.uvs.buffer and buffers.indices.buffer)
    assert(buffers.pathDistances.buffer and buffers.contourDistances.buffer)
    assert(buffers.vertices.count == buffers.vertexCapacity)
    assert(buffers.uvs.count == buffers.vertexCapacity)
    assert(buffers.indices.count == buffers.indexCapacity)
    assert(buffers.pathDistances.componentCount == 1)
    assert(buffers.contourDistances.componentCount == 1)
    assert(buffers.zeroBasedIndices)

    local view = assert(ribbon:newView())
    assert(view.group and view.mesh)
    local mesh = view.mesh
    assert(ribbon:addPoint(150, 20, 130))
    local updated, replaced, changed = ribbon:updateView(view)
    assert(updated == view and changed)
    if not replaced then assert(view.mesh == mesh) end

    local beforeStyle = ribbon:getStats()
    ribbon:setColor(1, 0.25, 0.1, 0.75):setAlpha(0.4)
    updated, replaced, changed = ribbon:updateView(view)
    assert(updated == view and changed and not replaced)
    assert(ribbon:getStats().buildCount == beforeStyle.buildCount,
        "style-only ribbon update rebuilt geometry")
    updated, replaced, changed = ribbon:updateView(view)
    assert(updated == view and not changed and not replaced,
        "stationary ribbon performed a redundant view update")

    local future = Geometry2D.ribbon.new({minDistance = 0})
    future:addPoint(0, 0, 1000)
    future:addPoint(20, 0, 2000)
    assert(future:expire(1500, 600) == 0, "future timestamp expired early")
    assert(future:pointCount() == 2)

    local oldPointCapacity = ribbon:getStats().pointCapacity
    ribbon:clear()
    updated, replaced, changed = ribbon:updateView(view)
    assert(updated == view and changed and view.mesh == nil)
    assert(ribbon:addPoint(0, 0, 200))
    assert(ribbon:addPoint(80, 0, 210))
    assert(ribbon:addPoint(8, 1, 220))
    local reversal = assert(ribbon:snapshot("table"))
    assert(reversal.logicalVertexCount >= 16)
    -- Vertex ordering is private; each emitted triangle must stay on one run.
    for i = 1, reversal.logicalIndexCount, 3 do
        local a = reversal.pathDistances[reversal.indices[i]]
        local b = reversal.pathDistances[reversal.indices[i + 1]]
        local c = reversal.pathDistances[reversal.indices[i + 2]]
        assert(math.max(a, b, c) <= 80.001 or math.min(a, b, c) >= 79.999,
            "near-reverse ribbon connected triangles across the split")
    end
    assert(ribbon:getStats().pointCapacity == oldPointCapacity,
        "clear discarded reusable point storage")
    view.group:removeSelf()

    ExpectRaisedMessage("Unknown ribbon.new option", Geometry2D.ribbon.new,
        {fringe = 1})
    ExpectRaisedMessage("non-decreasing", function() ribbon:addPoint(100, 0, 100) end)
    assert(ribbon:destroy() and not ribbon:destroy())
    ExpectRaisedMessage("destroyed", function() ribbon:snapshot() end)
    future:destroy()
end)

RunTest("ribbon triangle-list parity", function()
    -- AA interpolation regression, independent of triangle/vertex ordering.
    for _, mode in ipairs({"indexed", "triangles"}) do
        local r = Geometry2D.ribbon.new({mode = mode, width = 16, aaWidth = 4, minDistance = 0})
        r:addPoint(-80, 0, 0); r:addPoint(80, 0, 1)
        local d = assert(r:snapshot("table"))
        for i = 1, d.logicalTriangleCount do
            local x, y, distance = 0, 0, 0
            for k = 1, 3 do
                local id = (i - 1) * 3 + k
                id = d.indices and d.indices[id] or id
                x = x + d.vertices[2 * id - 1] / 3
                y = y + d.vertices[2 * id] / 3
                distance = distance + d.contourDistances[id] / 3
            end
            assert(math.abs(distance - math.max(0, math.abs(x) - 80, math.abs(y) - 8)) < 0.001,
                "AA distance interpolation crossed a silhouette-plane boundary")
        end
        r:destroy()
    end
    local indexed = Geometry2D.ribbon.new({minDistance = 0, miterLimit = 1.5})
    local triangles = Geometry2D.ribbon.new({mode = "triangles", minDistance = 0, miterLimit = 1.5})
    for _, p in ipairs({{0, 0}, {80, 0}, {30, 40}, {100, 100}}) do
        assert(indexed:addPoint(p[1], p[2], 0))
        assert(triangles:addPoint(p[1], p[2], 0))
    end
    local a, b = assert(indexed:snapshot("table")), assert(triangles:snapshot("table"))
    assert(b.mode == "triangles" and b.indices == nil and b.zeroBasedIndices == nil)
    assert(b.logicalIndexCount == 0 and b.indexCapacity == 0)
    assert(b.logicalVertexCount == a.logicalIndexCount and b.vertexCapacity % 3 == 0)
    for i = 1, b.logicalVertexCount do
        local j = a.indices[i]
        assert(b.vertices[2*i-1] == a.vertices[2*j-1] and b.vertices[2*i] == a.vertices[2*j])
        assert(b.pathDistances[i] == a.pathDistances[j])
        assert(b.contourDistances[i] == a.contourDistances[j])
    end
    for i = b.logicalVertexCount + 1, b.vertexCapacity do
        assert(b.vertices[2*i-1] == b.vertices[1] and b.vertices[2*i] == b.vertices[2])
    end
    local buffers = assert(triangles:snapshot())
    assert(buffers.indices == nil and buffers.vertices.count == b.vertexCapacity)
    local view = assert(triangles:newView())
    assert(view.mode == "triangles" and view.mesh)
    triangles:setWidth(12)
    assert(triangles:updateView(view))
    view.group:removeSelf()
    indexed:destroy()
    triangles:destroy()
end)

RunTest("dashed stroke output and validation", function()
    local straightPath = {{"M", 0, 0}, {"L", 100, 0}}
    local solidStroke = Geometry2D.path.meshStroke(straightPath, 8, {
        aa="none", cap = "butt", mode = "triangles",
    })
    local dashedStroke = Geometry2D.path.meshStroke(straightPath, 8, {
        aa="none", cap = "butt", mode = "triangles",
        dashPattern = {10, 10},
    })
    assert(solidStroke.alphas==nil and dashedStroke.alphas==nil)
    assert(#solidStroke.vertices / 2 == 18)
    assert(#dashedStroke.vertices == #solidStroke.vertices * 5)

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
    assert(limited == nil)
    assert(type(limitError) == "string" and
        limitError:find("maxDashSegments", 1, true))
    ExpectRaisedMessage("positive finite number",
        Geometry2D.path.meshStroke, straightPath, 8, {dashPattern = {10, 0}})

    print(("Dashed stroke mesh: %d vertices for five butt-cap dashes"):format(
        #dashedStroke.vertices / 2))
end)

RunTest("retained Bezier shape and stable view updates", function()
    local shape = Geometry2D.path.newShape()
    assert(shape:moveTo(0, 0) == shape)
    assert(shape:lineTo(80, 0):lineTo(80, 80):lineTo(0, 80):close() == shape)
    assert(shape:commandCount() == 5)
    shape:fill(0.2, 0.55, 1, 0.8)
        :strokeWidth(6)
        :strokeFill(1, 0.8, 0.2)
        :strokeJoin("miter")
        :strokeCap("butt")
        :configure({aaWidth = 1, mode = "indexed"})

    local view, createError = shape:newView()
    assert(view, createError)
    assert(view.group and view.fillMesh and view.strokeMesh)
    local group = view.group
    local fillMesh = view.fillMesh
    local strokeMesh = view.strokeMesh

    shape:setCommand(2, "L", 84, 0)
    local updated, replaced = shape:updateView(view)
    assert(updated == view)
    assert(replaced == false, "same topology should use mesh.path:update()")
    assert(view.group == group and view.fillMesh == fillMesh and view.strokeMesh == strokeMesh)

    shape:fill(0.9, 0.25, 0.35, 0.7)
    updated, replaced = shape:updateView(view)
    assert(updated == view and replaced == false)
    assert(view.fillMesh == fillMesh, "style-only update replaced fill mesh")

    shape:clear():moveTo(0, 0):lineTo(90, 0):lineTo(45, 75):close()
    updated, replaced = shape:updateView(view)
    assert(updated == view and replaced == true)
    assert(view.group == group, "topology update replaced the stable view group")
    assert(view.fillMesh ~= fillMesh or view.strokeMesh ~= strokeMesh)

    local imported = Geometry2D.path.newShape(bezier)
    assert(imported:commandCount() == #bezier)
    ExpectRaisedMessage("before moveTo", Geometry2D.path.newShape, {{"L", 1, 2}})
    ExpectRaisedMessage("only array entries", shape.strokeDash, shape, {10, 10, extra = true})
    ExpectRaisedMessage("between 0 and 1", shape.fill, shape, 2, 0, 0)
    ExpectRaisedMessage("does not accept option", shape.configure, shape, {output = "mesh"})

    local otherShape = Geometry2D.path.newShape(bezier)
    ExpectRaisedMessage("belongs to another", otherShape.updateView, otherShape, view)
    group:removeSelf()
end)

-- The render geometry used by fillExtendedData is populated asynchronously.
-- Keep this validation separate from the synchronous assertion count above.
local function SchedulePackedAttributeTest()
    graphics.defineVertexExtension({
        name = "Geometry2DAssertData",
        {name = "geom", type = "float", componentCount = 4},
    })

    local bufferData = Geometry2D.path.meshDistance(bezier, {method="partition",
        innerRange = 5,
        output = "buffers",
    })
    local mesh = display.newMesh(bufferData)
    assert(mesh.fillVertexCount == bufferData.vertices.count)
    mesh.fillExtension = "Geometry2DAssertData"

    local frames = 0
    local function WritePackedDistances()
        frames = frames + 1
        if frames < 2 then return end
        Runtime:removeEventListener("enterFrame", WritePackedDistances)
        local ok, err = pcall(function()
            mesh.fillExtendedData:setAttributeValues("geom", bufferData.distances)
        end)
        mesh:removeSelf()
        assert(ok, "packed distance bulk update failed: " .. tostring(err))
        print("[PASS] packed distance bulk attribute update")
    end
    Runtime:addEventListener("enterFrame", WritePackedDistances)
end

SchedulePackedAttributeTest()

local function SchedulePackedVertexColorTest()
    local mesh, attributes = Geometry2D.util.meshFill({
        -40,-40, 40,-40, 0,40,
    }, {aa="vertex", aaWidth=1, output = "mesh"})
    assert(attributes.fillVertexColors.buffer)
    assert(attributes.fillVertexColors.count == mesh.fillVertexCount)
    mesh.x, mesh.y = display.contentCenterX, display.contentCenterY
    mesh:translate(mesh.path:getVertexOffset())
    mesh:setFillColor(1, 0, 0)

    local frames = 0
    local function SamplePackedColor()
        frames = frames + 1
        if frames < 2 then return end
        Runtime:removeEventListener("enterFrame", SamplePackedColor)
        display.colorSample(display.contentCenterX, display.contentCenterY, function(color)
            mesh:removeSelf()
            assert(color.r > 0.8 and color.g < 0.2 and color.b < 0.2,
                ("packed fillVertexColors rendered unexpected color: %.3f, %.3f, %.3f")
                    :format(color.r, color.g, color.b))
            print("[PASS] output=mesh packed fillVertexColors")
        end)
    end
    Runtime:addEventListener("enterFrame", SamplePackedColor)
end

SchedulePackedVertexColorTest()

print(("geometry2d assert tests: %d synchronous groups passed; " ..
    "2 asynchronous groups scheduled"):format(passed))
