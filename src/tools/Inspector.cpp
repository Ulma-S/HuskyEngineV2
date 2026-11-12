// Inspector.h の実装。製品(Release)では中身が空になる。
#include "tools/Inspector.h"

#if HUSKY_DEVELOPMENT

#include "core/Property.h"
#include "engine/Actor.h"
#include "engine/Component.h"
#include "engine/RenderReflection.h" // render::Color の型情報(色は色の選び方で編集するため)

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*)

#include <cstdint>
#include <string>
#include <vector>

namespace husky::tools
{
    namespace
    {
        using core::Property;
        using core::PropertyType;

        // ハンドルが指している相手を、短い文字で表す(名前があるアクターなら名前、それ以外は型の名前)。
        // 文字列を作らずに、相手が持っている文字列をそのまま返す(毎フレームのメモリの確保を避けるため)。
        const char* DescribeHandle(core::ObjectHandle handle, core::ObjectRegistry& registry)
        {
            if (handle.IsNull())
            {
                return "(none)";
            }
            const core::Object* object = registry.Resolve(handle);
            if (object == nullptr)
            {
                return "(destroyed)"; // もう無い(弱い参照の相手が消えた)
            }
            if (const engine::Actor* actor = core::Cast<engine::Actor>(object); actor != nullptr && !actor->name.empty())
            {
                return actor->name.c_str();
            }
            return object->GetType().GetName();
        }

        // プロパティ1つを、型に合った部品(チェックボックス、数の入力、文字の入力など)で並べる。
        bool DrawProperty(const Property& property, void* container, core::ObjectRegistry& registry)
        {
            const char* name = property.GetName();
            switch (property.GetType())
            {
            case PropertyType::Bool:
                return ImGui::Checkbox(name, property.GetValuePtrAt<bool>(container));
            case PropertyType::Int32:
                // DragInt: マウスで左右にドラッグすると値が変わる。ダブルクリックで直接打ち込める。
                return ImGui::DragInt(name, property.GetValuePtrAt<std::int32_t>(container));
            case PropertyType::Float:
                return ImGui::DragFloat(name, property.GetValuePtrAt<float>(container), 0.5f);
            case PropertyType::String:
                return ImGui::InputText(name, property.GetValuePtrAt<std::string>(container));
            case PropertyType::ObjectHandle:
                ImGui::Text("%s: %s", name, DescribeHandle(*property.GetValuePtrAt<core::ObjectHandle>(container), registry));
                return false; // ハンドルは、今は表示だけ(付け替えの画面は、必要になったら作る)
            case PropertyType::ObjectHandleArray:
            {
                const auto& handles = *property.GetValuePtrAt<std::vector<core::ObjectHandle>>(container);
                // TreeNode: 折りたためる見出し。開いているときだけ true を返し、中身を作ったら TreePop で閉じる。
                if (ImGui::TreeNode(name, "%s (%zu)", name, handles.size()))
                {
                    for (const core::ObjectHandle handle : handles)
                    {
                        ImGui::BulletText("%s", DescribeHandle(handle, registry));
                    }
                    ImGui::TreePop();
                }
                return false;
            }
            case PropertyType::Struct:
            {
                void* structAddress = property.GetStructAddressAt(container);
                // 色は、数字4つより、色の選び方の部品で編集したほうが分かりやすいので、特別に扱う。
                // render::Color は float が r, g, b, a の順に4つ並んでいるので、先頭のアドレスを float[4] として渡せる。
                if (property.GetStructType() == core::StructTypeOf<render::Color>())
                {
                    return ImGui::ColorEdit4(name, &static_cast<render::Color*>(structAddress)->r);
                }
                // それ以外の構造体は、折りたためる見出しの中に、中のメンバーを同じやり方で並べる。
                bool changed = false;
                if (ImGui::TreeNode(name))
                {
                    changed = DrawProperties(*property.GetStructType(), structAddress, registry);
                    ImGui::TreePop();
                }
                return changed;
            }
            }
            return false;
        }
    }

    bool DrawProperties(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry)
    {
        bool changed = false;
        type.ForEachProperty([&](const Property& property) {
            // PushID: ImGui は部品を「名前」で見分けるので、同じ名前(x など)が別の場所にあっても混ざらないように、区切りを付ける。
            ImGui::PushID(property.GetName());
            changed |= DrawProperty(property, container, registry);
            ImGui::PopID();
        });
        return changed;
    }

    void SceneInspector::Draw(engine::Application& app)
    {
        engine::Scene& scene = app.GetScene();
        core::ObjectRegistry& registry = app.GetRegistry();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 300.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 400.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene"))
        {
            // --- アクターの一覧 ---
            ImGui::Text("Actors: %zu", scene.actors.size());
            ImGui::BeginChild("Actors", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
            // ImGuiListClipper: 見えている行だけを作る(1000 行あっても、画面に入る十数行ぶんしか処理しない)。
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(scene.actors.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const core::ObjectHandle handle = scene.actors[static_cast<std::size_t>(i)];
                    const engine::Actor* actor = registry.Resolve<engine::Actor>(handle);
                    if (actor == nullptr)
                    {
                        continue;
                    }
                    const char* label = actor->name.empty() ? actor->GetType().GetName() : actor->name.c_str();
                    ImGui::PushID(i);
                    if (ImGui::Selectable(label, handle == m_selected))
                    {
                        m_selected = handle;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            // --- 選んだアクター ---
            engine::Actor* selected = registry.Resolve<engine::Actor>(m_selected);
            if (selected == nullptr || selected->IsPendingDestroy())
            {
                ImGui::TextDisabled("(no actor selected)");
            }
            else
            {
                ImGui::SeparatorText(selected->GetType().GetName());
                DrawProperties(selected->GetType(), selected, registry);
                if (ImGui::Button("Destroy"))
                {
                    selected->Destroy(); // フレームの終わりに消える(弱い参照なので、選択は自然に外れる)
                }

                // --- コンポーネント ---
                for (std::size_t i = 0; i < selected->components.size(); ++i)
                {
                    engine::Component* component = registry.Resolve<engine::Component>(selected->components[i]);
                    if (component == nullptr || component->IsPendingDestroy())
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::CollapsingHeader(component->GetType().GetName(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        DrawProperties(component->GetType(), component, registry);
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }
}

#endif
