return function(G,shader,wait,sample)
    local json=require('json')
    local poly={-30,-30,30,-30,30,30,-30,30}
    print('SDF_PIXEL_METRIC '..json.encode{contentScaleX=display.contentScaleX,
        contentScaleY=display.contentScaleY,pixelWidth=display.pixelWidth,pixelHeight=display.pixelHeight})
    for _,scale in ipairs{.2,.5,1,2,5} do
        for _,variant in ipairs{'uniform','rotated','nonuniform','flipped','screenMetric'} do
            local sx,sy,rot=scale,scale,0
            if variant=='rotated' then rot=31 end
            if variant=='nonuniform' or variant=='screenMetric' then sx=scale*.6;sy=scale*1.2;rot=19 end
            if variant=='flipped' then sx=-sx;rot=19 end
            local options={method='partition',output='mesh',innerRange=12,outerRange=8}
            if variant=='screenMetric' then
                local a=math.rad(rot)
                options.distanceTransform={sx*math.cos(a)/display.contentScaleX,
                    sx*math.sin(a)/display.contentScaleY,-sy*math.sin(a)/display.contentScaleX,
                    sy*math.cos(a)/display.contentScaleY,0,0}
                options.innerRange=12;options.outerRange=3
            end
            local mesh,attr=G.util.meshDistance(poly,options);assert(mesh,attr)
            local group=display.newGroup();group:insert(mesh)
            mesh:translate(mesh.path:getVertexOffset())
            group.x,group.y=320,240;group.xScale,group.yScale=sx,sy;group.rotation=rot
            shader.attach(mesh);wait(2);shader.write(mesh,attr,2)
            wait(1)
            local x,y=group:localToContent(0,0)
            local center=sample(x,y)
            if variant=='screenMetric' then
                assert(center.r+center.b>.45 and center.r+center.b<.61,'pixel-metric center coverage')
                local t=options.distanceTransform
                local normalScale=math.abs(t[1]*t[4]-t[2]*t[3])/math.sqrt(t[3]^2+t[4]^2)
                x,y=group:localToContent(-30+2/normalScale,3.7)
                local edge=sample(x,y)
                -- Exactly 2 physical pixels inward is the 50% fill/stroke
                -- boundary, independent of object and content scaling.
                assert(math.abs(edge.r-.3)<.1 and math.abs(edge.b-.275)<.1,
                    'screen stroke width drift '..scale..' '..json.encode(edge))
            else
                assert(math.abs(center.b-.5)<.04 and center.r<.13,'transformed center fill')
            end
            local pixels={}
            for offset=-3,3,.25 do
                x,y=group:localToContent(-30+offset/math.abs(sx),3.7)
                pixels[#pixels+1]=sample(x,y)
            end
            local maxSum,minSum,aa=0,1,false
            for _,p in ipairs(pixels) do
                local sum=p.r+p.b
                assert(sum<=.61,'semi-transparent seam overdraw')
                maxSum=math.max(maxSum,sum);minSum=math.min(minSum,sum)
                if sum>.03 and sum<.46 then aa=true end
            end
            assert(maxSum>.4 and minSum<.02 and aa,'edge AA or boundary missing '..variant..' '..scale)
            -- At least two local units outside must not carry solid stroke.
            x,y=group:localToContent(-30-3/math.abs(sx),3.7)
            local outside=sample(x,y);assert(outside.r<.02 and outside.b<.02)
            -- Width/color/opacity changes reuse geometry and initialized attributes.
            -- A pixel-width stroke can legitimately consume a tiny shape's
            -- center. Turn it off for the pixel-metric fill-color probe.
            mesh.fill.effect.params={variant=='screenMetric' and 0 or 4,1,attr.innerRange,.4}
            mesh.fill.effect.fillColor={.1,1,.1,.5}
            mesh.fill.effect.strokeColor={1,.1,.1,.25}
            wait(1);x,y=group:localToContent(0,0);local changed=sample(x,y)
            assert(math.abs(changed.g-.2)<.04 and changed.r<.04,'uniform-only style update')
            print('SDF_PIXELS '..json.encode{scale=scale,variant=variant,edge=pixels,center=center,changed=changed})
            display.remove(group)
        end
    end
    -- Dense near-boundary geometry must never accumulate translucent layers.
    local groups={{{0,0,99,0,99,150,0,150}},{{100,0,200,0,200,150,100,150}}}
    for _,width in ipairs{0,1,4,7} do
        local m,a=G.util.meshDistanceGroups(groups,{method="partition",output='mesh',innerRange=10,outerRange=4})
        assert(m,a);m:translate(m.path:getVertexOffset());m:translate(100,100)
        shader.attach(m);wait(2);shader.write(m,a,width);wait(1)
        m.fill.effect.fillColor={0,0,1,.5};m.fill.effect.strokeColor={1,0,0,.5};wait(1)
        for x=197,203,.25 do local p=sample(x,173)
            assert(p.r+p.b<=.55,'neighbor AA overlap')
        end
        display.remove(m)
    end
    for _,mode in ipairs{'indexed','triangles'} do
        local poly={0,0,200,0,200,80,80,80,80,200,0,200}
        local m,a=G.util.meshDistance(poly,{method="partition",output='mesh',mode=mode,innerRange=12,outerRange=4})
        assert(m,a);m:translate(m.path:getVertexOffset());m:translate(100,100)
        shader.attach(m);wait(2);shader.write(m,a,4)
        m.fill.effect.fillColor={0,0,1,.5};m.fill.effect.strokeColor={1,0,0,.5};wait(1)
        for x=174.2,185.7,.5 do for y=174.3,185.8,.5 do
            local p=sample(x,y);local alpha=p.r+p.b
            assert(alpha<=.52,'concave corner double coverage')
            if x<177 or y<177 then assert(alpha>.47,'concave corner crack') end
            if x>182 and y>182 then assert(alpha<.02,'corner exterior carries solid stroke') end
        end end
        local mixed=false
        for x=102.5,105.5,.25 do local p=sample(x,140)
            if p.r>.05 and p.b>.05 then mixed=true end
        end
        assert(mixed,'inner stroke/fill AA transition absent')
        display.remove(m)
    end
    local previous
    for _,range in ipairs{2,12} do
        local mesh,a=G.util.meshDistance({0,0,160,0,160,160,0,160},{method="partition",output='mesh',outerRange=range})
        assert(mesh,a);mesh:translate(mesh.path:getVertexOffset());mesh:translate(100,100)
        mesh.fill={type='gradient',color1={1,0,0,1},color2={0,0,1,1},direction='right'}
        shader.attach(mesh,true);wait(2);shader.write(mesh,a,3)
        mesh.fill.effect.fillColor={1,1,1,.5};wait(1)
        local p=sample(140,180)
        assert(p.r>.1 and p.b>.1 and math.abs(p.r+p.b-.5)<.04,'textured fill/opacity')
        if previous then
            assert(math.abs(p.r-previous.r)<.01 and math.abs(p.b-previous.b)<.01,'AA padding changed material UV')
        end
        previous=p;display.remove(mesh)
    end
    print('SDF_PIXELS PASS 25 transform/metric cases, width/color/opacity, corners, neighboring AA, material UV')
end
