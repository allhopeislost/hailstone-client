#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

// The depth pre-pass (render/zone_renderer.cpp draw_depth): the opaque and alpha-tested zone geometry into the scene's
// depth buffer, with no colour. The alpha test is the colour pass's (coverage.glsl), so an alpha-tested edge covers
// the same samples in both passes.

#include "coverage.glsl"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(push_constant) uniform Push {
    uvec4 tex;
    uvec4 tex2;   // slot 4, mode, flags, a texture pack's maps
    vec4 params;  // alpha_ref, opacity, coverage scale
    vec4 slide;
    vec4 extra;
    vec4 extra2;
    vec4 extra3;
    vec4 extra4;
} pc;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color; // its alpha alone counts: alpha-to-coverage reads it

// zone.frag's effects whose alpha is 1 whatever their texture's (render/zone_renderer.cpp Mode).
const uint kModeLava = 3u;
const uint kModeRegionTerrain = 4u;

void main() {
    // The alpha zone.frag tests, for the effects the opaque pass draws (render/material_state.h): 1 for the lava and
    // the region terrain, else the texture's. The waterfall, the water and the detail pass, whose alpha is another,
    // are blended and never come here. Without an alpha test (reference 0) the alpha decides nothing: not read.
    uint mode = pc.tex2.y;
    float alpha = 1.0;
    if (pc.params.x > 0.0 && mode != kModeLava && mode != kModeRegionTerrain)
        alpha = texture(textures[nonuniformEXT(pc.tex.x)], v_uv).a;
    float out_alpha;
    if (!alpha_test(alpha, pc.params.x, pc.params.y, out_alpha)) discard;
    o_color = vec4(0.0, 0.0, 0.0, out_alpha);
}
