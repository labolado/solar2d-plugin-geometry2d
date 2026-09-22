-- SDF v2: UVs remain available for material/painting textures.
-- Attach the extension immediately, keep ready=0 until the first completed
-- render, bulk-write distances, then ready=1. No white/incorrect first frame.
local M = {name='generator.geometry2d.innerStroke',
    textureName='filter.geometry2d.innerStrokeTexture',extension='Geometry2DSDFv2'}
graphics.defineVertexExtension{
    name=M.extension, {name='signedDistance',type='float',componentCount=1},
}
local kernel={
    category='generator',group='geometry2d',name='innerStroke',
    vertexExtension=M.extension,
    uniformData={
        {name='fillColor',type='vec4',default={.2,.6,1,.5},index=0},
        {name='strokeColor',type='vec4',default={1,.2,.1,.5},index=1},
        {name='params',type='vec4',default={3,0,8,1},index=2}, -- width, ready, innerRange (-1 for fillAA), opacity
    },
    vertex=[[
        varying P_POSITION float v_Distance;
        P_POSITION vec2 VertexKernel(P_POSITION vec2 p) {
            v_Distance = CoronaSignedDistance;
            return p;
        }
    ]],
    fragment=[[
        varying P_POSITION float v_Distance;
        uniform P_COLOR vec4 u_UserData0;
        uniform P_COLOR vec4 u_UserData1;
        uniform P_POSITION vec4 u_UserData2;
        P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
            P_POSITION float d = v_Distance;
            P_POSITION float footprint = max(fwidth(d), 0.00001);
            P_POSITION float width = u_UserData2.x;
            // Negative params.z selects fillAA: the body is d=0, fully covered.
            // Its transition is exterior-only, not the centered SDF transition.
            bool exteriorOnly = u_UserData2.z < 0.0;
            P_COLOR float coverage = clamp((exteriorOnly ? 1.0 : 0.5) - d / footprint, 0.0, 1.0);
            P_COLOR float stroke = !exteriorOnly && width > 0.0 ?
                clamp(0.5 + (d + width) / footprint, 0.0, 1.0) : 0.0;
            if (!exteriorOnly && d <= -u_UserData2.z + 0.00001) stroke = 0.0;
            // Mix premultiplied colors ONCE, including differing opacities.
            P_COLOR vec4 fill = vec4(u_UserData0.rgb * u_UserData0.a, u_UserData0.a);
            P_COLOR vec4 line = vec4(u_UserData1.rgb * u_UserData1.a, u_UserData1.a);
            return CoronaColorScale(mix(fill,line,stroke) * coverage * u_UserData2.y * u_UserData2.w);
        }
    ]],
}
graphics.defineEffect(kernel)
kernel.category='filter';kernel.name='innerStrokeTexture'
kernel.fragment=kernel.fragment:gsub('P_COLOR vec4 line =','fill *= texture2D(CoronaSampler0, uv);\n            P_COLOR vec4 line =')
graphics.defineEffect(kernel)

function M.attach(mesh,textured)
    mesh.fillExtension=M.extension
    mesh.fill.effect=textured and M.textureName or M.name
    mesh.fill.effect.params={3,0,8,1}
end
function M.write(mesh,attributes,width,opacity)
    assert(attributes.kind=='distance','meshDistance attributes required; fill uses ordinary paint')
    local exteriorOnly=attributes.innerRange==0
    if exteriorOnly then
        assert(width==nil or width==0,'fillAA does not support an inner stroke')
        width=0
    end
    mesh.fillExtendedData:setAttributeValues('signedDistance',attributes.distances)
    mesh.fill.effect.params={width or 3,1,exteriorOnly and -1 or attributes.innerRange,opacity or 1}
end
return M
