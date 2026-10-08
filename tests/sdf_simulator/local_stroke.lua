return function(G,shader,wait,sample)
    local json=require('json')
    local shapes={
        {0,0,120,0,120,120,0,120},
        {0,0,120,0,120,40,40,40,40,120,0,120},
        {{0,0,120,0,120,120,0,120},{40,40,80,40,80,80,40,80}},
    }
    for _,mode in ipairs{'indexed','triangles'} do
        for number,poly in ipairs(shapes) do
            local mesh,a=G.util.meshDistance(poly,{method="local",mode=mode,output='mesh'})
            assert(mesh,a);assert(a.approximate)
            mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
            shader.attach(mesh)
            mesh.fill.effect.fillColor={0,0,1,.5};mesh.fill.effect.strokeColor={1,0,0,.5}
            wait(2);assert(sample(120,120).b<.01,'local stroke pending flash')
            shader.write(mesh,a,3);wait(1)
            local p=sample(120,120);assert(math.abs(p.b-.5)<.04 and p.r<.01,'local stroke body')
            p=sample(101,125);assert(p.r>.46 and p.b<.04,'local inner stroke')
            assert(sample(97,125).r<.01,'local stroke outside original contour')
            -- Constant combined premultiplied alpha catches body/band overlap
            -- independently of approximate stroke-color interpolation.
            for x=133.3,140,1.7 do for y=133.7,140,1.9 do
                p=sample(x,y)
                assert(math.abs(p.r+p.b-.5)<.045,'local join crack/overdraw '..json.encode(p))
            end end
            if number~=1 then assert(sample(160,160).b+sample(160,160).r<.01,'local hole/notch filled') end
            shader.write(mesh,a,0);wait(1)
            p=sample(101,125);assert(p.r<.01 and p.b>.46,'local cached width update')
            shader.write(mesh,a,5,.4);wait(1)
            p=sample(102,125);assert(math.abs(p.r-.2)<.04,'local opacity')
            mesh.fill.effect.strokeColor={0,1,0,.5};wait(1)
            p=sample(102,125);assert(math.abs(p.g-.2)<.04 and p.r<.01,'local cached color update')
            display.remove(mesh)
        end
    end
    for _,scale in ipairs{.2,.5,1,2,5} do
        local mesh,a=G.util.meshDistance({-30,-30,30,-30,30,30,-30,30},
            {method="local",innerRange=12,outerRange=8,output='mesh'})
        assert(mesh,a);mesh.x=display.contentCenterX;mesh.y=display.contentCenterY
        mesh.xScale=-scale;mesh.yScale=scale*.7;mesh.rotation=23
        shader.attach(mesh);mesh.fill.effect.fillColor={0,0,1,.5};mesh.fill.effect.strokeColor={1,0,0,.5}
        wait(2);shader.write(mesh,a,3);wait(1)
        assert(math.abs(sample(mesh.x,mesh.y).b-.5)<.04,'local transformed fill')
        local x0,y0=mesh:localToContent(0,0)
        local x1,y1=mesh:localToContent(1,0)
        local x2,y2=mesh:localToContent(0,1)
        local ax,ay=(x1-x0)/display.contentScaleX,(y1-y0)/display.contentScaleY
        local bx,by=(x2-x0)/display.contentScaleX,(y2-y0)/display.contentScaleY
        local footprint=(math.abs(by)+math.abs(bx))/math.abs(ax*by-ay*bx)
        local sx,sy=mesh:localToContent(30+.25*footprint,0)
        local p=sample(sx,sy)
        assert(p.r+p.b>.04 and p.r+p.b<.24,'local transformed AA '..json.encode(p))
        display.remove(mesh)
    end
    local previous
    for _,range in ipairs{2,6} do
        local mesh,a=G.util.meshDistance(shapes[1],{method="local",outerRange=range,output='mesh'})
        assert(mesh,a);mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
        mesh.fill={type='gradient',color1={1,0,0,1},color2={0,0,1,1},direction='right'}
        shader.attach(mesh,true);mesh.fill.effect.fillColor={1,1,1,.5};wait(2);shader.write(mesh,a,3);wait(1)
        local p=sample(130,160)
        assert(math.abs(p.r+p.b-.5)<.04,'local material alpha')
        if previous then assert(math.abs(p.r-previous.r)<.01 and math.abs(p.b-previous.b)<.01,'local material UV changed') end
        previous=p;display.remove(mesh)
    end
    print('LOCAL_STROKE_SIMULATOR PASS initialization, seams, holes, cached width/color/opacity, transforms and material UV')
end
