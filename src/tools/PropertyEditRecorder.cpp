// PropertyEditRecorder.h の実装。製品(Release)では中身が空になる。
#include "tools/PropertyEditRecorder.h"

#if HUSKY_DEVELOPMENT

#include "engine/Actor.h"
#include "engine/Component.h"
#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"
#include "engine/RenderReflection.h" // render::Color の型情報
#include "tools/EditorCommands.h"

namespace husky::tools
{
    void PropertyEditRecorder::BeginFrame(engine::Application& app, EditorCommandRegistry* commands)
    {
        m_app = &app;
        m_commands = commands;
        m_seenThisFrame = false;
    }

    void PropertyEditRecorder::SetOwner(const core::Object& owner)
    {
        m_owner = owner.GetHandle();
        m_depth = 0;
        m_overflow = 0;
    }

    bool PropertyEditRecorder::PushPath(const core::Property& property)
    {
        if (m_overflow > 0 || m_depth >= MaxPathDepth)
        {
            ++m_overflow; // 深すぎる。この下は記録しない(Pop の数を合わせるために数だけ数える)
            return false;
        }
        m_path[m_depth++] = &property;
        return true;
    }

    void PropertyEditRecorder::PopPath()
    {
        if (m_overflow > 0)
        {
            --m_overflow;
        }
        else if (m_depth > 0)
        {
            --m_depth;
        }
    }

    PropertyEditRecorder::Kind PropertyEditRecorder::GetKind(const core::Property& property)
    {
        switch (property.GetType())
        {
        case core::PropertyType::Bool: return Kind::Bool;
        case core::PropertyType::Int32: return Kind::Int32;
        case core::PropertyType::Float: return Kind::Float;
        case core::PropertyType::String: return Kind::String;
        case core::PropertyType::AssetRef: return Kind::AssetRef;
        case core::PropertyType::Struct:
            // 構造体は、中の値1つずつを記録する。色だけは、部品が4つの値を一度に変えるので、まとめて1つとして扱う。
            return property.GetStructType() == core::StructTypeOf<render::Color>() ? Kind::Color : Kind::None;
        default:
            return Kind::None; // ハンドル、配列、guid は、インスペクタでは書き換えない
        }
    }

    void PropertyEditRecorder::Take(Kind kind, const core::Property& property, void* container, Snapshot& out)
    {
        out.kind = kind;
        switch (kind)
        {
        case Kind::Bool: out.boolValue = *property.GetValuePtrAt<bool>(container); break;
        case Kind::Int32: out.intValue = *property.GetValuePtrAt<std::int32_t>(container); break;
        case Kind::Float: out.floatValue = *property.GetValuePtrAt<float>(container); break;
        // assign: 入れ物の容量を残したまま中身を入れ替える(短くなっても縮まないので、温まった後は確保しない)。
        case Kind::String: out.text.assign(*property.GetValuePtrAt<std::string>(container)); break;
        case Kind::Color: out.color = *static_cast<render::Color*>(property.GetStructAddressAt(container)); break;
        case Kind::AssetRef: out.assetRef = *property.GetValuePtrAt<core::AssetReference>(container); break;
        case Kind::None: break;
        }
    }

    void PropertyEditRecorder::Apply(const Snapshot& snapshot, const core::Property& property, void* container)
    {
        switch (snapshot.kind)
        {
        case Kind::Bool: *property.GetValuePtrAt<bool>(container) = snapshot.boolValue; break;
        case Kind::Int32: *property.GetValuePtrAt<std::int32_t>(container) = snapshot.intValue; break;
        case Kind::Float: *property.GetValuePtrAt<float>(container) = snapshot.floatValue; break;
        case Kind::String: property.GetValuePtrAt<std::string>(container)->assign(snapshot.text); break;
        case Kind::Color: *static_cast<render::Color*>(property.GetStructAddressAt(container)) = snapshot.color; break;
        case Kind::AssetRef: *property.GetValuePtrAt<core::AssetReference>(container) = snapshot.assetRef; break;
        case Kind::None: break;
        }
    }

    void PropertyEditRecorder::BeforeLeaf(const core::Property& property, void* container)
    {
        const Kind kind = m_overflow == 0 ? GetKind(property) : Kind::None;
        Take(kind, property, container, m_snapshot);
    }

    bool PropertyEditRecorder::IsEditingCurrentPath() const
    {
        if (!m_editing || m_overflow > 0 || m_editOwner != m_owner || m_editDepth != m_depth)
        {
            return false;
        }
        for (std::size_t i = 0; i < m_depth; ++i)
        {
            if (m_editPath[i] != m_path[i]) // 同じ Property を指していれば同じ(position.x と scale.x は道筋の途中が違う)
            {
                return false;
            }
        }
        return true;
    }

    void PropertyEditRecorder::AfterLeaf(const core::Property& property, void* container, bool changed, bool active)
    {
        // 種類は property から決め直す(構造体の見出しでは、中の値を描く間に m_snapshot が中の値の写しに変わっているため)。
        const Kind kind = m_overflow == 0 ? GetKind(property) : Kind::None;
        if (kind == Kind::None || m_snapshot.kind != kind || m_app == nullptr)
        {
            return; // 記録できない種類
        }

        if (changed && !IsEditingCurrentPath())
        {
            // 別の部品の編集が途中なら、先にそちらを確定する(ふつうは起きない。マウスは1つしか無いので)。
            if (m_editing)
            {
                Commit();
            }
            // 編集の始まり: 前の値を、保存と同じ形で控える。部品はもう値を書き換えているので、
            // 変わった後の値を控え → 前の値(BeforeLeaf の写し)に戻して読み → 変わった後の値に戻す。
            Take(m_snapshot.kind, property, container, m_after);
            Apply(m_snapshot, property, container);
            const core::Object* owner = m_app->GetRegistry().Resolve(m_owner);
            m_before = owner != nullptr ? ReadValueAtPath(*owner, m_path, m_depth) : engine::ArchiveValue();
            Apply(m_after, property, container);

            m_editing = true;
            m_editOwner = m_owner;
            m_editDepth = m_depth;
            for (std::size_t i = 0; i < m_depth; ++i)
            {
                m_editPath[i] = m_path[i];
            }
        }

        if (IsEditingCurrentPath())
        {
            m_seenThisFrame = true;
            if (!active)
            {
                Commit(); // 手を離した(チェックボックスなら、変わったそのフレーム)
            }
        }
    }

    void PropertyEditRecorder::EndFrame()
    {
        if (m_editing && !m_seenThisFrame)
        {
            Commit(); // 編集の途中の部品が描かれなくなった
        }
        m_seenThisFrame = false;
    }

    engine::ArchiveValue PropertyEditRecorder::ReadValueAtPath(const core::Object& owner,
                                                               const core::Property* const* path, std::size_t depth)
    {
        // 保存と同じ道具で、持ち主のプロパティを値の木にして、道筋のとおりに下りる(確定のときだけなので、確保してよい)。
        const engine::ArchiveValue all = engine::ObjectSerializer::Write(owner);
        const engine::ArchiveValue* value = all.Find("properties");
        for (std::size_t i = 0; i < depth && value != nullptr; ++i)
        {
            value = value->Find(path[i]->GetName());
        }
        return value != nullptr ? *value : engine::ArchiveValue();
    }

    void PropertyEditRecorder::Commit()
    {
        m_editing = false;
        if (m_app == nullptr || m_commands == nullptr || m_editDepth == 0)
        {
            return;
        }
        core::Object* owner = m_app->GetRegistry().Resolve(m_editOwner);
        if (owner == nullptr || owner->IsPendingDestroy())
        {
            return; // 編集の途中で消えた
        }

        // 今の値。前の値と同じなら(ドラッグして元の位置に戻した、など)、何もしない。
        const engine::ArchiveValue now = ReadValueAtPath(*owner, m_editPath, m_editDepth);
        std::string beforeText;
        std::string nowText;
        engine::JsonFormat::Write(m_before, beforeText, false);
        engine::JsonFormat::Write(now, nowText, false);
        if (beforeText == nowText)
        {
            return;
        }

        // property.set が今は使えない(再生中など)なら、値はその場で変わったまま、履歴には残さない。
        const EditorCommand* setProperty = m_commands->Find("property.set");
        if (setProperty == nullptr || EditorCommandRegistry::GetUnavailableReason(*setProperty, *m_app) != nullptr)
        {
            return;
        }

        // コマンドの引数: アクターは guid で、コンポーネントなら型の名前も付ける。道筋は点でつなぐ。
        engine::Actor* actor = core::Cast<engine::Actor>(owner);
        const engine::Component* component = nullptr;
        if (actor == nullptr)
        {
            component = core::Cast<engine::Component>(owner);
            actor = component != nullptr ? component->GetOwner() : nullptr;
        }
        if (actor == nullptr)
        {
            return; // アクターにもコンポーネントにも属さないもの(今のインスペクタでは描かない)
        }
        std::string path;
        for (std::size_t i = 0; i < m_editDepth; ++i)
        {
            path += i == 0 ? "" : ".";
            path += m_editPath[i]->GetName();
        }

        // 前の値に戻してから、コマンドで今の値にする(コマンドが前の値を正しく控え、Undo で戻せるように)。
        engine::ArchiveValue entry = engine::ArchiveValue::MakeObject();
        engine::ArchiveValue* parent = &entry.Set("properties", engine::ArchiveValue::MakeObject());
        for (std::size_t i = 0; i + 1 < m_editDepth; ++i)
        {
            parent = &parent->Set(m_editPath[i]->GetName(), engine::ArchiveValue::MakeObject());
        }
        parent->Set(m_editPath[m_editDepth - 1]->GetName(), m_before);
        engine::ObjectSerializer::ReadProperties(entry, *owner);

        engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
        args.Set("actor", engine::ArchiveValue(actor->guid.ToString()));
        if (component != nullptr)
        {
            // 同じ型のコンポーネントが2つ以上あると、最初のものを指してしまう(今のゲームには無い。必要になったら番号で指す)。
            args.Set("component", engine::ArchiveValue(component->GetType().GetName()));
        }
        args.Set("property", engine::ArchiveValue(path));
        args.Set("value", now);
        m_commands->Execute("property.set", *m_app, args);
    }
}

#endif
