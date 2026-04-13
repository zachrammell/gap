#version 330 core

uniform sampler2D image;
uniform vec2 resolution;
uniform float custom_float_value1;
uniform float custom_float_value2;

in vec2 out_uv;
in float out_cust1; // Blur horiz == 0.0, blur vert == 1.0.

out vec4 frag_color;

// Higher value, more centered glow.
// Lower values might need more taps.
#define GLOW_FALLOFF custom_float_value1
#define TAPS int(custom_float_value2)

#define kernel(x) exp(-GLOW_FALLOFF * (x) * (x))

vec4 blur(vec2 tex, sampler2D s0, vec2 texture_size)
{
    vec4 col = vec4(0.0);
    float dx = 1.0 / texture_size.x;
    float dy = 1.0 / texture_size.y;
    dx *= float(out_cust1 == 0.0);
    dy *= float(out_cust1 > 0.0);

    float k_total = 0.0;
    for (int i = -TAPS; i <= TAPS; i++)
    {
        float k = kernel(i);
        k_total += k;
        col += k * texture(s0, tex + vec2(float(i) * dx, float(i) * dy));
    }
    return vec4(col / k_total);
}

void main()
{
    frag_color = blur(out_uv, image, resolution);
}