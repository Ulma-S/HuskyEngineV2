// コンポーネント: アクターに取り付けて、機能(見た目、体力、AI など)を足す部品の基底クラス。
// Unity の MonoBehaviour にあたる。アクターは薄い入れ物で、何ができるかはコンポーネントで決まる。
//
// 使い方:
//   class Health : public Component
//   {
//       HUSKY_OBJECT(Health, Component)
//   public:
//       void Tick(double dt) override { ... }
//       std::int32_t GetTickOrder() const override { return TickOrder::Late; } // 省略すると Default(0)
//       std::int32_t hp = 10;
//   };
//
//   Health* health = actor->AddComponent<Health>();  // 作って取り付ける(作るのは必ず AddComponent で)
//   health->GetOwner();                              // 取り付け先のアクター
//   actor->GetComponent<Health>();                   // 型で探す
//   health->Destroy();                               // 外す(フレームの終わりに消える)
#pragma once

#include "core/Guid.h"
#include "core/Object.h"

#include <cstdint>

namespace husky::engine
{
    class Actor; // 前方宣言: 「Actor というクラスがある」とだけ伝える(Actor.h が Component.h を読むので、循環を避ける)

    // 実行順(Tick を呼ぶ順番を決める整数)の、よく使う値。Unity の DefaultExecutionOrder にあたる。
    // Scene::Tick は、シーンの全コンポーネントを実行順の小さい順に呼ぶ。小さい値のものが、全アクター分
    // 終わってから、大きい値のものが始まる。同じ値どうしは、シーンの一覧の順(アクターの順、その中の付けた順)。
    // ここに無い値(たとえば Default と Late の間の 500)も自由に使ってよい。
    //
    // namespace の中に constexpr の定数を並べて、TickOrder::Late のように名前で書けるようにしている。
    // (enum class にしないのは、500 のような「間の値」をそのまま書けるようにするため)
    namespace TickOrder
    {
        constexpr std::int32_t Input = -1000;    // 入力を読んで、何をしたいかを決める
        constexpr std::int32_t Default = 0;      // 動く。アクター自身の Tick もここ(そのアクターの Default のコンポーネントより先)
        constexpr std::int32_t Late = 1000;      // 全員が動いた後の処理(カメラの追従、当たり判定の結果を見るなど)
        constexpr std::int32_t PreRender = 2000; // 描画の直前に、見た目の準備をする
    }

    // 動作スレッド: ゲームスレッドのみ
    class Component : public core::Object
    {
        HUSKY_OBJECT(Component, core::Object)

        // Actor には、取り付け先(m_owner)を設定させる。
        friend class Actor;

    public:
        // 毎フレーム、シーンの更新(Scene::Tick)から、GetTickOrder() の順番で呼ばれる。
        // 子クラスで override して、機能を書く。Destroy 済みのコンポーネントや、
        // Destroy 済みのアクターに付いたコンポーネントでは呼ばれない。
        virtual void Tick(double dt) { (void)dt; }

        // このコンポーネントの実行順(宣言)。既定は TickOrder::Default(0)。
        // 先に、または後に動かしたい子クラスは、override して返す値を変える。
        // Scene::Tick はフレームの最初にこの値を読んで順番を決めるので、フレームの途中で変えても、効くのは次のフレームから。
        virtual std::int32_t GetTickOrder() const { return TickOrder::Default; }

        // 取り付け先のアクター。もう存在しなければ nullptr。
        Actor* GetOwner() const;

        // プレハブの中で、このコンポーネントを指す変わらない ID(PF-7a。Unity の fileID にあたる)。
        //   - プレハブのファイルの中: そのコンポーネント自身の ID。プレハブを書くとき(Prefab::Write)に振る。
        //   - プレハブから置いたもの: 「プレハブのどのコンポーネントから来たか」。プレハブから作ると値が写るので、そのまま覚えている。
        //   - それ以外(コードで AddComponent したもの、プレハブに無いのに足したもの): 空(null)。
        // シーンの保存は、この ID でプレハブのコンポーネントと組にする(並び順ではないので、足した・外したものがあっても組がずれない)。
        // GUID にしているのは、番号の振り方を考えずに重ならないことと、インスペクタで読むだけ(灰色)・property.set で変えられない扱いになるため。
        core::Guid prefabComponentId;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(prefabComponentId)
        HUSKY_REFLECTION_END()

    private:
        // 取り付け先のアクター。プロパティとして登録しないので、弱い参照(GC はここをたどらない)。
        // 生かす向きは「アクター → コンポーネント」(Actor::components が強い参照)の一方向だけにする。
        // コンポーネントはアクターの部品なので、アクターが消えたら一緒に消えるのが正しく、
        // コンポーネントがアクターを生かし続けてはいけない。
        core::ObjectHandle m_owner;
    };
}
