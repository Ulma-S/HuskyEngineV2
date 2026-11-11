// Method.h の、テンプレートでない部分の実装。
#include "core/Method.h"

#include "core/Object.h"

namespace husky::core
{
    bool Method::Invoke(Object& self, const std::vector<Value>& args, Value& result) const
    {
        // self が、このメソッドを持つ型(か、その子孫)であることを確かめる。
        // 違う型のオブジェクトで呼ぶと、中継関数の static_cast が間違った型として扱ってしまうため。
        if (m_owner == nullptr || !self.GetType().IsA(*m_owner))
        {
            return false;
        }
        // args.data(): vector の中身の先頭のアドレス(空でもよい。そのときは argCount が 0)。
        return m_invoker(self, args.data(), args.size(), result);
    }
}
