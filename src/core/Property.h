// リフレクションのプロパティ。クラスのメンバー変数1つ分の情報(名前、型、オフセット、参照フラグ)。
#pragma once

#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/Handle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits> // std::is_same_v
#include <vector>

namespace husky::core
{
    class Object;
    class TypeInfo;
    using ObjectHandle = Handle<Object>;

    // プロパティにできる型。ここに無い型を登録しようとすると、コンパイルエラーになる。
    enum class PropertyType : std::uint8_t
    {
        Bool,              // bool
        Int32,             // std::int32_t
        Float,             // float
        String,            // std::string
        ObjectHandle,      // ObjectHandle(ほかのオブジェクトへの参照)
        ObjectHandleArray, // std::vector<ObjectHandle>(ほかのオブジェクトへの参照の一覧)
        Struct,            // HUSKY_STRUCT_BEGIN で登録した構造体(中のメンバーも、それぞれプロパティとして持つ)
        AssetRef,          // AssetRef<T>(アセットへの参照。保存すると GUID になる。AssetRef.h)
        Guid,              // core::Guid(変わらない ID。保存すると GUID の文字列になる。アクターの ID など)
    };

    // --- 構造体のリフレクション(登録は StructReflection.h の HUSKY_STRUCT_BEGIN で行う) ---
    //
    // 構造体 T を登録すると、T と同じ名前空間に「HuskyStructType(const T*) という関数」ができる。
    // ここでは、その関数が呼べるかどうかで「T は登録された構造体か」を見分ける。
    //
    // ADL(引数依存の名前探索): 関数を名前空間を付けずに呼ぶと、C++ は引数の型の名前空間の中も探す。
    // そのため、husky::render::Color の登録が husky::render の中にあれば、ここ(husky::core)から呼んでも見つかる。
    //
    // std::void_t<...> の中の式が書ければ(=関数が見つかれば)下の特殊化が選ばれて true、書けなければ上の false になる。
    // (テンプレートの「置き換えに失敗しても、エラーにせずに候補から外す」性質を使う書き方。SFINAE と呼ばれる)
    template <typename T, typename = void>
    struct IsReflectedStruct : std::false_type
    {
    };
    template <typename T>
    struct IsReflectedStruct<T, std::void_t<decltype(HuskyStructType(static_cast<const T*>(nullptr)))>> : std::true_type
    {
    };

    // 登録された構造体 T の型情報(メンバーの一覧)。登録されていない型なら nullptr。
    template <typename T>
    const TypeInfo* StructTypeOf()
    {
        if constexpr (IsReflectedStruct<T>::value)
        {
            return &HuskyStructType(static_cast<const T*>(nullptr));
        }
        else
        {
            return nullptr;
        }
    }

    // プロパティの「詳しい型」の型情報。HUSKY_PROPERTY が Property に渡す。
    //   - 登録された構造体なら、その構造体の型情報(メンバーの一覧)
    //   - AssetRef<T> なら、アセットの型 T の型情報(T::StaticType())
    //   - それ以外(と、型を問わない AssetReference)は nullptr
    template <typename T>
    const TypeInfo* DetailTypeOf()
    {
        if constexpr (std::is_base_of_v<AssetReference, T> && !std::is_same_v<T, AssetReference>)
        {
            return &T::AssetType::StaticType();
        }
        else
        {
            return StructTypeOf<T>();
        }
    }

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
        else if constexpr (std::is_same_v<T, Guid>)
        {
            return PropertyType::Guid;
        }
        else if constexpr (std::is_same_v<T, std::vector<ObjectHandle>>)
        {
            return PropertyType::ObjectHandleArray;
        }
        else if constexpr (IsReflectedStruct<T>::value)
        {
            return PropertyType::Struct;
        }
        else if constexpr (std::is_base_of_v<AssetReference, T>)
        {
            // AssetRef<TextureAsset> など(AssetReference の子)。型を問わない AssetReference そのものも、ここに入る。
            return PropertyType::AssetRef;
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
    //
    // 値の読み書きは、必ずこのクラスの GetValuePtr / GetValuePtrAt を通す(シリアライザや GC はオフセットを直接触らない)。
    // 将来、スクリプトで定義した型のように「オフセットでは触れないプロパティ」(読み書きする関数を持つもの)を足すときに、
    // 変える場所をここに閉じ込めるため。
    class Property final
    {
    public:
        // detailType: type が Struct ならその構造体の型情報、AssetRef ならアセットの型の型情報(それ以外は nullptr)。
        Property(const char* name, PropertyType type, std::size_t offset, const TypeInfo* detailType = nullptr)
            : m_name(name)
            , m_type(type)
            , m_offset(offset)
            , m_detailType(detailType)
        {
        }

        const char* GetName() const { return m_name; }
        PropertyType GetType() const { return m_type; }

        // 持ち主(オブジェクトか構造体)の先頭から、このメンバーまでのバイト数。
        std::size_t GetOffset() const { return m_offset; }

        // type が Struct のとき、その構造体の型情報(メンバーの一覧)。それ以外は nullptr。
        const TypeInfo* GetStructType() const { return m_type == PropertyType::Struct ? m_detailType : nullptr; }

        // type が AssetRef のとき、アセットの型(TextureAsset など)の型情報。それ以外は nullptr。
        // インスペクタが、選べるアセットを絞り込むのに使う。
        const TypeInfo* GetAssetType() const { return m_type == PropertyType::AssetRef ? m_detailType : nullptr; }

        // シーンの中のほかのオブジェクトを直接参照するプロパティなら true(構造体の中のハンドルは、構造体のメンバーの側で true になる)。
        // シリアライザが「番号の対応表が要るか」を見分けるのに使う。AssetRef は GUID で保存するので含めない
        // (GC がたどるかどうかは ForEachReferenceAt が決める。AssetRef の中のハンドルも、そちらでたどる)。
        bool IsReference() const
        {
            return m_type == PropertyType::ObjectHandle || m_type == PropertyType::ObjectHandleArray;
        }

        // このプロパティが参照しているオブジェクトのハンドルを、すべて function(handle) に渡す。
        // ObjectHandle なら1つ、ObjectHandleArray なら中の要素を順に、構造体なら中のメンバーをたどって渡す。
        // GC が、プロパティの種類を気にせずに参照をたどれるようにするためのもの。
        // (中身は TypeInfo.h に書いてある。構造体の中をたどるのに、TypeInfo の中身が要るため)
        template <typename Function>
        void ForEachReference(const Object& object, Function&& function) const
        {
            ForEachReferenceAt(&object, function);
        }

        // container(このプロパティの持ち主。オブジェクトか構造体)の先頭のアドレスから、参照をたどる。
        template <typename Function>
        void ForEachReferenceAt(const void* container, Function&& function) const;

        // object の中の、このプロパティの値へのポインタを返す。
        // T がプロパティの型と合わなければ nullptr(間違った型で読み書きして、メモリを壊さないため)。
        template <typename T>
        T* GetValuePtr(Object& object) const
        {
            return GetValuePtrAt<T>(static_cast<void*>(&object));
        }

        // const 版。
        template <typename T>
        const T* GetValuePtr(const Object& object) const
        {
            return GetValuePtrAt<T>(static_cast<const void*>(&object));
        }

        // container(このプロパティの持ち主。オブジェクトか構造体)の先頭のアドレスから、値へのポインタを返す。
        // 構造体の中のメンバーを読むときは、container に構造体のアドレスを渡す。
        template <typename T>
        T* GetValuePtrAt(void* container) const
        {
            if (!Matches<T>())
            {
                return nullptr;
            }
            // 持ち主のアドレスを「1バイト単位のアドレス」(unsigned char*)として扱い、オフセット分だけ進める。
            // その場所を T として扱う。reinterpret_cast は「このアドレスを別の型として見る」強い型変換で、
            // 型の確認(上の if)を済ませた、この関数の中だけで使う。
            unsigned char* base = static_cast<unsigned char*>(container);
            return reinterpret_cast<T*>(base + m_offset);
        }

        // const 版。
        template <typename T>
        const T* GetValuePtrAt(const void* container) const
        {
            if (!Matches<T>())
            {
                return nullptr;
            }
            const unsigned char* base = static_cast<const unsigned char*>(container);
            return reinterpret_cast<const T*>(base + m_offset);
        }

        // 構造体のプロパティのとき、その構造体の先頭のアドレスを返す(型は気にせず、中のメンバーをたどるのに使う)。
        // 構造体でなければ nullptr。
        void* GetStructAddressAt(void* container) const
        {
            return m_type == PropertyType::Struct ? static_cast<unsigned char*>(container) + m_offset : nullptr;
        }

        const void* GetStructAddressAt(const void* container) const
        {
            return m_type == PropertyType::Struct ? static_cast<const unsigned char*>(container) + m_offset : nullptr;
        }

    private:
        // T が、このプロパティの型と合うか。構造体なら同じ構造体か(Color と Vector2 を取り違えないため)、
        // AssetRef なら同じアセットの型かも確かめる。
        template <typename T>
        bool Matches() const
        {
            if (PropertyTypeOf<T>() != m_type)
            {
                return false;
            }
            if constexpr (std::is_same_v<T, AssetReference>)
            {
                return true; // 型を問わないアセットの参照として読むときは、どのアセットの型でもよい(保存と GC が使う)
            }
            else
            {
                // AssetRef<TextureAsset> を AssetRef<SoundAsset> として読むような取り違えを防ぐ。
                return DetailTypeOf<T>() == m_detailType;
            }
        }

        const char* m_name;
        PropertyType m_type;
        std::size_t m_offset;
        const TypeInfo* m_detailType; // 構造体の型か、アセットの型(GetStructType / GetAssetType)
    };
}
