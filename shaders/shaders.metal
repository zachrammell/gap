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
  packed_float2 pos;
  packed_float4 color;
  packed_float2 uv;
  float cust1;
  float cust2;
};

struct Vertex2Fragment
{
  float4 pos [[position]];
  half4 color;
  half4 premul_color;
  float2 uv;
  float cust1 [[flat]];
  float cust2 [[flat]];
};

vertex Vertex2Fragment
vs_main(const uint vert_id [[vertex_id]],
        const uint inst_id [[instance_id]],
        constant Uniforms &uniforms [[buffer(0)]], 
        const device CPU2Vertex *verts [[buffer(1)]])
{
  CPU2Vertex c2v = verts[vert_id];

#if 0
  float2 scale = uniforms.camera_coord_factor * uniforms.camera_scale.x / uniforms.resolution;
  float2 offset = -scale * uniforms.camera_pos;
  float2 pixel_size = uniforms.camera_coord_factor / uniforms.resolution;
  offset = round(offset/pixel_size)*pixel_size;
  float2 pos = scale*c2v.pos + offset;
#elif 1
  float2 scale = uniforms.camera_coord_factor * (1.0 / uniforms.resolution);
  float2 pos = floor(c2v.pos) * scale - 1.0;
#else
#endif

  Vertex2Fragment v2f;
  v2f.pos = float4(pos, 0, 1);
  v2f.color = half4(c2v.color);
  v2f.uv = c2v.uv;
  v2f.premul_color = half4(float4(c2v.color.rgb * c2v.color.a, c2v.color.a));
  v2f.cust1 = c2v.cust1;
  v2f.cust2 = c2v.cust2;
  return v2f;
}

half3 vibrance( half3 c, half v )
{
  half l = dot(c, half3(0.2126, 0.7152, 0.0722));
  half3 chroma = c - half3(l);
  half sat = length(chroma);
  half factor;
  if (v > 0.0)
  {
    factor = 1.0 + v * (1.0 - saturate(sat));
  }
  else
  {
    factor = 1.0 + v;
  }

  return half3(l) + chroma * factor;
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
fs_image(Vertex2Fragment v2f [[stage_in]],
         constant Uniforms &uniforms [[buffer(0)]],
         texture2d<half> texture [[texture(0)]])
{
  constexpr sampler tex_sampler(mag_filter::linear, min_filter::linear);
  half4 color = v2f.color * texture.sample(tex_sampler, v2f.uv);
  if (uniforms.custom_vec2_value1.x != 0)
  {
#if 1
    color = half4(vibrance(color.rgb, -0.8), 1);
    half4 tint = half4(0,0,0,1);
    half tint_power = 0.25;
#else
    color = half4(vibrance(color.rgb, 3.0), 1);
    half4 tint = half4(1,1,1,1);
    half tint_power = 0.12;
#endif
    color = mix(color, tint, tint_power);
  }
  return color;
}

fragment half4
fs_text(Vertex2Fragment v2f [[stage_in]],
        texture2d<half> texture [[texture(0)]])
{
  constexpr sampler tex_sampler(mag_filter::linear, min_filter::linear);
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

struct TextSubpixelOut
{
  half4 color [[color(0), index(0)]];
  half4 multiply [[color(0), index(1)]];
};

fragment TextSubpixelOut
fs_text_subpixel(Vertex2Fragment v2f [[stage_in]],
        texture2d<float> texture [[texture(0)]])
{
  constexpr sampler tex_sampler(mag_filter::linear, min_filter::linear);

  float2 tex_size = float2(texture.get_width(), texture.get_height());
  float2 pixel = 1.0 / tex_size;

  float vshift = v2f.cust1;
  float4 current  = texture.sample(tex_sampler, v2f.uv);
  float4 previous = texture.sample(tex_sampler, v2f.uv + float2(-1.0, 0.0) * pixel);
  float r = current.r;
  float g = current.g;
  float b = current.b;
  if (vshift <= 1.0/3.0)
  {
    float z = 3.0 * vshift;
    r = mix(current.r, previous.b, z);
    g = mix(current.g, current.r,  z);
    b = mix(current.b, current.g,  z);
  }
  else if (vshift <= 2.0/3.0)
  {
    float z = 3.0 * vshift - 1.0;
    r = mix(previous.b, previous.g, z);
    g = mix(current.r,  previous.b, z);
    b = mix(current.g,  current.r,  z);
  }
  else if (vshift < 1.0)
  {
    float z = 3.0 - vshift - 2.0;
    r = mix(previous.g, previous.r, z);
    g = mix(previous.b, previous.g, z);
    b = mix(current.r,  previous.b, z);
  }
  float3 pixel_coverages = float3(r, g, b);

  // Coverage adjustment variant 1: Increase or decrease the slope of the gradient by a linear factor.
  // Gives sharper results than variant 2 but overdoing it degrades quality quickly.
  // coverage_adjustment = 0: does nothing
  // coverage_adjustment = +0.2: makes the glyphs slightly bolder (multiply slope by 1.2 with coverage 0 as reference point)
  // coverage_adjustment = -0.2: makes them slightly thinner (multiply slope by 1.2 with coverage 1 as reference point)
  constexpr float coverage_adjustment = 0.5;
  if (coverage_adjustment >= 0)
  {
    pixel_coverages = min(pixel_coverages * (1.0 + coverage_adjustment), 1.0);
  }
  else
  {
    pixel_coverages = max((1.0 - (1.0 - pixel_coverages) * (1.0 + -coverage_adjustment)), 0.0);
  }

  // Use dual-source blending to blend each subpixel (color channel) individually.
  // Note: The blend equation is setup for pre-multiplied alpha blending. color is already pre-multiplied in the vertex shader.
  // color * vec4(pixel_coverages, 1) gives us a color mask where all subpixels of the glyph have the proper values for the text
  // color and all other subpixels are 0. This is what we add to the framebuffer (since color is pre-multiplied).
  // The blend weights are then set to remove the portion of the background we no longer want. The blend equation does a 1 - alpha
  // for each channel so here we set the weights to the part that the glyph color contributes. But only where the glyph actual
  // covers the subpixels, thats what color.a * pixel_coverages does.
  TextSubpixelOut out;
  half3 pixel_coverages_half = half3(pixel_coverages);
  out.color = v2f.premul_color * half4(pixel_coverages_half, 1.0);
  out.multiply = half4(v2f.premul_color.a * pixel_coverages_half, v2f.premul_color.a);
  return out;
}

fragment half4
fs_blur_horiz(Vertex2Fragment v2f [[stage_in]],
              constant Uniforms &uniforms [[buffer(0)]], 
              texture2d<float> texture [[texture(0)]])
{
  //- brt: setup params
  constexpr sampler tex_sampler(mag_filter::linear, min_filter::linear);
  const float glow_falloff = uniforms.custom_vec2_value1.x;
  const int taps = int(uniforms.custom_vec2_value1.y);
  const float2 original_resolution = uniforms.custom_vec2_value2;
  float2 tex = v2f.uv;
  float flags = v2f.cust1;

  //- brt: perform blur
  float4 col = float4(0);
  float dx = 1.0 / original_resolution.x;
  float dy = 1.0 / original_resolution.y;
  dx *= float(flags == 0.0);
  dy *= float(flags > 0.0);
  float k_total = 0.0;
  for (int idx = -taps; idx <= taps; idx += 1)
  {
    float k = exp(-glow_falloff * idx * idx);
    k_total += k;
    col += k * texture.sample(tex_sampler, tex + float2(float(idx) * dx, float(idx) * dy));
  }
  half4 out = half4(col / k_total);
  return out;
}

