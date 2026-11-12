// ArchiveValue(値の木)と JSON の文字列を相互に変換する。シリアライズの2段目(ArchiveValue.h を参照)。
//
// 中では yyjson(外部のライブラリ)を使うが、このヘッダには yyjson の型を一切書かない。
// yyjson を使うのは JsonFormat.cpp だけなので、ライブラリを替えるときはそこだけを書き直せばよい。
// yyjson が内部で確保するメモリも、エンジンの入口(MemoryTracker)を通して数える。
//
// 使い方:
//   std::string text;
//   JsonFormat::Write(root, text);                   // 木 → 文字列
//   ArchiveValue loaded;
//   std::string error;
//   if (!JsonFormat::Read(text, loaded, &error)) { ... error に理由が入る ... }
//
// 動作スレッド: どこでもよい。
#pragma once

#include "engine/ArchiveValue.h"

#include <string>
#include <string_view>

namespace husky::engine::JsonFormat
{
    // value を JSON の文字列にして outText に入れる。pretty が true なら、改行と字下げ(2 文字)を入れて読みやすくする。
    // 戻り値: 成功したら true。小数が NaN や無限大のときは、JSON で表せないので false。
    bool Write(const ArchiveValue& value, std::string& outText, bool pretty = true);

    // JSON の文字列 text を読み、outValue に入れる。
    // 戻り値: 成功したら true。失敗したら false で、outError(nullptr でなければ)に理由と位置を入れる。
    // 数は、小数点や指数が無く int64 に収まれば Int、それ以外は Float になる。
    bool Read(std::string_view text, ArchiveValue& outValue, std::string* outError = nullptr);
}
