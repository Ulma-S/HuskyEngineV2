// シーン: 今動いているアクターの一覧を持つ。GC の根(ルート)にする。
//
// 使い方:
//   core::ObjectRegistry registry;
//   Scene* scene = registry.Create<Scene>();
//   registry.AddRoot(scene->GetHandle());         // シーンを根にすると、一覧のアクターは GC で回収されない
//
//   Enemy* enemy = scene->Spawn<Enemy>();         // アクターを作って一覧に入れる
//   ...
//   // 毎フレーム
//   scene->Tick(dt);                              // 全アクターを更新
//   scene->RemoveDestroyedActors();               // フレームの終わり: Destroy 済みのアクターを一覧から外し、
//   registry.FlushDestroyed();                    //                   破棄を確定する
//
// アクターを消すときは actor->Destroy() を呼ぶ(Unity の Destroy と同じく、フレームの終わりに消える)。
#pragma once

#include "core/ObjectRegistry.h"
#include "engine/Actor.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace husky::engine
{
    class Input; // 前方宣言: ポインタで持つだけなので、中身(Input.h)は要らない

    // 動作スレッド: ゲームスレッドのみ
    class Scene : public core::Object
    {
        HUSKY_OBJECT(Scene, core::Object)
    public:
        // シーンは ObjectRegistry で作ること(registry.Create<Scene>())。
        // アクターは、シーンを作ったのと同じレジストリで作る(GetRegistry() で取れる)。

        // T 型のアクターを作り、一覧の末尾に入れる。メモリが足りなければ nullptr。
        // シーンをレジストリで作っていない場合も nullptr。
        // 一覧の順番が、更新(Tick)と描画の順番になる(後から入れたものが後に更新され、手前に描かれる)。
        // Tick の途中で呼んでもよい。そのアクターは次のフレームから更新される。
        template <typename T, typename... Args>
        T* Spawn(Args&&... args)
        {
            static_assert(std::is_base_of_v<Actor, T>, "Spawn できるのは Actor の子孫だけです");
            core::ObjectRegistry* registry = GetRegistry();
            if (registry == nullptr)
            {
                return nullptr;
            }
            T* actor = registry->Create<T>(std::forward<Args>(args)...);
            if (actor != nullptr)
            {
                // T に同じ名前のメンバーがあっても大丈夫なように、Actor& として書く。
                // 設定するのはコンストラクタの後なので、アクターのコンストラクタの中では GetScene() は nullptr。
                Actor& base = *actor;
                base.m_scene = GetHandle();
                if (base.guid.IsNull())
                {
                    base.guid = core::Guid::Generate(); // 変わらない ID(Actor::guid)を振る
                }
                actors.push_back(actor->GetHandle());
            }
            return actor;
        }

        // このシーンを作ったレジストリ(アクターやコンポーネントもここにある)。レジストリで作っていなければ nullptr。
        // 保存・読み込み(SceneSerializer)で、ハンドルを引いたりオブジェクトを作ったりするのに使う。
        // (Object::GetRegistry は protected。同じ名前のメンバーを作ると名前が隠れるので、別の名前にしている)
        core::ObjectRegistry* GetObjectRegistry() const { return GetRegistry(); }

        // Spawn ではなく、ほかの方法(ファイルからの読み込みなど)で作ったアクターを、一覧の末尾に入れる。
        // Spawn と同じく、アクターにシーンを覚えさせる。あわせて、アクターの components に並んでいるコンポーネントに、
        // 持ち主(このアクター)を覚えさせ直す(読み込みでは、持ち主の情報はプロパティではないので、ファイルに入っていないため)。
        // actor は、このシーンと同じレジストリで作ったもので、まだどのシーンにも入っていないこと。戻り値: 入れられたら true。
        bool AddLoadedActor(Actor& actor);

        // 変わらない ID(Actor::guid)で、生きている(Destroy 済みでない)アクターを探す。無ければ nullptr。
        // アクターの数だけ順に見る(エディタのコマンドから呼ぶ程度なら十分速い。毎フレーム大量に呼ぶなら表を作る)。
        Actor* FindActorByGuid(const core::Guid& guid);

        // エンジンの入力。ゲームループが SetInput で渡す。渡されていなければ nullptr。
        // アクターやコンポーネントは GetScene()->GetInput() で読む(自分で Input を持ったり、配られたりしない)。
        const Input* GetInput() const { return m_input; }

        // 入力を渡す。シーンは借りるだけで、持ち主にはならない(Input の寿命は呼んだ側が決める)。
        // input はシーンより長く生きていること。外すときは nullptr を渡す。
        void SetInput(const Input* input) { m_input = input; }

        // 全アクターと全コンポーネントの Tick を、実行順(TickOrder)の小さい順に呼ぶ。
        //   - アクター自身の Tick の実行順は TickOrder::Default(0)。
        //   - 実行順が同じものは、シーンの一覧の順(アクターの順。1つのアクターの中では、アクター自身 → 付けた順のコンポーネント)。
        //   - 順番はフレームの最初に決める。このフレームの途中で Spawn / AddComponent したものは、次のフレームから更新する。
        //   - Destroy 済みのアクターと、そのコンポーネント、Destroy 済みのコンポーネントは飛ばす(フレームの途中で Destroy されたものも)。
        void Tick(double dt);

        // Destroy 済み(またはもう存在しない)アクターを、一覧から外す。戻り値: 外した数。
        // フレームの終わりに、ObjectRegistry::FlushDestroyed の前に呼ぶ。
        // あわせて、Destroy 済みのアクターのコンポーネントを Destroy し、
        // 生きているアクターからは Destroy 済みのコンポーネントを一覧から外す。
        std::size_t RemoveDestroyedActors();

        // 生きている(Destroy 済みでない)全アクターについて、function(actor) を一覧の順に呼ぶ。描画などに使う。
        // function の中で Spawn しないこと(一覧が変わってしまう)。
        template <typename Function>
        void ForEachActor(Function&& function)
        {
            for (const core::ObjectHandle handle : actors)
            {
                Actor* actor = ResolveActor(handle);
                if (actor != nullptr && !actor->IsPendingDestroy())
                {
                    function(*actor);
                }
            }
        }

        // シーンにいるアクターのハンドルの一覧。プロパティとして登録しているので、GC がたどる(強い参照)。
        std::vector<core::ObjectHandle> actors;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(actors)
        HUSKY_REFLECTION_END()

    private:
        // Tick の順番表の1行。1行が「アクター自身の Tick」か「コンポーネント1つの Tick」の1回分。
        struct TickEntry
        {
            std::int32_t order = 0;       // 実行順(小さいほうが先)
            std::uint32_t sequence = 0;   // 一覧の中の並び順(実行順が同じときに、これで並べる)
            core::ObjectHandle actor;     // 呼ぶアクター(コンポーネントの行なら、その持ち主)
            core::ObjectHandle component; // 呼ぶコンポーネント。isActor が true の行では使わない
            bool isActor = false;         // true: アクター自身の Tick の行
        };

        // Tick の順番表。毎フレーム作り直すが、vector のメモリは使い回す(毎フレームの確保を避けるため)。
        // ハンドルを持っているが、プロパティではないので GC はたどらない(Tick の間だけ使う、弱い参照)。
        std::vector<TickEntry> m_tickEntries;

        // 借りている入力(SetInput で渡されたもの)。GC 管理の型ではないので、ハンドルではなくポインタ。
        const Input* m_input = nullptr;

        // ハンドルからアクターを引く。もう無いか、レジストリで作っていないシーンなら nullptr。
        Actor* ResolveActor(core::ObjectHandle handle) const
        {
            core::ObjectRegistry* registry = GetRegistry();
            return registry != nullptr ? registry->Resolve<Actor>(handle) : nullptr;
        }
    };
}
