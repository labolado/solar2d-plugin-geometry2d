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
        {0,0, 10,0, 0,10}, {fringe = 0})
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
        {0,0, 10,0, 0,10}, {fringe = 0, output = "buffers"})
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
    bufferMesh:removeSelf()

    local legacyBufferData = Geometry2D.path.meshSDF(bezier, {
        output = "buffers",
        legacyUVs = true,
    })
    assert(legacyBufferData.uvs.buffer)

    ExpectRaisedMessage("use 'join'",
        Geometry2D.path.meshStroke, bezier, 8, {joint = "round"})
    ExpectRaisedMessage("requires output",
        Geometry2D.path.meshSDF, bezier, {legacyUVs = true})
    ExpectRaisedMessage("Relative path command",
        Geometry2D.path.flatten, {{"m", 0, 0}})

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
    local rejected, rejectError = Geometry2D.path.meshFill(bowTie, {fringe = 0})
    assert(rejected == nil)
    assert(type(rejectError) == "string" and
        rejectError:find("intersect", 1, true), rejectError)

    local resolved, resolveError = Geometry2D.path.meshFill(bowTie, {
        fringe = 0,
        intersections = "resolve",
        fillRule = "evenOdd",
    })
    assert(resolved, resolveError)
    assert(#resolved.indices > 0)

    local sameWindingHole, sameWindingError = Geometry2D.path.meshFill({
        {"M", 0, 0}, {"L", 100, 0}, {"L", 100, 100}, {"L", 0, 100}, {"Z"},
        {"M", 25, 25}, {"L", 75, 25}, {"L", 75, 75}, {"L", 25, 75}, {"Z"},
    }, {fringe = 0, fillRule = "evenOdd"})
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

RunTest("dashed stroke output and validation", function()
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
    assert(limited == nil)
    assert(type(limitError) == "string" and
        limitError:find("maxDashSegments", 1, true))
    ExpectRaisedMessage("positive finite number",
        Geometry2D.path.meshStroke, straightPath, 8, {dashPattern = {10, 0}})

    print(("Dashed stroke mesh: %d vertices for five butt-cap dashes"):format(
        #dashedStroke.alphas))
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
        :configure({fringe = 1, mode = "indexed"})

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

    local bufferData = Geometry2D.path.meshSDF(bezier, {
        distance = 5,
        output = "buffers",
        distanceSign = "outsidePositive",
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
    }, {fringe = 0, output = "mesh"})
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
