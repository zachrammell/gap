#include <metal_stdlib>
using namespace metal;

struct Uniforms
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

struct CPU2Vertex
{
  float2 pos;
  float4 color;
  float2 uv;
  float cust1;
  float cust2;
};

struct Vertex2Fragment
{
  float4 pos [[position]];
  half4 color;
  float2 uv;
  half4 premul_color;
  float cust1 [[flat]];
  float cust2 [[flat]];
};

vertex Vertex2Fragment
vs_main(const ushort vert_id [[vertex_id]],
        const ushort inst_id [[instance_id]],
        constant Uniforms &uniforms [[buffer(0)]], 
        const device CPU2Vertex *verts [[buffer(1)]])
{
  CPU2Vertex c2v = verts[vert_id];

  float2 scale = uniforms.camera_coord_factor * uniforms.camera_scale.x / uniforms.resolution;
  float2 offset = -scale * uniforms.camera_pos;
  float2 pixel_size = uniforms.camera_coord_factor / uniforms.resolution;
  offset = round(offset/pixel_size)*pixel_size;
  float2 pos = scale*c2v.pos + offset;

  Vertex2Fragment v2f;
  v2f.pos = float4(pos, 0, 1);
  v2f.color = half4(c2v.color);
  v2f.uv = c2v.uv;
  v2f.premul_color = half4(float4(c2v.color.rgb * c2v.color.a, c2v.color.a));
  v2f.cust1 = c2v.cust1;
  v2f.cust2 = c2v.cust2;
  return v2f;
}

// Borrowed from: https://stackoverflow.com/questions/1506299/applying-brightness-and-contrast-with-opengl-es
half4 adjust_brightness(half4 color)
{
  half bright = 1.25;
  half4 luminance = half4(1.0, 1.0, 1.0, 1.0);
  half contrast = 1.0;
  return mix(color * bright, mix(luminance, color, contrast), 0.5);
}

fragment half4
fs_image(Vertex2Fragment v2f [[stage_in]])
{
  return half4(1, 0, 0, 1);
}

fragment half4
fs_text(Vertex2Fragment v2f [[stage_in]],
        texture2d<half> texture [[texture(0)]],
        sampler tex_sampler [[sampler(0)]])
{
  half4 texel = texture.sample(tex_sampler, v2f.uv);
  bool sel = v2f.cust1 > 0 || (v2f.uv.x + v2f.uv.y) == 0;
  //- brt: brighten the final result a bit for a more readable text (if we're selecting from the glyph texture)
  texel = mix(adjust_brightness(texel), texel, half(sel));
  //- brt: see if we need to blend black color
  bool adjust = (v2f.cust2 > 0) && all(texel.rgb == half3(0, 0, 0)) && (texel.w > 0.0);
  texel = mix(texel, half4(v2f.color.rgb, texel.a), half(adjust));
  texel = mix(texel * v2f.color, texel, half(v2f.cust1));
  return texel;
}

fragment half4
fs_text_subpixel(Vertex2Fragment v2f [[stage_in]])
{
  return half4(1, 0, 0, 1);
}

fragment half4
fs_blur_horiz(Vertex2Fragment v2f [[stage_in]])
{
  return half4(1, 0, 0, 1);
}

