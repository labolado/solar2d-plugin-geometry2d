return function(G,shader,wait,sample)
    local json=require('json')
    local shapes={
        {0,0,120,0,120,40,40,40,40,120,0,120},
        {{0,0,120,0,120,120,0,120},{40,40,80,40,80,80,40,80}},
    }
    for _,geometry in ipairs{'fill','fillAA'} do
        for _,mode in ipairs{'indexed','triangles'} do
            for _,poly in ipairs(shapes) do
                local mesh,a=(geometry=='fill' and G.util.meshFill or G.util.meshDistance)(poly,geometry=='fill' and {aa='none',topology='normalize',mode=mode,output='mesh'} or {method='partition',innerRange=0,mode=mode,output='mesh'})
                assert(mesh,a)
                mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
                if geometry=='fillAA' then
                    shader.attach(mesh);wait(2)
                    assert(sample(120,120).b<.01,'fillAA initialization flashed')
                    shader.write(mesh,a,0)
                else
                    assert(a.distances==nil and a.kind=='fill')
                    mesh:setFillColor(.2,.6,1,.5)
                end
                wait(geometry=='fill' and 1 or 2)
                for _,p in ipairs{{120,120},{139.8,139.8},{125,185}} do
                    local color=sample(p[1],p[2])
                    assert(math.abs(color.b-.5)<.045,'light body opacity/seam '..json.encode(color))
                end
                assert(sample(160,160).b<.01,'hole/concave empty region covered')
                if geometry=='fillAA' then
                    -- Exterior-only transition: half a physical pixel outside
                    -- a straight edge is half covered; body is not half alpha.
                    local c=sample(100-.5*display.contentScaleX,120)
                    assert(c.b>.08 and c.b<.43,'exterior AA missing '..json.encode(c))
                end
                assert(sample(97,120).b<.01,'light mode exterior solid coverage')
                display.remove(mesh)
            end
        end
    end
    -- Adjacent regions' AA overlap geometrically in the gap, but must be
    -- partitioned: never blend the semi-transparent fill twice.
    local mesh,a=G.util.meshDistanceGroups({
        {0,0,60,0,60,100,0,100},{60.4,0,120,0,120,100,60.4,100},
    },{method="partition",innerRange=0,output='mesh',outerRange=3})
    assert(mesh,a);mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
    shader.attach(mesh);wait(2);shader.write(mesh,a,0);wait(2)
    assert(sample(160.2,150).b<=.53,'neighbor AA double coverage')
    display.remove(mesh)
    for _,scale in ipairs{.2,.5,1,2,5} do
        for _,geometry in ipairs{'fill','fillAA'} do
            local options=geometry=='fill' and {aa='none',topology='normalize',output='mesh'} or {method='partition',innerRange=0,output='mesh'}
            if geometry=='fillAA' then options.outerRange=8 end
            local m,attr=(geometry=='fill' and G.util.meshFill or G.util.meshDistance)({-30,-30,30,-30,30,30,-30,30},options)
            assert(m,attr);m.x=display.contentCenterX;m.y=display.contentCenterY
            m.xScale=-scale;m.yScale=scale*.7;m.rotation=23
            if geometry=='fillAA' then shader.attach(m);wait(2);shader.write(m,attr,0)
            else m:setFillColor(.2,.6,1,.5) end
            wait(2)
            assert(math.abs(sample(m.x,m.y).b-.5)<.04,'transformed body opacity')
            if geometry=='fillAA' then
                local x0,y0=m:localToContent(0,0)
                local x1,y1=m:localToContent(1,0)
                local x2,y2=m:localToContent(0,1)
                local ax,ay=(x1-x0)/display.contentScaleX,(y1-y0)/display.contentScaleY
                local bx,by=(x2-x0)/display.contentScaleX,(y2-y0)/display.contentScaleY
                local footprint=(math.abs(by)+math.abs(bx))/math.abs(ax*by-ay*bx)
                local sx,sy=m:localToContent(30+.5*footprint,0)
                local color=sample(sx,sy)
                assert(color.b>.06 and color.b<.44,'transformed exterior AA '..json.encode(color))
            end
            local x,y=m:localToContent(45,0)
            assert(sample(x,y).b<.01,'transformed exterior coverage')
            display.remove(m)
        end
    end
    -- Full Lua API timings including output assembly. No GPU-time claims.
    for _,geometry in ipairs{'fill','fillAA','innerStroke'} do
        for _,output in ipairs{'table','buffers'} do
            local start=system.getTimer();local last
            for _=1,30 do last=assert((geometry=='fill' and G.util.meshFill or G.util.meshDistance)(shapes[1],geometry=='fill' and {aa='none',topology='normalize',output=output} or {method='partition',innerRange=geometry=='fillAA' and 0 or 8,output=output})) end
            print('SDF_LIGHT_API '..json.encode{geometry=geometry,output=output,
                mean_ms=(system.getTimer()-start)/30,stats=last.stats})
        end
    end
    print('SDF_LIGHT_SIMULATOR PASS first-frame fill, fillAA initialization, holes, concave seams, adjacent AA, transforms')
end
