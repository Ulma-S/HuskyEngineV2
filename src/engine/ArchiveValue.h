// 保存・読み込みのための「値の木」。ファイルの形式(JSON など)にも、ライブラリ(yyjson など)にも依存しない。
//
// シリアライズは2段に分ける:
//   1. オブジェクト ⇄ ArchiveValue(値の木)      … リフレクションで走査する(次のステップ)。形式を知らない
//   2. ArchiveValue ⇄ 文字列やバイト列(JsonFormat) … 形式ごとに1つ。ライブラリを使うのはここだけ
// こうしておくと、
//   - JSON のライブラリを替えるときは、JsonFormat.cpp だけを書き直せばよい
//   - バイナリなど別の形式を足すときは、BinaryFormat のような2段目を足すだけで、1段目はそのまま使える
//
// C# で言えば、Newtonsoft.Json の JToken(JObject / JArray / JValue)に近い。
//
// 使い方:
//   ArchiveValue root = ArchiveValue::MakeObject();
//   root.Set("name", ArchiveValue("Player"));
//   root.Set("hp", ArchiveValue(std::int64_t{100}));
//   ArchiveValue& items = root.Set("items", ArchiveValue::MakeArray());
//   items.Append(ArchiveValue(1.5));
//
//   if (const ArchiveValue* hp = root.Find("hp")) { std::int64_t value = hp->AsInt(); }
//
// 動作スレッド: どこでもよい(ただし、同じ木を複数のスレッドから同時に書き換えないこと)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace husky::engine
{
    class ArchiveValue
    {
    public:
        // 値の種類。JSON の種類と同じだが、数は整数(Int)と小数(Float)を分けて持つ。
        enum class Type
        {
            Null,
            Bool,
            Int,    // std::int64_t
            Float,  // double
            String, // UTF-8 の文字列
            Array,  // 値の並び
            Object, // 「名前 → 値」の並び(足した順番を保つ)
        };

        // 何も入っていない値(Null)。
        ArchiveValue() = default;

        // 値から作る。explicit: ArchiveValue v = 5; のような「知らないうちの変換」を禁止し、ArchiveValue(5) と書かせる。
        explicit ArchiveValue(bool value);
        explicit ArchiveValue(std::int64_t value);
        explicit ArchiveValue(double value);
        // float から作ると、「float の精度で十分」という印が付く(種類は Float のまま、中身は double)。
        // 書き出す形式はこの印を見て、短く書ける(0.1f を 0.10000000149011612 ではなく 0.1 と書く)。
        explicit ArchiveValue(float value);
        explicit ArchiveValue(std::string value);
        // const char* を受けないと、"文字列" が bool に変換されて ArchiveValue(bool) が選ばれてしまう(C++ の落とし穴)。
        explicit ArchiveValue(const char* value);

        static ArchiveValue MakeArray();
        static ArchiveValue MakeObject();

        Type GetType() const { return m_type; }
        bool IsNull() const { return m_type == Type::Null; }
        // float から作った小数なら true(上の ArchiveValue(float) を参照)。
        bool IsSinglePrecision() const { return m_singlePrecision; }

        // 値を読む。種類が違うときは fallback を返す。
        // AsFloat は Int も小数に直して返す(JSON では 1 と 1.0 の区別があいまいなため)。
        bool AsBool(bool fallback = false) const;
        std::int64_t AsInt(std::int64_t fallback = 0) const;
        double AsFloat(double fallback = 0.0) const;
        // 文字列でなければ空の文字列を返す。
        const std::string& AsString() const;

        // --- 配列(Array)として使う ---
        // 要素の数。配列でなければ 0。
        std::size_t GetArraySize() const;
        // index 番目の要素。範囲外か配列でなければ、Null の値を返す。
        const ArchiveValue& At(std::size_t index) const;
        // 末尾に足し、足した要素への参照を返す(続けて中身を書ける)。配列でなければ、配列に変えてから足す。
        ArchiveValue& Append(ArchiveValue value);

        // --- オブジェクト(Object)として使う ---
        // メンバーの数。オブジェクトでなければ 0。
        std::size_t GetMemberCount() const;
        // index 番目のメンバーの名前と値(足した順)。
        const std::string& GetMemberName(std::size_t index) const;
        const ArchiveValue& GetMemberValue(std::size_t index) const;
        // 名前で探す。無ければ nullptr。
        const ArchiveValue* Find(std::string_view name) const;
        // 名前で値を入れ、入れた値への参照を返す。同じ名前があれば置き換える。オブジェクトでなければ、オブジェクトに変えてから入れる。
        ArchiveValue& Set(std::string_view name, ArchiveValue value);

    private:
        Type m_type = Type::Null;
        bool m_bool = false;
        std::int64_t m_int = 0;
        double m_float = 0.0;
        bool m_singlePrecision = false;
        std::string m_string;
        // 配列の要素、またはオブジェクトのメンバーの値。
        // std::vector は、まだ定義の途中の型(ここでは ArchiveValue 自身)も要素にできる(C++17 から)。
        std::vector<ArchiveValue> m_children;
        // オブジェクトのメンバーの名前。m_children と同じ番号で対応する(オブジェクトのときだけ使う)。
        std::vector<std::string> m_names;
    };
}
