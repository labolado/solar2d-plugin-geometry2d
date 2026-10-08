-- Test-only adaptation of application trail_renderer.lua:drawTrail() and
-- src/simulator_tests/trail_aa_direct/direct.lua (2026-09-14).
-- Keeps the same offset-line joins, reverseDot=-.9, boundary-normal AA and
-- packed alpha ramp, without the business project's class/queue dependencies.
local R={};R.__index=R
local function normalize(x,y)
    local length=math.sqrt(x*x+y*y)
    if length<1e-8 then return 0,0 end
    return x/length,y/length
end
local function trim(t,n) for i=#t,n+1,-1 do t[i]=nil end end
local bytes={};for i=0,255 do bytes[i]=string.char(255,255,255,i) end
function R.new(width,aaWidth)
    return setmetatable({points={},width=width,aaWidth=aaWidth,dirty=true,
        vertices={},indices={},distances={},alphas={},colors={},normals={},edges={},outer={},
        vcap=16,tcap=8,creates=0,generationMS=0,updateMS=0},R)
end
function R:addPoint(x,y,time)
    local p=self.points[#self.points]
    local distance=p and math.sqrt((x-p[1])^2+(y-p[2])^2) or 0
    if p and distance<=5 then return false end
    self.points[#self.points+1]={x,y,time,(p and p[4] or 0)+distance}
    self.dirty=true;return true
end
function R:expire(now,age)
    local removed=0
    while self.points[1] and self.points[1][3]<=now-age do
        table.remove(self.points,1);removed=removed+1
    end
    self.dirty=self.dirty or removed>0
    return removed
end
function R:build()
    local v,idx,s,a=self.vertices,self.indices,self.distances,self.alphas
    trim(v,0);trim(idx,0);trim(s,0);trim(a,0)
    local points=self.points
    if #points<2 then self.logicalV,self.logicalI=0,0;return end
    local function vertex(x,y,d)
        local id=#s+1;v[id*2-1],v[id*2],s[id]=x,y,d;return id
    end
    local function pair(p,dx,dy)
        return vertex(p[1]-dy*self.width/2,p[2]+dx*self.width/2,p[4]),
            vertex(p[1]+dy*self.width/2,p[2]-dx*self.width/2,p[4])
    end
    local function quad(l,r,c,d)
        local i=#idx;idx[i+1],idx[i+2],idx[i+3]=l,c,r
        idx[i+4],idx[i+5],idx[i+6]=r,c,d
    end
    local dx,dy=normalize(points[2][1]-points[1][1],points[2][2]-points[1][2])
    local l,r=pair(points[1],dx,dy)
    for i=2,#points do
        local p,n=points[i],points[i+1]
        local nx,ny=dx,dy
        if n then nx,ny=normalize(n[1]-p[1],n[2]-p[2]) end
        if n and dx*nx+dy*ny<-.9 then
            local c,d=pair(p,dx,dy);quad(l,r,c,d);l,r=pair(p,nx,ny)
        else
            local tx,ty=normalize(dx+nx,dy+ny)
            local function intersection(id,cx,cy)
                local x,y=v[id*2-1],v[id*2]
                local denominator=cy*dx-cx*dy
                local t=(cx*(y-p[2])-cy*(x-p[1]))/denominator
                return vertex(x+t*dx,y+t*dy,p[4])
            end
            local c,d=intersection(l,-ty,tx),intersection(r,ty,-tx)
            quad(l,r,c,d);l,r=c,d
        end
        dx,dy=nx,ny
    end
    local total=points[#points][4]-points[1][4]
    for i,d in ipairs(s) do a[i]=(d-points[1][4])/total end
    local coreV,coreI=#s,#idx
    self.coreV,self.coreI=coreV,coreI
    if self.aaWidth>0 then
        local normals,edges,outer=self.normals,self.edges,self.outer
        for i=1,coreV*4 do normals[i]=0 end
        trim(edges,0);trim(outer,0)
        local function edge(aid,bid,inside)
            local ax,ay=v[aid*2-1],v[aid*2]
            local ex,ey=v[bid*2-1]-ax,v[bid*2]-ay
            local nx,ny=normalize(ey,-ex)
            if nx==0 and ny==0 then return end
            if nx*(v[inside*2-1]-ax)+ny*(v[inside*2]-ay)>0 then nx,ny=-nx,-ny end
            for _,id in ipairs{aid,bid} do
                local k=(id-1)*4
                if normals[k+1]==0 and normals[k+2]==0 then normals[k+3],normals[k+4]=nx,ny end
                normals[k+1],normals[k+2]=normals[k+1]+nx,normals[k+2]+ny
            end
            edges[#edges+1]=aid;edges[#edges+1]=bid
        end
        local pc,pd
        for i=1,coreI,6 do
            local a0,c,b,d=idx[i],idx[i+1],idx[i+2],idx[i+5]
            edge(a0,c,b)
            if pc~=a0 or pd~=b then edge(b,a0,c) end
            if idx[i+6]~=c or idx[i+8]~=d then edge(c,d,b) end
            edge(d,b,c);pc,pd=c,d
        end
        for id=1,coreV do
            local k=(id-1)*4
            local nx,ny=normals[k+3],normals[k+4]
            if nx~=0 or ny~=0 then
                local sx,sy=normalize(normals[k+1],normals[k+2])
                if sx==0 and sy==0 then sx,sy=nx,ny end
                local distance=self.aaWidth/math.max(.5,sx*nx+sy*ny)
                local o=vertex(v[id*2-1]+sx*distance,v[id*2]+sy*distance,s[id])
                outer[id]=o;a[o]=0
            end
        end
        for i=1,#edges,2 do
            local a0,b=edges[i],edges[i+1];local oa,ob=outer[a0],outer[b]
            local k=#idx;idx[k+1],idx[k+2],idx[k+3]=a0,b,oa
            idx[k+4],idx[k+5],idx[k+6]=b,ob,oa
        end
    end
    self.logicalV,self.logicalI=#s,#idx
    while self.vcap<#s do self.vcap=self.vcap*2 end
    while self.tcap*3<#idx do self.tcap=self.tcap*2 end
    for i=#s+1,self.vcap do v[i*2-1],v[i*2],s[i],a[i]=v[1],v[2],s[1],0 end
    for i=#idx+1,self.tcap*3 do idx[i]=1 end
    local colors=self.colors
    for i=1,self.vcap do colors[i]=bytes[math.max(0,math.min(255,math.floor(a[i]*255+.5)))] end
    trim(colors,self.vcap)
end
function R:update(group)
    if not self.dirty then return end
    local start=system.getTimer();self:build();self.generationMS=self.generationMS+system.getTimer()-start
    start=system.getTimer()
    if self.logicalV==0 then display.remove(self.mesh);self.mesh=nil
    else
        local data={mode='indexed',vertices=self.vertices,indices=self.indices,
            fillVertexColors={buffer=table.concat(self.colors),count=self.vcap}}
        if self.mesh and self.lastV==self.vcap and self.lastI==#self.indices then self.mesh.path:update(data)
        else
            display.remove(self.mesh);data.parent=group;self.mesh=display.newMesh(data);self.creates=self.creates+1
        end
        self.mesh.x,self.mesh.y=self.mesh.path:getVertexOffset()
        self.mesh:setFillColor(.3,.8,1)
        self.lastV,self.lastI=self.vcap,#self.indices
    end
    self.updateMS=self.updateMS+system.getTimer()-start;self.dirty=false
end
return R
