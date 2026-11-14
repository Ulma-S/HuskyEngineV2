// core の構造体の型情報の中身(HR-1)。
// ヘッダには HUSKY_STRUCT_DECLARE だけを書き、中身は .cpp に置く(Object.h の HUSKY_OBJECT と同じ理由:
// ヘッダの inline 関数の中の static は、exe と DLL のそれぞれに別の写しができてしまうため)。
#include "core/StructReflection.h"
#include "core/Vector2.h"
#include "core/Vector3.h"

namespace husky::core
{
    HUSKY_STRUCT_BEGIN(Vector2)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
    HUSKY_STRUCT_END()

    HUSKY_STRUCT_BEGIN(Vector3)
        HUSKY_PROPERTY(x)
        HUSKY_PROPERTY(y)
        HUSKY_PROPERTY(z)
    HUSKY_STRUCT_END()
}
