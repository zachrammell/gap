#version 330 core

uniform sampler2D image;

in vec2 out_uv;

out vec4 frag_color;

#define GAMMA_INPUT 2.4

vec4 linearize(sampler2D decal, vec2 texCoord)
{
    return pow(texture(decal, texCoord), vec4(GAMMA_INPUT));
}

void main()
{
    frag_color = linearize(image, out_uv);
}