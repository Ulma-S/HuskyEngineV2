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

    // シーンにいるアクターと、その生きているコンポーネントから、プレハブの値を作る。
    // アクターの外を指すハンドル(カメラの追う相手など)は、プレハブの中に相手がいないので null と書き、warnings に残す。
    WriteResult Write(Actor& actor);

    struct InstantiateResult
    {
        Actor* actor = nullptr;            // 置いたアクター。置けなければ nullptr
        std::vector<std::string> warnings; // 知らない型、読めなかったプロパティなど(置けても出ることがある)
    };

    // プレハブの値から、アクターとコンポーネントを新しく作り、scene の一覧の末尾に置く。guid は新しく振られる。
    // "actors" がちょうど1つでなければ、何も作らずに失敗する(actor が nullptr、理由は warnings)。
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

    struct OverridesResult
    {
        const PrefabAsset* prefab = nullptr; // 比べたプレハブ。比べられなければ nullptr(理由は error)
        std::vector<Override> overrides;     // アクター、コンポーネント(並んだ順)の順。それぞれの中はプロパティの順
        std::vector<std::string> addedComponents;   // 置いた後に足したコンポーネント(プレハブに組が無い)の型の名前
        std::vector<std::string> removedComponents; // 置いた後に外したコンポーネント(プレハブにあって組が無い)の型の名前
        std::string error;
    };

    // プレハブから置いたアクターの、プレハブと違う値を集める(シーンに保存される「違い」と同じ比べ方)。
    // コンポーネントは、保存と同じく ID(Component::prefabComponentId)でプレハブのものと組にし、組になったものだけを比べる
    // (PF-7b)。組にならないものは、addedComponents / removedComponents に並べる。
    // 比べないもの: ハンドル(シーンとプレハブで指す相手が違う)、guid、プレハブのファイルに無い値。
    // アクターがプレハブから置いたものでない、プレハブが読み込まれていない、ID の無い古いプレハブとコンポーネントの並びが違うときは、
    // error に理由。
    OverridesResult FindOverrides(const Actor& actor);

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
    bool WriteChanges(const ArchiveValue& data, const std::vector<ValueChange>& changes, ArchiveValue& out,
                      std::string& error);

    // プレハブが changes のとおりに変わったことを、置いたもの actor に伝える。
    // 値がプレハブの古い値のまま(違いでない)ものだけを新しい値にし、違い(上書き)はそのまま残す。書き換えた値の数を返す。
    std::size_t FollowChanges(Actor& actor, const std::vector<ValueChange>& changes);
}
