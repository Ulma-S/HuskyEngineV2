// ピクセルシェーダ: ラスタライザが補間した UV を使って、テクスチャから色を読む。

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

// テクスチャ(画像そのもの)と、サンプラー(どう読むか: 拡大時にぼかすか、端をどう扱うか)。
// SDL_GPU の約束: ピクセルシェーダのテクスチャとサンプラーは space2 に置く。
// [[vk::...]] は Vulkan(SPIR-V)用の指定で、DXIL では無視される。
//   SDL_GPU の Vulkan では、テクスチャとサンプラーを1組(combined image sampler)として set 2 に置く約束なので、
//   2つを同じ番号(binding 0)の1組にまとめる。
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
Texture2D<float4> spriteTexture : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
SamplerState spriteSampler : register(s0, space2);

float4 main(PixelInput input) : SV_Target0
{
    // UV の位置の色をテクスチャから読み、頂点の色を掛ける。
    return spriteTexture.Sample(spriteSampler, input.uv) * input.color;
}
