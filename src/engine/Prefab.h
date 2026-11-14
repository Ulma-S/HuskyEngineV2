// プレハブ: 「アクター + コンポーネント + 値」の組み合わせを、1つの値の木(ArchiveValue)として持ち、
// 何度でもシーンに置けるようにする(Unity のプレハブ、UE のブループリントの既定値にあたる)。
//
// 形は、シーンのファイル(SceneSerializer.h)と同じで、アクターが1つだけのもの(「アクター1つの小さなシーン」)。
//   {
//     "version": 1,
//     "objects": [
//       { "id": 0, "type": "BouncerActor", "properties": { "name": "Bouncer", "transform": {...}, "components": [1] } },
//       { "id": 1, "type": "SpriteComponent", "properties": { ... } }
//     ],
//     "actors": [0]
//   }
// シーンと同じ道具(ObjectSerializer、SceneSerializer、JsonFormat)で読み書きするので、プロパティを足しても、
// プレハブのためのコードは要らない。
//
// シーンのファイルとの違いは1つだけ: アクターの guid(Actor::guid)を書かない。
// guid は「シーンの中のこの1体」を指す ID で、プレハブから置くたびに別のアクターになるため。置いたときに新しく振られる。
//
// 使い方:
//   Prefab::WriteResult saved = Prefab::Write(*actor);       // シーンにいるアクターから作る
//   Prefab::InstantiateResult placed = Prefab::Instantiate(saved.value, scene, app.GetTypeRegistry());
//   if (placed.actor != nullptr) { placed.actor->transform.position = ...; }
//
// 動作スレッド: ゲームスレッドのみ(オブジェクトを触るため)。
#pragma once

#include "core/TypeRegistry.h"
#include "engine/ArchiveValue.h"
#include "engine/PrefabAsset.h"
#include "engine/Scene.h"
#include "engine/SceneSerializer.h"

#include <string>
#include <vector>

namespace husky::engine::Prefab
{
    struct WriteResult
    {
        ArchiveValue value;
        std::vector<std::string> warnings; // アクターの外(ほかのアクターなど)を指すハンドルを null にした、など
        bool ok = false;                   // 作れたか(アクターがシーンに入っていない、Destroy 済みなら false)
    };

    // シーンにいるアクターと、その子孫(HIER-2b)と、それぞれの生きているコンポーネントから、プレハブの値を作る。
    // "actors" は根(actor)が最初で、子孫は親が先の順。アクターごとにプレハブの中の ID(Actor::prefabActorId)を新しく振る。
    // 根の親は書かない(プレハブの外なので。置いたものは親なしになる)。
    // アクターの外を指すハンドル(カメラの追う相手など)は、プレハブの中に相手がいないので null と書き、warnings に残す。
    WriteResult Write(Actor& actor);

    // --- 入れ子のプレハブ(HIER。プレハブの中にある、別のプレハブから置いたもの) ---
    // ファイルには、入れ子のアクターは元のプレハブ(prefab、prefabActorId)とその違いだけを書く(シーンのファイルと同じ形)。
    // 入れ子の一まとまりの根の記録には、このプレハブの中での ID "nestedId" を書く。中の子の ID は Guid::Combine(nestedId, 元の ID)。
    // 読むときは入れ子のプレハブを先に読み、開いた形(PrefabAsset::GetData)を作る。Apply はいつも外側のファイルに書く(ユーザーの決定)。

    // ファイルの値の木 raw の中の、入れ子のプレハブの GUID(重なりなし)。
    std::vector<core::Guid> FindNestedPrefabs(const ArchiveValue& raw);

    // 入れ子のプレハブを開いた形を作る: 入れ子のアクターは元のプレハブの値に違いを重ね、つながりを外し、ID をこのプレハブの中の
    // ID にする。入れ子が無ければ raw をそのまま返す。findPrefab が見つけられないものは、ファイルにある値だけで作る(warnings)。
    // registry に一時的なオブジェクトを作る(作り終えたら Destroy する)。ゲームスレッドで呼ぶ。
    ArchiveValue ExpandNested(const ArchiveValue& raw, core::ObjectRegistry& registry, const core::TypeRegistry& types,
                              const SceneSerializer::PrefabFinder& findPrefab, std::vector<std::string>& warnings);

    // プレハブの中の ID が prefabActorId のアクターが、入れ子のプレハブから来たものなら、その入れ子のプレハブの GUID。違えば空。
    core::Guid FindNestedSource(const PrefabAsset& prefab, const core::Guid& prefabActorId);

    struct InstantiateResult
    {
        Actor* actor = nullptr;            // 置いたアクター(根)。置けなければ nullptr
        std::vector<Actor*> actors;        // 置いたアクターすべて(根が最初、子孫は親が先の順。HIER-2b)
        std::vector<std::string> warnings; // 知らない型、読めなかったプロパティなど(置けても出ることがある)
    };

    // プレハブの値から、アクターとコンポーネントを新しく作り、scene の一覧の末尾に置く。guid は新しく振られる。
    // "actors" が無い・空なら、何も作らずに失敗する(actor が nullptr、理由は warnings)。子孫も一緒に作り、親子もつながる。
    InstantiateResult Instantiate(const ArchiveValue& prefab, Scene& scene, const core::TypeRegistry& types);

    // プレハブのアセットから置き、置いたアクターに元のプレハブ(Actor::prefab)を覚えさせる。
    // アセットが読み終わっていなければ(読み込み中・失敗)、何も作らずに失敗する。
    InstantiateResult Instantiate(const PrefabAsset& prefab, Scene& scene, const core::TypeRegistry& types);

    // プレハブと違う値(上書き、override)1つ。
    struct Override
    {
        std::string component;   // コンポーネントの型の名前。アクター自身の値なら空
        core::Guid componentId;  // コンポーネントのプレハブの中の ID(Component::prefabComponentId)。アクター自身や、ID の無い古いプレハブなら空
        std::string path;       // プロパティの道筋("transform.position.x"、"color.r" など。構造体の中は点でつなぐ)
        ArchiveValue value;      // 置いたものの値
        ArchiveValue prefabValue; // プレハブの値
        // 1体ずつ違うのが当たり前の値(アクターの name、transform.position、transform.rotation)。
        // Unity と同じく、「全部を戻す・全部を書き戻す」では飛ばす(名前を指せば、1つずつは戻せる・書き戻せる)。
        bool perInstance = false;
    };

    // 置いた後に足した・外したコンポーネント1つ(PF-7d)。
    struct ComponentChange
    {
        std::string type;       // コンポーネントの型の名前
        core::Guid componentId; // 外したもの: プレハブの中の ID。足したもの: 空(プレハブに組が無いので)
        // 足したもの: 置いたものの components の中の位置(component.remove で外せば Revert になる)。
        // 外したもの: Revert で戻すときに入れる位置(プレハブで1つ前にある、組になったコンポーネントのすぐ後ろ。無ければ 0)。
        std::size_t index = 0;
        // 足したもの: Apply でプレハブに入れるときの、プレハブの components の中の位置(置いたもので1つ前にある、
        // 組になったコンポーネントのプレハブでの位置 + 1。無ければ 0)(PF-7e)。外したものは使わない
        std::size_t prefabIndex = 0;
        ArchiveValue prefabEntry; // 外したもの: プレハブの中の記録({"type", "properties"})。足したものは null
    };

    // 置いた後に足した・消した子のアクター1つ(HIER)。
    struct ChildChange
    {
        std::string name;              // アクターの名前(表示用)
        core::Guid prefabActorId;      // 消したもの: プレハブの中の ID。足したもの: 空
        core::ObjectHandle actor;      // 足したもの: そのアクター。消したものは null
    };

    struct OverridesResult
    {
        const PrefabAsset* prefab = nullptr; // 比べたプレハブ。比べられなければ nullptr(理由は error)
        std::vector<Override> overrides;     // アクター、コンポーネント(並んだ順)の順。それぞれの中はプロパティの順
        std::vector<ComponentChange> addedComponents;   // 置いた後に足したコンポーネント(プレハブに組が無い)
        std::vector<ComponentChange> removedComponents; // 置いた後に外したコンポーネント(プレハブにあって組が無い)。プレハブの順
        // 置いた後に足した直接の子(このプレハブから来ていない子)。シーンの一覧の順(HIER)
        std::vector<ChildChange> addedChildren;
        // プレハブではこのアクターの直接の子なのに、置いたもの(同じプレハブから置いた一まとまり)に無い子。プレハブの順(HIER)
        std::vector<ChildChange> removedChildren;
        std::string error;
    };

    // プレハブから置いたアクターの、プレハブと違う値を集める(シーンに保存される「違い」と同じ比べ方)。
    // コンポーネントは、保存と同じく ID(Component::prefabComponentId)でプレハブのものと組にし、組になったものだけを比べる
    // (PF-7b)。組にならないものは、addedComponents / removedComponents に並べる。
    // 比べないもの: ハンドル(シーンとプレハブで指す相手が違う)、guid。
    // プレハブのファイルに無い値(コードで後から足したプロパティなど)は、置いたときに型の既定の値になるので、
    // types(型の登録簿)を渡せば、型の既定の値と比べる(prefab.apply で書き戻せるように)。渡さなければ比べない。
    // 既定の値は、型から1つ作って書き出したもの(型ごとに1回だけ作って控える)。
    // アクターがプレハブから置いたものでない、プレハブが読み込まれていない、ID の無い古いプレハブとコンポーネントの並びが違うときは、
    // error に理由。
    OverridesResult FindOverrides(const Actor& actor, const core::TypeRegistry* types = nullptr);

    // プレハブの値を1つ変える(prefab.apply で、置いたものの違いをプレハブへ書き戻すとき)。
    struct ValueChange
    {
        std::string component; // コンポーネントの型の名前(その型の最初のもの)。アクター自身なら空
        std::string path;      // プロパティの道筋("color.r" など)
        ArchiveValue oldValue; // 今のプレハブの値
        ArchiveValue newValue; // 新しいプレハブの値
        // コンポーネントのプレハブの中の ID。空でなければ、型の名前ではなくこの ID で指す(PF-7b)。
        // 空なら(ID の無い古いプレハブ)、型の名前の最初のもの。
        core::Guid componentId;
    };

    // プレハブの値の木 data に changes を書き込んだ、新しい値の木を out に作る(data は変えない)。
    // 指すコンポーネントがプレハブに無ければ false と error。
    // prefabActorId: 書き込む先のプレハブの中のアクター(Actor::prefabActorId。空なら根。HIER-2b)。
    bool WriteChanges(const ArchiveValue& data, const std::vector<ValueChange>& changes, ArchiveValue& out,
                      std::string& error, const core::Guid& prefabActorId = {});

    // プレハブが changes のとおりに変わったことを、置いたもの actor に伝える。
    // 値がプレハブの古い値のまま(違いでない)ものだけを新しい値にし、違い(上書き)はそのまま残す。書き換えた値の数を返す。
    std::size_t FollowChanges(Actor& actor, const std::vector<ValueChange>& changes);

    // --- コンポーネントの出し入れをプレハブへ書き戻す(PF-7e。prefab.applyComponent が使う) ---

    // 置いたものの component を、プレハブに書き込める記録({"type", "properties"})にする。prefabComponentId は componentId にする。
    // ハンドルは、持ち主のアクターを指すものだけをプレハブのアクターの番号にし、ほかは null にして warnings に書く(Write と同じ決まり)。
    // data は、書き込む先のプレハブの値の木(アクターの番号を知るため)。
    ArchiveValue MakeComponentEntry(const Component& component, const ArchiveValue& data, const core::Guid& componentId,
                                    std::vector<std::string>& warnings);

    // プレハブの値の木 data に、コンポーネントの記録 componentEntry を足した新しい値の木を out に作る(data は変えない)。
    // 番号は今あるもののいちばん大きいもの + 1。アクターの components の position 番目(長さ以上なら末尾)に入れる。
    // prefabActorId: 足す先のプレハブの中のアクター(空なら根。HIER-2b)。
    bool AddComponentToData(const ArchiveValue& data, const ArchiveValue& componentEntry, std::size_t position,
                            ArchiveValue& out, std::string& error, const core::Guid& prefabActorId = {});

    // --- 子のアクターの出し入れ(HIER。prefab.revertChild / prefab.applyChild が使う) ---

    // プレハブ prefab の中の ID が childId のアクターと、プレハブの中のその子孫を作り、scene に入れて parent の子にする。
    // 全員に元のプレハブを覚えさせる。作ったアクター(子が最初、親が先の順)を返す。作れなければ空と error。
    std::vector<Actor*> InstantiateChild(const PrefabAsset& prefab, const core::Guid& childId, Actor& parent, Scene& scene,
                                         const core::TypeRegistry& types, std::string& error);

    // 足した子を書き込んだときに振った、プレハブの中の ID(置いたものに覚えさせて、組にするため)。
    struct AddedChildIds
    {
        std::vector<std::pair<core::ObjectHandle, core::Guid>> actors;     // (アクター、prefabActorId)
        std::vector<std::pair<core::ObjectHandle, core::Guid>> components; // (コンポーネント、prefabComponentId)
    };

    // プレハブの値の木 data の、ID が parentPrefabActorId のアクターの子として、child とその子孫(とコンポーネント)を
    // 書き込んだ新しい値の木を out に作る。新しい ID を振り、ids に返す。child の外を指すハンドルは null にして warnings に書く。
    bool AddChildToData(const ArchiveValue& data, const core::Guid& parentPrefabActorId, const Actor& child,
                        ArchiveValue& out, AddedChildIds& ids, std::vector<std::string>& warnings, std::string& error);

    // プレハブの値の木 data から、ID が childId のアクターと、その子孫・コンポーネントを消した新しい値の木を out に作る。
    // 根は消せない。無ければ false と error。
    bool RemoveChildFromData(const ArchiveValue& data, const core::Guid& childId, ArchiveValue& out, std::string& error);

    // プレハブの値の木 data から、ID が componentId のコンポーネントを消した新しい値の木を out に作る。無ければ false と error。
    // prefabActorId: 消す先のプレハブの中のアクター(空なら根。HIER-2b)。
    bool RemoveComponentFromData(const ArchiveValue& data, const core::Guid& componentId, ArchiveValue& out,
                                 std::string& error, const core::Guid& prefabActorId = {});

    // FindOverrides が型ごとに控えた「型の既定の値」を捨てる(HR-3)。控えの鍵は型情報のアドレスなので、
    // ゲームの DLL を読み直すと、同じアドレスに新しい型(既定の値が変わったかもしれない)ができることがある。
    void ClearDefaultPropertiesCache();
}
