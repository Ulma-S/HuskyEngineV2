// EngineTypes.h の実装。
#include "engine/EngineTypes.h"

#include "engine/Actor.h"
#include "engine/AudioSourceComponent.h"
#include "engine/CameraComponent.h"
#include "engine/Collider2D.h"
#include "engine/Component.h"
#include "engine/Rigidbody2D.h"
#include "engine/Scene.h"
#include "engine/SpriteAnimatorComponent.h"
#include "engine/SpriteComponent.h"
#include "engine/UIActor.h"
#include "engine/FontAsset.h"
#include "engine/PixelRect.h"
#include "engine/PrefabAsset.h"
#include "engine/RenderReflection.h"
#include "engine/ScriptableData.h"
#include "engine/SoundAsset.h"
#include "engine/SpriteSheetAsset.h"
#include "engine/TextureAsset.h"
#include "engine/Transform.h"
#include "engine/UIComponents.h"

namespace husky::engine
{
    // エンジンの型の型情報の中身(HR-1)。ヘッダの HUSKY_OBJECT は宣言だけなので、ここで1つずつ作る。
    // このファイルは HuskyEngine.dll(静的リンクの構成ではエンジンのライブラリ)にだけ入るので、型情報はプログラムに1つだけになる。
    // エンジンに HUSKY_OBJECT の型を足したら、ここにも足す(忘れるとリンクのときに StaticType が見つからないエラーになる)。
    HUSKY_OBJECT_DEFINE(Scene)
    HUSKY_OBJECT_DEFINE(Actor)
    HUSKY_OBJECT_DEFINE(Component)
    HUSKY_OBJECT_DEFINE(SpriteComponent)
    HUSKY_OBJECT_DEFINE(SpriteAnimatorComponent)
    HUSKY_OBJECT_DEFINE(CameraComponent)
    HUSKY_OBJECT_DEFINE(Collider2D)
    HUSKY_OBJECT_DEFINE(BoxCollider2D)
    HUSKY_OBJECT_DEFINE(CircleCollider2D)
    HUSKY_OBJECT_DEFINE(Rigidbody2D)
    HUSKY_OBJECT_DEFINE(AudioSourceComponent)
    HUSKY_OBJECT_DEFINE(UIActor)
    HUSKY_OBJECT_DEFINE(CanvasComponent)
    HUSKY_OBJECT_DEFINE(UIImageComponent)
    HUSKY_OBJECT_DEFINE(UITextComponent)
    HUSKY_OBJECT_DEFINE(UIButtonComponent)
    HUSKY_OBJECT_DEFINE(ScriptableData)
    HUSKY_OBJECT_DEFINE(ScriptableDataAsset)
    HUSKY_OBJECT_DEFINE(TextureAsset)
    HUSKY_OBJECT_DEFINE(SpriteSheetAsset)
    HUSKY_OBJECT_DEFINE(PrefabAsset)
    HUSKY_OBJECT_DEFINE(FontAsset)
    HUSKY_OBJECT_DEFINE(Asset)
    HUSKY_OBJECT_DEFINE(SoundAsset)

    void RegisterEngineTypes(core::TypeRegistry& types)
    {
        types.Register<Scene>();
        types.Register<Actor>();
        types.Register<Component>();
        types.Register<SpriteComponent>();
        types.Register<SpriteAnimatorComponent>();
        types.Register<CameraComponent>();
        types.Register<BoxCollider2D>();    // Collider2D そのものは形が決まらないので登録しない
        types.Register<CircleCollider2D>();
        types.Register<Rigidbody2D>();
        types.Register<AudioSourceComponent>();
        types.Register<UIActor>();         // ゲーム用 UI のアクター(UI-5a)
        types.Register<CanvasComponent>();
        types.Register<UIImageComponent>(); // UI の画像と文字(UI-6a)
        types.Register<UITextComponent>();
        types.Register<UIButtonComponent>(); // UI のボタン(UI-6b)
    }
}

// 構造体と enum の型情報の中身(HR-1)。ヘッダには HUSKY_STRUCT_DECLARE / HUSKY_ENUM_DECLARE だけを書く。
// 登録は、型と同じ名前空間に書く(ADL で見つけるため)。
namespace husky::audio
{
    HUSKY_ENUM_BEGIN(Bus)
        HUSKY_ENUM_VALUE(Effect)
        HUSKY_ENUM_VALUE(Music)
    HUSKY_ENUM_END()
}

namespace husky::render
{
    HUSKY_STRUCT_BEGIN(Color)
        HUSKY_PROPERTY(r)
        HUSKY_PROPERTY(g)
        HUSKY_PROPERTY(b)
        HUSKY_PROPERTY(a)
    HUSKY_STRUCT_END()
}

namespace husky::engine
{
    HUSKY_STRUCT_BEGIN(PixelRect)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
        HUSKY_PROPERTY(width)
        HUSKY_PROPERTY(height)
    HUSKY_STRUCT_END()

    HUSKY_STRUCT_BEGIN(Transform)
        HUSKY_PROPERTY(position)
        HUSKY_PROPERTY(rotation)
        HUSKY_PROPERTY(scale)
    HUSKY_STRUCT_END()

    HUSKY_STRUCT_BEGIN(RectTransform)
        HUSKY_PROPERTY(anchorMin)
        HUSKY_PROPERTY(anchorMax)
        HUSKY_PROPERTY(pivot)
        HUSKY_PROPERTY(anchoredPosition)
        HUSKY_PROPERTY(sizeDelta)
    HUSKY_STRUCT_END()

    HUSKY_ENUM_BEGIN(BodyType2D)
        HUSKY_ENUM_VALUE(Static)
        HUSKY_ENUM_VALUE(Kinematic)
        HUSKY_ENUM_VALUE(Dynamic)
    HUSKY_ENUM_END()

    HUSKY_ENUM_BEGIN(TextAlign)
        HUSKY_ENUM_VALUE(Left)
        HUSKY_ENUM_VALUE(Center)
        HUSKY_ENUM_VALUE(Right)
    HUSKY_ENUM_END()

    HUSKY_ENUM_BEGIN(UIVerticalAlign)
        HUSKY_ENUM_VALUE(Top)
        HUSKY_ENUM_VALUE(Middle)
        HUSKY_ENUM_VALUE(Bottom)
    HUSKY_ENUM_END()
}
