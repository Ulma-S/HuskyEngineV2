// JsonFormat.h の実装。yyjson(外部のライブラリ)を使うのは、このファイルだけ。
// 別のライブラリに替えるときは、このファイルの中身だけを書き直す(JsonFormat.h と ArchiveValue はそのまま)。
#include "engine/JsonFormat.h"

#include "core/MemoryTracker.h"

#include <yyjson.h>

#include <algorithm> // std::min
#include <cmath>     // std::isfinite
#include <cstdint>
#include <cstring>   // std::memcpy
#include <limits>

namespace husky::engine::JsonFormat
{
    namespace
    {
        // --- yyjson のメモリの確保を、エンジンの入口(MemoryTracker)に通す ---
        // yyjson は、確保の関数を差し替える口(yyjson_alc)を持っている。そこにこの3つを渡す。
        // ctx は yyjson が関数に渡してくれる自由な値で、ここでは使わない。

        void* TrackedMalloc(void* /*ctx*/, std::size_t size)
        {
            return core::TrackedAllocate(size, 16);
        }

        // 大きさを変える: 新しく確保し、中身を移して、古いほうを返す。
        // yyjson は old_size(元の大きさ)も教えてくれるので、どこまで移せばよいかが分かる。
        void* TrackedRealloc(void* /*ctx*/, void* pointer, std::size_t oldSize, std::size_t size)
        {
            void* resized = core::TrackedAllocate(size, 16);
            if (resized != nullptr && pointer != nullptr)
            {
                std::memcpy(resized, pointer, std::min(oldSize, size));
                core::TrackedFree(pointer);
            }
            return resized;
        }

        void TrackedFreeForYyjson(void* /*ctx*/, void* pointer)
        {
            core::TrackedFree(pointer);
        }

        // yyjson に渡す確保の関数の組。{ } の中は、yyjson_alc の4つのメンバー(malloc, realloc, free, ctx)の順。
        const yyjson_alc TrackedAllocator = {TrackedMalloc, TrackedRealloc, TrackedFreeForYyjson, nullptr};

        // --- 書き出し: ArchiveValue → yyjson の可変の値(yyjson_mut_val) ---
        // 配列とオブジェクトは中をたどる(再帰)。表せない値(NaN など)があれば ok を false にする。
        yyjson_mut_val* ToYyjson(yyjson_mut_doc* doc, const ArchiveValue& value, bool& ok)
        {
            switch (value.GetType())
            {
            case ArchiveValue::Type::Null:
                return yyjson_mut_null(doc);
            case ArchiveValue::Type::Bool:
                return yyjson_mut_bool(doc, value.AsBool());
            case ArchiveValue::Type::Int:
                return yyjson_mut_sint(doc, value.AsInt());
            case ArchiveValue::Type::Float:
                if (!std::isfinite(value.AsFloat()))
                {
                    ok = false; // JSON には NaN や無限大の書き方が無い
                    return yyjson_mut_null(doc);
                }
                if (value.IsSinglePrecision())
                {
                    // float の精度で、元の float に戻せる一番短い書き方にする(0.1f → 0.1)。
                    return yyjson_mut_float(doc, static_cast<float>(value.AsFloat()));
                }
                return yyjson_mut_real(doc, value.AsFloat());
            case ArchiveValue::Type::String:
                // strncpy: 文字列を yyjson の文書の中へコピーする(ArchiveValue が先に消えても大丈夫なように)。
                return yyjson_mut_strncpy(doc, value.AsString().data(), value.AsString().size());
            case ArchiveValue::Type::Array:
            {
                yyjson_mut_val* array = yyjson_mut_arr(doc);
                for (std::size_t i = 0; i < value.GetArraySize(); ++i)
                {
                    yyjson_mut_arr_append(array, ToYyjson(doc, value.At(i), ok));
                }
                return array;
            }
            case ArchiveValue::Type::Object:
            {
                yyjson_mut_val* object = yyjson_mut_obj(doc);
                for (std::size_t i = 0; i < value.GetMemberCount(); ++i)
                {
                    const std::string& name = value.GetMemberName(i);
                    yyjson_mut_val* key = yyjson_mut_strncpy(doc, name.data(), name.size());
                    yyjson_mut_obj_add(object, key, ToYyjson(doc, value.GetMemberValue(i), ok));
                }
                return object;
            }
            }
            return yyjson_mut_null(doc); // ここには来ない(すべての種類を上で扱っている)
        }

        // --- 読み込み: yyjson の読み取り専用の値(yyjson_val) → ArchiveValue ---
        ArchiveValue FromYyjson(yyjson_val* value)
        {
            switch (yyjson_get_type(value))
            {
            case YYJSON_TYPE_BOOL:
                return ArchiveValue(yyjson_get_bool(value));
            case YYJSON_TYPE_NUM:
                if (yyjson_is_sint(value))
                {
                    return ArchiveValue(static_cast<std::int64_t>(yyjson_get_sint(value)));
                }
                if (yyjson_is_uint(value))
                {
                    // 符号なしの整数。int64 に収まれば Int、収まらなければ(とても大きい数)小数にする。
                    const std::uint64_t unsignedValue = yyjson_get_uint(value);
                    if (unsignedValue <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    {
                        return ArchiveValue(static_cast<std::int64_t>(unsignedValue));
                    }
                    return ArchiveValue(static_cast<double>(unsignedValue));
                }
                return ArchiveValue(yyjson_get_real(value));
            case YYJSON_TYPE_STR:
                return ArchiveValue(std::string(yyjson_get_str(value), yyjson_get_len(value)));
            case YYJSON_TYPE_ARR:
            {
                ArchiveValue array = ArchiveValue::MakeArray();
                std::size_t index = 0;
                std::size_t max = 0;
                yyjson_val* element = nullptr;
                // yyjson_arr_foreach は yyjson のマクロ。配列の要素を1つずつ element に入れて回す。
                yyjson_arr_foreach(value, index, max, element)
                {
                    array.Append(FromYyjson(element));
                }
                return array;
            }
            case YYJSON_TYPE_OBJ:
            {
                ArchiveValue object = ArchiveValue::MakeObject();
                std::size_t index = 0;
                std::size_t max = 0;
                yyjson_val* key = nullptr;
                yyjson_val* member = nullptr;
                yyjson_obj_foreach(value, index, max, key, member)
                {
                    object.Set(std::string_view(yyjson_get_str(key), yyjson_get_len(key)), FromYyjson(member));
                }
                return object;
            }
            default:
                return ArchiveValue(); // null(と、ここでは使わない種類)
            }
        }
    }

    bool Write(const ArchiveValue& value, std::string& outText, bool pretty)
    {
        // yyjson の「可変の文書」を作り、木を写してから、文字列に書き出す。
        yyjson_mut_doc* doc = yyjson_mut_doc_new(&TrackedAllocator);
        if (doc == nullptr)
        {
            return false;
        }
        bool ok = true;
        yyjson_mut_doc_set_root(doc, ToYyjson(doc, value, ok));

        std::size_t length = 0;
        const yyjson_write_flag flags = pretty ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
        char* text = ok ? yyjson_mut_write_opts(doc, flags, &TrackedAllocator, &length, nullptr) : nullptr;
        if (text != nullptr)
        {
            outText.assign(text, length);
            TrackedAllocator.free(nullptr, text); // yyjson が確保した文字列は、同じ確保の関数で返す
        }
        yyjson_mut_doc_free(doc);
        return text != nullptr;
    }

    bool Read(std::string_view text, ArchiveValue& outValue, std::string* outError)
    {
        yyjson_read_err error{};
        // yyjson_read_opts は char*(書き換えられる文字列)を受け取るが、YYJSON_READ_INSITU を付けなければ書き換えない。
        // そのため const_cast で const を外して渡してよい。
        yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), YYJSON_READ_NOFLAG,
                                           &TrackedAllocator, &error);
        if (doc == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = std::string("JSON を読めません: ") + (error.msg != nullptr ? error.msg : "不明な理由") +
                            "(" + std::to_string(error.pos) + " バイト目)";
            }
            return false;
        }
        outValue = FromYyjson(yyjson_doc_get_root(doc));
        yyjson_doc_free(doc);
        return true;
    }
}
