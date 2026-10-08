return function(Ribbon,Reference,effect,waitFrames)
    local function sample(x,y)
        -- Solar2D colorSample registers its listener on the main Lua state.
        -- Ask the enterFrame driver to call it there, outside this coroutine.
        local result=coroutine.yield({sampleX=x,sampleY=y})
        assert(result,'colorSample did not complete')
        assert(result.b and result.b==result.b,'non-finite pixel sample')
        return result
    end
    for _,scale in ipairs{.5,1,2} do
        for _,rotation in ipairs{0,30} do
            for _,width in ipairs{16,1} do
                local r=Ribbon.new{width=width,aaWidth=4,minDistance=0,color={.3,.8,1}}
                r:addPoint(-80,0,0);r:addPoint(-64,0,900);r:addPoint(80,0,1000)
                local view=assert(r:newView{effect=effect})
                local g=view.group;g.x,g.y=320,240;g.xScale,g.yScale=scale,scale;g.rotation=rotation
                waitFrames(1) -- first completed render after constructor/effect assignment
                local x,y=g:localToContent(40,0)
                local first=sample(x,y)
                if width==16 then
                    assert(math.abs(first.b-.75)<.09,'first-frame distance fade or core is wrong')
                    assert(math.abs(first.r-first.b*.3)<.04,'first-frame tint missing')
                    x,y=g:localToContent(-40,0)
                    local tail=sample(x,y)
                    assert(math.abs(tail.b-.25)<.09,'fade is not based on distance')
                end
                local native,coordinates={},{}
                for offset=-3,3,.5 do
                    x,y=g:localToContent(40,-width/2+offset/scale)
                    coordinates[#coordinates+1]={x,y}
                    native[#native+1]=sample(x,y).b
                end
                local maxValue=0;for _,b in ipairs(native) do maxValue=math.max(maxValue,b) end
                assert(maxValue>.01,'thin or transformed trail missing')
                g.isVisible=false
                local reference=Reference.new(width,4)
                reference:addPoint(-80,0,0);reference:addPoint(-64,0,900);reference:addPoint(80,0,1000)
                local rg=display.newGroup();rg.x,rg.y=g.x,g.y;rg.xScale,rg.yScale=scale,scale;rg.rotation=rotation
                reference:update(rg);waitFrames(1)
                local delta=0;local luaPixels={}
                for i,p in ipairs(coordinates) do
                    luaPixels[i]=sample(p[1],p[2]).b
                    delta=math.max(delta,math.abs(luaPixels[i]-native[i]))
                end
                print('RIBBON_PIXELS '..require('json').encode{scale=scale,rotation=rotation,width=width,
                    native=native,lua_outer=luaPixels,max_delta=delta,first_frame=first})
                display.remove(rg);g.isVisible=true
                if width==16 then
                    r:setAlpha(.4):setColor(.2,.5,1,.5);assert(r:updateView(view));waitFrames(1)
                    x,y=g:localToContent(40,0);local tinted=sample(x,y)
                    assert(math.abs(tinted.b-.15)<.04 and math.abs(tinted.r-.03)<.025,'color/global alpha mismatch')
                    local old=view.mesh
                    for i=1,20 do r:addPoint(80+i,0,1000+i) end
                    assert(r:updateView(view));assert(view.mesh~=old,'capacity test did not rebuild')
                    waitFrames(1);x,y=g:localToContent(40,0)
                    local rebuilt=sample(x,y)
                    assert(math.abs(rebuilt.b-(120/180)*.2)<.04,'rebuild first-frame color missing')
                    r:setWidth(8);assert(r:updateView(view));waitFrames(1)
                    x,y=g:localToContent(40,7)
                    assert(sample(x,y).b<.025,'width change did not move boundary')
                end
                display.remove(g);r:destroy()
            end
        end
    end
    -- Explicit exterior-ramp regression in both output modes, including a
    -- miter and a limited/beveled acute turn. Fade/tint-only tests miss this.
    for _,mode in ipairs{'indexed','triangles'} do
        for _,points in ipairs{
            {{-80,0},{80,0}}, {{-80,0},{0,0},{0,80}},
            {{-80,0},{0,0},{-60,40}},
        } do
            local r=Ribbon.new{mode=mode,width=16,aaWidth=4,minDistance=0,miterLimit=1.5}
            for i,p in ipairs(points) do r:addPoint(p[1],p[2],i) end
            local v=assert(r:newView{effect=effect})
            v.group.x,v.group.y=320,240
            waitFrames(1)
            local core=sample(280,240).b
            local ramp=false
            for offset=.125,1.5,.125 do
                local b=sample(280,232-offset).b
                if b>.01 and b<core-.01 then ramp=true end
            end
            assert(ramp,mode..' exterior AA ramp missing')
            if #points==2 then
                local cap=sample(400.5,240).b
                assert(cap>.01 and cap<.99,mode..' head-cap AA missing')
            elseif points[3][1]==0 then
                local corner=sample(328.25,231.75).b
                assert(corner>.01 and corner<.49,mode..' outer-miter AA missing')
            end
            r:setWidth(12);assert(r:updateView(v));waitFrames(1)
            local updated=false
            for offset=.125,1.5,.125 do
                local b=sample(280,234-offset).b
                if b>.01 and b<core-.01 then updated=true end
            end
            assert(updated,mode..' updated exterior AA ramp missing')
            display.remove(v.group);r:destroy()
        end
        print('RIBBON_PIXELS PASS '..mode..' exterior AA ramp and mesh update')
    end
    print('RIBBON_PIXELS PASS first-frame, rebuild, transforms, thin lines and style')
end
