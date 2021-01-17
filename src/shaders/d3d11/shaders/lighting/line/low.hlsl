#include "sdk/layouts/shape"
#include "sdk/channels/effect"
#include "sdk/core/lighting"
#include "sdk/core/atmosphere"
#include "sdk/core/material"
#include "sdk/core/position"
#include "common/lighting/line/buffer"

VOutput VS(VInput V)
{
	VOutput Result = (VOutput)0;
	Result.Position = float4(V.Position, 1.0);
	Result.TexCoord = Result.Position;

	return Result;
}

float4 PS(VOutput V) : SV_TARGET0
{
    float2 TexCoord = GetTexCoord(V.TexCoord);
	Fragment Frag = GetFragment(TexCoord);

    [branch] if (Frag.Depth >= 1.0)
    {
        [branch] if (ScatterIntensity <= 0.0)
            return float4(0, 0, 0, 0);
            
        Scatter A;
        A.Sun = ScatterIntensity;
        A.Planet = PlanetRadius;
        A.Atmos = AtmosphereRadius;
        A.Rlh = RlhEmission;
        A.Mie = MieEmission;
        A.RlhHeight = RlhHeight;
        A.MieHeight = MieHeight;
        A.MieG = MieDirection;
        return float4(GetAtmosphere(TexCoord, SkyOffset, float3(0, 6372e3, 0), Position, A), 1.0);
    }

	Material Mat = GetMaterial(Frag.Material);
	float G = GetRoughness(Frag, Mat);
	float3 M = GetMetallic(Frag, Mat);
    float3 E = GetSurface(Frag, Mat);
    float3 D = normalize(ViewPosition - Frag.Position);
    float3 R = GetCookTorranceBRDF(Frag.Normal, D, Position, Frag.Diffuse, M, G);
    float3 S = GetSubsurface(Frag.Normal, D, Position, Mat.Scatter) * E;

	return float4(Lighting * (R + S), 1.0);
};