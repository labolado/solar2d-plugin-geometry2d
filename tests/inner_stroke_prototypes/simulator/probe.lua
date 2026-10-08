-- Capability probe only: four real edges, optional unused slots. No tiling yet.
local M={}
local registered={}
local root=system.pathForFile('',system.ResourceDirectory)
local G=assert(package.loadlib(root..'/../../../src/mac/build/Release/plugin_geometry2d.dylib','luaopen_plugin_geometry2d'))()
M.geometry=G
function M.create(k)
    local name='InnerPrototypeEdges'..k
    if not registered[k] then
    local extension={name=name}
    local vary,assign,eval={},{},{}
    for i=1,k do
        extension[#extension+1]={name='edge'..i,type='float',componentCount=4}
        vary[#vary+1]='varying P_POSITION vec4 vEdge'..i..';'
        assign[#assign+1]='vEdge'..i..'=CoronaEdge'..i..';'
        eval[#eval+1]='evaluate(vEdge'..i..',p,d,winding);'
    end
    graphics.defineVertexExtension(extension)
    local declaration=table.concat(vary,'\n')
    graphics.defineEffect{
        category='generator',group='innerPrototype',name='edges'..k,vertexExtension=name,
        uniformData={{name='params',type='vec4',index=0,default={4,0,1,0}}},
        vertex=declaration..[[
            P_POSITION vec2 VertexKernel(P_POSITION vec2 p) {
        ]]..table.concat(assign,'\n')..[[ return p; }]],
        fragment=declaration..[[
            uniform P_POSITION vec4 u_UserData0;
            void evaluate(P_POSITION vec4 e, P_POSITION vec2 p,
                inout P_POSITION float d, inout P_POSITION float winding) {
                P_POSITION vec2 v=e.zw-e.xy;
                P_POSITION float len2=dot(v,v);
                if(len2>0.0) {
                    P_POSITION float t=clamp(dot(p-e.xy,v)/len2,0.0,1.0);
                    d=min(d,length(p-e.xy-t*v));
                    P_POSITION float c=v.x*(p.y-e.y)-v.y*(p.x-e.x);
                    if(e.y<=p.y && e.w>p.y && c>0.0) winding+=1.0;
                    if(e.y>p.y && e.w<=p.y && c<0.0) winding-=1.0;
                }
            }
            P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
                P_POSITION vec2 p=uv*128.0-64.0;
                P_POSITION float d=10000.0,winding=0.0;
        ]]..table.concat(eval,'\n')..[[
                if(abs(winding)>0.5)d=-d;
                P_POSITION float footprint=max(fwidth(d),0.00001);
                P_COLOR float coverage=clamp(0.5-d/footprint,0.0,1.0);
                P_COLOR float line=u_UserData0.x>0.0 ? clamp(0.5+(d+u_UserData0.x)/footprint,0.0,1.0) : 0.0;
                return CoronaColorScale(vec4(mix(vec3(0,0,1),vec3(1,0,0),line),1.0)*coverage*u_UserData0.y*u_UserData0.z);
            }
        ]],
    }
    registered[k]=true
    end
    local mesh=display.newMesh{mode='indexed',vertices={-64,-64,64,-64,64,64,-64,64},
        uvs={0,0,1,0,1,1,0,1},indices={1,2,3,1,3,4}}
    mesh.x,mesh.y=320,240
    mesh.fillExtension=name;mesh.fill.effect='generator.innerPrototype.edges'..k
    mesh.fill.effect.params={4,0,1,0}
    return mesh
end
function M.write(mesh,k)
    local edges={{-40,-40,40,-40},{40,-40,40,40},{40,40,-40,40},{-40,40,-40,-40}}
    for j=1,k do
        -- Rotate a square's vertex pool so its first float4 is this edge.
        -- Duplicate real edges for eight-slot testing; winding remains nonzero.
        local points={}
        for n=0,3 do
            local e=edges[(j+n-1)%4+1]
            points[#points+1]=e[1];points[#points+1]=e[2]
        end
        local data=assert(G.util.meshFill(points,{aa='none',output='buffers'}))
        mesh.fillExtendedData:setAttributeValues('edge'..j,
            {buffer=data.vertices.buffer,count=4,componentCount=4,stride=0})
    end
    mesh.fill.effect.params={4,1,.5,0}
end
return M
