cbuffer RenderConstant : register(b3)
{
	matrix LightWorldViewProjection;
	float3 Position;
	float Range;
	float3 Lighting;
	float Distance;
    float Umbra;
	float Softness;
	float Bias;
	float Iterations;
};