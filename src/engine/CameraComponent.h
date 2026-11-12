// カメラのコンポーネント。アクターに付けると、そのアクターの位置から世界を映す(Unity の Camera コンポーネントと同じ形)。
//
//   Actor* camera = scene.Spawn<Actor>();
//   camera->AddComponent<CameraComponent>();
//   camera->transform.position = glm::vec3(640, 360, 0);   // この位置が画面の真ん中に映る
//
// カメラの位置は、持ち主のアクターの位置(transform.position)。スプライト(左下の角)と違い、カメラは「真ん中」を指す。
// カメラを動かすには、アクターを動かすだけでよい(プレイヤーを追いかけるのも、ほかのアクターと同じ書き方になる)。
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/Camera.h"
#include "engine/Component.h"

namespace husky::engine
{
    class Scene;

    class CameraComponent : public Component
    {
        HUSKY_OBJECT(CameraComponent, Component)
    public:
        // 拡大率。1 なら 1 単位 = 1 ピクセル。2 なら 2 倍に大きく映る(見える範囲は縦横とも半分)。0 以下は 1 として扱う。
        float zoom = 1.0f;

        // Tick は使わない(描くときに読まれるだけ)。

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(zoom)
        HUSKY_REFLECTION_END()
    };

    // シーンで使うカメラを探す。シーンの一覧の順で、最初に見つかったカメラを返す(Destroy 済みは除く)。
    // 無ければ nullptr。カメラを何台も置いて切り替える仕組みは、要るようになったら足す。
    CameraComponent* FindSceneCamera(Scene& scene);

    // シーンを映すカメラの行列を作る。シーンにカメラがあればそれを使い、無ければ画面ちょうどの動かないカメラ
    // (MakeScreenCamera。カメラの無い古いシーンのファイルも、今までどおりに見える)を使う。
    CameraMatrices MakeSceneCamera(Scene& scene, float screenWidth, float screenHeight);
}
