// SpriteAnimatorComponent.h の実装。
#include "engine/SpriteAnimatorComponent.h"

#include "core/ObjectRegistry.h"
#include "engine/Actor.h"
#include "engine/SpriteComponent.h"
#include "engine/SpriteSheetAsset.h"
#include "engine/SpriteSheetData.h"

namespace husky::engine
{
    const SpriteSheetAsset* FindLoadedSpriteSheet(const SpriteComponent& sprite, core::ObjectRegistry& registry)
    {
        // asset は「前に読んだもの」なので、その GUID が今の guid と同じかも確かめる
        // (インスペクタで sheet を選び直した直後は、描画で読み直すまで古いアセットを指している)。
        const SpriteSheetAsset* sheet = registry.Resolve<SpriteSheetAsset>(sprite.sheet.asset);
        if (sheet == nullptr || !sheet->IsLoaded() || sheet->GetGuid() != sprite.sheet.guid)
        {
            return nullptr;
        }
        return sheet;
    }

    void SpriteAnimatorComponent::Play(std::int32_t animationId)
    {
        animation = animationId;
        playing = true;
        m_time = 0.0;
        m_timeAnimation = animationId; // 時間はこのアニメーションのもの(Tick で 0 に戻し直さない)
    }

    void SpriteAnimatorComponent::Tick(double dt)
    {
        Actor* owner = GetOwner();
        SpriteComponent* sprite = owner != nullptr ? owner->GetComponent<SpriteComponent>() : nullptr;
        if (sprite == nullptr) // 見た目が無ければ、書き換える先が無い
        {
            return;
        }
        core::ObjectRegistry* registry = GetRegistry(); // 自分を作ったレジストリ(アセットも同じレジストリにいる)
        const SpriteSheetAsset* sheet = registry != nullptr ? FindLoadedSpriteSheet(*sprite, *registry) : nullptr;
        if (sheet == nullptr) // まだ読めていない: 時間を進めずに待つ
        {
            return;
        }

        // animation が(インスペクタやファイルの読み込みで)書き換えられていたら、新しいアニメーションの最初から。
        if (animation != m_timeAnimation)
        {
            m_timeAnimation = animation;
            m_time = 0.0;
        }
        const SpriteAnimation* clip = sheet->FindAnimation(animation);
        if (clip == nullptr) // 0、または消されたアニメーション
        {
            return;
        }

        // speed > 0.0f は、NaN(不正な値)のときも false になるので、止まっているのと同じに扱える。
        if (playing && speed > 0.0f && dt > 0.0)
        {
            m_time += dt * static_cast<double>(speed);
        }
        const std::int32_t frame = SampleAnimationFrame(*clip, m_time);
        if (frame != 0)
        {
            sprite->sprite = frame;
        }
    }
}
