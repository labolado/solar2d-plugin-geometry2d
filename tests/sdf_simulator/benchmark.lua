return function(G,wait)
    local json=require('json')
    local poly={-12,-12,12,-12,12,0,0,0,0,12,-12,12}
    graphics.defineEffect{
        category='generator',group='geometry2d',name='sdfBenchmark',
        fragment=[[
            P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
                P_UV float coverage=clamp(0.5-uv.x/max(fwidth(uv.x),0.00001),0.0,1.0);
                return CoronaColorScale(vec4(0.5*coverage));
            }
        ]],
    }
    display.enableStatistics(true)
    local function build(old,output)
        if old then return assert(G.util.meshFill(poly,{aaWidth=2,join='round',output=output})) end
        return assert(G.util.meshDistance(poly,{method="partition",innerRange=8,outerRange=2,output=output}))
    end
    local function metric(t)
        local sum=0;for _,v in ipairs(t) do sum=sum+v end
        table.sort(t);return {mean=sum/#t,p95=t[math.ceil(#t*.95)],samples=#t}
    end
    for _,old in ipairs{true,false} do
        for _,output in ipairs{'table','buffers'} do
            collectgarbage('collect')
            local base=collectgarbage('count')
            local times,first={},0
            for i=1,31 do
                local start=system.getTimer();local data=build(old,output)
                local dt=system.getTimer()-start
                if i==1 then first=dt else times[#times+1]=dt end
                assert(data.vertices)
            end
            print('SDF_REBUILD '..json.encode{version=old and 1 or 2,output=output,first_ms=first,
                repeated_ms=metric(times),lua_uncollected_delta_kb=collectgarbage('count')-base})
        end
        for _,count in ipairs{32,128,512} do
            collectgarbage('collect');local base=collectgarbage('count')
            local data=build(old,'table')
            local uv={}
            for i=1,#data.vertices/2 do
                -- V1 has no interior distances. Use its old opaque-body ramp.
                uv[2*i-1]=old and ((1-data.alphas[i])*2-.5) or data.distances[i]
                uv[2*i]=0
            end
            local group=display.newGroup();local start=system.getTimer()
            for i=1,count do
                local mesh=display.newMesh{parent=group,mode='indexed',vertices=data.vertices,indices=data.indices,uvs=uv}
                mesh.x=20+(i-1)%24*25;mesh.y=20+math.floor((i-1)/24)%17*25
                mesh.fill.effect='generator.geometry2d.sdfBenchmark'
            end
            local upload=system.getTimer()-start
            wait(15)
            local intervals,draws,triangles={},0,0
            local prev=system.getTimer()
            for _=1,60 do
                wait(1);local now=system.getTimer();intervals[#intervals+1]=now-prev;prev=now
                local s={};display.getStatistics(s);draws=draws+s.drawCallCount;triangles=triangles+s.triangleCount
            end
            local frame=metric(intervals)
            print('SDF_RENDER '..json.encode{version=old and 1 or 2,count=count,fps=1000/frame.mean,
                frame_ms=frame,draw_calls=draws/60,renderer_triangles=triangles/60,
                mesh_creates=count,measured_rebuilds=0,create_upload_ms=upload,
                pool_vertices=#data.vertices/2,mesh_triangles=#data.indices/3,
                lua_delta_kb=collectgarbage('count')-base,
                gpu_ms='unavailable; frame timing is CPU/present, not a GPU timer'})
            display.remove(group);wait(2)
        end
    end
end
