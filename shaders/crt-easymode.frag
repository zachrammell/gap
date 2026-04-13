#version 330 core

// This is largely the crt-easymode shader found at https://github.com/libretro/glsl-shaders/blob/master/crt/shaders/crt-easymode.glsl.

uniform sampler2D image;
uniform vec2 resolution;

in vec2 out_uv;

out vec4 frag_color;

#define FragColor frag_color

#define COMPAT_PRECISION

#define OutputSize resolution
#define TextureSize resolution
#define InputSize resolution
#define Texture image

#define COMPAT_TEXTURE texture

#if 0
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;
#endif

#define FIX(c) max(abs(c), 1e-5)
#define PI 3.141592653589

#define TEX2D(c) dilate(COMPAT_TEXTURE(Texture, c))

// compatibility #defines
#define Source Texture
#define vTexCoord out_uv

#define SourceSize vec4(TextureSize, 1.0 / TextureSize) //either TextureSize or InputSize
#define outsize vec4(OutputSize, 1.0 / OutputSize)

#ifdef PARAMETER_UNIFORM
// All parameter floats need to have COMPAT_PRECISION in front of them
uniform COMPAT_PRECISION float SHARPNESS_H;
uniform COMPAT_PRECISION float SHARPNESS_V;
uniform COMPAT_PRECISION float MASK_STRENGTH;
uniform COMPAT_PRECISION float MASK_DOT_WIDTH;
uniform COMPAT_PRECISION float MASK_DOT_HEIGHT;
uniform COMPAT_PRECISION float MASK_STAGGER;
uniform COMPAT_PRECISION float MASK_SIZE;
uniform COMPAT_PRECISION float SCANLINE_STRENGTH;
uniform COMPAT_PRECISION float SCANLINE_BEAM_WIDTH_MIN;
uniform COMPAT_PRECISION float SCANLINE_BEAM_WIDTH_MAX;
uniform COMPAT_PRECISION float SCANLINE_BRIGHT_MIN;
uniform COMPAT_PRECISION float SCANLINE_BRIGHT_MAX;
uniform COMPAT_PRECISION float SCANLINE_CUTOFF;
uniform COMPAT_PRECISION float GAMMA_INPUT;
uniform COMPAT_PRECISION float GAMMA_OUTPUT;
uniform COMPAT_PRECISION float BRIGHT_BOOST;
uniform COMPAT_PRECISION float DILATION;
#else

#if 0 // Original values.
#define SHARPNESS_H 0.5
#define SHARPNESS_V 1.0
#define MASK_STRENGTH 0.3
#define MASK_DOT_WIDTH 1.0
#define MASK_DOT_HEIGHT 1.0
#define MASK_STAGGER 0.0
#define MASK_SIZE 1.0
#define SCANLINE_STRENGTH 1.0
#define SCANLINE_BEAM_WIDTH_MIN 1.5
#define SCANLINE_BEAM_WIDTH_MAX 1.5
#define SCANLINE_BRIGHT_MIN 0.35
#define SCANLINE_BRIGHT_MAX 0.65
#define SCANLINE_CUTOFF 400.0
#define GAMMA_INPUT 2.0
#define GAMMA_OUTPUT 1.8
#define BRIGHT_BOOST 1.2
#define DILATION 1.0

#pragma parameter SHARPNESS_H             "Sharpness Horizontal"     0.5   0.0  1.0     0.05
#pragma parameter SHARPNESS_V             "Sharpness Vertical"       1.0   0.0  1.0     0.05
#pragma parameter MASK_STRENGTH           "Mask Strength"            0.3   0.0  1.0     0.01
#pragma parameter MASK_DOT_WIDTH          "Mask Dot Width"           1.0   1.0  100.0   1.0
#pragma parameter MASK_DOT_HEIGHT         "Mask Dot Height"          1.0   1.0  100.0   1.0
#pragma parameter MASK_STAGGER            "Mask Stagger"             0.0   0.0  100.0   1.0
#pragma parameter MASK_SIZE               "Mask Size"                1.0   1.0  100.0   1.0
#pragma parameter SCANLINE_STRENGTH       "Scanline Strength"        1.0   0.0  1.0     0.05
#pragma parameter SCANLINE_BEAM_WIDTH_MIN "Scanline Beam Width Min." 1.5   0.5  5.0     0.5
#pragma parameter SCANLINE_BEAM_WIDTH_MAX "Scanline Beam Width Max." 1.5   0.5  5.0     0.5
#pragma parameter SCANLINE_BRIGHT_MIN     "Scanline Brightness Min." 0.35  0.0  1.0     0.05
#pragma parameter SCANLINE_BRIGHT_MAX     "Scanline Brightness Max." 0.65  0.0  1.0     0.05
#pragma parameter SCANLINE_CUTOFF         "Scanline Cutoff"          400.0 1.0  1000.0  1.0
#pragma parameter GAMMA_INPUT             "Gamma Input"              2.0   0.1  5.0     0.1
#pragma parameter GAMMA_OUTPUT            "Gamma Output"             1.8   0.1  5.0     0.1
#pragma parameter BRIGHT_BOOST            "Brightness Boost"         1.2   1.0  2.0     0.01
#pragma parameter DILATION                "Dilation"                 1.0   0.0  1.0     1.0
#endif
#define SHARPNESS_H 0.5
#define SHARPNESS_V 1.0
#define MASK_STRENGTH 0.6
#define MASK_DOT_WIDTH 1.0
#define MASK_DOT_HEIGHT 1.0
#define MASK_STAGGER 0.0
#define MASK_SIZE 1.0
#define SCANLINE_STRENGTH 1.0
#define SCANLINE_BEAM_WIDTH_MIN 2.0
#define SCANLINE_BEAM_WIDTH_MAX 2.0
#define SCANLINE_BRIGHT_MIN 0.3
#define SCANLINE_BRIGHT_MAX 0.6
#define SCANLINE_CUTOFF 400.0
#define GAMMA_INPUT 2.0
#define GAMMA_OUTPUT 1.8
#define BRIGHT_BOOST 1.2
#define DILATION 1.0

#endif

// Set to 0 to use linear filter and gain speed
#define ENABLE_LANCZOS 1

vec4 dilate(vec4 col)
{
    vec4 x = mix(vec4(1.0), col, DILATION);

    return col * x;
}

float curve_distance(float x, float sharp)
{

/*
    apply half-circle s-curve to distance for sharper (more pixelated) interpolation
    single line formula for Graph Toy:
    0.5 - sqrt(0.25 - (x - step(0.5, x)) * (x - step(0.5, x))) * sign(0.5 - x)
*/

    float x_step = step(0.5, x);
    float curve = 0.5 - sqrt(0.25 - (x - x_step) * (x - x_step)) * sign(0.5 - x);

    return mix(x, curve, sharp);
}

mat4 get_color_matrix(vec2 co, vec2 dx)
{
    return mat4(TEX2D(co - dx), TEX2D(co), TEX2D(co + dx), TEX2D(co + 2.0 * dx));
}

vec3 filter_lanczos(vec4 coeffs, mat4 color_matrix)
{
    vec4 col        = color_matrix * coeffs;
    vec4 sample_min = min(color_matrix[1], color_matrix[2]);
    vec4 sample_max = max(color_matrix[1], color_matrix[2]);

    col = clamp(col, sample_min, sample_max);

    return col.rgb;
}

void main()
{
    vec2 dx     = vec2(SourceSize.z, 0.0);
    vec2 dy     = vec2(0.0, SourceSize.w);
    vec2 pix_co = vTexCoord * SourceSize.xy - vec2(0.5, 0.5);
    vec2 tex_co = (floor(pix_co) + vec2(0.5, 0.5)) * SourceSize.zw;
    vec2 dist   = fract(pix_co);
    float curve_x;
    vec3 col, col2;

#if ENABLE_LANCZOS
    curve_x = curve_distance(dist.x, SHARPNESS_H * SHARPNESS_H);

    vec4 coeffs = PI * vec4(1.0 + curve_x, curve_x, 1.0 - curve_x, 2.0 - curve_x);

    coeffs = FIX(coeffs);
    coeffs = 2.0 * sin(coeffs) * sin(coeffs * 0.5) / (coeffs * coeffs);
    coeffs /= dot(coeffs, vec4(1.0));

    col  = filter_lanczos(coeffs, get_color_matrix(tex_co, dx));
    col2 = filter_lanczos(coeffs, get_color_matrix(tex_co + dy, dx));
#else
    curve_x = curve_distance(dist.x, SHARPNESS_H);

    col  = mix(TEX2D(tex_co).rgb,      TEX2D(tex_co + dx).rgb,      curve_x);
    col2 = mix(TEX2D(tex_co + dy).rgb, TEX2D(tex_co + dx + dy).rgb, curve_x);
#endif

    col = mix(col, col2, curve_distance(dist.y, SHARPNESS_V));
    col = pow(col, vec3(GAMMA_INPUT / (DILATION + 1.0)));

    float luma        = dot(vec3(0.2126, 0.7152, 0.0722), col);
    float bright      = (max(col.r, max(col.g, col.b)) + luma) * 0.5;
    float scan_bright = clamp(bright, SCANLINE_BRIGHT_MIN, SCANLINE_BRIGHT_MAX);
    float scan_beam   = clamp(bright * SCANLINE_BEAM_WIDTH_MAX, SCANLINE_BEAM_WIDTH_MIN, SCANLINE_BEAM_WIDTH_MAX);
    //float scan_weight = 1.0 - pow(cos(vTexCoord.y * 2.0 * PI * SourceSize.y) * 0.5 + 0.5, scan_beam) * SCANLINE_STRENGTH;
    // The original computation above made the scanlines disappear completely.  Investigation is needed to see
    // exactly why this happens.  I suspect it has to do with the multiplication of PI for the period.
    float scan_weight = 1.0 - pow(cos(vTexCoord.y * SourceSize.y) * 0.5 + 0.5, scan_beam) * SCANLINE_STRENGTH;

    float mask   = 1.0 - MASK_STRENGTH;    
    vec2 mod_fac = floor(vTexCoord * outsize.xy * SourceSize.xy / (InputSize.xy * vec2(MASK_SIZE, MASK_DOT_HEIGHT * MASK_SIZE)));
    int dot_no   = int(mod((mod_fac.x + mod(mod_fac.y, 2.0) * MASK_STAGGER) / MASK_DOT_WIDTH, 3.0));
    vec3 mask_weight;

    if      (dot_no == 0) mask_weight = vec3(1.0,  mask, mask);
    else if (dot_no == 1) mask_weight = vec3(mask, 1.0,  mask);
    else                  mask_weight = vec3(mask, mask, 1.0);

#if 0
    if (InputSize.y >= SCANLINE_CUTOFF) 
        scan_weight = 1.0;
#endif

    col2 = col.rgb;
    col *= vec3(scan_weight);
    col  = mix(col, col2, scan_bright);
    col *= mask_weight;
    col  = pow(col, vec3(1.0 / GAMMA_OUTPUT));

    FragColor = vec4(col * BRIGHT_BOOST, 1.0);
}

/* There was an interesting discussion about getting a better CRT-staggered look which is closer to a slot mask: https://forums.libretro.com/t/crt-easymode/1648

Thanks for the compliment Patrick.

I believe your friend is referring to the RGB mask that, by default, mimics an aperture grille pattern running vertically along the screen. The mask in this shader is fixed pixel, so the density of the pattern will vary depending on your display’s resolution. On a lower res screen it may appear more like an LCD grid, whereas on a 4K display it will appear very fine and perhaps closer to a real CRT with an aperture grille. I use a 1080p monitor and I wouldn’t call the mask realistic at that resolution, but I just happen to like the way it looks.

Your friend may also be more accustomed to CRTs with shadow masks. You can see the differnce here:

Crt-easymode can be configured in a Lottes style shadow mask (a la crt-lottes) by setting the following parameters like so: MASK_SUBPIXEL_WIDTH 2.0, MASK_SUBPIXEL_HEIGHT 1.0, MASK_STAGGER 3.0.

You can see a preview here (second pic):

viewtopic.php?f=6&t=2360 96

If someone prefers having no mask, they can set MASK_STRENGTH to 0.0 – and if they do that I recommend also lowering BRIGHT_BOOST to 1.0 as that parameter is meant to make up for the brightness loss caused by the mask.

Also, in case you’re still using RetroArch 1.0.0.2, lord ashram has a dropbox where he posts test builds. These newer builds have a parameter menu that lets you adjust shader parameters from within RetroArch.

1.0.0.3 Beta is apparently coming soon too.

*/