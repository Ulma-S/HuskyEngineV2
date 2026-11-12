// アクター: シーンに置かれる、ゲーム上の意味を持つもの(敵、プレイヤー、弾など)の基底クラス。
// 名前と Transform(位置・回転・拡大縮小)だけを持つ薄い入れ物。機能(見た目、体力、AI など)は、コンポーネント(Component.h)で足す。
#pragma once

#include "core/Guid.h"
#include "core/ObjectRegistry.h"
#include "engine/Component.h"
#include "engine/Transform.h"

#include <cstddef>
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

        std::string name; // 人が読む名前。ほかのアクターと重なってもよい(区別には guid を使う)
        // このアクターの変わらない ID。シーンに入れたとき(Scene::Spawn、AddLoadedActor)に、まだ無ければ振られる。
        // 保存しても読み戻しても、Play / Stop で作り直しても変わらない。エディタのコマンド(CLI や AI)がアクターを指すのに使う。
        // 人は名前で区別すればよく、GUID を意識しなくてよい(インスペクタでは灰色の文字で読むだけ)。
        // 書き換えてよいのは、シーンの読み込み(重なりを直すとき)だけ。
        core::Guid guid;
        // 位置・回転・拡大縮小(Transform.h)。位置はワールドの単位で、右が +x、上が +y。
        // 今のカメラ(zoom 1)では 1 単位 = 1 ピクセル。保存すると {"position": {"x", "y", "z"}, "rotation", "scale": {...}}。
        Transform transform;

        // 取り付けたコンポーネントのハンドルの一覧。プロパティとして登録しているので、GC がたどる(強い参照)。
        // アクターが生きている間は、そのコンポーネントも生きている。
        std::vector<core::ObjectHandle> components;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(name)
            HUSKY_PROPERTY(guid)
            HUSKY_PROPERTY(transform)
            HUSKY_PROPERTY(components)
        HUSKY_REFLECTION_END()

    private:
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
