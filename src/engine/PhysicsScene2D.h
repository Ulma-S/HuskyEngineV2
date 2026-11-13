// シーンのコライダーと、当たり判定の世界(PhysicsWorld2D)をつなぐ(COL-3)。Application が1つ持つ。
// 毎フレーム(シーンを更新した後)Update を呼ぶと、次の順で進める:
//   1. 同期: シーンの全コライダーを見て、新しいものは世界に置き、あるものは場所を伝え、消えたものは世界から外す
//      (大きさなどが変わったものは置き直す。前のフレームから動いていないものは伝えない)。位置の向きは「アクター → 世界」だけ(物理を足したら逆向きも足す)
//   2. 世界を dt 進める(今は物理が無いので、準備だけ)
//   3. 重なりを調べ、前のフレームと比べて、重なり始めた・離れたイベントを作る
//   4. イベントを届ける(Actor::OnOverlap と、そのアクターの全コンポーネントの Component::OnOverlap)
// 調べ終わってからまとめて届けるので、受け取った側が何かを消したり動かしたりしても、調べている途中の一覧は崩れない。
// 順番はシーンの一覧の順(毎回同じ結果になる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/PhysicsTypes.h"
#include "engine/PhysicsWorld2D.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace husky::engine
{
    class Scene;

    class PhysicsScene2D
    {
    public:
        PhysicsScene2D();

        // 上の 1〜4 を行う。
        void Update(Scene& scene, float dt);

        // 当たり判定の世界(ゲームが問い合わせに使う。OverlapPoint、Raycast など。結果の owner はコライダーのハンドル)。
        PhysicsWorld2D& GetWorld() { return *m_world; }
        const PhysicsWorld2D& GetWorld() const { return *m_world; }

        // 世界に置いているコライダーの数。
        std::size_t GetColliderCount() const { return m_order.size(); }

    private:
        // 重なっている相手 1 つ(相手が消えた後の「離れた」でもアクターを渡せるように、アクターも覚える)。
        struct Contact
        {
            core::ObjectHandle collider;
            core::ObjectHandle actor;
        };

        // 世界に置いたコライダー 1 つの控え。
        struct Entry
        {
            core::ObjectHandle collider;
            core::ObjectHandle actor;
            PhysicsShapeHandle2D shape;
            ShapeDesc2D desc; // 置いたときの決め事(変わったら置き直す)
            Pose2D pose;
            std::vector<Contact> contacts; // 前のフレームで重なっていた相手
            bool seen = false;             // このフレームの同期で、シーンの中に見つかったか
        };

        void Sync(Scene& scene);
        // コライダーのハンドルの控えを探す。無ければ(置いていない、同じ場所の別の代)nullptr。
        Entry* FindEntry(core::ObjectHandle collider);
        void DetectOverlaps();
        void Deliver(Scene& scene);

        std::unique_ptr<PhysicsWorld2D> m_world;
        // 控えは、コライダーのハンドルの場所の番号(handle.index)をそのまま添字にした配列に置く(ハッシュの表より速い)。
        // 同じ場所が、消えた後に別のコライダーへ貸し直されることがあるので、控えのハンドル(世代つき)と比べて見分ける。
        // 空いている場所は collider が null。
        std::vector<Entry> m_entries;
        std::vector<std::uint32_t> m_order;         // このフレームに置いているコライダーの場所の番号(シーンの一覧の順。調べる順・届ける順をそろえる)
        std::vector<std::uint32_t> m_previousOrder; // 前のフレームの m_order(消えたものを探すときは、これだけを見る)
        // 使い回す入れ物(毎フレームの確保を避ける)
        std::vector<QueryHit2D> m_hits;
        std::vector<Contact> m_current;
        std::vector<OverlapEvent> m_events;
    };
}
