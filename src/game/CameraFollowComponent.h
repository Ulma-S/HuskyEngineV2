// 追いかけるコンポーネント(ゲームのコード。「誰をどう追うか」はゲームごとの決め事なので、エンジンではなくゲームに置く)。付けたアクター(ふつうはカメラ)を、毎フレーム、相手(target)の位置 + offset へ動かす。
//
//   Actor* camera = ...;                                  // CameraComponent を付けたアクター
//   CameraFollowComponent* follow = camera->AddComponent<CameraFollowComponent>();
//   follow->target = player->GetHandle();                 // 追いかける相手
//   follow->offset = {64, 64, 0};                         // 相手の位置からのずらし(例: スプライトの真ん中を映す)
//
// 動かすのは、全員が動いた後(TickOrder::Late)。相手より先に動くと、1 フレーム前の位置を追うことになり、
// 相手が画面の中でぶれて見える。
// 編集中(EDIT)はシーンの Tick が止まるので、追いかけない(Play で動き出す)。
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Handle.h"
#include "core/Vector3.h"
#include "engine/Component.h"

namespace husky::game
{

    class CameraFollowComponent : public engine::Component
    {
        HUSKY_OBJECT(CameraFollowComponent, engine::Component)
    public:
        // 追いかける相手のアクター。プロパティなので強い参照(相手が GC で回収されない)で、保存もされる。
        // 何も指さない、または相手が Destroy されたら、その場に止まる。
        core::ObjectHandle target;
        // 相手の位置(transform.position)からのずらし。z は動かさない(カメラの奥行きはそのまま)。
        core::Vector3 offset;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(target)
            HUSKY_PROPERTY(offset)
        HUSKY_REFLECTION_END()

        void Tick(double dt) override;

        std::int32_t GetTickOrder() const override { return engine::TickOrder::Late; }

    private:
        // 追いかける相手を引く。いない(何も指さない、Destroy 済み)なら nullptr。
        const engine::Actor* FindTarget() const;
    };
}
