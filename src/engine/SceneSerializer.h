// シーン全体(アクターと、そのコンポーネント)⇄ ArchiveValue。
// 1つずつのオブジェクトは ObjectSerializer で読み書きし、ここでは「ハンドル ⇄ ファイルの中の番号」の付け替えを受け持つ。
//
// ファイルの形:
//   {
//     "version": 1,
//     "objects": [                                                   ← 保存した全オブジェクト
//       { "id": 0, "type": "BouncerActor", "properties": { ..., "components": [1] } },
//       { "id": 1, "type": "SpriteComponent", "properties": { "width": 32, ... } },
//       ...
//     ],
//     "actors": [0, 2, 4]                                            ← シーンの一覧に入れるアクターの番号(この順で並ぶ)
//   }
//
// 保存: 各オブジェクトに、ファイルの中だけの番号(0, 1, 2…)を振り、ハンドルのプロパティはその番号で書く。
// 読み込み: 2回に分けて回す。
//   1回目: 全オブジェクトを、型の名前から作る(TypeRegistry)。「ファイルの番号 → 新しいハンドル」の表ができる
//   2回目: 全オブジェクトのプロパティを読む。ハンドルのプロパティは、表で番号を新しいハンドルに付け替える
//   (1回で済ませようとすると、まだ作っていない相手(後ろの番号)を指すハンドルを読めないため)
//
// 使い方:
//   SceneSerializer::WriteResult saved = SceneSerializer::Write(scene);
//   std::string text;
//   JsonFormat::Write(saved.value, text);
//   ...
//   ArchiveValue loaded;
//   JsonFormat::Read(text, loaded);
//   SceneSerializer::ReadReport report = SceneSerializer::Read(loaded, otherScene, app.GetTypeRegistry());
//
// プレハブから置いたもの(PF-4。Unity と同じく、シーンにはプレハブとの違いだけを書く):
//   保存: アクターの "prefab" が読み込んだプレハブを指していれば、アクターとコンポーネントの "properties" には、
//         プレハブと値が違うものだけを書く(構造体は中まで比べ、position.x だけ、のように書ける)。
//         いつも書くもの: アクターの guid、prefab、components(番号の対応に要る)と、ハンドル(番号の振り方がプレハブと違い、比べられない)。
//         プレハブのファイルに無い値も、比べる相手が無いのでいつも書く(prefab.create で作ったプレハブは、全部の値を持つ)。
//         コンポーネントは並んだ順でプレハブのものと組にする。数か型がプレハブと違えば(足した・外した)、警告を出して全部の値を書く。
//   読み込み: そのアクターは、先にプレハブから作り(プレハブの値が入る)、その上にファイルの値を重ねる。
//         だから、プレハブを直すと、ファイルに書いていない(違いの無い)値は、直したプレハブの値になる。
//         プレハブが読めない・コンポーネントの並びが違うときは、警告を出してファイルの値だけで作る(書いていない値は型の既定値)。
//
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#include "core/TypeRegistry.h"
#include "engine/ArchiveValue.h"
#include "engine/PrefabAsset.h"
#include "engine/World.h"

#include <cstddef>
#include <functional> // std::function
#include <string>
#include <vector>

namespace husky::engine::SceneSerializer
{
    // ファイルの形の版。形を変えたら増やす(古い形を読めるようにするかは、そのときに決める)。
    constexpr std::int64_t FormatVersion = 1;

    struct WriteResult
    {
        ArchiveValue value;
        std::vector<std::string> warnings; // 保存しなかった相手を指すハンドルなど
    };

    // シーンの生きている(Destroy 済みでない)アクターと、その生きているコンポーネントを保存する。
    // プレハブから置いたアクター(Actor::prefab の中身が読み込まれている)は、プレハブとの違いだけを書く(下の「プレハブから置いたもの」)。
    // それ以外のオブジェクト(シーンの外のもの)を指すハンドルは、null として書き、warnings に残す。
    WriteResult Write(World& scene);

    // 保存の細かい決め事。
    struct WriteOptions
    {
        // 「シーンの中のこの1体」を表す値(Actor::guid と、元のプレハブ Actor::prefab)を書くか。プレハブ(Prefab.h)は書かない
        // (置くたびに新しいアクターになるので新しい guid を振り、元のプレハブは置いたときに書くため)。
        bool writeActorIdentity = true;
        // 書くものの外を指していても、null と書くだけで警告に数えないハンドル(HIER-2b。プレハブの根の親など)。
        core::ObjectHandle allowedOutside;
        // writeActorIdentity が false でも、元のプレハブ(prefab、prefabActorId)を書き、そのプレハブとの違いだけを書くアクター
        // (入れ子のプレハブ。プレハブの中にある、別のプレハブから置いたもの。HIER)。コンポーネントの ID も変えない。
        std::vector<const Actor*> nestedActors;
        // writeActorIdentity が false でも、プレハブの中の ID(prefabActorId、prefabComponentId)を振り直さない
        // (入れ子を開いた形を作るとき。HIER)。
        bool keepPrefabIds = false;
    };

    // scene の中の、指定したアクター(と、その生きているコンポーネント)だけを、シーンと同じ形で保存する。
    // Destroy 済みのアクターと、scene に入っていないアクターは飛ばす。ここに含まれない相手を指すハンドルは null と書き、warnings に残す。
    WriteResult WriteActors(World& scene, const std::vector<Actor*>& actors, const WriteOptions& options = {});

    struct ReadReport
    {
        std::size_t createdCount = 0;       // 作ったオブジェクトの数
        std::size_t addedActorCount = 0;    // シーンに入れたアクターの数
        std::vector<core::ObjectHandle> addedActors; // シーンに入れたアクター(入れた順)
        std::vector<std::string> warnings;  // 知らない型、読めなかったプロパティなど(途中で止めない)
    };

    // プレハブを GUID から探して、読み終わったものを返す関数(無い・読めなければ nullptr)。
    // どう読むか(アセットの表、ローダ、待つかどうか)は呼ぶ側が決める(Application::FindPrefabForScene)。
    using PrefabFinder = std::function<const PrefabAsset*(const core::Guid&)>;

    // value のオブジェクトを scene と同じレジストリに作り、アクターを scene の一覧の末尾に足す(今いるアクターは消さない)。
    // 型の名前は types で探す。知らない型のオブジェクトは作らず、それを指すハンドルはどこも指さないハンドルになる。
    // findPrefab を渡すと、プレハブから置いたアクターは、プレハブから作ってからファイルの値を重ねる(上の「プレハブから置いたもの」)。
    // 渡さなければ、ファイルの値だけで作る(プレハブそのものを読むときなど)。
    ReadReport Read(const ArchiveValue& value, World& scene, const core::TypeRegistry& types,
                    const PrefabFinder& findPrefab = {});

    // プレハブの値の木 prefab の中で、Actor::prefabActorId が prefabActorId のアクターの記録の番号(HIER-2b)。
    // prefabActorId が空なら根("actors" の最初)。見つからなければ -1。
    std::int64_t FindPrefabActorNumber(const ArchiveValue& prefab, const core::Guid& prefabActorId);
}
