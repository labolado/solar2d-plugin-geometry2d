-- Experimental shared eleven-edge slots plus one metadata attribute.
-- No installation, data texture, stencil, or production API change.
local M={}
local registered=false
local function register()
    if registered then return end
    local ext={name='PrototypeTiled11'}
    local declarations={'varying P_POSITION vec4 vMeta;'}
    local assign={'vMeta=CoronaTileMeta;'}
    local steps={}
    for i=1,11 do
        ext[#ext+1]={name='tileEdge'..i,type='float',componentCount=4}
        declarations[#declarations+1]='varying P_POSITION vec4 vE'..i..';'
        assign[#assign+1]='vE'..i..'=CoronaTileEdge'..i..';'
        steps[#steps+1]='edge(vE'..i..','..(i-1)..'.0,p,d,winding,normal);'
    end
    ext[#ext+1]={name='tileMeta',type='float',componentCount=4}
    graphics.defineVertexExtension(ext)
    local decl=table.concat(declarations,'\n')
    graphics.defineEffect{
        category='generator',group='innerPrototype',name='tiled11',vertexExtension=ext.name,
        uniformData={{name='params',type='vec4',index=0,default={4,0,.5,0}},
            {name='bounds',type='vec4',index=1,default={0,0,1,1}}},
        vertex=decl..'\nP_POSITION vec2 VertexKernel(P_POSITION vec2 p) {'..table.concat(assign,'\n')..'return p;}',
        fragment=decl..[[
            uniform P_POSITION vec4 u_UserData0;
            uniform P_POSITION vec4 u_UserData1;
            void edge(P_POSITION vec4 e, P_POSITION float index, P_POSITION vec2 p,
                inout P_POSITION float d, inout P_POSITION float winding,
                inout P_POSITION vec2 normal) {
                P_POSITION vec2 v=e.zw-e.xy;
                // Integer metadata travels through interpolated float varyings.
                // Reconstruct it before comparisons (N + epsilon is not N).
                P_POSITION float distanceCount=floor(vMeta.x+.5);
                P_POSITION float signCount=floor(vMeta.y+.5);
                if(index<distanceCount) {
                    P_POSITION float den=dot(v,v);
                    if(den>0.0) {
                        P_POSITION vec2 delta=p-e.xy-clamp(dot(p-e.xy,v)/den,0.0,1.0)*v;
                        P_POSITION float candidate=length(delta);
                        if(candidate<d) {
                            d=candidate;
                            normal=candidate>.000001?delta/candidate:vec2(-v.y,v.x)/sqrt(den);
                        }
                    }
                } else if(index<distanceCount+signCount) {
                    P_POSITION float c=v.x*(p.y-e.y)-v.y*(p.x-e.x);
                    if(e.y<=p.y && e.w>p.y && c>0.0)winding+=1.0;
                    if(e.y>p.y && e.w<=p.y && c<0.0)winding-=1.0;
                }
            }
            P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
                P_POSITION vec2 p=u_UserData1.xy+uv*u_UserData1.zw;
                P_POSITION float d=10000.0,winding=0.0;
                P_POSITION vec2 normal=vec2(1,0);
        ]]..table.concat(steps,'\n')..[[
                if(abs(winding)>.5 || vMeta.z>1.5)d=-d;
                if(vMeta.z>.5 && vMeta.z<1.5)d=-10000.0;
                // Differentiate the continuous coordinates, NOT clipped winding.
                // Helper fragments can lie outside a tile's clipped sign polygon.
                P_POSITION float fw=max(abs(dot(normal,dFdx(p)))+abs(dot(normal,dFdy(p))),.00001);
                P_COLOR float coverage=clamp(.5-d/fw,0.0,1.0);
                P_COLOR float line=u_UserData0.x>0.0?clamp(.5+(d+u_UserData0.x)/fw,0.0,1.0):0.0;
                return CoronaColorScale(vec4(mix(vec3(0,0,1),vec3(1,0,0),line),1)*coverage*u_UserData0.y*u_UserData0.z);
            }
        ]],
    }
    registered=true
end
function M.create(dir,name)
    register()
    local loader=assert(package.loadlib(dir..'/buffers.dylib','luaopen_plugin_geometry2d'))()
    local function read(suffix)
        local f=assert(io.open(dir..'/'..name..suffix,'rb'));local s=f:read('*a');f:close();return s
    end
    local info=require('json').decode(read('.json'))
    local p,u,idx,attr=read('-positions.bin'),read('-uvs.bin'),read('-indices.bin'),read('-attributes.bin')
    assert(#p==info.vertices*8 and #u==#p and #idx==info.indices*2 and #attr==info.vertices*192)
    local mesh=display.newMesh{mode='indexed',
        vertices={buffer=loader.bytes(p),count=info.vertices},uvs={buffer=loader.bytes(u),count=info.vertices},
        indices={buffer=loader.bytes(idx),count=info.indices},zeroBasedIndices=true}
    mesh.fillExtension='PrototypeTiled11';mesh.fill.effect='generator.innerPrototype.tiled11'
    local b=info.bounds
    mesh.fill.effect.bounds={b[1],b[2],b[3]-b[1],b[4]-b[2]}
    mesh.fill.effect.params={4,0,.5,0}
    local bytes=loader.bytes(attr)
    return mesh,info,function()
        for i=1,12 do
            mesh.fillExtendedData:setAttributeValues(i==12 and 'tileMeta' or 'tileEdge'..i,
                {buffer=bytes,count=info.vertices,componentCount=4,stride=192,offset=(i-1)*16})
        end
        mesh.fill.effect.params={4,1,.5,0}
    end
end
return M
