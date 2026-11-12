// 頂点シェーダ: 頂点バッファから受け取ったワールドの位置に、カメラの行列(ビュー × 射影)を掛けて、クリップ空間(-1〜+1)に変換する。
// UV(テクスチャのどこを使うか)と色は、そのままピクセルシェーダへ渡す。

// 頂点バッファの1頂点分。C++ 側の頂点の構造体と、並び・型を一致させる。
// SDL_GPU の約束: 頂点の入力には TEXCOORD0, TEXCOORD1, ... という名前を付け、番号を C++ 側の location と合わせる。
struct VertexInput
{
    float2 position : TEXCOORD0; // ワールドの位置。右が +x、上が +y(z は今は 0)
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
// 行列は既定で列優先(column_major)に並ぶ。C++ の glm::mat4 と同じ並びなので、並べ替えずに受け取れる。
cbuffer FrameUniforms : register(b0, space1)
{
    float4x4 viewProjection; // カメラの行列 = 射影行列 × ビュー行列(ワールド → クリップ座標)
};

VertexOutput main(VertexInput input)
{
    // バッチ描画では、各スプライトの四隅のワールドの位置は CPU 側で頂点に直接書き込んである。
    // 2D なので z は 0。w = 1 は「位置を表す点」の印(行列の平行移動の分が効く)。
    const float4 world = float4(input.position, 0.0, 1.0);

    // ワールド → クリップ座標。mul(行列, ベクトル) は「行列 × 列ベクトル」で、glm の viewProjection * world と同じ。
    // クリップ座標は画面の左下が (-1, -1)、右上が (+1, +1)。上下の向きは、射影行列が受け持つ。
    VertexOutput output;
    output.position = mul(viewProjection, world);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
