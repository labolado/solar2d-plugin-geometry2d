return function(Ribbon,Reference,effect,waitFrames)
    display.enableStatistics(true)
    local function metric(values)
        assert(#values>0,'no benchmark samples')
        table.sort(values);local total=0;for _,v in ipairs(values) do total=total+v end
        return {mean=total/#values,p95=values[math.ceil(#values*.95)],n=#values}
    end
    for _,count in ipairs{32,128,512} do
        for _,mode in ipairs{'lua_no_aa','lua_outer_aa','native_outer_aa'} do
            collectgarbage('collect');local luaBase=collectgarbage('count')
            local group=display.newGroup();local trails,views={},{}
            local native=mode=='native_outer_aa'
            for i=1,count do
                if native then
                    trails[i]=Ribbon.new{width=28,aaWidth=2,color={.3,.8,1}}
                    views[i]=assert(trails[i]:newView{effect=effect});group:insert(views[i].group)
                else trails[i]=Reference.new(28,mode=='lua_no_aa' and 0 or 2) end
            end
            local start=system.getTimer();local tick=0
            local intervals,work={},{};local previous,draws,triangles,samples=nil,0,0,0
            local peakLua,nativeBytes,logicalTriangles,submittedTriangles=0,0,0,0
            local startGeneration,startUpload,startCreates=0,0,0
            local function totals()
                local generation,upload,creates,bytes,logical,submitted=0,0,0,0,0,0
                for _,t in ipairs(trails) do
                    if native then
                        local s=assert(t:getStats());generation=generation+s.totalBuildMilliseconds
                        creates=creates+s.meshCreateCount;bytes=bytes+s.nativeCapacityBytes
                        logical=logical+s.logicalTriangleCount;submitted=submitted+s.triangleCapacity
                    else
                        generation=generation+t.generationMS;upload=upload+t.updateMS;creates=creates+t.creates
                        logical=logical+(t.logicalI or 0)/3;submitted=submitted+(t.mesh and t.tcap or 0)
                    end
                end
                return generation,upload,creates,bytes,logical,submitted
            end
            local measuredStarted=false
            print('RIBBON_BENCH BEGIN '..mode..' count='..count)
            while true do
                waitFrames(1)
                local now=system.getTimer();local elapsed=now-start
                if elapsed>3000 then break end
                local measured=elapsed>=1000
                if measured and not measuredStarted then
                    startGeneration,startUpload,startCreates=totals();measuredStarted=true
                end
                if measured and previous and previous-start>=1000 then intervals[#intervals+1]=now-previous end
                previous=now
                local begin=system.getTimer()
                local due=math.floor(elapsed*60/1000)
                while tick<due do
                    tick=tick+1
                    for id,t in ipairs(trails) do
                        local phase=tick*.09+id*.17;local radius=35+25*math.sin(tick*.027)
                        local accepted,message=t:addPoint(320+radius*3*math.sin(phase),240+radius*2*math.sin(phase*1.7),start+tick*1000/60)
                        assert(accepted~=nil,message)
                    end
                end
                for id,t in ipairs(trails) do
                    t:expire(now,500)
                    if native then assert(t:updateView(views[id])) else t:update(group) end
                end
                if measured then
                    work[#work+1]=system.getTimer()-begin
                    local s={};display.getStatistics(s)
                    assert(type(s.drawCallCount)=='number' and type(s.triangleCount)=='number','renderer statistics unavailable')
                    draws,triangles,samples=draws+s.drawCallCount,triangles+s.triangleCount,samples+1
                    peakLua=math.max(peakLua,collectgarbage('count')-luaBase)
                end
            end
            local generation,upload,creates
            generation,upload,creates,nativeBytes,logicalTriangles,submittedTriangles=totals()
            local frame=metric(intervals)
            print('RIBBON_BENCH_RESULT '..require('json').encode{mode=mode,count=count,fps=1000/frame.mean,
                frame_ms=frame,work_ms=metric(work),generation_ms_per_frame=(generation-startGeneration)/#work,
                lua_display_update_ms_per_frame=native and 'included in work_ms' or (upload-startUpload)/#work,
                mesh_creates_measured=creates-startCreates,mesh_creates_total=creates,draw_calls=draws/samples,
                renderer_triangles=triangles/samples,logical_triangles_final=logicalTriangles,
                submitted_triangles_final=submittedTriangles,lua_peak_delta_kb=peakLua,native_array_capacity_bytes=nativeBytes,
                ticks=tick})
            -- Freeze time below any expiry boundary: repeated updates must do no work.
            if native then
                for i,t in ipairs(trails) do
                    local _,replaced,updated=t:updateView(views[i]);assert(not replaced and not updated)
                end
            end
            for i,t in ipairs(trails) do
                t:expire(start+4000,500)
                if native then assert(t:updateView(views[i]));assert(not views[i].mesh);t:destroy()
                else t:update(group);assert(not t.mesh) end
            end
            display.remove(group);waitFrames(2)
        end
    end
end
