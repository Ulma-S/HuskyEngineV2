// AssetLoader.h と、TextureAsset.h、SpriteSheetAsset.h、PrefabAsset.h の実装。ScriptableData.h の箱(ScriptableDataAsset)のデストラクタも。
#include "engine/AssetLoader.h"
#include "engine/EngineLog.h"

#include "engine/JsonFormat.h"
#include "engine/ObjectSerializer.h"
#include "engine/Prefab.h"          // 入れ子のプレハブを開く(Prefab::ExpandNested。HIER)
#include "engine/SceneSerializer.h"
#include "platform/FileSystem.h"
#include "platform/Image.h"
#include "audio/WavFile.h"


#include <algorithm> // std::find
#include <utility> // std::move, std::swap

namespace husky::engine
{
    namespace
    {
        // ---- テクスチャ(ジョブの中で呼ぶ) ----

        // 1段目「元のファイルを読む」: PNG を読み、CPU のメモリ上の画像にする。
        bool ReadTextureSource(const std::string& path, platform::Image& outImage)
        {
            return platform::LoadPng(path.c_str(), outImage); // 失敗の理由は platform がログに出す
        }

        // 2段目「実行用のデータにする」: 今は素通し(受け取った画像を、そのまま返す)。
        // std::move で受け取って返すので、画像のピクセルはコピーされない(中身の持ち主が移るだけ)。
        platform::Image CookTexture(platform::Image source)
        {
            return source;
        }

        // ---- スプライトシート(ジョブの中で呼ぶ) ----

        // .spritesheet(JSON)を読み、中の画像の GUID と、切り出しとアニメーションの一覧を取り出す。読めなければ false。
        bool ReadSpriteSheet(const std::string& path, core::Guid& outTexture, std::vector<SpriteSlice>& outSlices,
                             std::vector<SpriteAnimation>& outAnimations,
                             std::vector<std::string>& outWarnings)
        {
            std::string text;
            ArchiveValue root;
            std::string error;
            if (!platform::ReadTextFile(path.c_str(), text))
            {
                return false; // 理由は platform がログに出す
            }
            if (!JsonFormat::Read(text, root, &error))
            {
                outWarnings.push_back("JSON として読めません: " + error);
                return false;
            }
            const ArchiveValue* texture = root.Find("texture");
            if (texture == nullptr || texture->GetType() != ArchiveValue::Type::String
                || !core::Guid::Parse(texture->AsString(), outTexture))
            {
                outWarnings.push_back("texture に、画像の GUID の文字列がありません");
                return false;
            }
            outSlices = ReadSpriteSlices(root, &outWarnings);
            outAnimations = ReadSpriteAnimations(root, outSlices, &outWarnings);
            return true;
        }

        // ---- プレハブと ScriptableData(ジョブの中で呼ぶ) ----

        // JSON のファイル(.prefab、.sdata)を読んで、値の木にする。形(プレハブならアクターが1つか)は、使う側が確かめる。
        bool ReadJsonFile(const std::string& path, ArchiveValue& outData, std::vector<std::string>& outWarnings)
        {
            std::string text;
            std::string error;
            if (!platform::ReadTextFile(path.c_str(), text))
            {
                return false; // 理由は platform がログに出す
            }
            if (!JsonFormat::Read(text, outData, &error))
            {
                outWarnings.push_back("JSON として読めません: " + error);
                return false;
            }
            return true;
        }

        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }
    }

    TextureAsset::~TextureAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnTextureDestroyed(*this);
        }
    }

    SoundAsset::~SoundAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnSoundDestroyed(*this);
        }
    }

    FontAsset::~FontAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnFontDestroyed(*this);
        }
    }

    FontAtlas* FontAsset::GetAtlas(float pixelHeight)
    {
        if (m_face == nullptr || pixelHeight <= 0.0f)
        {
            return nullptr;
        }
        for (const std::unique_ptr<FontAtlas>& atlas : m_atlases)
        {
            if (atlas->GetPixelHeight() == pixelHeight)
            {
                return atlas.get();
            }
        }
        m_atlases.push_back(std::make_unique<FontAtlas>(*m_face, pixelHeight));
        return m_atlases.back().get();
    }

    SpriteSheetAsset::~SpriteSheetAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnSpriteSheetDestroyed(*this);
        }
    }

    PrefabAsset::~PrefabAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnPrefabDestroyed(*this);
        }
    }

    ScriptableDataAsset::~ScriptableDataAsset()
    {
        if (m_loader != nullptr)
        {
            m_loader->OnScriptableDataDestroyed(*this);
        }
    }

    AssetLoader::AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry, core::JobSystem& jobs,
                             const core::TypeRegistry& types)
        : m_database(database)
        , m_registry(registry)
        , m_types(types)
        , m_jobs(jobs)
    {
        // registry は、ここではまだ使わない(Application では、このローダより後に作られるため。
        // 参照を覚えておくだけなら、相手が作られる前でもよい)。
    }

    AssetLoader::~AssetLoader()
    {
        // ジョブは記録(m_pending の中身)に書き込むので、記録を壊す前に、すべて終わるのを待つ。
        for (const std::unique_ptr<PendingLoad>& load : m_pending)
        {
            m_jobs.Wait(load->counter);
        }
        m_pending.clear(); // 結果は捨てる(レジストリはもう無いことがあるので、アセットには触らない)
    }

    void AssetLoader::SetRenderDevice(render::RenderDevice* device)
    {
        if (device == m_device)
        {
            return;
        }

        // 前の GPU のテクスチャを、すべて手放す(GPU が外れる、または別の GPU に替わるため)。
        if (m_device != nullptr)
        {
            // 字のアトラスは捨てる(次に使うとき、新しい GPU で作り直す。中の字も絵にし直す)。
            for (const auto& entry : m_fonts)
            {
                if (FontAsset* font = ResolveFont(entry.second))
                {
                    for (const std::unique_ptr<FontAtlas>& atlas : font->m_atlases)
                    {
                        atlas->ReleaseTexture(*m_device);
                    }
                    font->m_atlases.clear();
                }
            }
            for (const auto& entry : m_textures) // entry.first が GUID、entry.second がハンドル
            {
                if (TextureAsset* asset = ResolveTexture(entry.second))
                {
                    m_device->ReleaseTexture(asset->m_texture);
                    asset->m_texture = {};
                }
            }
        }
        m_device = device;

        // 新しい GPU のために、読み終わっていて GPU のテクスチャを持っていないアセットを、読み直す。
        // (読み込み中のものは、終わったとき(FinishTexture)に、その時点の GPU に送られるので、読み直さない)
        // StartTextureLoad はキャッシュ(m_textures)を書き換えないので、回しながら呼んでよい。
        if (m_device != nullptr)
        {
            for (const auto& entry : m_textures)
            {
                TextureAsset* asset = ResolveTexture(entry.second);
                if (asset != nullptr && asset->IsLoaded() && asset->m_texture.IsNull())
                {
                    StartTextureLoad(*asset);
                }
            }
        }
    }

    void AssetLoader::Update()
    {
        // ワーカーがいなければ、読み込みのジョブは誰も実行しないので、ここで実行する(同期実行)。
        if (m_jobs.GetWorkerCount() == 0)
        {
            m_jobs.RunPendingJobs();
        }

        // 終わった記録の結果をアセットに書き、記録を外す。
        // 外すときは、最後の記録と入れ替えてから末尾を消す(途中を消して後ろを詰めるより速い。順番は気にしない)。
        // Finish の中で新しい読み込みが始まる(m_pending に足される)ことがあるので、要素は毎回 m_pending から引く。
        for (std::size_t i = 0; i < m_pending.size();)
        {
            if (!m_pending[i]->counter.IsDone())
            {
                ++i;
                continue;
            }
            std::unique_ptr<PendingLoad> done = std::move(m_pending[i]);
            std::swap(m_pending[i], m_pending.back());
            m_pending.pop_back();
            done->Finish(*this); // 外してから書く(Finish の中で m_pending が増えても、done は動かない)
        }
        UpdateWaitingPrefabs();
    }

    bool AssetLoader::WaitsOn(const core::Guid& from, const core::Guid& target, int depth) const
    {
        if (depth > 32)
        {
            return true; // 深すぎる: 輪とみなす
        }
        for (const WaitingPrefab& waiting : m_waitingPrefabs)
        {
            const PrefabAsset* asset = ResolvePrefab(waiting.asset);
            if (asset == nullptr || asset->GetGuid() != from)
            {
                continue;
            }
            for (const core::Guid& nested : waiting.nested)
            {
                if (nested == target || WaitsOn(nested, target, depth + 1))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void AssetLoader::DropWaiting(std::size_t index)
    {
        for (const core::ObjectHandle nested : m_waitingPrefabs[index].nestedAssets)
        {
            m_registry.RemoveRoot(nested);
        }
        std::swap(m_waitingPrefabs[index], m_waitingPrefabs.back());
        m_waitingPrefabs.pop_back();
    }

    bool AssetLoader::UpdateWaitingPrefabs()
    {
        bool progressed = false;
        for (std::size_t i = 0; i < m_waitingPrefabs.size();)
        {
            PrefabAsset* asset = ResolvePrefab(m_waitingPrefabs[i].asset);
            if (asset == nullptr || asset->m_state != AssetState::Loading)
            {
                DropWaiting(i); // 回収された・読み直しで外れた
                progressed = true;
                continue;
            }
            bool ready = true;
            bool loop = false;
            for (const core::ObjectHandle nestedHandle : m_waitingPrefabs[i].nestedAssets)
            {
                const PrefabAsset* nested = ResolvePrefab(nestedHandle);
                if (nested != nullptr && nested->GetState() == AssetState::Loading)
                {
                    ready = false;
                    loop = loop || nested->GetGuid() == asset->m_guid || WaitsOn(nested->GetGuid(), asset->m_guid, 0);
                }
            }
            if (loop)
            {
                HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", asset->m_guid.ToString(),
                          "入れ子のプレハブが輪になっている(自分を中に含む)ので読めません");
                asset->m_state = AssetState::Failed;
                DropWaiting(i);
                progressed = true;
                continue;
            }
            if (!ready)
            {
                ++i;
                continue;
            }
            // 入れ子が読み終わった(読めなかったものは、ファイルにある値だけで作る): 開いた形を作る
            std::vector<std::string> warnings;
            const SceneSerializer::PrefabFinder finder = [this](const core::Guid& guid) -> const PrefabAsset* {
                const auto found = m_prefabs.find(guid);
                const PrefabAsset* nested = found != m_prefabs.end() ? ResolvePrefab(found->second) : nullptr;
                return nested != nullptr && nested->IsLoaded() ? nested : nullptr;
            };
            asset->m_data = Prefab::ExpandNested(asset->m_raw, m_registry, m_types, finder, warnings);
            for (const std::string& warning : warnings)
            {
                HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", asset->m_guid.ToString(), warning);
            }
            asset->m_state = AssetState::Loaded;
            DropWaiting(i);
            progressed = true;
        }
        return progressed;
    }

    void AssetLoader::WaitForAllLoads()
    {
        // スプライトシートが読み終わると、中の画像の読み込みが新しく始まる。増えたものも含めて、無くなるまで繰り返す。
        while (!m_pending.empty() || !m_waitingPrefabs.empty())
        {
            for (const std::unique_ptr<PendingLoad>& load : m_pending)
            {
                m_jobs.Wait(load->counter); // 待っている間、このスレッドも積まれたジョブを実行する
            }
            const bool hadPending = !m_pending.empty();
            Update();
            // 読み込み中のものが無いのに、入れ子を待っているプレハブが進まない: 待つ相手がもう来ない(念のため、読めなかったことにする)
            if (!hadPending && m_pending.empty() && !m_waitingPrefabs.empty() && !UpdateWaitingPrefabs())
            {
                while (!m_waitingPrefabs.empty())
                {
                    if (PrefabAsset* stuck = ResolvePrefab(m_waitingPrefabs.back().asset))
                    {
                        stuck->m_state = AssetState::Failed;
                    }
                    DropWaiting(m_waitingPrefabs.size() - 1);
                }
            }
        }
    }

    bool AssetLoader::CanLoadAs(const core::TypeInfo& assetType, std::string_view relativePath)
    {
        // アセットの型ごとに、読めるファイルの拡張子を決める。新しい型を足したら、ここにも足す。
        if (assetType.IsA(TextureAsset::StaticType()))
        {
            return EndsWith(relativePath, ".png"); // 1段目の ReadTextureSource が PNG を読むため
        }
        if (assetType.IsA(FontAsset::StaticType()))
        {
            return EndsWith(relativePath, ".ttf") || EndsWith(relativePath, ".otf");
        }
        if (assetType.IsA(SoundAsset::StaticType()))
        {
            return EndsWith(relativePath, ".wav"); // 読めるのは WAV だけ(audio::DecodeWav)
        }
        if (assetType.IsA(SpriteSheetAsset::StaticType()))
        {
            return EndsWith(relativePath, ".spritesheet");
        }
        if (assetType.IsA(PrefabAsset::StaticType()))
        {
            return EndsWith(relativePath, ".prefab");
        }
        // AssetRef<BouncerSettings> のアセットの型は、中身の型(ScriptableData の子)。箱の型で聞かれても同じに答える。
        // ファイルの中の "type" までは見ない(どの子の型かで絞るのは、要るようになったら)。
        if (assetType.IsA(ScriptableData::StaticType()) || assetType.IsA(ScriptableDataAsset::StaticType()))
        {
            return EndsWith(relativePath, ".sdata");
        }
        return false;
    }

    std::string AssetLoader::FindPathOrLog(const core::Guid& guid) const
    {
        std::string path = m_database.GetFullPath(guid);
        if (path.empty())
        {
            HUSKY_LOG(AssetLog::GuidNotInDatabase, guid.ToString());
        }
        return path;
    }

    template <typename Record, typename Job>
    void AssetLoader::Schedule(std::unique_ptr<Record> record, Job&& job)
    {
        Record* raw = record.get(); // ジョブには、記録の場所(ポインタ1つ)だけを渡す
        core::JobCounter& counter = raw->counter;
        m_pending.push_back(std::move(record));
        m_jobs.Schedule([raw, job] { job(*raw); }, counter);
    }

    // ================= テクスチャ =================

    core::ObjectHandle AssetLoader::LoadTexture(const core::Guid& guid)
    {
        // 1. キャッシュ: まだ生きていれば、それを返す(読み込み中でも)。破棄を予約済み(この後で消える)なら、作り直す。
        const auto found = m_textures.find(guid);
        if (found != m_textures.end())
        {
            const TextureAsset* cached = ResolveTexture(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        // 2. アセットのオブジェクトを「読み込み中」で作り、ジョブを積む。
        TextureAsset* asset = m_registry.Create<TextureAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "テクスチャ", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartTextureLoad(*asset);
        m_textures[guid] = asset->GetHandle(); // 古いもの(破棄を予約済み)があれば置き換える
        return asset->GetHandle();
    }

    TextureAsset* AssetLoader::ResolveTexture(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<TextureAsset>(handle);
    }

    TextureAsset* AssetLoader::Resolve(core::AssetRef<TextureAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        // 前に読んだアセットが、まだ生きていて、同じ GUID なら、それを使う(毎フレーム呼ばれるので、ここは速く済ませる)。
        TextureAsset* asset = ResolveTexture(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadTexture(reference.guid);
        return ResolveTexture(reference.asset);
    }

    void AssetLoader::StartTextureLoad(TextureAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid); // 表はゲームスレッドでだけ引く
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingTextureLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): 記録のパスを読み、記録の image と succeeded に書く。ほかは触らない。
        Schedule(std::move(record), [](PendingTextureLoad& load) {
            load.succeeded = ReadTextureSource(load.path, load.image);
            if (load.succeeded)
            {
                load.image = CookTexture(std::move(load.image));
            }
        });
    }

    void AssetLoader::FinishTexture(PendingTextureLoad& load)
    {
        // 書く先のアセットが、まだ生きていて、読み込み中のままか(途中で回収された、作り直された、なら捨てる)。
        TextureAsset* asset = ResolveTexture(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_width = load.image.width;
        asset->m_height = load.image.height;

        // GPU に送る(描画の準備ができていれば)
        if (m_device != nullptr)
        {
            asset->m_texture = m_device->CreateTexture(load.image);
            if (asset->m_texture.IsNull())
            {
                asset->m_state = AssetState::Failed;
                return;
            }
        }
        asset->m_state = AssetState::Loaded;
    }

    void AssetLoader::OnTextureDestroyed(TextureAsset& asset)
    {
        if (m_device != nullptr && !asset.m_texture.IsNull())
        {
            m_device->ReleaseTexture(asset.m_texture); // GPU のメモリは、描画層が数フレーム遅らせて解放する
        }
        // キャッシュが、このアセットを指しているときだけ外す(同じ GUID で、もう新しいものを作っていることがある)。
        // 読み込み中の記録は、ここでは消さない(ジョブがまだ書き込んでいるかもしれない)。終わったときに捨てる。
        const auto found = m_textures.find(asset.m_guid);
        if (found != m_textures.end() && found->second == asset.GetHandle())
        {
            m_textures.erase(found);
        }
    }

    // ================= 音 =================

    core::ObjectHandle AssetLoader::LoadSound(const core::Guid& guid)
    {
        // 流れはテクスチャと同じ: キャッシュにあれば返し、無ければ「読み込み中」で作ってジョブを積む。
        const auto found = m_sounds.find(guid);
        if (found != m_sounds.end())
        {
            const SoundAsset* cached = ResolveSound(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        SoundAsset* asset = m_registry.Create<SoundAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "音", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartSoundLoad(*asset);
        m_sounds[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    SoundAsset* AssetLoader::ResolveSound(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<SoundAsset>(handle);
    }

    SoundAsset* AssetLoader::Resolve(core::AssetRef<SoundAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        SoundAsset* asset = ResolveSound(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadSound(reference.guid);
        return ResolveSound(reference.asset);
    }

    void AssetLoader::StartSoundLoad(SoundAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingSoundLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読み(1段目)、ミキサーの形に直す(2段目)。結果は記録にだけ書く。
        Schedule(std::move(record), [](PendingSoundLoad& load) {
            std::string bytes; // ReadTextFile は中身をそのままのバイトで読む(文字として直さない)
            if (!platform::ReadTextFile(load.path.c_str(), bytes))
            {
                load.error = "could not read the file";
                return;
            }
            audio::DecodedSound decoded;
            if (!audio::DecodeWav(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), decoded, load.error))
            {
                return;
            }
            load.samples = audio::ConvertToOutputFormat(decoded);
            if (load.samples.empty())
            {
                load.error = "the sound is empty";
                return;
            }
            load.succeeded = true;
        });
    }

    void AssetLoader::FinishSound(PendingSoundLoad& load)
    {
        SoundAsset* asset = ResolveSound(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "音", load.path, load.error != nullptr ? load.error : "unknown error");
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_frameCount = static_cast<int>(load.samples.size()) / audio::OutputChannels;
        // 音の層に波を預ける(付いていなければ、長さだけを覚えて「読めた」にする)。
        if (m_audio != nullptr)
        {
            asset->m_sound = m_audio->CreateSound(std::move(load.samples));
        }
        asset->m_state = AssetState::Loaded;
    }

    void AssetLoader::OnSoundDestroyed(SoundAsset& asset)
    {
        if (m_audio != nullptr && !asset.m_sound.IsNull())
        {
            m_audio->ReleaseSound(asset.m_sound); // 鳴っていれば止め、音のスレッドが手放してから捨てる
        }
        const auto found = m_sounds.find(asset.m_guid);
        if (found != m_sounds.end() && found->second == asset.GetHandle())
        {
            m_sounds.erase(found);
        }
    }

    // ================= フォント(UI-3b) =================

    core::ObjectHandle AssetLoader::LoadFont(const core::Guid& guid)
    {
        // 流れは音と同じ: キャッシュにあれば返し、無ければ「読み込み中」で作ってジョブを積む。
        const auto found = m_fonts.find(guid);
        if (found != m_fonts.end())
        {
            const FontAsset* cached = ResolveFont(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        FontAsset* asset = m_registry.Create<FontAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "フォント", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartFontLoad(*asset);
        m_fonts[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    FontAsset* AssetLoader::ResolveFont(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<FontAsset>(handle);
    }

    FontAsset* AssetLoader::Resolve(core::AssetRef<FontAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        FontAsset* asset = ResolveFont(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadFont(reference.guid);
        return ResolveFont(reference.asset);
    }

    void AssetLoader::StartFontLoad(FontAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingFontLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読み、フォントとして開く。結果は記録にだけ書く。
        Schedule(std::move(record), [](PendingFontLoad& load) {
            std::string bytes; // ReadTextFile は中身をそのままのバイトで読む
            if (!platform::ReadTextFile(load.path.c_str(), bytes))
            {
                return;
            }
            load.face = CreateFontFace(std::move(bytes));
            load.succeeded = load.face != nullptr;
        });
    }

    void AssetLoader::FinishFont(PendingFontLoad& load)
    {
        FontAsset* asset = ResolveFont(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "フォント", load.path, "could not read the file as a font");
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_face = std::move(load.face);
        asset->m_state = AssetState::Loaded;
    }

    void AssetLoader::OnFontDestroyed(FontAsset& asset)
    {
        if (m_device != nullptr)
        {
            for (const std::unique_ptr<FontAtlas>& atlas : asset.m_atlases)
            {
                atlas->ReleaseTexture(*m_device);
            }
        }
        const auto found = m_fonts.find(asset.m_guid);
        if (found != m_fonts.end() && found->second == asset.GetHandle())
        {
            m_fonts.erase(found);
        }
    }

    // ================= スプライトシート =================

    core::ObjectHandle AssetLoader::LoadSpriteSheet(const core::Guid& guid)
    {
        const auto found = m_spriteSheets.find(guid);
        if (found != m_spriteSheets.end())
        {
            const SpriteSheetAsset* cached = ResolveSpriteSheet(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        SpriteSheetAsset* asset = m_registry.Create<SpriteSheetAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "スプライトシート", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartSpriteSheetLoad(*asset);
        m_spriteSheets[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    SpriteSheetAsset* AssetLoader::ResolveSpriteSheet(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<SpriteSheetAsset>(handle);
    }

    SpriteSheetAsset* AssetLoader::Resolve(core::AssetRef<SpriteSheetAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        SpriteSheetAsset* asset = ResolveSpriteSheet(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadSpriteSheet(reference.guid);
        return ResolveSpriteSheet(reference.asset);
    }

    void AssetLoader::StartSpriteSheetLoad(SpriteSheetAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingSpriteSheetLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読み、記録の texture、slices、animations、warnings、succeeded に書く。
        Schedule(std::move(record), [](PendingSpriteSheetLoad& load) {
            load.succeeded = ReadSpriteSheet(load.path, load.texture, load.slices, load.animations, load.warnings);
        });
    }

    void AssetLoader::FinishSpriteSheet(PendingSpriteSheetLoad& load)
    {
        for (const std::string& warning : load.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "スプライトシート", load.path, warning);
        }
        SpriteSheetAsset* asset = ResolveSpriteSheet(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_slices = std::move(load.slices);
        asset->m_animations = std::move(load.animations);
        asset->texture.guid = load.texture;
        asset->texture.asset = {};
        asset->m_state = AssetState::Loaded;

        // 依存の読み込み: 中で指している画像の読み込みを、すぐに始める(描くときまで待たない)。
        // Resolve は AssetRef に、読み始めたテクスチャのハンドルを書く。AssetRef はプロパティなので、
        // このスプライトシートが生きている間、テクスチャも GC に回収されない。
        Resolve(asset->texture);
    }

    void AssetLoader::OnSpriteSheetDestroyed(SpriteSheetAsset& asset)
    {
        const auto found = m_spriteSheets.find(asset.m_guid);
        if (found != m_spriteSheets.end() && found->second == asset.GetHandle())
        {
            m_spriteSheets.erase(found);
        }
    }

    // ================= プレハブ =================
    // 流れはスプライトシートと同じ(キャッシュを見る → 無ければ作って読み始める → ジョブ → Finish で値を移す)。

    core::ObjectHandle AssetLoader::LoadPrefab(const core::Guid& guid)
    {
        const auto found = m_prefabs.find(guid);
        if (found != m_prefabs.end())
        {
            const PrefabAsset* cached = ResolvePrefab(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        PrefabAsset* asset = m_registry.Create<PrefabAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "プレハブ", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartPrefabLoad(*asset);
        m_prefabs[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    PrefabAsset* AssetLoader::ResolvePrefab(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<PrefabAsset>(handle);
    }

    PrefabAsset* AssetLoader::Resolve(core::AssetRef<PrefabAsset>& reference)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        PrefabAsset* asset = ResolvePrefab(reference.asset);
        if (asset != nullptr && asset->GetGuid() == reference.guid)
        {
            return asset;
        }
        reference.asset = LoadPrefab(reference.guid);
        return ResolvePrefab(reference.asset);
    }

    void AssetLoader::StartPrefabLoad(PrefabAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingPrefabLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読み、記録の data、warnings、succeeded に書く。
        // 値の木はただのデータ(GC 管理のオブジェクトではない)なので、ジョブで作ってよい。
        Schedule(std::move(record), [](PendingPrefabLoad& load) {
            load.succeeded = ReadJsonFile(load.path, load.data, load.warnings);
        });
    }

    void AssetLoader::FinishPrefab(PendingPrefabLoad& load)
    {
        for (const std::string& warning : load.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "プレハブ", load.path, warning);
        }
        PrefabAsset* asset = ResolvePrefab(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }
        asset->m_raw = std::move(load.data); // 値の木を、コピーせずにアセットへ移す
        // 入れ子のプレハブ(別のプレハブから置いたもの)が無ければ、そのまま使える
        const std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(asset->m_raw);
        if (nested.empty())
        {
            asset->m_data = asset->m_raw;
            asset->m_state = AssetState::Loaded;
            return;
        }
        // 入れ子があれば、先にそれを読む(読み終わってから開いた形を作る。UpdateWaitingPrefabs)
        WaitingPrefab waiting;
        waiting.asset = asset->GetHandle();
        waiting.nested = nested;
        for (const core::Guid& guid : nested)
        {
            const core::ObjectHandle handle = LoadPrefab(guid);
            m_registry.AddRoot(handle); // 待っている間に回収されないように
            waiting.nestedAssets.push_back(handle);
        }
        m_waitingPrefabs.push_back(std::move(waiting));
    }

    void AssetLoader::OnPrefabDestroyed(PrefabAsset& asset)
    {
        const auto found = m_prefabs.find(asset.m_guid);
        if (found != m_prefabs.end() && found->second == asset.GetHandle())
        {
            m_prefabs.erase(found);
        }
    }

    std::size_t AssetLoader::ReloadPrefabsNesting(const core::Guid& guid)
    {
        // 先に集めてから読み直す(読み直すと m_prefabs が変わるため)
        std::vector<core::Guid> outers;
        for (const auto& [cachedGuid, handle] : m_prefabs)
        {
            const PrefabAsset* cached = ResolvePrefab(handle);
            if (cached == nullptr || cachedGuid == guid)
            {
                continue;
            }
            const std::vector<core::Guid> nested = Prefab::FindNestedPrefabs(cached->GetRawData());
            if (std::find(nested.begin(), nested.end(), guid) != nested.end())
            {
                outers.push_back(cachedGuid);
            }
        }
        std::size_t count = 0;
        for (const core::Guid& outer : outers)
        {
            ReloadPrefab(outer);
            count += 1 + ReloadPrefabsNesting(outer); // その外側も(輪は読み込みで断るので、ここでは深さを気にしない)
        }
        return count;
    }

    core::ObjectHandle AssetLoader::ReloadPrefab(const core::Guid& guid)
    {
        m_prefabs.erase(guid); // キャッシュから外すだけ(古いアセットの後始末は OnPrefabDestroyed。外した後は何もしない)
        return LoadPrefab(guid);
    }

    // ================= ScriptableData =================
    // 流れはプレハブと同じ。違うのは Finish で、値の木の "type" の名前から中身を作り、Property 越しに値を入れること。

    core::ObjectHandle AssetLoader::LoadScriptableData(const core::Guid& guid)
    {
        const auto found = m_scriptableData.find(guid);
        if (found != m_scriptableData.end())
        {
            const ScriptableDataAsset* cached = ResolveScriptableDataAsset(found->second);
            if (cached != nullptr && !cached->IsPendingDestroy())
            {
                return found->second;
            }
        }

        ScriptableDataAsset* asset = m_registry.Create<ScriptableDataAsset>();
        if (asset == nullptr)
        {
            HUSKY_LOG(AssetLog::OutOfObjectMemory, "ScriptableData", guid.ToString());
            return {};
        }
        asset->m_guid = guid;
        asset->m_loader = this;
        StartScriptableDataLoad(*asset);
        m_scriptableData[guid] = asset->GetHandle();
        return asset->GetHandle();
    }

    ScriptableDataAsset* AssetLoader::ResolveScriptableDataAsset(core::ObjectHandle handle) const
    {
        return m_registry.Resolve<ScriptableDataAsset>(handle);
    }

    const ScriptableData* AssetLoader::GetScriptableData(const ScriptableDataAsset& asset) const
    {
        if (!asset.IsLoaded())
        {
            return nullptr;
        }
        return m_registry.Resolve<ScriptableData>(asset.m_data);
    }

    const ScriptableData* AssetLoader::ResolveScriptableData(core::AssetReference& reference, const core::TypeInfo& expected)
    {
        if (reference.IsNull())
        {
            reference.asset = {};
            return nullptr;
        }
        ScriptableDataAsset* asset = ResolveScriptableDataAsset(reference.asset);
        if (asset == nullptr || asset->GetGuid() != reference.guid)
        {
            reference.asset = LoadScriptableData(reference.guid);
            asset = ResolveScriptableDataAsset(reference.asset);
            if (asset == nullptr)
            {
                return nullptr;
            }
        }
        else if (asset->m_superseded)
        {
            // ファイルが書き直された(ReloadScriptableData)。新しい箱を引き(誰も指していなくて回収されていたら、読み直す)、
            // 読み終わってから乗り換える。読み込み中は古い中身を返すので、使う側から見て途中で消えない。
            const core::ObjectHandle newer = LoadScriptableData(reference.guid);
            const ScriptableDataAsset* newerAsset = ResolveScriptableDataAsset(newer);
            if (newerAsset != nullptr && newerAsset->GetState() != AssetState::Loading)
            {
                reference.asset = newer;
                asset = ResolveScriptableDataAsset(newer);
            }
        }

        const ScriptableData* data = GetScriptableData(*asset);
        if (data == nullptr)
        {
            return nullptr; // 読み込み中か、失敗
        }
        if (!data->GetType().IsA(expected))
        {
            if (asset->m_warnedType != &expected)
            {
                HUSKY_LOG(AssetLog::ScriptableDataWrongType, reference.guid.ToString(), data->GetType().GetName(), expected.GetName());
                asset->m_warnedType = &expected;
            }
            return nullptr;
        }
        return data;
    }

    void AssetLoader::StartScriptableDataLoad(ScriptableDataAsset& asset)
    {
        asset.m_state = AssetState::Loading;
        std::string path = FindPathOrLog(asset.m_guid);
        if (path.empty())
        {
            asset.m_state = AssetState::Failed;
            return;
        }

        auto record = std::make_unique<PendingScriptableDataLoad>();
        record->asset = asset.GetHandle();
        record->guid = asset.m_guid;
        record->path = std::move(path);
        // ジョブ(ワーカースレッド): ファイルを読んで値の木にするだけ。中身のオブジェクト(GC 管理)は作らない。
        // ファイルを読んで JSON にする手順はプレハブと同じ(ReadJsonFile)。
        Schedule(std::move(record), [](PendingScriptableDataLoad& load) {
            load.succeeded = ReadJsonFile(load.path, load.data, load.warnings);
        });
    }

    void AssetLoader::FinishScriptableData(PendingScriptableDataLoad& load)
    {
        for (const std::string& warning : load.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "ScriptableData", load.path, warning);
        }
        ScriptableDataAsset* asset = ResolveScriptableDataAsset(load.asset);
        if (asset == nullptr || asset->m_guid != load.guid || asset->m_state != AssetState::Loading)
        {
            return;
        }
        if (!load.succeeded)
        {
            asset->m_state = AssetState::Failed;
            return;
        }

        // 1. 型の名前を読む
        const ArchiveValue* typeName = load.data.Find("type");
        if (typeName == nullptr || typeName->GetType() != ArchiveValue::Type::String)
        {
            HUSKY_LOG(AssetLog::ScriptableDataNoType, load.path);
            asset->m_state = AssetState::Failed;
            return;
        }

        // 2. 名前で登録簿から型を探し、ScriptableData の子かを確かめてから作る
        //    (作ってから確かめると、違う型のオブジェクトを作って捨てることになるので、先に型情報で確かめる)
        const core::TypeInfo* type = m_types.Find(typeName->AsString());
        if (type == nullptr || !type->IsA(ScriptableData::StaticType()))
        {
            if (type == nullptr)
                HUSKY_LOG(AssetLog::ScriptableDataTypeNotRegistered, typeName->AsString(), load.path);
            else
                HUSKY_LOG(AssetLog::ScriptableDataTypeNotData, typeName->AsString(), load.path);
            asset->m_state = AssetState::Failed;
            return;
        }
        core::Object* created = m_types.Create(typeName->AsString(), m_registry);
        ScriptableData* data = created != nullptr && created->IsA<ScriptableData>() ? static_cast<ScriptableData*>(created) : nullptr;
        if (data == nullptr)
        {
            HUSKY_LOG(AssetLog::ScriptableDataCreateFailed, load.path);
            if (created != nullptr)
            {
                created->Destroy();
            }
            asset->m_state = AssetState::Failed;
            return;
        }

        // 3. 値を Property 越しに入れる(知らない名前や型の違いは飛ばして警告。途中で止めない)
        const ObjectSerializer::ReadReport report = ObjectSerializer::ReadProperties(load.data, *data);
        for (const std::string& warning : report.warnings)
        {
            HUSKY_LOG(AssetLog::LoadWarning, "ScriptableData", load.path, warning);
        }
        data->m_assetGuid = asset->m_guid;
        data->MarkReadOnly(); // ここから先は読むだけ(書く入口が断る。SD-2)
#if HUSKY_DEVELOPMENT
        // 生のポインタでの書き込みは印では止められないので、読み込んだときの値を控え、後で比べる(CheckScriptableDataUnchanged)。
        asset->m_snapshot = ObjectSerializer::Write(*data);
#endif
        asset->m_data = data->GetHandle(); // 箱のプロパティ(強い参照)で持つので、箱が生きている間は回収されない
        asset->m_state = AssetState::Loaded;
    }

    core::ObjectHandle AssetLoader::ReloadScriptableData(const core::Guid& guid)
    {
        // 古い箱には印を付け、キャッシュから外す(古い箱と中身は、指している AssetRef が乗り換えるまで残る)。
        const auto found = m_scriptableData.find(guid);
        if (found != m_scriptableData.end())
        {
            if (ScriptableDataAsset* old = ResolveScriptableDataAsset(found->second))
            {
                old->m_superseded = true;
            }
            m_scriptableData.erase(found);
        }
        return LoadScriptableData(guid);
    }

    std::size_t AssetLoader::CheckScriptableDataUnchanged()
    {
        std::size_t changedCount = 0;
#if HUSKY_DEVELOPMENT
        for (const auto& [guid, handle] : m_scriptableData)
        {
            ScriptableDataAsset* asset = ResolveScriptableDataAsset(handle);
            const ScriptableData* data = asset != nullptr ? GetScriptableData(*asset) : nullptr;
            if (data == nullptr)
            {
                continue;
            }
            // 先に、値の木を作らずに比べる(変わっていなければ確保しない。毎秒呼ばれるため)
            if (ObjectSerializer::Matches(*data, asset->m_snapshot))
            {
                continue;
            }
            const ArchiveValue current = ObjectSerializer::Write(*data);
            // どのプロパティが変わったかを、名前で出す(構造体は、中のどこが変わったかまでは見ない)。
            const ArchiveValue* now = current.Find("properties");
            const ArchiveValue* before = asset->m_snapshot.Find("properties");
            if (now != nullptr && before != nullptr)
            {
                for (std::size_t i = 0; i < now->GetMemberCount(); ++i)
                {
                    const ArchiveValue* old = before->Find(now->GetMemberName(i));
                    if (old == nullptr || !old->Equals(now->GetMemberValue(i)))
                    {
                        HUSKY_LOG(AssetLog::ReadOnlyDataModified, m_database.GetFullPath(guid), data->GetType().GetName(), guid.ToString(), now->GetMemberName(i));
                        ++changedCount;
                    }
                }
            }
            // 同じ書き換えを毎回出さないように、控えを今の値にする(次からは、新しい書き換えだけを出す)。
            asset->m_snapshot = current;
        }
#endif
        return changedCount;
    }

    void AssetLoader::OnScriptableDataDestroyed(ScriptableDataAsset& asset)
    {
        const auto found = m_scriptableData.find(asset.m_guid);
        if (found != m_scriptableData.end() && found->second == asset.GetHandle())
        {
            m_scriptableData.erase(found);
        }
    }
}
