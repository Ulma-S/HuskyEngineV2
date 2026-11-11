// リフレクション用の「何か1つの値」を入れる箱。メソッドの引数と戻り値に使う。
#pragma once

#include "core/Property.h"

#include <cstdint>
#include <string>
#include <type_traits> // std::bool_constant, std::is_same_v
#include <variant>     // std::variant, std::monostate

namespace husky::core
{
    // 次のどれか1つが入る。プロパティにできる型(PropertyType)と同じ5種類 + 空。
    //   std::monostate : 空(戻り値の無いメソッドの結果など)
    //   bool / std::int32_t / float / std::string / ObjectHandle
    //
    // std::variant は「決まった型のうち、どれか1つ」を入れられる箱。今どの型が入っているかも覚えている。
    // C# で object に入れて「is int」で確かめるのに近いが、入れられる型が決まっているので安全。
    //
    // 取り出すときは std::get_if<型>(&value) を使う(型が違えば nullptr が返る)。
    // std::get<型> は型が違うと例外を投げるので、例外を使わないこのプロジェクトでは使わない。
    //
    // 落とし穴: Value v = "abc"; と書くと、文字列ではなく bool が入ることがある
    //   (C++17 では、"abc" のアドレスが bool への変換に選ばれてしまうため)。
    //   文字列は Value v = std::string("abc"); のように、std::string にしてから入れること。
    using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string, ObjectHandle>;

    // T が Value に入れられる型なら true(コンパイル時に決まる)。
    // プロパティにはできても Value に入らない型(std::vector<ObjectHandle> など)を、メソッドの引数で使わせないために使う。
    // 中身: 「Value の候補の型(Ts...)のどれかと T が同じか」を、(... || ...) で全部 or して調べる。
    namespace value_detail
    {
        template <typename T, typename Variant>
        struct IsAlternativeOf;

        template <typename T, typename... Ts>
        struct IsAlternativeOf<T, std::variant<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
        {
        };
    }

    template <typename T>
    constexpr bool IsValueType = value_detail::IsAlternativeOf<T, Value>::value;
}
