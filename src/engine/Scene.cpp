// Scene.h の実装。
#include "engine/Scene.h"

#include <algorithm> // std::remove_if

namespace husky::engine
{
    void Scene::Tick(double dt)
    {
        // 回す数を最初に決めておく。Tick の中で Spawn されると actors の末尾に足されるが、
        // それは次のフレームから更新する。
        const std::size_t count = actors.size();

        // 範囲 for(for (auto h : actors))ではなく番号で回す。
        // Spawn で actors が大きくなると、vector は中身を別のメモリへ引っ越すことがあり、
        // 範囲 for が使っている「今どこを見ているか」が無効になってしまうため。
        // 番号なら、引っ越した後でも actors[i] で正しい場所を見られる。
        for (std::size_t i = 0; i < count; ++i)
        {
            Actor* actor = ResolveActor(actors[i]);
            if (actor != nullptr && !actor->IsPendingDestroy())
            {
                actor->Tick(dt);
                // アクターの Tick の中で自分を Destroy したら、コンポーネントは更新しない。
                if (!actor->IsPendingDestroy())
                {
                    actor->TickComponents(dt);
                }
            }
        }
    }

    std::size_t Scene::RemoveDestroyedActors()
    {
        // 先にコンポーネントを片付ける。
        // Destroy 済みのアクターは、そのコンポーネントもまとめて Destroy する(部品はアクターと一緒に消える)。
        // 生きているアクターは、Destroy 済みのコンポーネントを一覧から外す。
        // どちらも FlushDestroyed の前なので、同じフレームの終わりに破棄が確定する。
        for (const core::ObjectHandle handle : actors)
        {
            Actor* actor = ResolveActor(handle);
            if (actor == nullptr)
            {
                continue;
            }
            if (actor->IsPendingDestroy())
            {
                actor->DestroyComponents();
            }
            else
            {
                actor->RemoveDestroyedComponents();
            }
        }

        const std::size_t before = actors.size();
        // Destroy 済みか、もう存在しないアクターのハンドルを、一覧から消す。
        actors.erase(std::remove_if(actors.begin(), actors.end(),
                                    [this](core::ObjectHandle handle) {
                                        const Actor* actor = ResolveActor(handle);
                                        return actor == nullptr || actor->IsPendingDestroy();
                                    }),
                     actors.end());
        return before - actors.size();
    }
}
