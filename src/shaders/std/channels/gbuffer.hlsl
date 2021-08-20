#include "std/channels/geometry.hlsl"
#include "std/objects/gbuffer.hlsl"

GBuffer Compose(float2 TexCoord, float4 Diffuse, float3 Normal, float Depth, float Mid)
{
	GBuffer Result;
    Result.DiffuseBuffer = Diffuse;
    Result.NormalBuffer.xyz = Normal;
    Result.NormalBuffer.w = Mid;
    Result.DepthBuffer = Depth;
    Result.SurfaceBuffer.x = RoughnessMap.Sample(Sampler, TexCoord).x;
    Result.SurfaceBuffer.y = MetallicMap.Sample(Sampler, TexCoord).x;
    Result.SurfaceBuffer.z = OcclusionMap.Sample(Sampler, TexCoord).x;
    Result.SurfaceBuffer.w = EmissionMap.Sample(Sampler, TexCoord).x;

    return Result;
}