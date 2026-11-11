// コンポーネント: アクターに取り付けて、機能(見た目、体力、AI など)を足す部品の基底クラス。
// Unity の MonoBehaviour にあたる。アクターは薄い入れ物で、何ができるかはコンポーネントで決まる。
//
// 使い方:
//   class Health : public Component
//   {
//       HUSKY_OBJECT(Health, Component)
//   public:
//       void Tick(double dt) override { ... }
//       std::int32_t hp = 10;
//   };
//
//   Health* health = actor->AddComponent<Health>();  // 作って取り付ける(作るのは必ず AddComponent で)
//   health->GetOwner();                              // 取り付け先のアクター
//   actor->GetComponent<Health>();                   // 型で探す
//   health->Destroy();                               // 外す(フレームの終わりに消える)
#pragma once

#include "core/Object.h"

namespace husky::engine
{
    class Actor; // 前方宣言: 「Actor というクラスがある」とだけ伝える(Actor.h が Component.h を読むので、循環を避ける)

    // 動作スレッド: ゲームスレッドのみ
    class Component : public core::Object
    {
        HUSKY_OBJECT(Component, core::Object)

        // Actor には、取り付け先(m_owner)を設定させる。
        friend class Actor;

    public:
        // 毎フレーム、シーンの更新(Scene::Tick)から、持ち主のアクターの Tick の後に呼ばれる。
        // 子クラスで override して、機能を書く。Destroy 済みのコンポーネントや、
        // Destroy 済みのアクターに付いたコンポーネントでは呼ばれない。
        virtual void Tick(double dt) { (void)dt; }

        // 取り付け先のアクター。もう存在しなければ nullptr。
        Actor* GetOwner() const;

    private:
        // 取り付け先のアクター。プロパティとして登録しないので、弱い参照(GC はここをたどらない)。
        // 生かす向きは「アクター → コンポーネント」(Actor::components が強い参照)の一方向だけにする。
        // コンポーネントはアクターの部品なので、アクターが消えたら一緒に消えるのが正しく、
        // コンポーネントがアクターを生かし続けてはいけない。
        core::ObjectHandle m_owner;
    };
}
