// ワールド: 今動いているアクターの平らな一覧を持つ(世界は1つ)。GC の根(ルート)にする。
// SC-2a(2026-11-14)で Scene から改名した。「シーン」はこれから、アクターをまとめて読む・下ろす入れ物の名前にする。
// 変数の名前は、まだ多くの場所で scene のまま(型は World)。
//
// 使い方:
//   core::ObjectRegistry registry;
//   World* world = registry.Create<World>();
//   registry.AddRoot(world->GetHandle());         // ワールドを根にすると、一覧のアクターは GC で回収されない
//
//   Enemy* enemy = world->Spawn<Enemy>();         // アクターを作って一覧に入れる
//   ...
//   // 毎フレーム
//   world->Tick(dt);                              // 全アクターを更新
//   world->RemoveDestroyedActors();               // フレームの終わり: Destroy 済みのアクターを一覧から外し、
//   registry.FlushDestroyed();                    //                   破棄を確定する
//
// アクターを消すときは actor->Destroy() を呼ぶ(Unity の Destroy と同じく、フレームの終わりに消える)。
#pragma once

#include "core/ObjectRegistry.h"
#include "engine/Actor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace husky::audio
{
    class AudioDevice; // 前方宣言(ポインタで持つだけ)
}

namespace husky::engine
{
    class Input; // 前方宣言: ポインタで持つだけなので、中身(Input.h)は要らない
    class AssetLoader; // 同じく前方宣言(ポインタで持つだけ)
    class Scene;       // 入れ物のシーン(Scene.h)

    // 動作スレッド: ゲームスレッドのみ
    class World : public core::Object
    {
        HUSKY_OBJECT(World, core::Object)
    public:
        // シーンは ObjectRegistry で作ること(registry.Create<World>())。
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
                // 設定するのはコンストラクタの後なので、アクターのコンストラクタの中では GetWorld() は nullptr。
                Actor& base = *actor;
                base.m_world = GetHandle();
                base.m_scene = GetRootSceneHandle(); // 置き場所を指定しなければルートシーン(SC-2b)
                if (base.guid.IsNull())
                {
                    base.guid = core::Guid::Generate(); // 変わらない ID(Actor::guid)を振る
                }
                actors.push_back(actor->GetHandle());
            }
            return actor;
        }

        // T 型のアクターを作り、scene に入れる(Spawn と MoveToScene)。作れなければ nullptr。
        template <typename T, typename... Args>
        T* SpawnInScene(Scene& scene, Args&&... args)
        {
            T* actor = Spawn<T>(std::forward<Args>(args)...);
            if (actor != nullptr)
            {
                MoveToScene(*actor, scene);
            }
            return actor;
        }

        // ---- シーン(SC-2b。入れ物の木。Scene.h) ----

        // ルートシーン。無ければ作る(ワールドをレジストリで作っていなければ nullptr)。
        Scene* GetRootScene();
        // 名前でシーンを探す(ルートシーンも含む、木の全部)。無ければ nullptr。
        Scene* FindScene(std::string_view sceneName);

        // シーンを読む・下ろすのを頼む。実際に行うのは、このフレームの物理の後(Application::ProcessSceneRequests)。
        // 名前はシーンの一覧(Application::LoadSceneList)の名前。parent を省くとルートシーンの下に読む。
        // 一覧に無い・もう読んである・読めないときは、そのときにエラーのログを出して何もしない。
        void LoadScene(std::string_view sceneName, Scene* parent = nullptr);
        // ルートシーンは下ろせない(エラーのログ)。
        void UnloadScene(std::string_view sceneName);

        // 頼まれたものの一覧(Application が読む)。
        struct SceneRequest
        {
            bool load = true;          // true: 読む、false: 下ろす
            std::string sceneName;     // シーンの名前
            core::ObjectHandle parent; // 読むときの親のシーン(空ならルートシーン)
        };
        const std::vector<SceneRequest>& GetSceneRequests() const { return m_sceneRequests; }
        void ClearSceneRequests() { m_sceneRequests.clear(); }

        // parent の下に、空のシーンを作ってつなぐ(読み込みは呼ぶ側。Application が使う)。作れなければ nullptr。
        Scene* CreateChildScene(Scene& parent, std::string_view sceneName, const core::Guid& asset);

        // シーンをその場で下ろす: 中のアクター(子のシーンの中も)を Destroy し、シーンを木から外して Destroy する。
        // ルートシーンは下ろさない(false)。戻り値: 下ろせたら true。outDestroyedActors に Destroy したアクターの数。
        bool UnloadSceneNow(Scene& scene, std::size_t* outDestroyedActors = nullptr);

        // ルートシーンの下の子のシーンを全部その場で下ろす(エディタの Stop など、木を最初に戻すとき)。
        // 中のアクターも Destroy する。頼まれたものの一覧も空にする。
        void UnloadAllChildScenes();

        // アクターを子孫ごと scene へ移す。アクターに別のシーンの親がいるときは、エラーのログを出して false。
        bool MoveToScene(Actor& actor, Scene& scene);

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
        // アクターやコンポーネントは GetWorld()->GetInput() で読む(自分で Input を持ったり、配られたりしない)。
        const Input* GetInput() const { return m_input; }

        // 入力を渡す。シーンは借りるだけで、持ち主にはならない(Input の寿命は呼んだ側が決める)。
        // input はシーンより長く生きていること。外すときは nullptr を渡す。
        void SetInput(const Input* input) { m_input = input; }

        // アセットのローダ(SD-4)。入力と同じく、ゲームループ(Application)が SetAssetLoader で渡す。渡されていなければ nullptr。
        // アクターやコンポーネントが、プロパティの AssetRef(ScriptableData など)を引くときに GetWorld()->GetAssetLoader() で使う。
        // Resolve は参照(AssetRef)とローダのキャッシュを書き換えるので、const ではない。ゲームスレッドだけ。
        AssetLoader* GetAssetLoader() const { return m_assetLoader; }
        void SetAssetLoader(AssetLoader* loader) { m_assetLoader = loader; }

        // 音を鳴らす窓口(AUD-4)。ローダと同じく、Application が SetAudio で渡す。渡されていなければ nullptr(音は鳴らない)。
        audio::AudioDevice* GetAudio() const { return m_audio; }
        void SetAudio(audio::AudioDevice* audio) { m_audio = audio; }

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

        // 生きているアクターの、T 型(か、その子の型)の最初のコンポーネント(一覧の順)。無ければ nullptr(SC-5)。
        // シーンをまたぐ参照は保存できないので、別のシーンの係(常駐の system のシーンのものなど)は、始まるときにこれで探す。
        // アクターを全部調べるので、毎フレームではなく、始まるときに1回探して弱い参照で覚える使い方にする。
        template <typename T>
        T* FindComponent()
        {
            T* found = nullptr;
            ForEachActor([&found](Actor& actor) {
                if (found == nullptr)
                {
                    T* component = actor.GetComponent<T>();
                    found = component != nullptr && !component->IsPendingDestroy() ? component : nullptr;
                }
            });
            return found;
        }

        // シーンにいるアクターのハンドルの一覧。プロパティとして登録しているので、GC がたどる(強い参照)。
        std::vector<core::ObjectHandle> actors;

        // ルートシーン(強い参照。GetRootScene が最初に呼ばれたときに作る)。
        core::ObjectHandle rootScene;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(actors)
            HUSKY_PROPERTY(rootScene)
        HUSKY_REFLECTION_END()

    private:
        // ルートシーンのハンドル(無ければ作る)。Spawn がテンプレートなので、Scene の中身を知らずに呼べる形にしている。
        core::ObjectHandle GetRootSceneHandle();

        std::vector<SceneRequest> m_sceneRequests; // LoadScene / UnloadScene で頼まれたもの

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
        AssetLoader* m_assetLoader = nullptr; // 借りているだけ(持ち主は Application)
        audio::AudioDevice* m_audio = nullptr; // 借りているだけ(持ち主は Application)

        // ハンドルからアクターを引く。もう無いか、レジストリで作っていないシーンなら nullptr。
        Actor* ResolveActor(core::ObjectHandle handle) const
        {
            core::ObjectRegistry* registry = GetRegistry();
            return registry != nullptr ? registry->Resolve<Actor>(handle) : nullptr;
        }
    };
}
