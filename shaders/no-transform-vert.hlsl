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

struct VS_INPUT
{
    float2 pos   : POS;
    float4 color : COL0;
    float2 uv    : TEX;
    float cust1  : CUST0;
    float cust2  : CUST1;
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

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.pos = float4(input.pos / resolution, 0, 1);
    output.color = input.color;
    output.uv  = input.uv;
    output.premul_color = float4(input.color.rgb * input.color.a, input.color.a);
    output.cust1 = input.cust1;
    output.cust2 = input.cust2;
    return output;
}