local name='generator.geometry2d.ribbonPrototype'
graphics.defineEffect{
    category='generator', group='geometry2d', name='ribbonPrototype',
    vertexData={
        {name='tailLength',default=0,min=-3e38,max=3e38,index=0},
        {name='headLength',default=1,min=-3e38,max=3e38,index=1},
    },
    fragment=[[
        P_COLOR vec4 FragmentKernel(P_UV vec2 uv) {
            P_UV float span = max(CoronaVertexUserData.y - CoronaVertexUserData.x, 0.00001);
            P_COLOR float fade = clamp((uv.x - CoronaVertexUserData.x) / span, 0.0, 1.0);
            P_UV float footprint = max(fwidth(uv.y), 0.00001);
            P_COLOR float coverage = clamp(1.0 - max(uv.y, 0.0) / footprint, 0.0, 1.0);
            return CoronaColorScale(vec4(fade * coverage));
        }
    ]],
}
return name
