// Object の型情報の中身。
// ヘッダに書くと、exe と DLL のそれぞれに別の写しができてしまうので、.cpp に置く(Object.h の HUSKY_OBJECT の説明)。
#include "core/Object.h"

namespace husky::core
{
    const TypeInfo& Object::StaticType()
    {
        static const TypeInfo info("Object", nullptr);
        return info;
    }
}
