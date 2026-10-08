return function(G)
    local json=require('json')
    local fixtures={
        convex={{{0,0,200,0,200,200,0,200}}},
        concave={{{0,0,200,0,200,80,80,80,80,200,0,200}}},
        thin={{{0,0,200,0,200,3,0,3}}},
        sharp={{{0,0,200,0,1,2}}},
        hole={{{0,0,200,0,200,200,0,200},{60,60,140,60,140,140,60,140}}},
        tinyHole={{{0,0,200,0,200,200,0,200},{99,99,101,99,101,101,99,101}}},
        island={{{0,0,200,0,200,200,0,200},{60,60,140,60,140,140,60,140}},{{70,70,130,70,130,130,70,130}}},
        nearRegions={{{0,0,99,0,99,200,0,200}},{{100,0,200,0,200,200,100,200}}},
        slit={{{0,0,200,0,200,200,90,200,90,80,80,80,80,200,0,200}}},
        short={{{0,0,200,0,200,80,80,80,80,82,0,82}}},
        splitCore={{{0,0,60,0,60,28,100,28,100,0,160,0,160,60,100,60,100,32,60,32,60,60,0,60}}},
    }
    local star={}
    for i=0,39 do local r=i%2==0 and 95 or 55;local a=i*math.pi/20
        star[#star+1]=100+r*math.cos(a);star[#star+1]=100+r*math.sin(a)
    end
    fixtures.saw={{star}}
    local function exact(groups,x,y)
        local closest,inside=math.huge,false
        for _,group in ipairs(groups) do
            local groupInside=false
            for _,ring in ipairs(group) do
                local inRing=false
                for i=1,#ring,2 do
                    local j=(i+1)%#ring+1
                    local ax,ay,bx,by=ring[i],ring[i+1],ring[j],ring[j+1]
                    local dx,dy=bx-ax,by-ay
                    local t=math.max(0,math.min(1,((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy)))
                    closest=math.min(closest,math.sqrt((x-ax-t*dx)^2+(y-ay-t*dy)^2))
                    if (ay>y)~=(by>y) and x<(bx-ax)*(y-ay)/(by-ay)+ax then inRing=not inRing end
                end
                groupInside=groupInside~=inRing
            end
            inside=inside or groupInside
        end
        return inside and -closest or closest
    end
    local function interpolate(m,k,x,y)
        local a,b,c=m.indices[k],m.indices[k+1],m.indices[k+2]
        local v=m.vertices
        local ax,ay,bx,by,cx,cy=v[2*a-1],v[2*a],v[2*b-1],v[2*b],v[2*c-1],v[2*c]
        local det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
        assert(det>0,'flipped/degenerate triangle')
        local u=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/det
        local w=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/det
        return u,w,1-u-w,u*m.distances[a]+w*m.distances[b]+(1-u-w)*m.distances[c]
    end
    local total,worst=0,0
    for name,groups in pairs(fixtures) do
        local before=json.encode(groups)
        local m,err=G.util.meshDistanceGroups(groups,{method="partition",innerRange=8,outerRange=4})
        assert(m,name..': '..tostring(err));assert(before==json.encode(groups),'input mutated')
        assert(m.kind=='distance' and m.method=='partition' and #m.uvs==#m.vertices and #m.distances*2==#m.vertices)
        for i=1,#m.distances do
            assert(m.distances[i]>=-8 and m.distances[i]<=4)
            local b=m.uvBounds
            assert(math.abs(b[1]+m.uvs[2*i-1]*(b[3]-b[1])-m.vertices[2*i-1])<.0001)
            assert(math.abs(b[2]+m.uvs[2*i]*(b[4]-b[2])-m.vertices[2*i])<.0001)
        end
        for k=1,#m.indices,3 do
            local x,y=0,0
            for j=k,k+2 do local i=m.indices[j];x=x+m.vertices[2*i-1]/3;y=y+m.vertices[2*i]/3 end
            local _,_,_,d=interpolate(m,k,x,y)
            local target=math.max(-8,exact(groups,x,y))
            local e=math.abs(d-target);worst=math.max(worst,e)
            if e>=.101 then
                local v={}
                for j=k,k+2 do local id=m.indices[j];v[#v+1]={m.vertices[2*id-1],m.vertices[2*id],m.distances[id]} end
                print('SDF_BAD_TRI '..json.encode(v))
            end
            assert(e<.101,name..' interpolation '..e..' x='..x..' y='..y..' d='..d..' expected='..target..' triangle='..k)
        end
        for x=-2.371,202,5.137 do for y=-2.193,202,5.311 do
            local hits=0
            for k=1,#m.indices,3 do
                local a,b,c,d=interpolate(m,k,x,y)
                if a>1e-7 and b>1e-7 and c>1e-7 then
                    hits=hits+1
                    local e=math.abs(d-math.max(-8,exact(groups,x,y)))
                    worst=math.max(worst,e)
                    assert(e<.101)
                end
            end
            assert(hits<=1,name..' duplicate coverage')
            if exact(groups,x,y)<3.8 then assert(hits==1,name..' missing coverage') end
            total=total+1
        end end
        local tri=assert(G.util.meshDistanceGroups(groups,{method="partition",innerRange=8,outerRange=4,mode='triangles'}))
        assert(#tri.vertices==#m.indices*2 and tri.indices==nil)
        for i,id in ipairs(m.indices) do
            assert(tri.vertices[2*i-1]==m.vertices[2*id-1] and tri.vertices[2*i]==m.vertices[2*id])
            assert(tri.distances[i]==m.distances[id])
        end
        print('SDF_GEOMETRY '..name..' '..json.encode(m.stats))
    end
    local poly={0,0,100,0,100,100,0,100}
    for _,bad in ipairs{{distance=5},{distanceSign='outsideNegative'},{join='round'},{innerRange=-1},{outerRange=0/0},{distanceTolerance=1e-6},{distanceTransform={1,0,0,1,0,0,extra=1}}} do
        bad.method='partition'
        assert(not pcall(G.util.meshDistance,poly,bad),'invalid option accepted')
    end
    for _,options in ipairs{{maxWork=1},{maxVertices=3},{distanceTransform={0,0,0,0,0,0}}} do
        options.method='partition'
        local ok,m,err=pcall(G.util.meshDistance,poly,options)
        assert(ok and m==nil and type(err)=='string','geometry error contract')
    end
    assert(not pcall(G.util.meshDistance,{0,0,100,0,0,100,extra=1}))
    local invalid,reason=G.util.meshDistance({0,0,10,0,20,0})
    assert(invalid==nil and type(reason)=='string')
    for _,short in ipairs{{},{0,0},{0,0,1,1}} do
        local ok,m,err=pcall(G.util.meshDistance,short)
        assert(ok and m==nil and type(err)=='string')
    end
    assert(not pcall(G.util.meshDistance,{0,0,1}))
    assert(not pcall(G.util.meshDistance,{poly=poly,holes='invalid'}))
    invalid,reason=G.util.meshDistance({poly=poly,holes={{110,110,120,110,120,120}}})
    assert(invalid==nil and type(reason)=='string')
    local fine,why=G.util.meshDistance({0,0,2,0,2,2,0,2},
        {method="partition",innerRange=.1,outerRange=.1,distanceTolerance=.0001})
    assert(fine,why)
    print('SDF_CONTRACTS PASS samples='..total..' max_distance_error='..worst)
end
