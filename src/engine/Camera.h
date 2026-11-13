// カメラの行列を作る関数。
//
// 座標変換の流れ(設計メモ「座標系とカメラ」):
//   ワールドの位置 →(ビュー行列)→ カメラから見た位置 →(射影行列)→ クリップ座標(-1〜+1)→ 画面
// 描画層(RenderDevice::SetViewProjection)には、2つを掛け合わせた「射影行列 × ビュー行列」を1つ渡す。
//
// ワールドの決まり: 右が +x、上が +y。最初は 1 単位 = 1 ピクセル(換算はカメラの射影行列が受け持つ)。
// 動作スレッド: どこでもよい(ただの計算。GC 管理のオブジェクトを触らない)
#pragma once

#include "core/Matrix4.h" // 外へは自前の行列の型で渡す(glm は Camera.cpp の中だけ)
#include "core/Vector3.h"

namespace husky::engine
{
    // ビュー行列と射影行列の組。
    struct CameraMatrices
    {
        core::Matrix4 view;       // ワールド → カメラから見た位置(カメラを動かすと変わる)。既定は単位行列
        core::Matrix4 projection; // カメラから見た位置 → クリップ座標(見える範囲と、ピクセルとの換算)

        // 描画層に渡す行列。行列は右から順に掛かるので、先にビュー、次に射影が効く。
        // 掛け算は計算なので、glm を使う Camera.cpp に書く。
        core::Matrix4 ViewProjection() const;
    };

    // カメラが「どこを、どれだけの拡大で映すか」(行列を作る前の、人が決める値)。エディタのカメラ(RS-2)の状態にも使う。
    struct CameraView
    {
        float centerX = 0.0f; // 画面の真ん中に映るワールドの位置
        float centerY = 0.0f;
        float zoom = 1.0f;    // 拡大率(1 なら 1 単位 = 1 ピクセル)
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

    // 画面の点(ピクセル。左上が (0, 0)、右が +x、下が +y。マウスの位置と同じ決まり)を、ワールドの位置にする(RS-2)。
    // 描くときの変換(ワールド → ビュー → 射影 → 画面)を逆にたどる: 画面 → クリップ座標(-1〜+1)→ 行列の逆行列 → ワールド。
    // 画面の y は下が + だが、ワールドとクリップ座標は上が + なので、ここで裏返す。z は 0 の面の上の点を返す。
    core::Vector3 ScreenToWorld(const CameraMatrices& camera, float screenX, float screenY, float screenWidth, float screenHeight);
}
