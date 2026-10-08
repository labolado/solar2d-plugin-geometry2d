display.setStatusBar(display.HiddenStatusBar)
display.setDefault('background',0,0,0)
Runtime:addEventListener('unhandledError',function(e)
    print(tostring(e.errorMessage))
    print(e.stackTrace or debug.traceback())
    print('SIMULATOR_TEST_EXIT: 1');io.flush()
    return true
end)
local root=system.pathForFile('',system.ResourceDirectory)
package.path=root..'/../sdf_simulator/?.lua;'..root..'/../../examples/solar2d/?.lua;'..package.path
local G=require('plugin.geometry2d')
local shader=require('sdf_shader')
local test,listener
local function wait(n) for _=1,n do coroutine.yield() end end
local function sample(x,y) return coroutine.yield{x=x,y=y} end
local function finish(ok,err)
    Runtime:removeEventListener('enterFrame',listener)
    if err then print(debug.traceback(test,tostring(err))) end
    print('SIMULATOR_TEST_EXIT: '..(ok and '0' or '1'));io.flush()
end
test=coroutine.create(function()
    require('contracts')(G)
    require('lightweight')(G,shader,wait,sample)
    require('fill_outputs')(G,wait,sample)
    require('local_stroke')(G,shader,wait,sample)
    local shapes={
        {0,0,200,0,200,200,0,200},
        {0,0,200,0,200,80,80,80,80,200,0,200},
        {{0,0,200,0,200,200,0,200},{60,60,140,60,140,140,60,140}},
    }
    for _,poly in ipairs(shapes) do
        local data,err=G.util.meshDistance(poly,{method="partition",output='buffers'})
        assert(data,err)
        assert(data.uvs.count==data.vertices.count and data.distances.componentCount==1)
        print('SDF_STATS '..require('json').encode(data.stats))
    end
    for _,mode in ipairs{'indexed','triangles'} do
        local mesh,attributes=G.util.meshDistance(shapes[1],{method="partition",output='mesh',mode=mode,innerRange=12,outerRange=4})
        assert(mesh,attributes)
        mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
        shader.attach(mesh)
        wait(2)
        local p=sample(110,180);assert(p.r<.01 and p.b<.01,'uninitialized mesh flashed')
        shader.write(mesh,attributes)
        wait(2)
        p=sample(120,180);assert(p.b>.45 and p.r<.15,'fill color/alpha '..require('json').encode(p))
        p=sample(101,180);assert(p.r>.45 and p.b<.1,'inner stroke '..require('json').encode(p))
        p=sample(98,180);assert(p.r<.01 and p.b<.01,'solid stroke outside boundary')
        display.remove(mesh)
    end
    print('SDF_SIMULATOR PASS initialization, signed-distance fill and inner stroke')
    -- Retain the old visible mesh while preparing a replacement. Bulk
    -- attributes require a render, but replacement must not flash or blank.
    local saved=assert(G.util.meshDistance(shapes[1],{method="partition",output='buffers',innerRange=12}))
    assert(G.util.meshDistance(shapes[2],{method="partition",output='buffers'}));collectgarbage('collect')
    local old=display.newMesh(saved);old:translate(old.path:getVertexOffset());old:translate(100,100)
    shader.attach(old);wait(2);shader.write(old,saved);wait(1)
    local replacement,a=G.util.meshDistance(shapes[2],{method="partition",output='mesh',innerRange=12})
    assert(replacement,a);replacement:translate(replacement.path:getVertexOffset());replacement:translate(100,100)
    shader.attach(replacement);wait(2)
    local p=sample(120,150);assert(math.abs(p.b-.5)<.04,'pending replacement blanked/flashed')
    shader.write(replacement,a);display.remove(old);wait(1)
    p=sample(120,150);assert(math.abs(p.b-.5)<.04,'first visible replacement missing attributes')
    display.remove(replacement)
    print('SDF_SIMULATOR PASS owning buffers and prepared replacement first visible frame')
    require('pixels')(G,shader,wait,sample)
    require('benchmark')(G,wait)
end)
listener=function()
    local response
    while true do
        local ok,request=coroutine.resume(test,response)
        if not ok then finish(false,request);return end
        if coroutine.status(test)=='dead' then finish(true);return end
        if type(request)~='table' then return end
        response=nil
        display.colorSample(request.x,request.y,function(e) response={r=e.r,g=e.g,b=e.b,a=e.a} end)
        assert(response,'colorSample did not respond synchronously')
    end
end
Runtime:addEventListener('enterFrame',listener)
