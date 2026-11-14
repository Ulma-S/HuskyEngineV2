// render の型のうち、プロパティとして保存したいものを、構造体のリフレクションに登録する。
//
// render のヘッダには手を入れず、engine の側で登録している(render は「描く」ことだけに集中させ、
// リフレクションやシリアライズの事情を持ち込まないため)。登録は型と同じ名前空間(husky::render)に書く決まりなので、
// ここで husky::render を開き直して書いている(StructReflection.h の「しくみ」を参照)。
//
// render の型をプロパティに使うヘッダ(SpriteComponent.h など)は、このヘッダを読み込むこと。
// 読み込まずに HUSKY_PROPERTY(color) と書くと、「この型はプロパティにできません」のコンパイルエラーになる。
#pragma once

#include "core/StructReflection.h"
#include "render/RenderDevice.h"

namespace husky::render
{
    HUSKY_STRUCT_DECLARE(Color) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)
}
