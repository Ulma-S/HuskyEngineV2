// 頂点シェーダ: 頂点バッファから受け取ったピクセル単位の位置を、クリップ空間(-1〜+1)に変換する。
// UV(テクスチャのどこを使うか)と色は、そのままピクセルシェーダへ渡す。

// 頂点バッファの1頂点分。C++ 側の頂点の構造体と、並び・型を一致させる。
// SDL_GPU の約束: 頂点の入力には TEXCOORD0, TEXCOORD1, ... という名前を付け、番号を C++ 側の location と合わせる。
struct VertexInput
{
    float2 position : TEXCOORD0; // ピクセル単位。左上が (0, 0)、右が +x、下が +y
    float2 uv : TEXCOORD1;       // テクスチャ座標。テクスチャの左上が (0, 0)、右下が (1, 1)
    float4 color : TEXCOORD2;    // テクスチャの色に掛ける色。白 (1,1,1,1) ならテクスチャの色そのまま
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

// ユニフォーム: CPU から毎フレーム渡す小さな値。cbuffer(定数バッファ)として受け取る。
// SDL_GPU の約束: 頂点シェーダのユニフォームは register(b番号, space1) に置く。
// cbuffer の中身は 16 バイト単位で並ぶので、C++ 側も float 4つ(16 バイト)で送る。
cbuffer FrameUniforms : register(b0, space1)
{
    float2 screenSize; // 画面の幅と高さ(ピクセル)
    float2 padding;    // 使わない。16 バイトに揃えるための詰め物
};

VertexOutput main(VertexInput input)
{
    // バッチ描画では、各スプライトの位置は CPU 側で頂点に直接書き込んである。
    const float2 pixel = input.position;

    // ピクセル座標 → クリップ空間。
    //   x: 0〜幅   を -1〜+1 に(右が +)
    //   y: 0〜高さ を +1〜-1 に(クリップ空間は上が + なので、上下を反転する)
    const float2 clip = float2(pixel.x / screenSize.x * 2.0 - 1.0,
                               1.0 - pixel.y / screenSize.y * 2.0);

    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
