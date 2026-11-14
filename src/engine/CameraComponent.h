// カメラのコンポーネント。アクターに付けると、そのアクターの位置から世界を映す(Unity の Camera コンポーネントと同じ形)。
//
//   Actor* camera = scene.Spawn<Actor>();
//   camera->AddComponent<CameraComponent>();
//   camera->transform.position = {640, 360, 0};   // この位置が画面の真ん中に映る
//
// カメラの位置は、持ち主のアクターの位置(transform.position)。画面の真ん中に映る点を指す。
// カメラを動かすには、アクターを動かすだけでよい(プレイヤーを追いかけるのも、ほかのアクターと同じ書き方になる)。
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/Camera.h"
#include "engine/Component.h"

namespace husky::engine
{
    class World;

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
    CameraComponent* FindSceneCamera(World& scene);

    // シーンを映すカメラの行列を作る。シーンにカメラがあればそれを使い、無ければ画面ちょうどの動かないカメラ
    // (MakeScreenCamera。カメラの無い古いシーンのファイルも、今までどおりに見える)を使う。
    CameraMatrices MakeSceneCamera(World& scene, float screenWidth, float screenHeight);

    // シーンのカメラが「どこを、どれだけの拡大で映すか」(CameraView)。カメラが無ければ、画面ちょうどのカメラの値
    // (真ん中 = 画面の大きさの半分、zoom 1)。エディタのカメラを、ゲームのカメラと同じ見え方から始めるのに使う(RS-2)。
    CameraView FindSceneCameraView(World& scene, float screenWidth, float screenHeight);

    // マウスの下のワールドの位置(UI-1)。シーンの入力(World::GetInput)のマウスの位置を、シーンのカメラ
    // (MakeSceneCamera と同じ見え方)で ScreenToWorld に通す。画面のピクセル(下が +y)からワールド(上が +y)へ直る。
    // 入力が無い、画面の大きさが 0(テストで渡していない、CLI)のときは false を返し、outWorld は変えない。
    // ゲームのカメラで直すので、編集中のエディタのカメラ(シーンビュー)とは合わない(編集中はシーンの Tick が止まっている)。
    // 動作スレッド: ゲームスレッド
    bool FindMouseWorldPosition(World& scene, core::Vector3& outWorld);
}
