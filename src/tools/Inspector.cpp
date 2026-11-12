// Inspector.h の実装。製品(Release)では中身が空になる。
#include "tools/Inspector.h"

#if HUSKY_DEVELOPMENT

#include "core/Property.h"
#include "engine/Actor.h"
#include "engine/AssetLoader.h"
#include "engine/Component.h"
#include "engine/RenderReflection.h" // render::Color の型情報(色は色の選び方で編集するため)
#include "engine/SpriteAnimatorComponent.h" // アニメーションの ID を名前で選ぶ表示
#include "engine/SpriteComponent.h"   // 切り出しの ID を名前で選ぶ表示
#include "engine/SpriteSheetAsset.h"

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*)

#include <algorithm> // std::sort
#include <cstdint>
#include <string>
#include <utility>   // std::pair
#include <vector>

namespace husky::tools
{
    namespace
    {
        using core::Property;
        using core::PropertyType;

        // ---- プロパティごとの表示の差し替え ----

        struct DrawerEntry
        {
            const Property* property; // 差し替えるプロパティ(TypeInfo の中にあるもの。登録の後は場所が変わらない)
            PropertyDrawer drawer;
        };

        // 差し替えの一覧。関数の中の static 変数なので、最初に呼ばれたときに1回だけ作られる
        // (グローバル変数にしないのは、ほかのグローバル変数の初期化との順番の問題を避けるため)。
        std::vector<DrawerEntry>& Drawers()
        {
            static std::vector<DrawerEntry> drawers;
            return drawers;
        }

        PropertyDrawer FindDrawer(const Property& property);

        // SpriteComponent::sprite(切り出しの ID)を、スプライトシートの切り出しの名前から選ぶ箱で並べる。
        // スプライトシートがまだ読めていなければ、ID の数字を表示するだけ。
        bool DrawSpriteSlicePicker(const Property& property, void* container, core::ObjectRegistry& registry,
                                   const engine::AssetDatabase* /*assets*/)
        {
            // container はこのプロパティの持ち主(SpriteComponent)の先頭。単一継承なので、そのまま型を戻せる。
            auto* component = static_cast<engine::SpriteComponent*>(container);
            std::int32_t* id = property.GetValuePtrAt<std::int32_t>(container);
            const engine::SpriteSheetAsset* sheet = registry.Resolve<engine::SpriteSheetAsset>(component->sheet.asset);
            if (sheet == nullptr || !sheet->IsLoaded() || sheet->GetGuid() != component->sheet.guid)
            {
                ImGui::Text("%s: %d (sheet not loaded)", property.GetName(), *id);
                return false;
            }

            const engine::SpriteSlice* current = sheet->FindSlice(*id);
            const char* preview = *id == 0 ? "(none)" : (current != nullptr ? current->name.c_str() : "(missing)");
            bool changed = false;
            if (ImGui::BeginCombo(property.GetName(), preview))
            {
                if (ImGui::Selectable("(none)", *id == 0))
                {
                    *id = 0;
                    changed = true;
                }
                // 切り出しは、ファイルに書いた順に並べる(名前が同じでも ID で区別できるように、PushID で区切る)。
                for (const engine::SpriteSlice& slice : sheet->GetSlices())
                {
                    ImGui::PushID(slice.id);
                    if (ImGui::Selectable(slice.name.c_str(), slice.id == *id) && slice.id != *id)
                    {
                        *id = slice.id;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // SpriteAnimatorComponent::animation(アニメーションの ID)を、アニメーションの名前から選ぶ箱で並べる。
        // アニメーションは、同じアクターの SpriteComponent が指すスプライトシートから探す。
        // SpriteComponent が無いか、スプライトシートがまだ読めていなければ、ID の数字を表示するだけ。
        bool DrawSpriteAnimationPicker(const Property& property, void* container, core::ObjectRegistry& registry,
                                       const engine::AssetDatabase* /*assets*/)
        {
            // container はこのプロパティの持ち主(SpriteAnimatorComponent)の先頭。単一継承なので、そのまま型を戻せる。
            auto* animator = static_cast<engine::SpriteAnimatorComponent*>(container);
            std::int32_t* id = property.GetValuePtrAt<std::int32_t>(container);
            engine::Actor* owner = animator->GetOwner();
            const engine::SpriteComponent* sprite = owner != nullptr ? owner->GetComponent<engine::SpriteComponent>() : nullptr;
            const engine::SpriteSheetAsset* sheet = sprite != nullptr ? engine::FindLoadedSpriteSheet(*sprite, registry) : nullptr;
            if (sheet == nullptr)
            {
                ImGui::Text("%s: %d (sheet not loaded)", property.GetName(), *id);
                return false;
            }

            const engine::SpriteAnimation* current = sheet->FindAnimation(*id);
            const char* preview = *id == 0 ? "(none)" : (current != nullptr ? current->name.c_str() : "(missing)");
            bool changed = false;
            if (ImGui::BeginCombo(property.GetName(), preview))
            {
                if (ImGui::Selectable("(none)", *id == 0))
                {
                    *id = 0;
                    changed = true;
                }
                for (const engine::SpriteAnimation& clip : sheet->GetAnimations())
                {
                    ImGui::PushID(clip.id);
                    if (ImGui::Selectable(clip.name.c_str(), clip.id == *id) && clip.id != *id)
                    {
                        *id = clip.id; // 時間を 0 に戻すのは、アニメーター自身(次の Tick で ID の変化に気づく)
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // 最初から入っている差し替えを登録する(1回だけ)。
        void RegisterBuiltInDrawers()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }
            registered = true;
            RegisterPropertyDrawer(engine::SpriteComponent::StaticType(), "sprite", DrawSpriteSlicePicker);
            RegisterPropertyDrawer(engine::SpriteAnimatorComponent::StaticType(), "animation", DrawSpriteAnimationPicker);
        }

        PropertyDrawer FindDrawer(const Property& property)
        {
            RegisterBuiltInDrawers();
            for (const DrawerEntry& entry : Drawers())
            {
                if (entry.property == &property)
                {
                    return entry.drawer;
                }
            }
            return nullptr;
        }

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

        // アセットの参照を、選択肢の箱(コンボボックス)で並べる。選べるのは、assetType として読めるアセットだけ。
        // 選び直したら GUID だけを書き換え、読んであったアセットのハンドルは手放す(描くときに、ローダが新しい GUID で読む)。
        bool DrawAssetRef(const char* name, core::AssetReference& reference, const core::TypeInfo& assetType,
                          const engine::AssetDatabase* assets)
        {
            // 今の値の表示: 表にあればパス、無ければ GUID(ファイルが消えた、.meta が壊れたなど)。
            // ふだん(何も指さない、表にある)は、文字列を作らずに、ある文字列をそのまま使う(毎フレームの確保を避ける)。
            const std::string* currentPath = assets != nullptr ? assets->FindPath(reference.guid) : nullptr;
            std::string missing; // 表に無いときだけ作る
            const char* preview = "(none)";
            if (currentPath != nullptr)
            {
                preview = currentPath->c_str();
            }
            else if (!reference.IsNull())
            {
                missing = "(missing) " + reference.guid.ToString();
                preview = missing.c_str();
            }
            if (assets == nullptr)
            {
                ImGui::Text("%s: %s", name, preview);
                return false;
            }

            bool changed = false;
            // BeginCombo: 箱が開いているときだけ true を返す。中身を作ったら EndCombo で閉じる。
            // 一覧を集めて並べ替えるのは、箱が開いている間だけ(閉じているときは何もしないので、毎フレームの負担が無い)。
            if (ImGui::BeginCombo(name, preview))
            {
                if (ImGui::Selectable("(none)", reference.IsNull()))
                {
                    reference.guid = {};
                    reference.asset = {};
                    changed = true;
                }
                std::vector<std::pair<std::string, core::Guid>> choices; // (パス, GUID)。パスの順に並べるため
                assets->ForEachAsset([&](const core::Guid& guid, const std::string& path) {
                    if (engine::AssetLoader::CanLoadAs(assetType, path))
                    {
                        choices.emplace_back(path, guid);
                    }
                });
                std::sort(choices.begin(), choices.end());
                for (const auto& [path, guid] : choices)
                {
                    if (ImGui::Selectable(path.c_str(), guid == reference.guid) && guid != reference.guid)
                    {
                        reference.guid = guid;
                        reference.asset = {};
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // プロパティ1つを、型に合った部品(チェックボックス、数の入力、文字の入力など)で並べる。
        bool DrawProperty(const Property& property, void* container, core::ObjectRegistry& registry,
                          const engine::AssetDatabase* assets)
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
                    changed = DrawProperties(*property.GetStructType(), structAddress, registry, assets);
                    ImGui::TreePop();
                }
                return changed;
            }
            case PropertyType::AssetRef:
                return DrawAssetRef(name, *property.GetValuePtrAt<core::AssetReference>(container),
                                    *property.GetAssetType(), assets);
            case PropertyType::Guid:
            {
                // 変わらない ID は、人が書き換えるものではないので、灰色の文字で読むだけにする(人は名前で区別すればよい)。
                // GUID の文字列の形(8-4-4-4-12 桁)を、文字列を作らずに書く(毎フレームの確保を避ける)。
                const core::Guid& guid = *property.GetValuePtrAt<core::Guid>(container);
                ImGui::TextDisabled("%s: %08llx-%04llx-%04llx-%04llx-%012llx", name,
                                    static_cast<unsigned long long>(guid.high >> 32),
                                    static_cast<unsigned long long>((guid.high >> 16) & 0xffffu),
                                    static_cast<unsigned long long>(guid.high & 0xffffu),
                                    static_cast<unsigned long long>(guid.low >> 48),
                                    static_cast<unsigned long long>(guid.low & 0xffffffffffffull));
                return false;
            }
            }
            return false;
        }
    }

    bool DrawProperties(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                        const engine::AssetDatabase* assets)
    {
        bool changed = false;
        type.ForEachProperty([&](const Property& property) {
            // PushID: ImGui は部品を「名前」で見分けるので、同じ名前(x など)が別の場所にあっても混ざらないように、区切りを付ける。
            ImGui::PushID(property.GetName());
            // 差し替えが登録されていれば、そちらで表示する。無ければ、型に合った標準の部品で表示する。
            const PropertyDrawer drawer = FindDrawer(property);
            changed |= drawer != nullptr ? drawer(property, container, registry, assets)
                                         : DrawProperty(property, container, registry, assets);
            ImGui::PopID();
        });
        return changed;
    }

    bool RegisterPropertyDrawer(const core::TypeInfo& type, const char* propertyName, PropertyDrawer drawer)
    {
        const Property* property = type.FindProperty(propertyName);
        if (property == nullptr)
        {
            return false;
        }
        for (DrawerEntry& entry : Drawers())
        {
            if (entry.property == property)
            {
                entry.drawer = drawer; // 同じプロパティなら置き換える
                return true;
            }
        }
        Drawers().push_back(DrawerEntry{property, drawer});
        return true;
    }

    void SceneInspector::Draw(engine::Application& app)
    {
        engine::Scene& scene = app.GetScene();
        core::ObjectRegistry& registry = app.GetRegistry();

        // 最初は画面の右上に出す(左上のデバッグの窓は、コマンドが増えると下へ伸びるので、その下に置くと重なる)。
        const float width = 360.0f;
        const float x = ImGui::GetIO().DisplaySize.x - width - 16.0f;
        ImGui::SetNextWindowPos(ImVec2(x > 16.0f ? x : 16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, 400.0f), ImGuiCond_FirstUseEver);
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
                DrawProperties(selected->GetType(), selected, registry, &app.GetAssetDatabase());
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
                        DrawProperties(component->GetType(), component, registry, &app.GetAssetDatabase());
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }
}

#endif
