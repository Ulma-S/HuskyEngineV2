// シーンビューのグリッド(方眼)の線を作る(Unity のシーンビューのグリッドにあたる)。
// 線は、細長い白いスプライト(render::Sprite。テクスチャ無し = 白い板)として作り、シーンより奥に描く。
// 描画層に「線を描く」機能を足さずに、今ある「スプライトをまとめて描く」仕組み(SpriteBatch)をそのまま使うため。
//
// 決まり(Unity に近づけた):
//   - 線の間隔は 10 のべき乗(1, 10, 100, ...)。画面で線と線が 16 ピクセル以上離れる、いちばん細かい間隔を選ぶ
//     (ズームで寄ると細かく、引くと粗くなる)。
//   - 10 本ごとの線(太い区切り)は少し濃く、原点を通る線(軸)は色を付ける: x 軸(y = 0)は赤、y 軸(x = 0)は緑。
//   - 線の太さは、画面でいつも 1 ピクセル(ワールドでは 1 / zoom)。
// GPU も ImGui も使わないので、テストできる。毎フレーム呼んでも、入れ物(out)を使い回せばメモリを確保しない。
// 動作スレッド: どこでもよい
#pragma once

#if HUSKY_DEVELOPMENT

#include "engine/Camera.h"
#include "render/RenderDevice.h"

#include <vector>

namespace husky::tools
{
    // 線の間隔(ワールドの単位)。画面で minPixels ピクセル以上離れる、いちばん小さい 10 のべき乗。
    float ChooseGridSpacing(float zoom, float minPixels = 16.0f);

    // カメラ(view)が映している範囲のグリッドの線を、out に作り直す(前の中身は消す。確保した容量は残る)。
    // 線が多すぎるとき(画面がとても大きいなど)は、縦横それぞれ maxLinesPerAxis 本で打ち切る。
    void BuildGridSprites(const engine::CameraView& view, float screenWidth, float screenHeight,
                          std::vector<render::Sprite>& out, int maxLinesPerAxis = 400);
}

#endif
