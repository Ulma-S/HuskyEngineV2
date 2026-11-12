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
// 今はハンドルのプロパティ(ObjectHandle、std::vector<ObjectHandle>)は書かず、読み込みでも飛ばす。
// ハンドルの値(番号と世代)は、そのときのメモリの状態でしか意味が無いので、ファイルにそのまま書いても役に立たない。
// シーン全体を保存するとき(J-3)に、「ファイルの中の番号」に置き換えて扱う。
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
#include <string>
#include <vector>

namespace husky::engine::ObjectSerializer
{
    // object の型の名前と、値のプロパティ(ハンドル以外)を書き出す。
    ArchiveValue Write(const core::Object& object);

    // ReadProperties の結果。
    struct ReadReport
    {
        std::size_t readCount = 0;         // 読み込めたプロパティの数
        std::vector<std::string> warnings; // 読み込めなかったものの理由(知らない名前、型の違い、範囲外など)
    };

    // value の "properties" を、名前で探して object のプロパティに書き込む。
    //   - value に無いプロパティは、今の値のまま(ファイルが古くて、後から足したプロパティが無い場合など)
    //   - object に無い名前や、型が合わない値は、飛ばして warnings に理由を残す(途中で止めない)
    //   - "type" が object の型と違っても、名前が合うものは読み込み、warnings に残す
    ReadReport ReadProperties(const ArchiveValue& value, core::Object& object);
}
