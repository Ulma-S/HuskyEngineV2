// テスト用の音の出す先。本物の装置は使わず、テストが手で「次の N フレームを書いて」を呼ぶ。
// 本物では音のスレッドが呼ぶところを、テストのスレッドが呼ぶので、結果は毎回同じになる。
#pragma once

#include "audio/AudioOutput.h"
#include "audio/Mixer.h"

#include <doctest/doctest.h>

#include <vector>

namespace husky::tests
{
    using husky::audio::AudioOutput;
    using husky::audio::OutputChannels;
    using husky::audio::RenderCallback;

    // 出す先の様子。出す先は AudioDevice が持って壊すので、テストはこちらを見る。
    struct ManualOutputState
    {
        RenderCallback callback = nullptr;
        void* user = nullptr;
        bool started = false;
        bool stopped = false;

        // 本物の音のスレッドの代わりに呼ぶ。
        std::vector<float> Render(int frameCount)
        {
            std::vector<float> out(static_cast<size_t>(frameCount * OutputChannels), -9.0f);
            REQUIRE(callback != nullptr);
            callback(user, out.data(), frameCount);
            return out;
        }
    };

    class ManualOutput final : public AudioOutput
    {
    public:
        ManualOutput(ManualOutputState& state, bool canStart) : m_state(state), m_canStart(canStart) {}

        bool Start(RenderCallback callback, void* user) override
        {
            if (!m_canStart)
            {
                return false;
            }
            m_state.callback = callback;
            m_state.user = user;
            m_state.started = true;
            return true;
        }
        void Stop() override
        {
            m_state.stopped = true;
            m_state.callback = nullptr;
        }
        const char* GetName() const override { return "manual"; }

    private:
        ManualOutputState& m_state;
        bool m_canStart;
    };
}
