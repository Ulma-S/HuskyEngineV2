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
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#include "core/TypeRegistry.h"
#include "engine/ArchiveValue.h"
#include "engine/Scene.h"

#include <cstddef>
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
    // それ以外のオブジェクト(シーンの外のもの)を指すハンドルは、null として書き、warnings に残す。
    WriteResult Write(Scene& scene);

    struct ReadReport
    {
        std::size_t createdCount = 0;       // 作ったオブジェクトの数
        std::size_t addedActorCount = 0;    // シーンに入れたアクターの数
        std::vector<std::string> warnings;  // 知らない型、読めなかったプロパティなど(途中で止めない)
    };

    // value のオブジェクトを scene と同じレジストリに作り、アクターを scene の一覧の末尾に足す(今いるアクターは消さない)。
    // 型の名前は types で探す。知らない型のオブジェクトは作らず、それを指すハンドルはどこも指さないハンドルになる。
    ReadReport Read(const ArchiveValue& value, Scene& scene, const core::TypeRegistry& types);
}
