-- Independent all-boundary CPU oracle. No tile candidate/sign records are used.
local M={}
local function distance(rings,x,y)
    local best,winding,nx,ny,ambiguous=math.huge,0,1,0,false
    for _,ring in ipairs(rings) do
        for i=1,#ring,2 do
            local j=(i+1)%#ring+1
            local ax,ay,bx,by=ring[i],ring[i+1],ring[j],ring[j+1]
            local vx,vy=bx-ax,by-ay
            local den=vx*vx+vy*vy
            local t=den>0 and math.max(0,math.min(1,((x-ax)*vx+(y-ay)*vy)/den)) or 0
            local dx,dy=x-ax-t*vx,y-ay-t*vy
            local candidate=math.sqrt(dx*dx+dy*dy)
            local gx,gy
            if candidate>1e-6 then gx,gy=dx/candidate,dy/candidate
            elseif den>0 then gx,gy=-vy/math.sqrt(den),vx/math.sqrt(den)
            else gx,gy=1,0 end
            if math.abs(candidate-best)<.0001 and math.abs(gx*nx+gy*ny)<.999 then ambiguous=true end
            if candidate<best then
                if candidate<best-.0001 then ambiguous=false end
                best,nx,ny=candidate,gx,gy
            end
            local cross=vx*(y-ay)-vy*(x-ax)
            if ay<=y and by>y and cross>0 then winding=winding+1 end
            if ay>y and by<=y and cross<0 then winding=winding-1 end
        end
    end
    return winding~=0 and -best or best,nx,ny,ambiguous
end
function M.run(mesh,info,wait,sample,name)
    local points={}
    -- Probe original boundaries, and both sides of tile edges including T junctions.
    for _,ring in ipairs(info.rings) do
        local step=2*math.max(1,math.ceil(#ring/48))
        for i=1,#ring,step do
            local j=(i+1)%#ring+1
            local x,y=(ring[i]+ring[j])*.5,(ring[i+1]+ring[j+1])*.5
            local dx,dy=ring[j]-ring[i],ring[j+1]-ring[i+1]
            local length=math.sqrt(dx*dx+dy*dy)
            if length>0 then
                for _,offset in ipairs{-16,-4,-2,0,2,4,16} do
                    points[#points+1]={x-dy/length*offset,y+dx/length*offset}
                end
            end
        end
    end
    local stride=math.max(1,math.floor(#info.tiles/96))
    for i=1,#info.tiles,stride do
        local b=info.tiles[i]
        points[#points+1]={b[1],(b[2]+b[4])*.5}
        points[#points+1]={(b[1]+b[3])*.5,b[2]}
        points[#points+1]={b[1],b[2]}
    end
    if #points>192 then
        local bounded={}
        for i=0,191 do bounded[#bounded+1]=points[1+math.floor(i*(#points-1)/191)] end
        points=bounded
    end
    -- Fixed screen probe from the pre-optimization mesh failure. Keep this
    -- independent of the current tile set, so changing subdivisions cannot
    -- silently remove the regression probe.
    if name=='47' then points[#points+1]={screenX=339,screenY=188} end
    local b=info.bounds
    local cx,cy=(b[1]+b[3])*.5,(b[2]+b[4])*.5
    local tested,solid,outside,stroke,transition,analytic,maxError=0,0,0,0,0,0,0
    local function clamp(x) return math.max(0,math.min(1,x)) end
    local variants={{.2,.2,0},{.2,.2,23,.37},{.5,.5,23},{1,1,0},{2,2,37},{5,5,0},{-.7,1.3,23}}
    -- Width changes ONLY the uniform. Geometry/attributes are not regenerated.
    for _,v in ipairs(variants) do
        mesh.x,mesh.y=320+(v[4] or 0),240+(v[4] or 0)
        mesh.xScale,mesh.yScale,mesh.rotation=v[1],v[2],v[3]
        for _,width in ipairs{0,1,4} do
            mesh.fill.effect.params={width,1,.5,0};wait(2)
            -- Conservative one-pixel footprint plus sample-position uncertainty.
            -- Transition tests use intervals, not an assumed exact GPU sample center.
            local radius=1.5*math.sqrt(1/v[1]^2+1/v[2]^2)
            for _,p in ipairs(points) do
                local x,y
                if p.screenX then x,y=p.screenX,p.screenY
                else x,y=mesh:localToContent(p[1]-cx,p[2]-cy) end
                for _,shift in ipairs{-1,0,1} do
                    local sx,sy=math.floor(x)+shift,math.floor(y)
                    if sx>2 and sx<637 and sy>2 and sy<477 then
                        local lx,ly=mesh:contentToLocal(sx,sy)
                        local d,nx,ny,ambiguous=distance(info.rings,lx+cx,ly+cy)
                        local c=sample(sx,sy);local alpha=c.r+c.b
                        local expected
                        if d>radius then expected='outside';outside=outside+1
                        elseif d< -radius then
                            if width==0 or d< -width-radius then expected='fill';solid=solid+1
                            elseif d> -width+radius then expected='stroke';stroke=stroke+1 end
                        end
                        local ok=alpha<=.54 and c.g<.02
                        if expected=='outside' then ok=ok and alpha<.04
                        elseif expected=='fill' then ok=ok and math.abs(c.b-.5)<.04 and c.r<.04
                        elseif expected=='stroke' then ok=ok and math.abs(c.r-.5)<.04 and c.b<.04
                        else transition=transition+1 end
                        if not ambiguous then
                            local ux,uy=mesh:contentToLocal(sx+1,sy)
                            local vx,vy=mesh:contentToLocal(sx,sy+1)
                            local fw=math.max(.00001,math.abs(nx*(ux-lx)+ny*(uy-ly))+math.abs(nx*(vx-lx)+ny*(vy-ly)))
                            local coverage=clamp(.5-d/fw)
                            local line=width>0 and clamp(.5+(d+width)/fw) or 0
                            local er,eb=.5*coverage*line,.5*coverage*(1-line)
                            local error=math.max(math.abs(c.r-er),math.abs(c.b-eb))
                            maxError=math.max(maxError,error);analytic=analytic+1
                            assert(error<.035,'ANALYTIC_FIRST_DIFFERENCE '..require('json').encode{
                                case=name,scale=v,strokeWidth=width,screen={sx,sy},distance=d,
                                footprint=fw,localPoint={lx+cx,ly+cy},expected={er,eb},color=c,error=error})
                        end
                        tested=tested+1
                        assert(ok,'COVERAGE_FIRST_DIFFERENCE '..require('json').encode{
                            case=name,scale=v,strokeWidth=width,screen={sx,sy},distance=d,
                            radius=radius,expected=expected or 'transition',color=c})
                    end
                end
            end
        end
    end
    assert(tested>0 and outside>0,'coverage probes missing '..name)
    print('TILED_COVERAGE '..require('json').encode{case=name,samples=tested,fill=solid,
        outside=outside,stroke=stroke,transition=transition,analytic=analytic,maxError=maxError})
end
return M
