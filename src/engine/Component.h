// コンポーネント: アクターに取り付けて、機能(見た目、体力、AI など)を足す部品の基底クラス。
// Unity の MonoBehaviour にあたる。アクターは薄い入れ物で、何ができるかはコンポーネントで決まる。
//
// 使い方:
//   class Health : public Component
//   {
//       HUSKY_OBJECT(Health, Component)
//   public:
//       void Tick(double dt) override { ... }
//       TickPhase GetTickPhase() const override { return TickPhase::PostUpdate; } // 省略すると Update
//       std::int32_t hp = 10;
//   };
//
//   Health* health = actor->AddComponent<Health>();  // 作って取り付ける(作るのは必ず AddComponent で)
//   health->GetOwner();                              // 取り付け先のアクター
//   actor->GetComponent<Health>();                   // 型で探す
//   health->Destroy();                               // 外す(フレームの終わりに消える)
#pragma once

#include "core/Object.h"

#include <cstdint>

namespace husky::engine
{
    class Actor; // 前方宣言: 「Actor というクラスがある」とだけ伝える(Actor.h が Component.h を読むので、循環を避ける)

    // コンポーネントの Tick が呼ばれる段階(フェーズ)。Scene::Tick は、この順に1フェーズずつ、
    // シーンの全アクターのコンポーネントを回す。あるフェーズは、前のフェーズが全員分終わってから始まる。
    //
    // enum class: C# の enum と同じく、名前の付いた整数の集まり。TickPhase::Update のように型名を付けて書く。
    // 「: std::uint8_t」は、中身を 1 バイトの整数にするという指定。
    enum class TickPhase : std::uint8_t
    {
        Input,      // 入力を読んで、何をしたいかを決める
        Update,     // 動く。アクター自身の Tick もここ(そのアクターの Update のコンポーネントより先)
        PostUpdate, // 全員が動いた後の処理(カメラの追従、当たり判定の結果を見るなど)
        PreRender,  // 描画の直前に、見た目の準備をする
        Count,      // フェーズの数(フェーズではない。for で回すのに使う)
    };

    // 動作スレッド: ゲームスレッドのみ
    class Component : public core::Object
    {
        HUSKY_OBJECT(Component, core::Object)

        // Actor には、取り付け先(m_owner)を設定させる。
        friend class Actor;

    public:
        // 毎フレーム、シーンの更新(Scene::Tick)から、GetTickPhase() のフェーズで呼ばれる。
        // 子クラスで override して、機能を書く。Destroy 済みのコンポーネントや、
        // Destroy 済みのアクターに付いたコンポーネントでは呼ばれない。
        virtual void Tick(double dt) { (void)dt; }

        // このコンポーネントの Tick を、どのフェーズで呼ぶか(宣言)。既定は Update。
        // 別のフェーズで動かしたい子クラスは、override して返す値を変える。
        // 型ごとに決まった値を返すこと(同じオブジェクトで、途中で値を変えない)。
        virtual TickPhase GetTickPhase() const { return TickPhase::Update; }

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
