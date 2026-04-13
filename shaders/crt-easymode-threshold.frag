#version 330 core

uniform sampler2D image;
uniform sampler2D prev_pass_tex;

in vec2 out_uv;

out vec4 frag_color;

vec3 saturate(vec3 x)
{
    return clamp(x, 0.0, 1.0);
}

vec4 threshold(sampler2D s0, vec2 texCoord, sampler2D PASSPREV3)
{
    vec3 diff = saturate(texture(s0, texCoord).rgb - texture(PASSPREV3, texCoord).rgb);

    return vec4(diff, 1.0);
}

void main()
{
    frag_color = threshold(image, out_uv, prev_pass_tex);
}