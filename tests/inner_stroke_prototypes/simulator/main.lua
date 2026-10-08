display.setStatusBar(display.HiddenStatusBar)
display.setDefault('background',0,0,0)
local json=require('json')
local probe=require('probe')
local test,listener
local function wait(n) for _=1,n do coroutine.yield() end end
local function sample(x,y) return coroutine.yield{x=x,y=y} end
local function finish(ok,err)
    Runtime:removeEventListener('enterFrame',listener)
    if err then print(debug.traceback(test,tostring(err))) end
    print('SIMULATOR_TEST_EXIT: '..(ok and '0' or '1'));io.flush()
end
Runtime:addEventListener('unhandledError',function(e)finish(false,e.errorMessage..'\n'..(e.stackTrace or ''));return true end)
test=coroutine.create(function()
    local fixture=os.getenv('GEOMETRY2D_PRIVATE_FIXTURES')
        or system.pathForFile('../fixtures/backup_geometry.json',system.ResourceDirectory)
    local f=assert(io.open(fixture,'rb'))
    local cases=assert(json.decode(f:read('*a')));f:close()
    for _,case in ipairs(cases) do
        local original=json.encode(case.groups)
        for _,method in ipairs{'local','partition'} do
            for run=0,2 do
                local t=system.getTimer()
                local called,m,e=pcall(probe.geometry.util.meshDistanceGroups,case.groups,
                    {method=method,innerRange=4,outerRange=2,output='table'})
                local ms=system.getTimer()-t
                assert(called,'unexpected Lua contract error '..tostring(m))
                assert(json.encode(case.groups)==original,'input changed')
                print('BASELINE '..json.encode{case=case.name,method=method,run=run,warmup=run==0,
                    success=m~=nil,totalMs=ms,vertices=m and #m.vertices/2 or 0,error=e})
                wait(1)
            end
        end
    end
    print('BASELINE_DIAGNOSTIC_COMPLETE (not all geometries passed)')
    local max=system.getInfo('maxVertexAttributes')
    print('PROBE_CAPABILITIES '..json.encode{extraAttributes=max})
    for _,k in ipairs{4,8,12} do
        if max<k then print('PROBE_UNSUPPORTED slots='..k..' insufficient attributes')
        else
            for rebuild=1,2 do
                local mesh=probe.create(k);wait(2)
                assert(sample(320,240).b<.01,'first frame not hidden')
                probe.write(mesh,k);wait(2)
                local center=sample(320,240)
                assert(math.abs(center.b-.5)<.04,'shader failed or center incorrect '..json.encode(center))
                assert(sample(358,240).r>.4,'inner stroke missing')
                assert(sample(365,240).r<.01,'solid stroke outside outline')
                for _,scale in ipairs{.2,.5,1,2,5} do
                    mesh.xScale=-scale;mesh.yScale=scale;mesh.rotation=23;wait(2)
                    assert(math.abs(sample(320,240).b-.5)<.04,'transform center incorrect')
                end
                display.remove(mesh);wait(1)
            end
            print('PROBE_PASS slots='..k..' first-frame/bulk/rebuild/fill/stroke/transforms')
        end
    end
    local dir=os.getenv('GEOMETRY2D_PROTOTYPE_DATA')
    if dir and max>=12 then
        local tiled=require('tiled')
        local only=os.getenv('GEOMETRY2D_PROTOTYPE_CASE')
        local names={'convex','hole','vanish','split','island'}
        for _,case in ipairs(cases) do names[#names+1]=case.name end
        for _,name in ipairs(only and {only} or names) do
            local mesh,info,write=tiled.create(dir,name)
            mesh.x,mesh.y=320,240
            local b=info.bounds
            local scale=math.min(240/(b[3]-b[1]),240/(b[4]-b[2]))
            mesh.xScale,mesh.yScale=scale,scale
            wait(2);assert(sample(320,240).b<.01,'tiled first frame flashed')
            write();wait(2)
            local lit,over=0,0
            for y=144,336,24 do for x=224,416,24 do
                local c=sample(x,y)
                if c.r+c.b>.1 then lit=lit+1 end
                if c.r+c.b>.55 then over=over+1 end
            end end
            assert(lit>0,'tiled mesh invisible '..name)
            assert(over==0,'tiled half-opacity overdraw '..name)
            print('TILED_SMOKE '..json.encode{case=name,litSamples=lit,overdrawSamples=over,vertices=info.vertices,triangles=info.indices/3})
            require('coverage').run(mesh,info,wait,sample,name)
            display.remove(mesh);wait(1)
        end
        print('TILED_COVERAGE_COMPLETE (sampled CPU color oracle; not exhaustive/device acceptance)')
    else print('TILED_SMOKE_SKIPPED: export directory or 12 attributes required') end
end)
listener=function()
    local response
    while true do
        local ok,request=coroutine.resume(test,response)
        if not ok then finish(false,request);return end
        if coroutine.status(test)=='dead' then finish(true);return end
        if type(request)~='table' then return end
        response=nil
        display.colorSample(request.x,request.y,function(e)response={r=e.r,g=e.g,b=e.b,a=e.a}end)
        assert(response,'sample unavailable')
    end
end
Runtime:addEventListener('enterFrame',listener)
