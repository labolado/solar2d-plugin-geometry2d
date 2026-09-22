-- Stateless API migration and output contracts. Called by assert_tests.lua.
return function(G)
    local poly={0,0,120,0,120,120,0,120}
    local hole={poly,{40,40,80,40,80,80,40,80}}
    local path={{'M',0,0},{'L',120,0},{'L',120,120},{'L',0,120},{'Z'}}
    for _,case in ipairs{{G.util.meshDistance,poly},{G.util.meshDistanceGroups,{poly}},{G.path.meshDistance,path}} do
        local fn,input=case[1],case[2]
        assert(fn(input).method=='local')
        assert(fn(input,nil).method=='local')
        assert(fn(input,{}).method=='local')
        for _,output in ipairs{'table','buffers','mesh'} do
            local m,a=fn(input,{output=output});assert(m,a)
            local data=output=='mesh' and a or m
            assert(data.method=='local' and data.approximate==true)
            if output=='mesh' then display.remove(m) end
        end
        local explicit=assert(fn(input,{method='local'}))
        local implicit=assert(fn(input))
        assert(#implicit.vertices==#explicit.vertices)
        for i,v in ipairs(implicit.vertices) do assert(v==explicit.vertices[i]) end
        for i,v in ipairs(implicit.distances) do assert(v==explicit.distances[i]) end
        for _,opts in ipairs{{innerRange=0},{distanceTolerance=.1},{distanceTransform={1,0,0,1,0,0}}} do
            assert(not pcall(fn,input,opts),'partition-only option silently accepted by local')
            opts.method='partition'
            local result=assert(fn(input,opts))
            assert(result.method=='partition' and result.approximate==false)
        end
    end
    assert(G.util.meshSDF==nil and G.util.meshSDFGroups==nil and G.path.meshSDF==nil)
    local before=require('json').encode(hole)
    -- Stable failure contract, including short-but-malformed arrays.
    for _,name in ipairs{'meshFill','meshDistance','meshFillGroups','meshDistanceGroups'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local ok,m,e=pcall(G.util[name],{},{output=output})
            assert(ok and m==nil and type(e)=='string' and #e>0,name)
        end
    end
    for _,name in ipairs{'meshFill','meshDistance'} do
        local ok,m,e=pcall(G.util[name],{0,0,10,0})
        assert(ok and m==nil and type(e)=='string')
        assert(not pcall(G.util[name],{0,'bad'}))
    end
    local cancelPath={}
    for _,x in ipairs{0,200,200} do
        for _,c in ipairs{{'M',x,0},{'L',x+100,0},{'L',x+100,100},{'L',x,100},{'Z'}} do
            cancelPath[#cancelPath+1]=c
        end
    end
    for _,name in ipairs{'meshFill','meshDistance'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local m,a=G.path[name](cancelPath,{intersections='resolve',fillRule='evenOdd',output=output})
            assert(m,a)
            local data=output=='mesh' and a or m
            assert(data.uvBounds[1]==0 and data.uvBounds[3]==300 and data.uvBounds[4]==100)
            if output=='table' then
                for i=1,#data.vertices,2 do assert(math.abs(data.uvs[i]*300-data.vertices[i])<.001) end
            end
            if output=='mesh' then display.remove(m) end
        end
    end
    for _,aa in ipairs{'none','vertex'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local m,e=G.path.meshStroke({{'M',0,0},{'L',100,0}},10,
                {aa=aa,cap='round',tessTol=1e-8,output=output})
            assert(m==nil and type(e)=='string')
        end
        assert(G.path.meshStroke({{'M',0,0},{'L',100,0}},10,
            {aa=aa,cap='round',tessTol=.25}))
        assert(G.path.meshStroke({{'M',0,0},{'L',100,0}},10,
            {aa=aa,cap='butt',join='miter',tessTol=1e-8}))
    end
    local m,e=G.fringe.stroke({0,0,50,0,100,0},10,{cap='round',tessTol=1e-8})
    assert(m==nil and type(e)=='string')
    m,e=G.fringe.fill(poly,{join='round',tessTol=1e-8})
    assert(m==nil and type(e)=='string')
    for _,aa in ipairs{'none','vertex'} do
        for _,topology in ipairs{'direct','normalize'} do
            for _,mode in ipairs{'indexed','triangles'} do
                local tableCount
                for _,output in ipairs{'table','buffers','mesh'} do
                    local m,a=G.util.meshFill(hole,{aa=aa,topology=topology,mode=mode,output=output})
                    assert(m,a)
                    local data=output=='mesh' and a or m
                    assert(data.kind=='fill' and data.aa==aa and data.topology==topology)
                    assert(data.distances==nil and data.method==nil and data.backend==nil and data.geometry==nil)
                    assert((data.alphas~=nil)==(aa=='vertex'))
                    if output=='table' then
                        tableCount=#m.vertices/2
                        assert(#m.uvs==#m.vertices)
                        if mode=='indexed' then assert(m.indices) else assert(m.indices==nil) end
                        for i,v in ipairs(m.vertices) do assert(math.abs(m.uvs[i]*120-v)<.001) end
                    else
                        local n=output=='mesh' and a.vertexCount or m.vertices.count
                        assert(n==tableCount)
                        if aa=='vertex' then
                            assert(data.alphas.buffer and data.alphas.count==n and data.alphas.componentCount==1)
                            assert(data.fillVertexColors.buffer and data.fillVertexColors.count==n)
                        else assert(data.fillVertexColors==nil) end
                        if output=='buffers' then
                            assert(m.vertices.buffer and m.vertices.count==n)
                            assert(m.uvs.buffer and m.uvs.count==n)
                            if mode=='indexed' then assert(m.indices.buffer and m.zeroBasedIndices) end
                        else display.remove(m) end
                    end
                end
            end
        end
    end
    assert(require('json').encode(hole)==before)
    assert(G.path.meshFill(path,{aa='none'}).alphas==nil)
    assert(G.path.meshFill(path,{aa='none',tessTol=.2}))
    local shape=G.path.newShape(path):strokeWidth(4):configure({aa='none'})
    local view=assert(shape:newView())
    assert(shape:configure({aa='vertex',aaWidth=2}):updateView(view))
    assert(shape:configure({aa='none'}):updateView(view))
    display.remove(view.group)
    local rect=assert(G.util.meshFill(poly,{aa='none',maxVertices=4}))
    assert(#rect.vertices==8 and #rect.indices==6 and rect.stats.outputBytes==76)
    local m,err=G.util.meshFill(poly,{aa='none',mode='triangles',maxVertices=4})
    assert(m==nil and type(err)=='string')
    local groups={poly,{60,0,180,0,180,120,60,120}}
    local union=assert(G.util.meshFillGroups(groups,{topology='normalize',aa='none'}))
    local area=0
    for i=1,#union.indices,3 do
        local a,b,c=union.indices[i]*2,union.indices[i+1]*2,union.indices[i+2]*2
        local v=union.vertices
        area=area+math.abs((v[b-1]-v[a-1])*(v[c]-v[a])-(v[b]-v[a])*(v[c-1]-v[a-1]))/2
    end
    assert(math.abs(area-180*120)<.001,'normalized overlapping groups not unioned')
    local bow={{'M',0,0},{'L',120,120},{'L',0,120},{'L',120,0},{'Z'}}
    assert(G.path.meshFill(bow,{topology='normalize'})==nil,'normalization bypassed intersection policy')
    assert(G.path.meshFill(bow,{topology='normalize',intersections='resolve',aa='none'}))
    for _,inner in ipairs{0,8} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local d,a=G.util.meshDistance(poly,{method='partition',innerRange=inner,output=output});assert(d,a)
            local attr=output=='mesh' and a or d
            assert(attr.kind=='distance' and attr.method=='partition' and attr.innerRange==inner)
            assert(attr.distances and attr.alphas==nil and attr.fillVertexColors==nil)
            assert(attr.geometry==nil and attr.backend==nil and attr.sdfVersion==nil)
            if output=='table' then
                for _,v in ipairs(d.distances) do assert(v>=-inner and v<=2.0001) end
            else
                assert(attr.distances.buffer and attr.distances.componentCount==1)
                assert(attr.distances.count==(output=='mesh' and a.vertexCount or d.vertices.count))
            end
            if output=='mesh' then display.remove(d) end
        end
    end
    for _,aa in ipairs{'none','vertex'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local m,a=G.path.meshStroke(path,4,{aa=aa,output=output});assert(m,a)
            local attr=output=='mesh' and a or m
            assert((attr.alphas~=nil)==(aa=='vertex') and attr.distances==nil)
            if aa=='none' then assert(attr.fillVertexColors==nil) end
            if output=='mesh' then display.remove(m) end
        end
    end
    local function rejects(fn,input,opts)
        local ok,e=pcall(fn,input,opts)
        assert(not ok and type(e)=='string','invalid option accepted')
    end
    for _,opts in ipairs{{backend='earcut'},{geometry='fill'},{fringe=1},{aa='shader'},
        {aa='none',aaWidth=1},{aa='none',join='round'},{aa='none',tessTol=.25},{method='local'},
        {topology='auto'},{innerRange=1},{maxVertices=1000001}} do rejects(G.util.meshFill,poly,opts) end
    for _,opts in ipairs{{backend='robust'},{geometry='innerStroke'},{method='auto'},{aa='none'},
        {innerRange=-1},{outerRange=0},{method='local',innerRange=0},
        {method='local',distanceTransform={1,0,0,1,0,0}},{method='partition',miterLimit=2}} do
        rejects(G.util.meshDistance,poly,opts)
    end
end
