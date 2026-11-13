// 型の付いた、ほかのオブジェクト(アクターやコンポーネント)への参照(SF-1)。プロパティにでき、GC は強い参照としてたどる。
// Unity の [SerializeField] PlayerController player; にあたる。
//
// 使い方:
//   class Turret : public engine::Component
//   {
//       HUSKY_OBJECT(Turret, engine::Component)
//       core::ObjectRef<PlayerActor> target;           // PlayerActor(か、その子)だけを指す
//   public:
//       HUSKY_REFLECTION_BEGIN()
//           HUSKY_PROPERTY(target)                     // private でも、クラスの中で登録すれば保存される
//       HUSKY_REFLECTION_END()
//   };
//   PlayerActor* player = target.Resolve(registry);    // 指していない・消えた・型が違うなら nullptr
//   target.Set(somePlayer);                            // 指す相手を決める(nullptr で外す)
//
// 中身はハンドル1つだけ(ObjectHandle と同じ)。保存も今の ObjectHandle と同じく、シーンのファイルの中の番号で書く。
// 違うのは T の型の情報を持つこと:
//   - 読み込みで、番号の相手が T でなければ null にして警告する(型の違うものを黙ってつながない)
//   - インスペクタが、選べる相手を T で絞る(REF-1)
// T は型の印に使うだけで、中身は T によらず同じ(ObjectReference)。保存と GC は T を知らずに扱える
// (AssetRef<T> と AssetReference の関係と同じ。スクリプトの型からも、型の情報(TypeInfo)を値として渡せば同じ形で使える)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Handle.h"

namespace husky::core
{
    class Object;
    using ObjectHandle = Handle<Object>;

    // 型を問わないオブジェクトの参照。保存と GC は、この形で扱う。
    struct ObjectReference
    {
        ObjectHandle handle; // 指すオブジェクト(どこも指さなければ IsNull)

        bool IsNull() const { return handle.IsNull(); }
    };

    // T 型(か、その子)のオブジェクトへの参照。メンバーは足さない(ObjectReference と同じ形のまま、型の印だけを付ける)。
    template <typename T>
    struct ObjectRef : ObjectReference
    {
        using ObjectType = T;

        // 指している T を返す。指していない・もう無い・T でない(Cast が失敗する)なら nullptr。
        // Registry はテンプレートにして、ここで ObjectRegistry.h を読まないようにしている
        // (ObjectRegistry.h → Object.h → Property.h → この ObjectRef.h と、読み込みが輪にならないように)。
        template <typename Registry>
        T* Resolve(const Registry& registry) const
        {
            return registry.template Resolve<T>(handle);
        }

        // 指す相手を決める。nullptr なら、どこも指さない。
        void Set(const T* object) { handle = object != nullptr ? object->GetHandle() : ObjectHandle{}; }
    };
}
