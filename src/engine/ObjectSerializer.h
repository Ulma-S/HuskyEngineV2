// オブジェクト ⇄ ArchiveValue(値の木)。シリアライズの1段目(ArchiveValue.h を参照)。
// リフレクションのプロパティ(HUSKY_PROPERTY で登録したもの)を走査して、名前と値を読み書きする。
// 形式(JSON など)もライブラリも知らない。
//
// 1つのオブジェクトの形:
//   {
//     "type": "Enemy",                              ← 型の名前(読み込むときに、どの型を作るかを決めるのに使う。J-3)
//     "properties": { "name": "...", "hp": 10 }     ← 親クラスのプロパティから順に
//   }
//
// ハンドルのプロパティ(ObjectHandle、std::vector<ObjectHandle>)の値(表の場所と世代)は、そのときのメモリの状態でしか
// 意味が無いので、ファイルにそのまま書いても役に立たない。そこで、呼ぶ側(SceneSerializer)が
// 「ハンドル ⇄ ファイルの中の番号」の対応(HandleToId / IdToHandle)を渡したときだけ、番号に置き換えて読み書きする。
// 対応を渡さなければ、ハンドルは書かず、読み込みでも飛ばす。
//
// 使い方:
//   ArchiveValue saved = ObjectSerializer::Write(*enemy);
//   ...
//   ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(saved, *otherEnemy);
//   for (const std::string& warning : report.warnings) { ... }
//
// 動作スレッド: ゲームスレッドのみ(オブジェクトを触るため)。
#pragma once

#include "core/Object.h"
#include "engine/ArchiveValue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace husky::engine::ObjectSerializer
{
    // ハンドル → ファイルの中の番号。番号が無い(保存しない相手)なら負の数を返す。
    // std::function: 関数(ラムダ式も)を入れておける型。C# の Func<ObjectHandle, long> にあたる。
    using HandleToId = std::function<std::int64_t(core::ObjectHandle)>;
    // ファイルの中の番号 → ハンドル。知らない番号なら、どこも指さないハンドル(IsNull)を返す。
    using IdToHandle = std::function<core::ObjectHandle(std::int64_t)>;

    // object の型の名前と、プロパティを書き出す。
    // toId を渡すと、ハンドルのプロパティも番号で書く(相手に番号が無ければ null)。渡さなければ、ハンドルは書かない。
    ArchiveValue Write(const core::Object& object, const HandleToId& toId = {});

    // ReadProperties の結果。
    struct ReadReport
    {
        std::size_t readCount = 0;         // 読み込めた値の数(構造体の中のメンバーは、1つずつ数える)
        std::vector<std::string> warnings; // 読み込めなかったものの理由(知らない名前、型の違い、範囲外など)
    };

    // value の "properties" を、名前で探して object のプロパティに書き込む。
    //   - value に無いプロパティは、今の値のまま(ファイルが古くて、後から足したプロパティが無い場合など)
    //   - object に無い名前や、型が合わない値は、飛ばして warnings に理由を残す(途中で止めない)
    //   - "type" が object の型と違っても、名前が合うものは読み込み、warnings に残す
    //   - toHandle を渡すと、ハンドルのプロパティも番号から読む(null はどこも指さないハンドル)。渡さなければ、飛ばして warnings に残す
    ReadReport ReadProperties(const ArchiveValue& value, core::Object& object, const IdToHandle& toHandle = {});

    // object の今の値が、前に Write(object) で書いた値の木 written と同じか(対応表なしの Write と同じく、ハンドルは比べない)。
    // Write して Equals で比べるのと同じ答えを、値の木を作らずに出す(メモリを確保しない)。毎秒のような頻度で
    // 「書き換えられていないか」を確かめるため(AssetLoader::CheckScriptableDataUnchanged)。
    // 動作スレッド: ゲームスレッド
    bool Matches(const core::Object& object, const ArchiveValue& written);
}
