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
#include <type_traits>
#include <utility>
#include <vector>

namespace husky::engine
{
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
                actors.push_back(actor->GetHandle());
            }
            return actor;
        }

        // 全アクターの Tick を、一覧の順に呼ぶ。Destroy 済みのアクターは飛ばす。
        // 各アクターの Tick の直後に、そのアクターのコンポーネントの Tick を呼ぶ(Actor::TickComponents)。
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
        // ハンドルからアクターを引く。もう無いか、レジストリで作っていないシーンなら nullptr。
        Actor* ResolveActor(core::ObjectHandle handle) const
        {
            core::ObjectRegistry* registry = GetRegistry();
            return registry != nullptr ? registry->Resolve<Actor>(handle) : nullptr;
        }
    };
}
