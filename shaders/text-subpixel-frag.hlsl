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
    nointerpolation float cust1 : CUST0;
    nointerpolation float cust2 : CUST1;
};

#define vshift input.cust1
#define coverage_adjustment 0.5

sampler sampler0;
Texture2D texture0;

struct PS_BLEND
{
    float4 color      : SV_Target0;
    float4 multiply   : SV_Target1;
};

// https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-blend-state#blending-pixel-shader-outputs
// This tells us that we only need a single render target for this pixel shader.
PS_BLEND main(PS_INPUT input) : SV_Target
{
    // This approach was obtained from https://github.com/arkanis/gl-4.5-subpixel-text-rendering/tree/main
    // in the article https://arkanis.de/weblog/2023-08-14-simple-good-quality-subpixel-text-rendering-in-opengl-with-stb-truetype-and-dual-source-blending/.

    // Shift the subpixel weights according to the subpixel position of this specific glyph (the atlas only contains the glyph with a subpixel shift of 0)
    // Based on the shifting code from the paper Higher Quality 2D Text Rendering by Nicolas P. Rougier, Listing 2. Subpixel positioning fragment shader, from https://jcgt.org/published/0002/01/04/paper.pdf
    float2 tex_size;
    texture0.GetDimensions(tex_size.x, tex_size.y);
    float2 pixel = float2(1.0 / tex_size);

    float4 current = texture0.Sample(sampler0, input.uv);
    float4 previous= texture0.Sample(sampler0, input.uv + float2(-1.,0.)*pixel.xy);
    float r = current.r;
    float g = current.g;
    float b = current.b;
    if (vshift <= 1.0/3.0) {
        float z = 3.0 * vshift;
        r = lerp(current.r, previous.b, z);
        g = lerp(current.g, current.r, z);
        b = lerp(current.b, current.g, z);
    }
    else if (vshift <= 2.0/3.0) {
        float z = 3.0 * vshift - 1.0;
        r = lerp(previous.b, previous.g, z);
        g = lerp(current.r,  previous.b, z);
        b = lerp(current.g,  current.r,  z);
    }
    else if (vshift < 1.0) {
        float z = 3.0 * vshift - 2.0;
        r = lerp(previous.g, previous.r, z);
        g = lerp(previous.b, previous.g, z);
        b = lerp(current.r,  previous.b, z);
    }
    float3 pixel_coverages = float3(r, g, b);

    // Coverage adjustment variant 1: Increase or decrease the slope of the gradient by a linear factor.
    // Gives sharper results than variant 2 but overdoing it degrades quality quickly.
    // coverage_adjustment = 0: does nothing
    // coverage_adjustment = +0.2: makes the glyphs slightly bolder (multiply slope by 1.2 with coverage 0 as reference point)
    // coverage_adjustment = -0.2: makes them slightly thinner (multiply slope by 1.2 with coverage 1 as reference point)
    if (coverage_adjustment >= 0) {
        pixel_coverages = min(pixel_coverages * (1 + coverage_adjustment), 1);
    }
    else {
        pixel_coverages = max((1 - (1 - pixel_coverages) * (1 + -coverage_adjustment)), 0);
    }

    // Coverage adjustment variant 2: Use a power function to distort the coverages toward higher or lower values.
    // Note: The code might look similar to gamma correction 
    // coverage_adjustment = 1.0: does nothing
    // coverage_adjustment = 0.80: makes the glyphs slightly bolder, nice for source code, etc.
    // coverage_adjustment = 1.20: makes them slightly thinner, but can make bright text on bright backgrounds harder to read.
    // coverage_adjustment = 2.2 and 0.45: Gives you the look of text distorted by gamma correction (2.2 for black on white, 0.45 = 1/2.2 for white on black).
    // Comment variant 1 and uncomment this one to give it a try.
    //pixel_coverages = pow(pixel_coverages, vec3(coverage_adjustment));

    // Use dual-source blending to blend each subpixel (color channel) individually.
    // Note: The blend equation is setup for pre-multiplied alpha blending. color is already pre-multiplied in the vertex shader.
    // color * vec4(pixel_coverages, 1) gives us a color mask where all subpixels of the glyph have the proper values for the text
    // color and all other subpixels are 0. This is what we add to the framebuffer (since color is pre-multiplied).
    // The blend weights are then set to remove the portion of the background we no longer want. The blend equation does a 1 - alpha
    // for each channel so here we set the weights to the part that the glyph color contributes. But only where the glyph actual
    // covers the subpixels, thats what color.a * pixel_coverages does.
    PS_BLEND output;
    output.color = input.premul_color * float4(pixel_coverages, 1);
    output.multiply = float4(input.premul_color.a * pixel_coverages, input.premul_color.a);
    return output;
}