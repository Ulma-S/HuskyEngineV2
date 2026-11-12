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

    // 2D のカメラ(正射影)。ワールドの (centerX, centerY) が画面の真ん中に映る。
    // zoom は拡大率: 1 なら 1 単位 = 1 ピクセル、2 なら 1 単位 = 2 ピクセル(見える範囲は縦横とも半分)。
    // zoom が 0 以下なら 1 として扱う(見える範囲が無限や裏返しになるのを防ぐ)。
    //   ビュー行列: カメラの位置の「逆」にずらす。カメラが右へ動くと、世界は左へずれて見える。
    //   射影行列: カメラを中心に、幅 screenWidth / zoom、高さ screenHeight / zoom の箱を、画面いっぱいに映す。
    CameraMatrices MakeOrthographicCamera(float centerX, float centerY, float zoom, float screenWidth, float screenHeight);

    // 画面の大きさ(ピクセル)ちょうどの範囲を映す、動かないカメラ。シーンにカメラが無いときに使う。
    // ワールドの (0, 0) が画面の左下、(screenWidth, screenHeight) が右上に来る(1 単位 = 1 ピクセル)。
    // 中身は「画面の真ん中を中心にした、zoom 1 の MakeOrthographicCamera」。
    CameraMatrices MakeScreenCamera(float screenWidth, float screenHeight);
}
