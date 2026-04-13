cbuffer Uniforms : register(b0)
{
    float2 resolution;
    float time;
    float camera_coord_factor;
    float2 camera_scale;
    float2 camera_pos;
    float2 custom_vec2_value1;
    float2 custom_vec2_value2;
    float2 custom_vec2_value3;
};

struct PS_INPUT
{
    float4 pos                  : SV_POSITION;
    float4 color                : COL0;
    float2 uv                   : TEX;
    float4 premul_color         : COL1;
    nointerpolation float cust1 : CUST0; // take_texel.
    nointerpolation float cust2 : CUST1; // blend_black.
};

#define take_texel input.cust1
#define blend_black input.cust2

sampler sampler0;
Texture2D texture0;

// Borrowed from: https://stackoverflow.com/questions/1506299/applying-brightness-and-contrast-with-opengl-es
float4 adjust_brightness(float4 color)
{
    float bright = 1.25;
    float4 luminance = float4(1.0, 1.0, 1.0, 1.0);
    float contrast = 1.0;
    return lerp(color * bright, lerp(luminance, color, contrast), 0.5);
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 texel = texture0.Sample(sampler0, input.uv);
    bool sel = take_texel > 0 || (input.uv.x + input.uv.y) == 0;
    // Brighten the final result a bit for a more readable text (if we're selecting from the glyph texture).
    texel = lerp(adjust_brightness(texel), texel, float4(float(sel), float(sel), float(sel), float(sel)));
    // See if we need to blend black color.
    bool adjust = (blend_black > 0) && (texel.rgb == float3(0, 0, 0)) && (texel.w > 0.0);
    texel = lerp(texel, float4(input.color.rgb, texel.a), float4(float(adjust), float(adjust), float(adjust), float(adjust)));
    texel = lerp(texel * input.color, texel, float4(float(take_texel), float(take_texel), float(take_texel), float(take_texel)));
    return texel;
}