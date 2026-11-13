// ScriptableData.h の実装(書き出し)。ScriptableDataAsset のデストラクタは、ほかのアセットと同じく AssetLoader.cpp にある。
#include "engine/ScriptableData.h"

#include "engine/ObjectSerializer.h"

namespace husky::engine
{
    ArchiveValue WriteScriptableData(const ScriptableData& data, std::vector<std::string>* outWarnings)
    {
        if (outWarnings != nullptr)
        {
            // ObjectSerializer::Write は、番号の対応(toId)を渡さなければハンドルを書かない。
            // 黙って消えると分かりにくいので、書かなかったプロパティの名前を残す(構造体の中のハンドルは今は数えない)。
            data.GetType().ForEachProperty([&](const core::Property& property) {
                if (property.IsReference())
                {
                    outWarnings->push_back(std::string("ハンドルのプロパティは書きません: ") + property.GetName());
                }
            });
        }
        return ObjectSerializer::Write(data);
    }
}
