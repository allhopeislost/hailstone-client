#include <doctest/doctest.h>

#include "assets/model.h"
#include "render/material_state.h"

using namespace hailstone;

// The depth pre-pass draws the opaque pass's materials and takes its alpha from the texture, or 1
// (shaders/zone_depth.frag). The effects whose alpha zone.frag computes another way must stay out of that pass.
TEST_CASE("depth pre-pass: the effects with an alpha of their own are not in the opaque pass") {
    using assets::EqgShader;
    for (EqgShader shader : {EqgShader::Water, EqgShader::LavaH, EqgShader::WaterFall, EqgShader::WldDetail}) {
        assets::Material m;
        m.eqg_shader = shader;
        CHECK(render::material_state(m).pass != 0);
    }
}

TEST_CASE("depth pre-pass: the effects it draws are in the opaque pass") {
    using assets::EqgShader;
    for (EqgShader shader : {EqgShader::None, EqgShader::Chroma, EqgShader::Terrain, EqgShader::Lava, EqgShader::Lava2}) {
        assets::Material m;
        m.eqg_shader = shader;
        CHECK(render::material_state(m).pass == 0);
    }
}
