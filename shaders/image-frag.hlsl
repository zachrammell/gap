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

sampler sampler0;
Texture2D texture0;

float4 main(PS_INPUT input) : SV_Target
{
    float4 out_col = input.color * texture0.Sample(sampler0, input.uv);
    return out_col;
}