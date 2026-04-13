#version 330 core

uniform sampler2D image;
uniform vec2 resolution;

in vec4 out_color;
in vec2 out_uv;
in float out_cust1;
in vec4 out_premul_color;

layout(location = 0, index = 0) out vec4 frag_color;
layout(location = 0, index = 1) out vec4 blend_weights;

#define vshift out_cust1
#define pixel vec2(1.0 / textureSize(image, 0))
#define coverage_adjustment 0.5

void main() {
    // This approach was obtained from https://github.com/arkanis/gl-4.5-subpixel-text-rendering/tree/main
    // in the article https://arkanis.de/weblog/2023-08-14-simple-good-quality-subpixel-text-rendering-in-opengl-with-stb-truetype-and-dual-source-blending/.

    // Shift the subpixel weights according to the subpixel position of this specific glyph (the atlas only contains the glyph with a subpixel shift of 0)
    // Based on the shifting code from the paper Higher Quality 2D Text Rendering by Nicolas P. Rougier, Listing 2. Subpixel positioning fragment shader, from https://jcgt.org/published/0002/01/04/paper.pdf
    vec4 current = texture2D(image, out_uv);
    vec4 previous= texture2D(image, out_uv+vec2(-1.,0.)*pixel.xy);
    float r = current.r;
    float g = current.g;
    float b = current.b;
    if (vshift <= 1.0/3.0) {
        float z = 3.0 * vshift;
        r = mix(current.r, previous.b, z);
        g = mix(current.g, current.r, z);
        b = mix(current.b, current.g, z);
    }
    else if (vshift <= 2.0/3.0) {
        float z = 3.0 * vshift - 1.0;
        r = mix(previous.b, previous.g, z);
        g = mix(current.r,  previous.b, z);
        b = mix(current.g,  current.r,  z);
    }
    else if (vshift < 1.0) {
        float z = 3.0 * vshift - 2.0;
        r = mix(previous.g, previous.r, z);
        g = mix(previous.b, previous.g, z);
        b = mix(current.r,  previous.b, z);
    }
    vec3 pixel_coverages = vec3(r, g, b);

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
    frag_color = out_premul_color * vec4(pixel_coverages, 1);
    blend_weights = vec4(out_premul_color.a * pixel_coverages, out_premul_color.a);
}