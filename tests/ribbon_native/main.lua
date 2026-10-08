-- The mock checks descriptor consumption, offsets and lifecycle only.
display={}
function display.newGroup()
    local g={}
    function g:insert(child) child.parent=self;self[#self+1]=child end
    function g:removeSelf() for _,child in ipairs(self) do child:removeSelf() end end
    return g
end
function display.remove(o) if o then o:removeSelf() end end
function display.newMesh(data)
    local m={fill={},x=0,y=0,path={}}
    local p=m.path
    function p:update(d)
        local vertices,uvs,indices=readBuffer(d.vertices),readBuffer(d.uvs),readBuffer(d.indices,true)
        assert(#vertices==d.vertices.count*2 and #uvs==#vertices)
        assert(#indices==d.indices.count)
        if self.vertices then assert(#self.vertices==#vertices and #self.indices==#indices) end
        self.vertices,self.indices=vertices,indices
        local minX,minY,maxX,maxY=math.huge,math.huge,-math.huge,-math.huge
        for i=1,#vertices,2 do
            minX,minY=math.min(minX,vertices[i]),math.min(minY,vertices[i+1])
            maxX,maxY=math.max(maxX,vertices[i]),math.max(maxY,vertices[i+1])
        end
        self.ox,self.oy=(minX+maxX)/2,(minY+maxY)/2
    end
    function p:getVertexOffset() return self.ox,self.oy end
    function m:translate(x,y) self.x,self.y=self.x+x,self.y+y end
    function m:setFillColor(...) self.color={...} end
    function m:removeSelf() self.parent=nil;self.path=nil end
    p:update(data)
    return m
end
local ok,message=xpcall(function()
    assert(loadfile('tests/ribbon_simulator/contracts.lua'))()(Ribbon)
    local Reference=assert(loadfile('tests/ribbon_simulator/lua_reference.lua'))()
    assert(loadfile('tests/ribbon_simulator/geometry_compare.lua'))()(Ribbon,Reference)
    local r=Ribbon.new{minDistance=0}
    r:addPoint(0,0,0);r:addPoint(100,0,1)
    local b=assert(r:snapshot())
    assert(#readBuffer(b.vertices)==b.vertices.count*2)
    r:setColor(.1,.2,.3)
    assert(pcall(readBuffer,b.vertices), 'style invalidated borrowed geometry')
    r:setWidth(40)
    assert(not pcall(readBuffer,b.vertices), 'stale buffer remained readable')
    b=assert(r:snapshot())
    r:destroy()
    assert(not pcall(readBuffer,b.vertices), 'destroyed generator buffer remained readable')
    do
        local owner=Ribbon.new{minDistance=0}
        owner:addPoint(0,0,0);owner:addPoint(50,0,1)
        b=assert(owner:snapshot())
    end
    collectgarbage('collect')
    assert(not pcall(readBuffer,b.vertices), 'buffer retained generator')
    local small=Ribbon.new{minDistance=0,maxPoints=2}
    small:addPoint(0,0,0);small:addPoint(10,0,1)
    b=assert(small:snapshot())
    local accepted,failure=small:addPoint(20,0,2)
    assert(accepted==nil and failure:find('maxPoints',1,true))
    assert(pcall(readBuffer,b.vertices),'failed append invalidated unchanged geometry')
    small:destroy()
    local ring=Ribbon.new{minDistance=0,maxPoints=32,initialPointCapacity=4}
    local capacity
    for i=1,1000 do
        ring:expire(i,8)
        assert(ring:addPoint(i*10,0,i))
        if i>=2 then assert(ring:snapshot()) end
        if i==16 then capacity=ring:getStats().nativeCapacityBytes end
        if i>16 then assert(ring:getStats().nativeCapacityBytes==capacity,'steady-state native storage grew') end
    end
    ring:destroy()
    print('RIBBON_NATIVE PASS borrowed buffer invalidation; mocked display, no rendering')
end,debug.traceback)
if not ok then error(message) end
