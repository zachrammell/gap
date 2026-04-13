#version 330 core

uniform sampler2D image;

in vec2 out_uv;

out vec4 frag_color;

void main() {
    frag_color = texture(image, out_uv);
}