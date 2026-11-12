// カメラの行列を作る関数。
//
// 座標変換の流れ(設計メモ「座標系とカメラ」):
//   ワールドの位置 →(ビュー行列)→ カメラから見た位置 →(射影行列)→ クリップ座標(-1〜+1)→ 画面
// 描画層(RenderDevice::SetViewProjection)には、2つを掛け合わせた「射影行列 × ビュー行列」を1つ渡す。
//
// ワールドの決まり: 右が +x、上が +y。最初は 1 単位 = 1 ピクセル(換算はカメラの射影行列が受け持つ)。
// 動作スレッド: どこでもよい(ただの計算。GC 管理のオブジェクトを触らない)
#pragma once

#include <glm/mat4x4.hpp>

namespace husky::engine
{
    // ビュー行列と射影行列の組。
    struct CameraMatrices
    {
        glm::mat4 view = glm::mat4(1.0f);       // ワールド → カメラから見た位置(カメラを動かすと変わる)
        glm::mat4 projection = glm::mat4(1.0f); // カメラから見た位置 → クリップ座標(見える範囲と、ピクセルとの換算)

        // 描画層に渡す行列。行列は右から順に掛かるので、先にビュー、次に射影が効く。
        glm::mat4 ViewProjection() const { return projection * view; }
    };

    // 画面の大きさ(ピクセル)ちょうどの範囲を映す、2D のカメラ(正射影)。
    // ワールドの (0, 0) が画面の左下、(screenWidth, screenHeight) が右上に来る(1 単位 = 1 ピクセル)。
    // カメラは動かないので、ビュー行列は単位行列(何もしない行列)。カメラを動かすのは CAM-2 から。
    CameraMatrices MakeScreenCamera(float screenWidth, float screenHeight);
}
