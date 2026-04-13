#version 330 core

uniform sampler2D image;
uniform vec2 resolution;

in vec4 out_color;
in vec2 out_uv;
in float out_cust1; // take_texel.
in float out_cust2; // blend_black.

out vec4 frag_color;

#define take_texel out_cust1
#define blend_black out_cust2

// The logic below was extracted from the project in: https://stackoverflow.com/questions/71988257/small-msdf-font-textures-have-artefacts
// Project: https://github.com/Blatko1/awesome-msdf
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float screenPxRange() {
    vec2 unitRange = vec2(6.0)/(textureSize(image,0) / 2);
    vec2 screenTexSize = vec2(1.0)/fwidth(out_uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

// Borrowed from: https://stackoverflow.com/questions/1506299/applying-brightness-and-contrast-with-opengl-es
vec4 adjust_brightness(vec4 color) {
    float bright = 1.25;
    vec4 luminance = vec4(1.0);
    float contrast = 1.0;
    return mix(color * bright, mix(luminance, color, contrast), 0.5);
}

//float thickness = 0.01; // Range: -0.03 < thickness < 0.03
float maxThickness = 0.01;

void main() {
#if 0 // For SDF
    vec4 texel = texture(image, out_uv);
    float dist = texel.r;
    if (dist <= 0.0001) {
        discard;
    }

    float thickness = maxThickness;

    float pxDist = screenPxRange() * (dist - 0.5 + thickness);
    float opacity = smoothstep(-0.5, 0.5, pxDist) * out_color.a;

    // If we want to give the text a slight 'glow' we can use this.
#if 0
    vec3 rgb = mix(out_color.rgb, out_color.rgb, opacity);
    float mu = smoothstep(0.5, 1.2, sqrt(dist));
    frag_color = vec4(rgb, max(opacity, mu) * out_color.a);
#endif

    frag_color = vec4(out_color.rgb, opacity);
#endif
    vec4 texel = texture(image, out_uv);
    bool sel = take_texel > 0 || (out_uv.x + out_uv.y) == 0;
    // Brighten the final result a bit for a more readable text (if we're selecting from the glyph texture).
    texel = mix(adjust_brightness(texel), texel, vec4(float(sel)));
    // See if we need to blend black color.
    bool adjust = blend_black > 0 && texel.rgb == vec3(0, 0, 0) && texel.a > 0;
    texel = mix(texel, vec4(out_color.rgb, texel.a), vec4(float(adjust)));
    texel = mix(texel * out_color, texel, vec4(float(take_texel)));
    frag_color = texel;
}