// シーンのコライダーと、当たり判定の世界(PhysicsWorld2D)をつなぐ(COL-3)。Application が1つ持つ。
// 毎フレーム(シーンを更新した後)Update を呼ぶと、次の順で進める:
//   1. 同期: シーンの全コライダーを見て、新しいものは世界に置き、あるものは場所を伝え、消えたものは世界から外す
//      (大きさなどが変わったものは置き直す。前のフレームから動いていないものは伝えない)。
//      Rigidbody2D を持つアクターは、先に体を置き(PHY-2)、そのアクターのコライダーはすべてその体に付ける。
//      体の場所は、アクターが前に伝えた(書き戻した)場所から動いていたときだけ、瞬間移動として伝える。
//   2. 世界を決まった刻み(1/60 秒)で、貯まった分だけ進める(PHY-1。Dynamic の体が動く)。
//      進めたら、Dynamic の体の場所と回転をアクターの transform に書き戻す(Box2D → アクター。PHY-2)。
//      描くときの位置(Actor::GetRenderTransform)には、最後の 1 歩の前と今の場所を、貯まった時間の割合で混ぜたものを入れる(PHY-4)
//   3. 重なりを調べ、前のフレームと比べて、重なり始めた・離れたイベントを作る(物理で押し合う組は除く)。
//      ぶつかり始めた・離れたイベント(PHY-3)は、進めたときの Box2D の接触のイベントから作る
//   4. イベントを届ける(Actor::OnOverlap / OnCollision と、そのアクターの全コンポーネントの Component::OnOverlap / OnCollision)
// 調べ終わってからまとめて届けるので、受け取った側が何かを消したり動かしたりしても、調べている途中の一覧は崩れない。
// 順番はシーンの一覧の順(毎回同じ結果になる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/FixedStepper.h"
#include "engine/PhysicsTypes.h"
#include "engine/PhysicsWorld2D.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace husky::engine
{
    class Actor;
    class Scene;

    class PhysicsScene2D
    {
    public:
        PhysicsScene2D();

        // 上の 1〜4 を行う。
        // dt は画面のフレームの長さ。物理は決まった刻み(GetStepper)で、貯まった分だけ進める。
        void Update(Scene& scene, double dt);

        // 物理を進める決まった刻みの時計(既定は 1/60 秒、1 フレームに最大 4 歩)。
        const FixedStepper& GetStepper() const { return m_stepper; }
        // 前の Update で進めた歩数。
        int GetLastStepCount() const { return m_lastStepCount; }

        // 当たり判定の世界(ゲームが問い合わせに使う。OverlapPoint、Raycast など。結果の owner はコライダーのハンドル)。
        PhysicsWorld2D& GetWorld() { return *m_world; }
        const PhysicsWorld2D& GetWorld() const { return *m_world; }

        // 世界に置いているコライダーの数。
        std::size_t GetColliderCount() const { return m_order.size(); }
        // 世界に置いている体(Rigidbody2D)の数。
        std::size_t GetBodyCount() const { return m_bodyOrder.size(); }

    private:
        // 世界に置いた体(Rigidbody2D)1 つの控え(PHY-2)。
        struct BodyEntry
        {
            core::ObjectHandle rigidbody;
            core::ObjectHandle actor;
            PhysicsBodyHandle2D body;
            BodyDesc2D desc; // 置いたときの決め事(変わったら置き直す)
            Pose2D pose;     // 最後に伝えた、または書き戻した場所(アクターがここから動いていたら、コードが動かした)
            // 最後に体から読んだ角度(Box2D の値。-180〜180 度)。Box2D は角度を近似の sin・cos で持つので、
            // 30 度を渡して読むと 30.03 度のように少しずれる。書き戻しは「この角度からの差」だけを足し、
            // 回っていない体ではアクターの角度を 1 つも変えない
            float bodyRotation = 0.0f;
            Pose2D previous; // 最後の 1 歩の前の場所(アクターの角度で)。描くときは previous から pose までを混ぜる(PHY-4)
            bool moving = false; // このフレームにコードが動かした Kinematic(MoveBodyTo で速さを付けた。進めた後に止める)
            bool seen = false;
        };

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
            PhysicsBodyHandle2D body; // 付けている体(Rigidbody2D の体)。null なら、形だけの自分の体(AddShape)
            ShapeDesc2D desc; // 置いたときの決め事(変わったら置き直す)
            Pose2D pose;
            std::vector<Contact> contacts; // 前のフレームで重なっていた相手
            bool seen = false;             // このフレームの同期で、シーンの中に見つかったか
            bool dynamic = false;          // Dynamic の体に付いているか(物理で押し合うか。重なりとぶつかりの分け方に使う)
        };

        struct CollisionPair; // 下で定義する

        void Sync(Scene& scene);
        // アクターの Rigidbody2D に合わせて体を置く・置き直す・伝える。体が無ければ null を返す。
        // dynamic には、体が Dynamic かを入れる(体が無ければ false)。
        PhysicsBodyHandle2D SyncBody(const Actor& actor, core::ObjectRegistry& registry, const Pose2D& pose, bool& dynamic);
        // 体を世界から外す。付いていたコライダーの控えの形も「置いていない」にする(体と一緒に消えるため)。
        void RemoveBody(BodyEntry& entry);
        // Dynamic の体の場所と回転を、アクターの transform に書き戻す。
        void WriteBackBodies(Scene& scene);
        // 最後の 1 歩の前に、動く体(Static 以外)の場所を previous に控える(PHY-4)。
        void RecordPreviousPoses();
        // 動く体のアクターに、描くときの transform(previous と pose を、貯まった時間の割合で混ぜたもの)を入れる(PHY-4)。
        void ApplyInterpolation(Scene& scene);
        // コライダーのハンドルの控えを探す。無ければ(置いていない、同じ場所の別の代)nullptr。
        Entry* FindEntry(core::ObjectHandle collider);
        void DetectOverlaps();
        // 世界の接触のイベントを、ぶつかっている組とイベントにする(PHY-3)。
        void ProcessContactEvents();
        // collider を含む組を外し、両方に「離れた」を積む(コライダーが消えた・置き直したとき)。
        void DropCollisionPairs(core::ObjectHandle collider);
        void PushCollision(bool begin, const CollisionPair& pair, const core::Vector2& normal, const core::Vector2& point);
        void Deliver(Scene& scene);

        std::unique_ptr<PhysicsWorld2D> m_world;
        FixedStepper m_stepper;
        int m_lastStepCount = 0;
        int m_pendingSteps = 0; // このフレームに進める歩数(同期の前に決める)
        // 控えは、コライダーのハンドルの場所の番号(handle.index)をそのまま添字にした配列に置く(ハッシュの表より速い)。
        // 同じ場所が、消えた後に別のコライダーへ貸し直されることがあるので、控えのハンドル(世代つき)と比べて見分ける。
        // 空いている場所は collider が null。
        std::vector<Entry> m_entries;
        std::vector<std::uint32_t> m_order;         // このフレームに置いているコライダーの場所の番号(シーンの一覧の順。調べる順・届ける順をそろえる)
        std::vector<std::uint32_t> m_previousOrder; // 前のフレームの m_order(消えたものを探すときは、これだけを見る)
        // 体の控え。コライダーと同じく、Rigidbody2D のハンドルの場所の番号を添字にする。
        std::vector<BodyEntry> m_bodies;
        std::vector<std::uint32_t> m_bodyOrder;
        std::vector<std::uint32_t> m_previousBodyOrder;
        // 使い回す入れ物(毎フレームの確保を避ける)
        std::vector<QueryHit2D> m_hits;
        std::vector<Contact> m_current;
        std::vector<OverlapEvent> m_events;
        // ぶつかっている組(PHY-3。Box2D の「離れた」は、形が消えた後だと持ち主が分からないので、組は自分で覚えて片付ける)
        struct CollisionPair
        {
            core::ObjectHandle colliderA;
            core::ObjectHandle colliderB;
            core::ObjectHandle actorA;
            core::ObjectHandle actorB;
        };
        // 届けるぶつかりのイベント 1 つ(届け先のアクターも覚える)
        struct PendingCollision
        {
            core::ObjectHandle selfActor;
            CollisionEvent event;
        };
        std::vector<CollisionPair> m_collisionPairs;
        std::vector<ContactEvent2D> m_contactEvents;
        std::vector<PendingCollision> m_collisionEvents;
    };
}
