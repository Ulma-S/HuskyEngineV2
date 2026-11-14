// アクター: シーンに置かれる、ゲーム上の意味を持つもの(敵、プレイヤー、弾など)の基底クラス。
// 名前と Transform(位置・回転・拡大縮小)だけを持つ薄い入れ物。機能(見た目、体力、AI など)は、コンポーネント(Component.h)で足す。
#pragma once

#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/Matrix4.h"
#include "core/ObjectRegistry.h"
#include "engine/Component.h"
#include "engine/PrefabAsset.h"
#include "engine/Transform.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace husky::engine
{
    class Scene; // 前方宣言(Scene.h が Actor.h を読むので、ここでは読まない)

    // 動作スレッド: ゲームスレッドのみ
    class Actor : public core::Object
    {
        HUSKY_OBJECT(Actor, core::Object)
    public:
        // このアクターがいるシーン。Scene::Spawn で作ったものだけが持つ。
        // シーンの外で作った(registry.Create<Actor>() など)か、シーンがもう無ければ nullptr。
        // シーンからは、エンジンの機能(入力など)を引ける(Scene::GetInput など)。
        Scene* GetScene() const;

        // 毎フレーム、シーンの更新(Scene::Tick)から呼ばれる。dt はこのフレームの経過時間(秒)。
        // 子クラスで override して、動きを書く。Destroy 済みのアクターでは呼ばれない。
        // 実行順は TickOrder::Default(0)で、このアクターの Default のコンポーネントより先に呼ばれる。
        virtual void Tick(double dt) { (void)dt; }

        // このアクターのコライダーが、ほかのアクターのコライダーと重なり始めた・離れたときに呼ばれる(COL-3)。
        // 同じイベントは、このアクターの全コンポーネントの Component::OnOverlap にも届く。シーンを更新している間だけ。
        virtual void OnOverlap(const OverlapEvent& event) { (void)event; }
        // このアクターのコライダーが、物理でほかのコライダーとぶつかり始めた・離れたときに呼ばれる(PHY-3)。
        // 同じイベントは、このアクターの全コンポーネントの Component::OnCollision にも届く。
        virtual void OnCollision(const CollisionEvent& event) { (void)event; }

        // T 型のコンポーネントを作り、このアクターに取り付ける。メモリが足りなければ nullptr。
        // アクターをレジストリで作っていない場合も nullptr。
        // 一覧の順番が、コンポーネントの更新の順番になる。
        // Tick の途中で呼んでもよい。そのコンポーネントは次のフレームから更新される。
        template <typename T>
        T* AddComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "AddComponent できるのは Component の子孫だけです");
            core::ObjectRegistry* registry = GetRegistry();
            if (registry == nullptr)
            {
                return nullptr;
            }
            T* component = registry->Create<T>();
            if (component == nullptr)
            {
                return nullptr;
            }
            // T に同じ名前のメンバーがあっても大丈夫なように、Component& として書く(Object の m_handle と同じ理由)。
            Component& base = *component;
            base.m_owner = GetHandle();
            components.push_back(component->GetHandle());
            return component;
        }

        // 取り付けた T 型(か T の子孫)のコンポーネントのうち、一覧で最初のものを返す。無ければ nullptr。
        // Destroy 済みのものは返さない。
        template <typename T>
        T* GetComponent() const
        {
            static_assert(std::is_base_of_v<Component, T>, "GetComponent できるのは Component の子孫だけです");
            for (const core::ObjectHandle handle : components)
            {
                T* component = ResolveComponent<T>(handle);
                if (component != nullptr && !component->IsPendingDestroy())
                {
                    return component;
                }
            }
            return nullptr;
        }

        // 作ってある component(型の登録簿で名前から作ったものなど)を、一覧の index 番目に差し込んで取り付ける(PF-7c)。
        // index が一覧の長さ以上なら末尾。エディタが型を文字列で受け取って足すとき(component.add)に使う。
        // AddComponent<T>() はテンプレートで、型をコードに書く必要があるため、型が実行するまで分からないときはこちらを使う。
        // 戻り値: 取り付けた位置。
        std::size_t InsertComponent(Component& component, std::size_t index);

        // component を一覧からすぐに外し、Destroy の印を付ける(メモリはフレームの終わりに片付く)(PF-7c)。
        // 一覧からすぐに外すので、同じフレームのうちに別のものを同じ位置へ差し込んでも、並びが崩れない。
        // 戻り値: 外す前の位置。このアクターに付いていなければ -1(何もしない)。
        std::int64_t RemoveComponent(Component& component);

        std::string name; // 人が読む名前。ほかのアクターと重なってもよい(区別には guid を使う)
        // このアクターの変わらない ID。シーンに入れたとき(Scene::Spawn、AddLoadedActor)に、まだ無ければ振られる。
        // 保存しても読み戻しても、Play / Stop で作り直しても変わらない。エディタのコマンド(CLI や AI)がアクターを指すのに使う。
        // 人は名前で区別すればよく、GUID を意識しなくてよい(インスペクタでは灰色の文字で読むだけ)。
        // 書き換えてよいのは、シーンの読み込み(重なりを直すとき)だけ。
        core::Guid guid;
        // 位置・回転・拡大縮小(Transform.h)。位置はワールドの単位で、右が +x、上が +y。
        // 今のカメラ(zoom 1)では 1 単位 = 1 ピクセル。保存すると {"position": {"x", "y", "z"}, "rotation", "scale": {...}}。
        Transform transform;

        // このアクターを作った元のプレハブ(PF-3)。プレハブから置いたとき(Prefab::Instantiate)に書かれ、そうでなければ何も指さない。
        // 保存される(GUID)。強い参照なので、置いたアクターがいる間は、プレハブのアセットも回収されない。
        // 今は「どこから来たか」を覚えるだけ。PF-4 で、プレハブとの違いだけを保存し、プレハブを直すと伝わるようにする。
        core::AssetRef<PrefabAsset> prefab;

        // 取り付けたコンポーネントのハンドルの一覧。プロパティとして登録しているので、GC がたどる(強い参照)。
        // アクターが生きている間は、そのコンポーネントも生きている。
        std::vector<core::ObjectHandle> components;

        // 描くときに使う transform(PHY-4)。物理の体は 1/60 秒ごとにしか動かないので、PhysicsScene2D が前の歩と今の歩の間を
        // 混ぜた(補間した)位置を入れる。ゲームの決め事・保存・当たり判定は、いつも本当の transform を使う。
        // 入れた後に transform が書き換えられていたら(コードやエディタが動かした)、古いので transform をそのまま返す。
        const Transform& GetRenderTransform() const
        {
            return m_hasRenderTransform && SameTransform(m_renderBase, transform) ? m_renderTransform : transform;
        }
        // 描くときの transform を入れる(今の transform を基準として覚える)。
        void SetRenderTransform(const Transform& renderTransform)
        {
            m_renderTransform = renderTransform;
            m_renderBase = transform;
            m_hasRenderTransform = true;
        }
        void ClearRenderTransform() { m_hasRenderTransform = false; }

        // ---- 親子の階層(HIER-1)----
        // transform は親から見たローカルの値。ワールドの行列 = 親のワールドの行列 × 自分のローカルの行列。
        // 親が無ければ、ローカル = ワールド(今までどおり)。
        // 今は保存しない(HIER-2 で保存する)。親を消しても子は消えず、そのときのワールドの場所のまま親なしになる(HIER-2 で一緒に消す)。

        // 親。無い(か、もう消えた)なら nullptr。
        Actor* GetParent() const;
        // 子のハンドルの一覧(付けた順)。消えたものは、フレームの終わりに外れる。
        const std::vector<core::ObjectHandle>& GetChildren() const { return m_children; }
        // 親を付け替える。nullptr なら親なしにする。keepWorldTransform が true なら、画面で見える場所(ワールドの値)を保つよう
        // transform を直す。false なら transform をそのまま新しい親から見た値として使う(見える場所が変わる)。
        // 断るとき(false を返し、警告を出す): 自分か自分の子孫を親にする(輪になる)、別のレジストリ・消えかけのアクター、
        // 自分が Rigidbody2D を持つ(物理がワールドの場所を決めるので、最初は動く体は親を持てない)。
        bool SetParent(Actor* parent, bool keepWorldTransform = true);
        // 本当の transform から作ったワールドの行列(当たり判定、エディタ、ゲームの決め事に使う)。
        core::Matrix4 GetWorldMatrix() const;
        // 描くときの transform(GetRenderTransform。物理の補間)から作ったワールドの行列。親も描くときの値を使う。
        core::Matrix4 GetRenderWorldMatrix() const;
        // ワールドの位置・回転・拡大縮小。親が無ければ transform そのもの(誤差なし)。
        Transform GetWorldTransform() const;
        // ワールドの値が world になるよう、transform(ローカル)を書く。親が無ければ transform = world。
        void SetWorldTransform(const Transform& world);
        core::Vector3 GetWorldPosition() const;
        // ワールドの位置だけを変える(回転・拡大縮小のローカルの値はそのまま)。
        void SetWorldPosition(const core::Vector3& position);

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(name)
            HUSKY_PROPERTY(guid)
            HUSKY_PROPERTY(transform)
            HUSKY_PROPERTY(prefab)
            HUSKY_PROPERTY(components)
        HUSKY_REFLECTION_END()

    private:
        static bool SameTransform(const Transform& a, const Transform& b)
        {
            return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
        }

        // 描くときの transform(保存しない)。m_renderBase は、入れたときの本当の transform
        Transform m_renderTransform;
        Transform m_renderBase;
        bool m_hasRenderTransform = false;

        // 親と子(HIER-1)。プロパティにしないので弱い参照(生かしているのはシーンの一覧)。保存は HIER-2。
        core::ObjectHandle m_parent;
        std::vector<core::ObjectHandle> m_children;

        // 子の一覧から child を外す(無ければ何もしない)。
        void RemoveChild(core::ObjectHandle child);
        // Destroy 済みのアクターを親子から外す(フレームの終わりに Scene が呼ぶ)。子はワールドの場所のまま親なしになる。
        void DetachFromHierarchy();

        // 下のコンポーネントの更新と片付けは、Scene だけが呼ぶ。
        // friend: Scene には private なメンバーを触らせる(ゲームのコードから直接呼ばれないようにするため)。
        friend class Scene;

        // components に並んでいる全コンポーネントに、持ち主(このアクター)を覚えさせ直す。
        // ファイルから読み込んだときに、Scene::AddLoadedActor が呼ぶ。
        void RelinkComponentOwners();

        // 全コンポーネントの Destroy を呼ぶ。アクターを Destroy したときに、Scene が呼ぶ。
        void DestroyComponents();

        // Destroy 済み(またはもう存在しない)コンポーネントを、一覧から外す。戻り値: 外した数。
        // フレームの終わりに、ObjectRegistry::FlushDestroyed の前に呼ぶ(Scene::RemoveDestroyedActors が呼ぶ)。
        std::size_t RemoveDestroyedComponents();

        // このアクターがいるシーン。Scene::Spawn が設定する。
        // プロパティとして登録しないので、弱い参照(アクターがシーンを生かし続けることはない。生かす向きはシーン → アクター)。
        core::ObjectHandle m_scene;

        // ハンドルからコンポーネントを引く。もう無いか、型が違うか、レジストリで作っていなければ nullptr。
        template <typename T>
        T* ResolveComponent(core::ObjectHandle handle) const
        {
            core::ObjectRegistry* registry = GetRegistry();
            return registry != nullptr ? registry->Resolve<T>(handle) : nullptr;
        }
    };
}
