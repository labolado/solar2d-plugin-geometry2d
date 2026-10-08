-- Runs in the Simulator and in the Lua 5.1 binding harness (with a display mock).
return function(Ribbon)
    local function raises(text, fn)
        local ok, message = pcall(fn)
        assert(not ok and tostring(message):find(text, 1, true), tostring(message))
    end
    local function close(a, b) assert(math.abs(a-b) < 0.001, a .. ' ~= ' .. b) end
    -- A rectangle has an independently known max-plane distance. Check
    -- triangle interiors, not just vertices: the old AA slices assigned valid
    -- vertex distances but interpolated 4 where the side distance was 0.5.
    for _,mode in ipairs{'indexed','triangles'} do
        local r=Ribbon.new{mode=mode,width=16,aaWidth=4,minDistance=0}
        r:addPoint(-80,0,0);r:addPoint(80,0,1)
        local d=assert(r:snapshot('table'))
        local area=0
        for i=1,d.logicalTriangleCount do
            local ids={}
            for k=1,3 do
                local id=(i-1)*3+k
                ids[k]=d.indices and d.indices[id] or id
            end
            local a,b,c=ids[1],ids[2],ids[3]
            local ax,ay=d.vertices[2*a-1],d.vertices[2*a]
            local bx,by=d.vertices[2*b-1],d.vertices[2*b]
            local cx,cy=d.vertices[2*c-1],d.vertices[2*c]
            area=area+math.abs((bx-ax)*(cy-ay)-(cx-ax)*(by-ay))/2
            for _,weights in ipairs{{1/3,1/3,1/3},{.2,.3,.5}} do
                local u,v,w=unpack(weights)
                local x,y=u*ax+v*bx+w*cx,u*ay+v*by+w*cy
                close(u*d.contourDistances[a]+v*d.contourDistances[b]+w*d.contourDistances[c],
                    math.max(0,math.abs(x)-80,math.abs(y)-8))
            end
        end
        close(area,168*24)
        r:destroy()
        print('RIBBON_CONTRACT PASS '..mode..' AA side/cap/corner interpolation and area')
    end
    local function make(points, aa)
        local r = Ribbon.new{width=20, aaWidth=aa or 2, minDistance=0}
        for i,p in ipairs(points) do assert(r:addPoint(p[1], p[2], p[3] or i)) end
        return r
    end
    for name,points in pairs{
        straight={{0,0},{10,0},{100,0}}, corner={{0,0},{60,0},{60,60}},
        reverse={{0,0},{80,0},{8,1}}, crossing={{0,0},{80,80},{0,80},{80,0}},
    } do
        local r = make(points)
        local d = assert(r:snapshot('table'))
        local bounds={math.huge,math.huge,-math.huge,-math.huge}
        for i=1,d.logicalVertexCount do
            local x,y=d.vertices[i*2-1],d.vertices[i*2]
            assert(x==x and y==y and math.abs(x)<math.huge and math.abs(y)<math.huge)
            bounds[1],bounds[2]=math.min(bounds[1],x),math.min(bounds[2],y)
            bounds[3],bounds[4]=math.max(bounds[3],x),math.max(bounds[4],y)
            close(d.uvs[i*2-1],d.pathDistances[i])
            close(d.uvs[i*2],d.contourDistances[i])
        end
        for i=d.logicalVertexCount+1,d.vertexCapacity do
            local x,y=d.vertices[i*2-1],d.vertices[i*2]
            assert(x>=bounds[1] and y>=bounds[2] and x<=bounds[3] and y<=bounds[4])
        end
        for i=d.logicalIndexCount+1,d.indexCapacity,3 do
            assert(d.indices[i]==d.indices[i+1] and d.indices[i]==d.indices[i+2])
        end
        for _,id in ipairs(d.indices) do assert(id>=1 and id<=d.vertexCapacity) end
        if name=='reverse' then
            for i=1,d.logicalIndexCount,3 do
                local a,b,c=d.pathDistances[d.indices[i]],d.pathDistances[d.indices[i+1]],d.pathDistances[d.indices[i+2]]
                assert(math.max(a,b,c)<=80.001 or math.min(a,b,c)>=79.999)
            end
        end
        local b=assert(r:snapshot())
        assert(b.vertices.buffer and b.uvs.buffer and b.indices.buffer)
        assert(b.vertices.count==d.vertexCapacity and b.indices.count==d.indexCapacity)
        assert(b.pathDistances.componentCount==1 and b.contourDistances.componentCount==1)
        r:destroy()
        print('RIBBON_CONTRACT PASS '..name)
    end

    -- Unequal lengths and deliberately unrelated time spacing: the middle fade is .1.
    local r=make({{0,0,0},{10,0,999},{100,0,1000}})
    local d=assert(r:snapshot('table'))
    local foundMiddle=false
    for i=1,d.logicalVertexCount do
        if math.abs(d.vertices[2*i-1]-10)<0.001 then
            close((d.pathDistances[i]-d.tailLength)/d.activeLength,.1)
            foundMiddle=true
        end
    end
    assert(foundMiddle, 'missing shared join vertices')
    assert(r:expire(500,1)==1 and r:pointCount()==2, 'future point expired early')
    d=assert(r:snapshot('table'));close(d.tailLength,10);close(d.headLength,100)
    assert(r:expire(500,1)==0)
    raises('non-decreasing',function() r:addPoint(200,0,998) end)
    raises('finite',function() r:addPoint(0/0,0,1001) end)
    raises("expected 'buffers' or 'table'",function() r:snapshot(1) end)
    raises('Unknown ribbon.new option',function() Ribbon.new{joint='round'} end)
    local view=assert(r:newView())
    local group=view.group
    local oldMesh=view.mesh
    r:setWidth(30)
    local v,replaced,changed=r:updateView(view)
    assert(v==view and changed and not replaced and oldMesh==view.mesh)
    local before=r:getStats()
    r:setColor(.2,.4,.8,.75):setAlpha(.5)
    v,replaced,changed=r:updateView(view)
    assert(v==view and changed and not replaced)
    assert(r:getStats().buildCount==before.buildCount)
    v,replaced,changed=r:updateView(view)
    assert(v==view and not changed and not replaced)

    -- A failed display call must return nil/message and leave the generator usable.
    local original=display.newMesh
    local failed=Ribbon.new{minDistance=0}
    assert(failed:addPoint(0,0,0));assert(failed:addPoint(100,0,1))
    display.newMesh=function() error('injected constructor failure') end
    local result,message=failed:newView()
    display.newMesh=original
    assert(result==nil and message:find('injected constructor failure',1,true))
    display.newMesh=function()
        failed:clear() -- would invalidate the buffers being consumed
    end
    result,message=failed:newView()
    display.newMesh=original
    assert(result==nil and message:find('during a view update',1,true))
    assert(failed:pointCount()==2)
    local recovered=assert(failed:newView())
    display.remove(recovered.group)
    failed:destroy()

    r:clear();assert(r:updateView(view));assert(view.mesh==nil)
    assert(r:getStats().pointCapacity==before.pointCapacity)
    assert(r:addPoint(0,0,0));assert(r:addPoint(50,0,1))
    assert(r:updateView(view));assert(view.group==group and view.mesh)
    for i=2,20 do assert(r:addPoint(i*50,0,i)) end
    oldMesh=view.mesh
    v,replaced,changed=r:updateView(view)
    assert(v==view and replaced and changed and view.mesh~=oldMesh and view.group==group)
    display.remove(group)
    assert(r:destroy() and not r:destroy())
    raises('destroyed',function() r:clear() end)
    assert(tostring(r):find('destroyed',1,true))
    print('RIBBON_CONTRACT PASS lifetime, style, expiry, update, rebuild and error recovery')
end
