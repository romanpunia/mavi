#include "std/layouts/vertex.hlsl"
#include "std/channels/depth.hlsl"
#include "std/buffers/object.hlsl"

VOutputLinear vs_main(VInput V)
{
	VOutputLinear Result = (VOutputLinear)0;
	Result.Position = Result.UV = mul(float4(V.Position, 1.0), ob_WorldViewProj);
	Result.TexCoord = V.TexCoord * ob_TexCoord.xy;

	return Result;
}

float ps_main(VOutputLinear V) : SV_DEPTH
{
	float Threshold = (ob_Diffuse ? 1.0 - GetDiffuse(V.TexCoord).w : 1.0) * Materials[ob_Mid].Transparency;
	[branch] if (Threshold > 0.5)
		discard;
	
	return V.UV.z / V.UV.w;
};