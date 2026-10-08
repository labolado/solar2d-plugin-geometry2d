return function(Ribbon,Reference)
    -- The legacy reference intentionally overlaps joins. Vertex ordering and
    -- topology no longer match. Report a symmetric nearest-vertex distance
    -- (not a silhouette/pixel error); assert parity of the two native modes.
    local function directedDistance(a,na,b,nb)
        local maximum=0
        for i=1,na do
            local nearest=math.huge
            for j=1,nb do
                local dx,dy=a[2*i-1]-b[2*j-1],a[2*i]-b[2*j]
                nearest=math.min(nearest,math.sqrt(dx*dx+dy*dy))
            end
            maximum=math.max(maximum,nearest)
        end
        return maximum
    end
    for name,points in pairs{
        straight={{0,0},{10,0},{100,0}},corner={{0,0},{60,0},{60,60}},
        reverse={{0,0},{80,0},{8,1}},crossing={{0,0},{80,80},{0,80},{80,0}},
    } do
        for _,aa in ipairs{0,2} do
            local r=Ribbon.new{width=20,aaWidth=aa,minDistance=5}
            local list=Ribbon.new{width=20,aaWidth=aa,minDistance=5,mode='triangles'}
            local ref=Reference.new(20,aa)
            for i,p in ipairs(points) do
                r:addPoint(p[1],p[2],i);ref:addPoint(p[1],p[2],i);list:addPoint(p[1],p[2],i)
            end
            ref:build();local data=assert(r:snapshot('table'))
            local expanded=assert(list:snapshot('table'))
            assert(expanded.logicalVertexCount==data.logicalIndexCount and expanded.indices==nil)
            for i=1,expanded.logicalVertexCount do
                local j=data.indices[i]
                assert(expanded.vertices[2*i-1]==data.vertices[2*j-1] and expanded.vertices[2*i]==data.vertices[2*j])
                assert(expanded.pathDistances[i]==data.pathDistances[j])
                assert(expanded.contourDistances[i]==data.contourDistances[j])
            end
            local maximum=math.max(directedDistance(ref.vertices,ref.logicalV,data.vertices,data.logicalVertexCount),
                directedDistance(data.vertices,data.logicalVertexCount,ref.vertices,ref.logicalV))
            print('RIBBON_GEOMETRY '..name..' aa='..aa..' nearest_vertex_delta='..maximum..
                ' legacy_triangles='..ref.logicalI/3 ..' native_triangles='..data.logicalTriangleCount)
            r:destroy();list:destroy()
        end
    end
end
