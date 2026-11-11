// 世代付きハンドル。オブジェクトを「番号 + 世代」で指す、安全な参照。
//
// ポインタとの違い:
//   ポインタは、指していたものが破棄された後、同じメモリに別のものが作られると、黙ってそれを指してしまう。
//   ハンドルは、場所(index)が再利用されても世代(generation)が違うので、「元のものはもう無い」と分かる。
#pragma once

#include <cstdint>

namespace husky::core
{
    // T は「何を指すハンドルか」を区別するためだけに使う(タグ)。中身には T は出てこない。
    // そのため Handle<Enemy> と Handle<Texture> は別の型になり、取り違えるとコンパイルエラーになる。
    //
    // template<typename T> は C# の class Handle<T> に近い。
    // ただし C++ では、使った型ごとにコンパイラが別々のコードを作る(だから実装もヘッダに書く)。
    template <typename T>
    struct Handle
    {
        std::uint32_t index = 0;      // HandleTable の中の場所
        std::uint32_t generation = 0; // その場所の何代目か。0 は「どこも指していない」を表す

        // 何かを指しているか。指している先がまだ生きているかは、HandleTable に聞かないと分からない。
        bool IsNull() const { return generation == 0; }

        // == と != を使えるようにする(演算子のオーバーロード)。
        bool operator==(const Handle& other) const
        {
            return index == other.index && generation == other.generation;
        }
        bool operator!=(const Handle& other) const { return !(*this == other); }
    };
}
