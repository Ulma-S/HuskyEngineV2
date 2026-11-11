// リフレクションのプロパティ。クラスのメンバー変数1つ分の情報(名前、型、オフセット、参照フラグ)。
#pragma once

#include "core/Handle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits> // std::is_same_v

namespace husky::core
{
    class Object;
    using ObjectHandle = Handle<Object>;

    // プロパティにできる型。ここに無い型を登録しようとすると、コンパイルエラーになる。
    enum class PropertyType : std::uint8_t
    {
        Bool,         // bool
        Int32,        // std::int32_t
        Float,        // float
        String,       // std::string
        ObjectHandle, // ObjectHandle(ほかのオブジェクトへの参照)
    };

    // C++ の型から PropertyType を選ぶ。コンパイル時に決まる。
    // if constexpr: 条件がコンパイル時に決まる if。選ばれなかった側はコンパイルされない。
    template <typename T>
    constexpr PropertyType PropertyTypeOf()
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return PropertyType::Bool;
        }
        else if constexpr (std::is_same_v<T, std::int32_t>)
        {
            return PropertyType::Int32;
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            return PropertyType::Float;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return PropertyType::String;
        }
        else if constexpr (std::is_same_v<T, ObjectHandle>)
        {
            return PropertyType::ObjectHandle;
        }
        else
        {
            // sizeof(T) == 0 は常に偽。ただし T に依存した式なので、この行が選ばれたときだけエラーになる。
            static_assert(sizeof(T) == 0, "この型はプロパティにできません(PropertyType に追加が必要)");
            return PropertyType::Bool;
        }
    }

    // プロパティ1つ分の情報。
    // 動作スレッド: どこでもよい(登録後は読み取り専用)。値の読み書きは、オブジェクトを触ってよいゲームスレッドで。
    class Property final
    {
    public:
        Property(const char* name, PropertyType type, std::size_t offset)
            : m_name(name)
            , m_type(type)
            , m_offset(offset)
        {
        }

        const char* GetName() const { return m_name; }
        PropertyType GetType() const { return m_type; }

        // オブジェクトの先頭から、このメンバーまでのバイト数。
        std::size_t GetOffset() const { return m_offset; }

        // ほかのオブジェクトを参照するプロパティなら true。GC はこれが true のものをたどる。
        bool IsReference() const { return m_type == PropertyType::ObjectHandle; }

        // object の中の、このプロパティの値へのポインタを返す。
        // T がプロパティの型と合わなければ nullptr(間違った型で読み書きして、メモリを壊さないため)。
        template <typename T>
        T* GetValuePtr(Object& object) const
        {
            if (PropertyTypeOf<T>() != m_type)
            {
                return nullptr;
            }
            // オブジェクトのアドレスを「1バイト単位のアドレス」(unsigned char*)として扱い、オフセット分だけ進める。
            // その場所を T として扱う。reinterpret_cast は「このアドレスを別の型として見る」強い型変換で、
            // 型の確認(上の if)を済ませた、この関数の中だけで使う。
            unsigned char* base = reinterpret_cast<unsigned char*>(&object);
            return reinterpret_cast<T*>(base + m_offset);
        }

        // const 版。
        template <typename T>
        const T* GetValuePtr(const Object& object) const
        {
            if (PropertyTypeOf<T>() != m_type)
            {
                return nullptr;
            }
            const unsigned char* base = reinterpret_cast<const unsigned char*>(&object);
            return reinterpret_cast<const T*>(base + m_offset);
        }

    private:
        const char* m_name;
        PropertyType m_type;
        std::size_t m_offset;
    };
}
