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
}
