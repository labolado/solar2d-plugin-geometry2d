return function(G,wait,sample)
    local poly={{0,0,120,0,120,120,0,120},{40,40,80,40,80,80,40,80}}
    for _,mode in ipairs{'indexed','triangles'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local data,a=G.util.meshFill(poly,{aa="vertex",aaWidth=4,
                mode=mode,output=output})
            assert(data,a)
            local mesh=output=='mesh' and data or display.newMesh(data)
            if output=='table' then
                for i,alpha in ipairs(data.alphas) do mesh:setFillVertexColor(i,1,1,1,alpha) end
            end
            mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
            mesh:setFillColor(.2,.6,1,.5)
            wait(1) -- no shader, extension or post-render attribute upload
            local inside=sample(120,120)
            assert(math.abs(inside.b-.5)<.04,'fringe first-frame fill/color')
            assert(sample(160,160).b<.01,'fringe hole filled')
            local edge=sample(98,120)
            assert(math.abs(edge.b-.25)<.06,'fringe first-frame alpha ramp')
            assert(sample(94,120).b<.01,'fringe outside coverage')
            mesh:setFillColor(1,0,0,.25);wait(1)
            local tinted=sample(98,120)
            assert(math.abs(tinted.r-.125)<.05 and tinted.b<.01,'tint destroyed alpha ramp')
            display.remove(mesh)
        end
    end
    local previous
    for _,width in ipairs{1,12} do
        local mesh,a=G.util.meshFill(poly,{aa="vertex",aaWidth=width,output='mesh'})
        assert(mesh,a);mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
        mesh.fill={type='gradient',color1={1,0,0,1},color2={0,0,1,1},direction='right'}
        mesh:setFillColor(1,1,1,.5);wait(1)
        local c=sample(120,160)
        assert(c.r>.02 and c.b>.02 and math.abs(c.r+c.b-.5)<.04,'fringe material')
        if previous then assert(math.abs(c.r-previous.r)<.01 and math.abs(c.b-previous.b)<.01,'fringe width shifted material UV') end
        previous=c;display.remove(mesh)
    end
    for _,mode in ipairs{'indexed','triangles'} do
        for _,output in ipairs{'table','buffers','mesh'} do
            local data,a=G.util.meshFill(poly,{aa="none",mode=mode,output=output})
            assert(data,a)
            local attributes=output=='mesh' and a or data
            assert(attributes.alphas==nil and attributes.distances==nil and attributes.fillVertexColors==nil)
            local mesh=output=='mesh' and data or display.newMesh(data)
            mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
            mesh:setFillColor(.2,.6,1,.5);wait(1)
            assert(math.abs(sample(120,120).b-.5)<.04,'earcut fill first frame')
            assert(sample(160,160).b<.01,'earcut fill hole')
            assert(sample(98,120).b<.01,'earcut fill has exterior band')
            display.remove(mesh)
        end
    end
    print('FILL_OUTPUTS PASS none/vertex table/buffers/mesh first-frame, holes, tint and material UV')
end
