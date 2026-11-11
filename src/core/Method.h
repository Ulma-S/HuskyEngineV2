// リフレクションのメソッド。メンバー関数1つ分の情報と、「名前と値の配列」で呼び出すための仕組み。
//
// 呼び出しの流れ:
//   method->Invoke(object, {引数...}, result)
//     → 型ごとに自動生成された中継関数(Invoker)
//       → 引数の数と型を確かめる → Value から取り出す → 本物のメンバー関数を呼ぶ → 戻り値を Value に入れる
#pragma once

#include "core/Property.h"
#include "core/Value.h"

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility> // std::index_sequence
#include <vector>

namespace husky::core
{
    class Object;
    class TypeInfo;

    // メソッド1つ分の情報。
    // 動作スレッド: 情報の読み取りはどこでもよい。Invoke はオブジェクトを触るので、ゲームスレッドのみ。
    class Method final
    {
    public:
        // 中継関数の型。self の上で、args(argCount 個)を引数にメソッドを呼び、戻り値を result に入れる。
        // 引数の数や型が合わなければ、呼ばずに false を返す。
        using Invoker = bool (*)(Object& self, const Value* args, std::size_t argCount, Value& result);

        Method(const char* name, std::optional<PropertyType> returnType, std::vector<PropertyType> parameterTypes,
               Invoker invoker)
            : m_name(name)
            , m_returnType(returnType)
            , m_parameterTypes(std::move(parameterTypes))
            , m_invoker(invoker)
        {
        }

        const char* GetName() const { return m_name; }

        // 戻り値の型。戻り値が無い(void)メソッドなら空(std::nullopt)。
        std::optional<PropertyType> GetReturnType() const { return m_returnType; }

        // 引数の型の一覧(順番どおり)。
        const std::vector<PropertyType>& GetParameterTypes() const { return m_parameterTypes; }

        // このメソッドを持つ型。TypeInfo::AddMethod が設定する。
        const TypeInfo* GetOwner() const { return m_owner; }

        // self の上でメソッドを呼ぶ。
        // 戻り値: 呼べたら true。self の型が違う、引数の数や型が違う場合は、呼ばずに false。
        // result: 戻り値(void のメソッドなら空の Value)。
        bool Invoke(Object& self, const std::vector<Value>& args, Value& result) const;

    private:
        friend class TypeInfo; // AddMethod で m_owner を設定させる

        const char* m_name;
        std::optional<PropertyType> m_returnType;
        std::vector<PropertyType> m_parameterTypes;
        Invoker m_invoker;
        const TypeInfo* m_owner = nullptr;
    };

    // ---- ここから下は、中継関数を自動生成するための仕組み(テンプレート) ----
    namespace method_detail
    {
        // メンバー関数ポインタの型から、クラス・戻り値・引数の型を取り出す。
        // template の「特殊化」: 型の形(R (C::*)(Args...))に合ったものが選ばれ、その部品に名前が付く。
        template <typename T>
        struct MethodTraits;

        template <typename C, typename R, typename... Args>
        struct MethodTraits<R (C::*)(Args...)>
        {
            using Class = C;
            using Return = R;
            // 引数の型の一覧。std::decay_t で const や & を外す(const std::string& → std::string)。
            using Parameters = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t Arity = sizeof...(Args); // 引数の数
        };

        // const なメンバー関数(int GetHp() const など)用。中身は上と同じ。
        template <typename C, typename R, typename... Args>
        struct MethodTraits<R (C::*)(Args...) const> : MethodTraits<R (C::*)(Args...)>
        {
        };

        // 中継関数の本体。I... は 0, 1, 2, ... (引数の番号の列)。
        // 「... 」が付いた式は、I の数だけ並べて展開される(パック展開)。
        //   例: 引数が2つなら、std::get_if<A0>(&args[0]), std::get_if<A1>(&args[1]) になる。
        template <auto MethodPtr, std::size_t... I>
        bool InvokeWithIndices(Object& self, const Value* args, Value& result, std::index_sequence<I...>)
        {
            using Traits = MethodTraits<decltype(MethodPtr)>;
            using Class = typename Traits::Class;
            using Return = typename Traits::Return;
            using Parameters = typename Traits::Parameters;

            // 1. 引数の型が合っているかを、全部確かめる。
            //    std::tuple_element_t<I, Parameters>: I 番目の引数の型。
            //    (... && 式) は「式を && で全部つなぐ」(引数が0個なら true)。
            const bool typesMatch = (... && std::holds_alternative<std::tuple_element_t<I, Parameters>>(args[I]));
            if (!typesMatch)
            {
                return false;
            }

            // 2. self を本当のクラスとして扱う。型は Method::Invoke で確かめ済み。
            Class& object = static_cast<Class&>(self);

            // 3. Value から引数を取り出して呼ぶ。(object.*MethodPtr)(...) はメンバー関数ポインタの呼び出し方。
            if constexpr (std::is_void_v<Return>)
            {
                (object.*MethodPtr)(*std::get_if<std::tuple_element_t<I, Parameters>>(&args[I])...);
                result = Value{}; // 戻り値なし
            }
            else
            {
                result = Value{(object.*MethodPtr)(*std::get_if<std::tuple_element_t<I, Parameters>>(&args[I])...)};
            }
            return true;
        }

        // Method::Invoker の形をした中継関数。MethodPtr ごとに1つ作られる。
        template <auto MethodPtr>
        bool Invoke(Object& self, const Value* args, std::size_t argCount, Value& result)
        {
            using Traits = MethodTraits<decltype(MethodPtr)>;
            if (argCount != Traits::Arity)
            {
                return false;
            }
            return InvokeWithIndices<MethodPtr>(self, args, result, std::make_index_sequence<Traits::Arity>{});
        }

        // 引数の型の一覧を、PropertyType の vector にする。
        // 対応していない型が引数にあると、PropertyTypeOf の static_assert でコンパイルエラーになる。
        template <typename... Params>
        std::vector<PropertyType> ParameterTypes(std::tuple<Params...>*)
        {
            return {PropertyTypeOf<Params>()...};
        }
    }

    // メンバー関数ポインタから Method を作る。HUSKY_METHOD マクロから呼ばれる。
    //   使い方: MakeMethod<&Enemy::Heal>("Heal")
    // template <auto MethodPtr>: 関数そのもの(のアドレス)をテンプレートの引数にする(C++17)。
    template <auto MethodPtr>
    Method MakeMethod(const char* name)
    {
        using Traits = method_detail::MethodTraits<decltype(MethodPtr)>;
        using Return = std::decay_t<typename Traits::Return>;

        std::optional<PropertyType> returnType;
        if constexpr (!std::is_void_v<Return>)
        {
            returnType = PropertyTypeOf<Return>(); // 対応していない戻り値の型はコンパイルエラー
        }

        // static_cast<Parameters*>(nullptr): 型だけを ParameterTypes に伝えるための、中身の無いポインタ。
        std::vector<PropertyType> parameterTypes =
            method_detail::ParameterTypes(static_cast<typename Traits::Parameters*>(nullptr));

        return Method(name, returnType, std::move(parameterTypes), &method_detail::Invoke<MethodPtr>);
    }
}
