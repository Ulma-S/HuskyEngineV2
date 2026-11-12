# CLAUDE.md(叩き台)

個人の学習・趣味用C++ゲームエンジン(HuskyEngineV2)。Windows先行、将来Mac。作り直しを前提に、最小構成から作る。
作業フォルダ: `D:\Develop\HuskyEngineV2`。開発環境: Windows x64、Visual Studio 2022。
詳細な設計の根拠は「ゲームエンジン設計メモ(仮)」(`spec/設計メモ.md`)を参照する。

## 学習方針(最優先)

このプロジェクトは勉強のために作る。速く作ることより、各処理が何をしているかを理解することを優先する。

1. 実装は、30分〜1時間で終わる小さなステップに分ける。
2. 各ステップの前に、「何をするか」と「なぜそうするか」を短く説明する。
3. 実装後に、理解できたかをユーザーに確認する。確認が取れるまで、次のステップへ進まない。
4. ユーザーが「理解をスキップする」と言った間は、確認を省いて先へ進んでよい。スキップした範囲は`docs/skipped.md`(仮)に記録し、後で振り返れるようにする。
5. コードのコメントとコミットメッセージは、何をしているかが分かる言葉で書く。

ユーザーはゲームプログラマーで、C#に慣れ、C++には不慣れ。ゲームエンジンの一般的な概念は知っているため、C++固有の事柄(所有権、テンプレート、マクロ、ビルド)は特に丁寧に説明する。

## トークン(コンテキスト)の節約

1つのセッションでコンテキストがすぐ埋まったため(2026-11-12。主な原因は全画面のスクリーンショットの多用、ファイルの丸ごとの読み書き、長いログ)、次を守る。

- スクリーンショットは、ログやテストで確かめられないときだけ撮る。撮るときは縮小するか、要る部分だけを切り出す。ボタンなどの位置は一度調べたらメモに残し、位置を探すためだけに撮らない。
- ファイルは要る範囲だけを読む(行の範囲を指定する、Grepで場所を探してから読む)。直すときは部分的な書き換え(Edit)にし、ファイルの丸ごとの書き直しや読み直しを避ける。
- ビルド、テスト、ゲームのログは、要る行だけを取り出す(警告・エラー・件数・成否)。全文を出さない。
- 説明は要点に絞る。表や候補の一覧は、ユーザーが選ぶ必要があるときだけにする。
- 2〜4ステップごとにセッションを区切ることを提案する。続きは、メモリの進み具合のメモから再開する。

## 技術スタック

- 言語: C++17(MSVC、Visual Studio 2022)。例外とRTTIは使わない(ビルド設定でも無効にする)。
- ウィンドウと入力: SDL3。グラフィックス: SDL_GPU。
- ビルド: CMake。依存はvcpkgかFetchContent。パスはWindows固有にしない。
- 数学: glm(1.0.1、FetchContent。2026-11-12にユーザーが了承し、CAM-1で入れた)。計算は glm で行い、自前では作らない。ただし、データとして持つ・保存する型(アクターの位置など)は glm に依存させず、自前の型(`core::Vector3`。値だけで計算の機能は持たない)にする(2026-11-13にユーザーと決定)。計算するときは`engine/GlmConversion.h`の`ToGlm` / `FromGlm`で変える。行列(`glm::mat4`、描画層が受け取る)は今は glm のまま。使うターゲットは`glm`ではなく`husky_glm`(設定をそろえる入れ物。奥行きを0〜1にする`GLM_FORCE_DEPTH_ZERO_TO_ONE`)をリンクする。生成のときにglm自身のCMakeの「Deprecation Warning」が出るが、glm側の問題で無視してよい。行列は最初から4×4(`glm::mat4`)で、3Dを見越して作る。
- 座標系(2026-11-12に決定。詳しくは設計メモ「座標系とカメラ」): yは上が+、ワールドの単位(最初は1単位 = 1ピクセル、換算はカメラ)、位置はx・y・z。カメラはビュー行列と射影行列(2Dは正射影)。親子の階層は今は作らない。
- JSON: yyjson(0.12.0、FetchContent)。使うのは`engine/JsonFormat.cpp`の中だけ(PRIVATEでリンク)。替えるときはそのファイルだけを書き直す。
- 開発用UI: Dear ImGui(v1.92.9b、FetchContent。開発ビルドのみ)。CMakeの`husky_imgui`で、本体とSDL3/SDL_GPUのバックエンドをまとめる。リンクはジェネレータ式でDebugだけ(`$<$<CONFIG:Debug>:husky_imgui>`)。ImGuiを直接使ってよいのは、render(つなぎ込み: `RenderDevice::BeginDevUiFrame`)とtools(画面)だけ。platformはImGuiを知らず、`SetNativeEventHook`でイベントを渡すだけ。ImGuiのメモリも`MemoryTracker`に通す。画面の文字は、日本語のフォントを入れるまで英語で書く。ASanのビルドでは`husky_imgui`もASan付きでコンパイルする(MSVCのSTLの注釈`annotate_string`をそろえるため)。
  - インスペクタ(`tools/Inspector`)は、リフレクション(`TypeInfo`と`Property`)から自動で画面を作る。新しい型やプロパティを足しても、画面のコードは要らない。
    - 型だけでは分からない見せ方(整数のIDを名前の一覧から選ばせるなど)が要るプロパティは、`tools::RegisterPropertyDrawer`で表示を差し替える(例: `SpriteComponent::sprite`は、スプライトシートの切り出しの名前から選ぶ)。
- 依存ライブラリを追加するときは、先にユーザーへ相談する。

## モジュール構成と依存方向

エンジンの部品(core / platform / render / engine / tools)と、エンジンを使う側のアプリケーション(game)に分け、依存は一方向にする(2026-11-12、ユーザーの指摘で game を「モジュール」から「アプリケーション」に改めた)。

- core: 型、メモリ、リフレクション、GC、世代付きハンドル、ジョブ。他のモジュールを一切参照しない。
- engine: シーン、アクター、コンポーネント、アセット、シリアライズ、入力、描画抽象。core、platform、render(描画層)に依存する。
- render(描画層): SDL_GPUで描く。`RenderDevice`と独自の型(`Color`、`Sprite`など)だけを公開する。platformに依存する。engineを参照しない(依存の向きは engine → render)。
- platform: ウィンドウ、ファイル、スレッドなどOS依存のコード。SDL3はここだけが触る。coreに依存する。
- tools: ImGuiのデバッグUIとインスペクタ、エディタの機能(シーンの保存など)。engineに依存する。リリースビルドには含めない。最初はゲームと同じプロセスに組み込み、いずれ別プロセスとプロセス間通信も検討する(設計メモ「UIとツール」)。シーンのファイルを保存するのはtools(開発ビルド)だけで、製品のゲームは読み込むだけ。
  - 開発ビルドの印: CMakeがDebugの構成(ASanも)にだけ`HUSKY_DEVELOPMENT=1`を付ける。エディタの機能は`#if HUSKY_DEVELOPMENT`で囲み、製品(Release)からコードごと外す。今は、開発ビルドの`F5`で今のシーンを`assets/scenes/main.json`(ソースの assets)に保存する(コマンド`scene.save`。中身は`Application::SaveSceneFile`)。乱数で大量に並べたこのファイルは`.gitignore`で管理しない。
  - エディタの操作は、ImGuiの中に直接書かず、関数(将来の「エディタのコマンド」)として外に置く。ImGui、コマンドラインツール、MCP(AIから使う)が同じ操作を呼べるようにするため(設計メモ「AIから使いやすいエディタ」)。
  - エディタのコマンドは`tools/EditorCommands.h`(`EditorCommand`と`EditorCommandRegistry`)。1つのコマンドは、機械向けの名前(`"scene.save"`のように「対象.動作」。一度決めたら変えない)と、人向けの表示名・説明・ショートカットを両方持つ。引数は`ArchiveValue`(JSONと同じ形)、結果は人が読む1行(`message`)と細かい内容(`details`)。ボタンもショートカット(`HandleShortcuts`)も、名前で`Execute`を呼ぶだけ。新しいエディタの操作は、まずコマンドとして作る。人も使うので、説明と結果の表示(成功か失敗かと理由)を必ず付ける。今あるのは`scene.save`(F5)、`actor.spawn`、`actor.list`、`property.set`(引数あり。構造体の中は`transform.position.x`のように点でつないだ道筋で指す。`component`でコンポーネントのプロパティも指せる)、`editor.undo` / `editor.redo`と`asset.refresh`(`.meta`を作り、スプライトシートを整え、アセットの表を作り直す。起動時もこれを呼ぶ)。毎フレーム動く画面とショートカットのコードも、温まった後はメモリを確保しない(使い回す入れ物を使う。MSVCのDebugのSTLは空の`std::vector`でも確保する)。
  - Undo はコマンドの単位(ED-8a、2026-11-13): 元に戻せるコマンドは、成功したときに結果へ`undo`と`redo`(どちらも「コマンドの名前 + 引数」、`EditorCommandCall`。アクターはguidで指す)を書く。`EditorCommandRegistry::Execute`がそれを履歴に積み(上限100件)、`editor.undo` / `editor.redo`が実行する。戻すコマンドが今は使えなければ履歴は残し、実行に失敗したらその1件を捨てる。新しいコマンドで戻せるものを作るときは、`undo`と`redo`を書く。
  - 編集モード(`tools/EditorPlayMode.h`。2026-11-12): 開発ビルドは編集の状態(EDIT)で起動し、シーンの`Tick`を止める(`Application::SetSceneTickEnabled`。描画、アセット、GC、開発用のUIは動く)。`editor.play`でシーンをメモリの`ArchiveValue`に控えて再生し、`editor.stop`で控えから読み直して戻す。`editor.pause` / `editor.step`(コマ送り。`RequestSceneStep`)もある。シーンの保存は編集中だけ(`whyUnavailable`)。製品とテストは、ずっと再生のまま。コマンドの`whyUnavailable`(今は使えない理由。文字列の定数を返す)があると、画面ではボタンが灰色になり、理由はマウスを乗せると出る。
  - コマンドの引数(`EditorCommandArg`。2026-11-12): 名前、種類(`bool` / `int` / `float` / `string`。JSONで書ける値だけ)、説明、省略できるか。`Execute`が実行の前に形で確かめ(足りない、種類が違う、知らない名前は、実行せずに理由つきで失敗)、コマンドの中では確かめ直さない。`--list`、ツールチップ、AIへの説明にも同じ形を出す。既定値(`defaultValue`)と選べる値(`choices`。アプリの中身で決まるので関数)も持てる。画面(`tools::DebugWindow`。クラスで、入力した値を覚える)では、引数のあるコマンドのボタンは「…」付きで、押すと引数の形から自動で作った入力欄の小窓が開き、Runで実行する(新しいコマンドを足しても画面のコードは要らない)。
  - アクターを指す引数(2026-11-12): 名前かguidの文字列を受け付ける(`tools::FindActorFromArg`。guidの形ならguidで、ほかは名前で探す)。名前が見つからない・複数のアクターに当てはまるときは、黙って選ばず、候補をguid・型・位置つきで並べて断る。選択肢(`EditorArgChoice`)は値(guid)と表示("Player (PlayerActor)")を分け、人は表示から選ぶ(GUIDを意識しない)。選択肢の関数は、ほかの引数も受け取る(選んだアクターのプロパティの一覧など)。画面は選択肢を30フレームごとか、ほかの引数が変わったときだけ作り直す。種類を問わない引数(`Any`。`property.set`の`value`)は、画面では打った文字を数やboolとして読めればその値、読めなければ文字列で渡す。結果の`data`は機械向け(CLIの`--json-out`の`"data"`)。シーンを読むだけのコマンドは`readsScene`(CLIは`--scene`を読み、保存しない)。
  - エディタのCLI(`HuskyEditorCli.exe`。中身は`tools/EditorCli.h`の`RunEditorCli`。2026-11-12): 画面を開かずにコマンドを名前で1回だけ実行する(Unityの`-batchmode`、UEのコマンドレットにあたる)。`--list`、`<コマンド> [引数のJSON]`、`--assets <フォルダ>`(既定はソースの`assets`)、`--json-out <ファイル>`(結果のJSON。エンジンのログも標準出力に出るので、今はファイルに書く)。終了コードは0 = 成功、1 = コマンドの失敗、2 = 使い方の誤り。CLIに登録するのは、動いているゲームが要らないコマンドだけ(今は`asset.refresh`と`actor.spawn`)。シーンを書き換えるコマンド(`modifiesScene`)は`--scene <ファイル>`を読んでから実行し、成功したら保存する(無ければ空のシーンから作る)。CLIの`main`はゲームの型を登録する関数を`RunEditorCli`に渡す。開発ビルドだけで使える。
- game(アプリケーション): このゲームのコード。エンジンの部品のように付け外しするものではない。engineに依存し、エンジンはgameを知らない。中は2つに分ける: ゲームのコード(`src/game/`、静的ライブラリ`husky_game`。型、ロジック、`game::RegisterGameTypes`、`game::SpawnInitialActors`)と、起動するexe(`src/main.cpp`。ウィンドウを開いて動かすだけ)。エディタ(`HuskyEditorCli`など)もゲームのコードをリンクし、ゲームの型を登録する(シーンの`"type"`から作れるように。toolsはゲームを知らないので、exeから関数で渡す)。UnityやUEと同じ形で、将来のホットリロードではゲームのコードがDLLになる。

## オブジェクトモデル

- ゲーム上の意味を持つものは、GC管理のアクター(薄い入れ物)とし、機能はコンポーネントで足す。継承は1〜2階層まで。
- ゲームオブジェクトは必ずGC管理型の基底クラスから派生させる。
- 破棄は`Destroy()`で印を付け、フレーム末にまとめて確定する。
- 設計メモの「ワールド」は、このプロジェクトでは「シーン」(`engine::Scene`)と呼ぶ。シーンはアクターの一覧(`actors`)を持ち、GCの根になる。アクターは`Scene::Spawn<T>()`で作る。
- フレーム末の順番: `Scene::RemoveDestroyedActors()` → `ObjectRegistry::FlushDestroyed()` → (1秒に1回)`ObjectRegistry::CollectGarbage()`。
- `Object`の子クラスに、`Object`のメンバーと同じ名前のメンバーを作らない(名前が隠れて、レジストリの設定が壊れる)。
- アクターは変わらないID(`Actor::guid`、`core::Guid`。2026-11-12)を持つ。シーンに入れたとき(`Spawn`、`AddLoadedActor`)に無ければ振られ、保存しても、読み戻しても、Play / Stopで作り直しても変わらない。名前(`name`)は重なってよく、区別にはguidを使う。読み込みでguidがシーンの中(Destroy済みを除く)で重なったら、後のほうに振り直して警告を出す。探すのは`Scene::FindActorByGuid`。人はGUIDを意識しなくてよいようにする(インスペクタでは灰色で読むだけ。コマンドは名前でも指せるようにする)。
- アクターの位置・回転・拡大縮小は`Actor::transform`(`engine::Transform`。2026-11-13にユーザーと決定)。付け外しできるコンポーネントではなく、どのアクターも必ず持つ構造体のメンバー(UnityのTransformと同じく外せない)。中身は`position`(`core::Vector3`)、`rotation`(z軸まわりの角度、度、反時計回りが+)、`scale`(`core::Vector3`)。回転と拡大縮小は今は保存するだけで、描画にはまだ効かない。親子の階層は無い。古い形(`"x"`, `"y"`)のシーンのファイルは、知らないプロパティとして警告が出て、位置は原点になる。
- `Pool<T>`とECSは今は作らない(保留)。

## メモリとGC

- GCは停止型のマーク&スイープ。フレーム末に破棄を確定してから、必要なときだけ回す。
- 根の登録口は`ObjectRegistry::AddRoot` / `ObjectRegistry::RemoveRoot`。プロパティとして登録した`ObjectHandle`は強い参照(GCがたどる)、登録していない`ObjectHandle`は弱い参照。
- 世代付きハンドルを最初から用意する。弱参照と破棄済み検出に使う。
- 生の`new`/`delete`と`malloc`/`free`は禁止。スマートポインタ(`std::make_unique`など)、プール、ハンドルのみを使う。確保の方針とアロケータの使い分けは、設計メモの「メモリの確保の方針」を参照する。
  - 例外: placement new(確保済みのメモリの上でコンストラクタを呼ぶ)と、デストラクタの明示的な呼び出しは、`FreeListAllocator::New` / `FreeListAllocator::Delete`の中だけで使ってよい。アロケータでオブジェクトを作るときは、必ずこの2つを使う。
  - `Delete`は、オブジェクトのアドレスが`New`で借りたメモリの先頭と一致することを前提にしている。基底クラスのポインタで渡すのは、単一継承の場合だけにする。
  - 例外: `malloc`/`free`は、`core/MemoryTracker.cpp`(汎用のヒープの入口)の中だけで使ってよい。
- プログラム全体の`operator new`/`delete`は`core/GlobalOperatorNew.cpp`で差し替え、すべて`MemoryTracker`を通して数える(量、件数、最大、確保の回数。毎秒のログに出る)。このファイルは静的ライブラリに入れず、exe(ゲームとテスト)ごとにソースとして入れる(CMakeの`HUSKY_GLOBAL_NEW_SOURCE`)。新しいexeを作るときも入れる。
- SDLの中の確保も`MemoryTracker`に通す(`platform::InstallMemoryHooks`、`SDL_SetMemoryFunctions`)。SDLが何かを確保する前でないと壊れるので、exeの`main`の最初の行で呼ぶ(ゲームは`src/main.cpp`、テストは`tests/TestMain.cpp`)。新しいexeを作るときも呼ぶ。
- ゲームオブジェクトのメモリは、`ObjectRegistry`が持つ自作のヒープアロケータ(`FreeListAllocator`、フリーリスト型・ファーストフィット)から借りる。容量を使い切ると`Create`は`nullptr`を返す。
- 自作アロケータの中の誤用は、そのままではASanから見えない。`FreeListAllocator`は、貸し出し中の中身(頼まれた大きさ)だけを触れるようにし、それ以外に印を付ける(ポイズニング、`core/AddressSanitizer.h`)。アロケータ自身の関数には`HUSKY_NO_SANITIZE_ADDRESS`を、クラスの中の宣言に付ける。返したブロックが貸し直された後の古いポインタは検出できない。

## リフレクション

- プロパティ(名前、型、オフセット、参照フラグ)とメソッドを、マクロで登録する。プロパティにできる型は`PropertyType`(bool、int32、float、string、ハンドルとその配列、構造体、`AssetRef`、`core::Guid`。GUIDは保存すると文字列)。
- 構造体(`Object`ではない値)は、`core/StructReflection.h`の`HUSKY_STRUCT_BEGIN` / `HUSKY_PROPERTY` / `HUSKY_STRUCT_END`で、型の外から登録する。登録は構造体と同じ名前空間に書く(ADLで見つけるため)。入れ子の構造体は中のものを先に登録する。外から触るので、登録するメンバーは public にする。登録した構造体は`HUSKY_PROPERTY`でそのままプロパティにでき、保存は入れ子になり、中のハンドルはGCがたどる。
- プロパティの値は、必ず`Property`(`GetValuePtr` / `GetValuePtrAt` / `GetStructAddressAt`)を通して読み書きする。シリアライザやGCがオフセットを直接触らない(将来、スクリプトの型など「関数で読み書きするプロパティ」を足すときに、変える場所を`Property`に閉じ込めるため)。
- メソッドの引数と戻り値は、数値、文字列、ハンドル、GC管理オブジェクトへの参照、リフレクション対応の構造体に限る。オーバーロードは非対応。
- スクリプトなど外部へ公開するオブジェクトは、必ずハンドル経由にする。生ポインタを外に出さない。

## フレームとスレッド

- 更新は可変dt(上限クランプ付き)を標準とする。dtの決め方はゲームループの1か所に集約し、テストでは外から固定値を注入できるようにする。
- ゲームスレッドは1本。アクター、コンポーネント、GC、リフレクションはゲームスレッド専用。
- ジョブシステムはスレッド数1(同期実行)でも動く作りにする。
  - `core::JobSystem`。`Schedule(job, counter)`で積み、`Wait(counter)`で待つ(待つ側も積まれたジョブを実行する)。完了の印`JobCounter`は使う側が持ち、ジョブが終わるまで壊さない。
  - ジョブの中では、GC管理のオブジェクトを触らない(ただのデータだけ)。ワーカーの数は`JobSystem`を作るときに決める。0本なら同期実行(`Wait`を呼んだスレッドが積んだ順に実行する。テストの結果が毎回同じになる)。
  - `Application`がジョブシステムを持つ(`GetJobSystem`)。ワーカーの数はコンストラクタの引数で、既定は0(テスト)。ゲームの`main`は`JobSystem::RecommendedWorkerCount()`(コア数 - 1)を渡す。
- 関数とクラスには、動作するスレッドをコメントで明記する。

## アセットとシリアライズ

- アセットは`AssetRef<T>`(中身はGUID)で参照する。
  - `core::AssetRef<T>`(`core/AssetRef.h`)はプロパティにできる(`PropertyType::AssetRef`)。中身は`guid`(保存される。JSONではGUIDの文字列、何も指さなければnull)と`asset`(読み込んだアセットのハンドル。保存しない)。
  - `asset`はGCが強い参照としてたどる。`AssetRef`を持つオブジェクトが生きている間は、アセットも回収されない。
  - 読むのは使うとき(`AssetLoader::Resolve`)。GUIDが変わっていれば読み直す。インスペクタやファイルからはGUIDだけを書き換える。
  - `Property::IsReference`はシーンの中のオブジェクトへの参照(保存に番号の対応表が要るもの)だけで、`AssetRef`は含まない。
  - GUIDは、アセットの隣の`.meta`(`enemy.png.meta`、中身は`{"guid": "..."}`)に書く。`.meta`はアセットと一緒にgitで管理し、ファイルを動かすときは一緒に動かす。
  - テクスチャは「画像 + `.meta`」をそのままアセットにする(1枚の画像 = 1つのテクスチャ)。切り出しは、画像をGUIDで指す別のアセット(スプライトシート、`.spritesheet`、JSON。`engine/SpriteSheetAsset.h`)に書く(`"texture": "<GUID>", "sprites": [{"id", "name", "x", "y", "width", "height"}]`)。1枚の画像をテクスチャとして直接使いながら、いくつものスプライトシートからも使える。将来テクスチャも包むアセットにする余地を残す(設計メモ「テクスチャを包むアセットへの移し替え」)。
  - 切り出しは「スプライトシートのGUID + 切り出しのID」で指す(Unityのfile IDと同じ考え方)。IDはスプライトシートの中で重ならない0以外のint32で、一度振ったら変えない。名前は表示用。アニメーション(`"animations": [{"id", "name", "frames", "frameSeconds", "loop"}]`。コマは切り出しのID)も同じくIDで指す(`engine/SpriteSheetData.h`)。手で書くときはIDを省き、コマを切り出しの名前で書いてよい。開発ビルドが起動時と`asset.refresh`でIDを振り、名前をIDに直して書き戻す(`NormalizeSpriteSheetsInFolder`。ソースの assets に書く)。製品はIDだけを読む。
  - スプライトシートを読み終わると、ローダが中の画像の読み込みも始める(依存の読み込み)。スプライトシートの`texture`はプロパティの`AssetRef`なので、スプライトシートが生きている間は画像も回収されない。アセットの種類を足すときは、ローダの読み込み中の記録(`PendingLoad`の子)と`Start〜`/`Finish〜`を足す。
  - `SpriteComponent`は、`sheet`(`AssetRef<SpriteSheetAsset>`)と`sprite`(切り出しのID)で切り出しを指せる。`sheet`を指していれば、`texture`と`region`より優先する。どれを貼るかは`ChooseSpriteTexture`(GPUを使わない。テストできる)が決める。
  - アニメーションは`SpriteAnimatorComponent`(`animation`(ID)、`playing`、`speed`を保存。経った時間は保存しない)が再生し、同じアクターの`SpriteComponent::sprite`を書き換える(実行順は`PreRender`)。スプライトシートを読み始めるのは描画なので、読めるまでは時間を進めない。スプライト専用で、骨のアニメーションは別の仕組みにする(設計メモ「保留事項と将来拡張」)。
  - `engine::AssetDatabase::Scan`が`assets`の中の`.meta`を読み、「GUID → パス」の表を作る。GUIDが重なった`.meta`は、どれも表に入れずにエラーとして出す(黙ってどちらかを選ばない)。
  - `.meta`を作る(GUIDを振る)のはエディタの役目なので、開発ビルドだけ(`CreateMissingMetaFiles`。ソースの assets に書く)。すでにある`.meta`は作り直さない。
  - アセットのフォルダ: 開発ビルド(Debug、ASan)は、ソースの`assets`を直接読み書きする(`HUSKY_SOURCE_DIR`。Unity、UE、Godotのエディタがプロジェクトのフォルダを直接読むのと同じ)。製品(Release)は、ビルドのたびにexeの隣へコピーされる`assets`を読むだけ。決めるのは`src/main.cpp`の1か所。テストはexeの隣のコピーを読む(2026-11-12に決定)。
- ロードは1か所のローダに集約する。`Load(guid)`は必ずハンドル(読み込み中、完了、失敗)を返す。読み込みは非同期(ジョブ)。
  - ファイルを読む・実行用のデータにする(1段目と2段目)はジョブで行い、結果は「読み込み中の記録」に書く。ジョブはGC管理のアセットを触らない。ゲームスレッドが毎フレーム`AssetLoader::Update`(`StepFrame`の中)で、終わったものをGPUに送り、アセットの状態を書く。待ちたいときは`WaitForAllLoads`。
  - ローダは`engine::AssetLoader`(`Application::GetAssetLoader`)。テクスチャは`LoadTexture(guid)`で、GC管理の`TextureAsset`を指すハンドル(弱い参照)を返す。失敗しても状態が`Failed`のアセットを返す。同じGUIDは、生きている間は同じアセットを返す。
  - アセットを生かしておくのは、使う側の強い参照(ふつうはプロパティの`AssetRef`、または根)。どこからも指されなければGCで回収され、そのときGPUのテクスチャも手放す。
  - GPU(`RenderDevice`)は`Application::Run`の中でしか無いので、ローダは`SetRenderDevice`で付け外しする。GPUが無いときはファイルだけを読んで「読めた」にし、GPUが付いたときにGPUへ送る。
- ローダ内部は「元ファイルを読む」「実行用データにする」の2段にする(`ReadTextureSource` / `CookTexture`)。2段目は最初は素通し。
- シリアライズはJSON。リフレクションの走査は、アーカイブのインターフェース越しに行う。
  - 2段に分ける: (1)オブジェクト ⇄ `engine::ArchiveValue`(形式に依存しない値の木)、(2)`ArchiveValue` ⇄ 文字列(`engine::JsonFormat`)。リフレクションの走査は(1)だけが行い、形式やライブラリを知らない。別の形式(バイナリなど)は(2)を足して対応する。
  - 外部ライブラリの内部の確保は、差し替え口があれば`MemoryTracker`へ流す(yyjsonは`yyjson_alc`で対応済み)。

## レンダラ

- 公開するのは`RenderDevice`と、エンジン独自の型(`Texture`、`Buffer`、`Pipeline`など)だけ。SDL_GPUの型は、描画層の実装の外に出さない。
- ゲーム側は描画コマンドを積むだけにする。GPUの呼び出しは描画層だけが行う。
- GPUリソースは世代付きハンドルで管理し、解放は数フレーム遅らせる。
  - テクスチャ: `RenderDevice::CreateTexture`で作り、`TextureHandle`(`core::Handle<render::Texture>`。中身は`RenderDevice.cpp`の中だけで定義)で指す。`ReleaseTexture`でハンドルはすぐ無効になり、GPUのメモリは`DeferredReleaseQueue`で3フレーム後に解放する。何も指さない、または手放されたハンドルは白い板として描く。
  - 描画命令は、同じテクスチャが続く区間ごとに1回(`SpriteBatch`)。積んだ順(奥から手前)は変えない。
- カメラ(CAM-1、2026-11-12): `render::Sprite`の位置はワールドの単位(y上が+、`x`,`y`は左下の角)。描画層は`RenderDevice::SetViewProjection`で行列(射影 × ビュー、`glm::mat4`)を1つ受け取り、頂点シェーダが掛ける。行列を作るのはengine(`engine/Camera.h`。`MakeOrthographicCamera`(中心、zoom)と、カメラが無いとき用の`MakeScreenCamera`(画面ちょうど、左下が原点、1単位 = 1ピクセル))。`Application`が毎フレーム`MakeSceneCamera`で作って渡す。
- カメラのコンポーネント(CAM-2、2026-11-12): `engine::CameraComponent`(`zoom`)をふつうのアクターに付ける。アクターの位置が画面の真ん中に映る(スプライトの左下の角とは違う)。シーンの一覧で最初のカメラを使い(`FindSceneCamera`)、無ければ`MakeScreenCamera`(カメラの無い古いシーンも今までどおり見える)。ゲームの最初の配置は、跳ね回るもの → カメラ("MainCamera") → プレイヤーの順。
- カメラの追従(CAM-3、2026-11-13): `game::CameraFollowComponent`(ゲームのコード。「誰をどう追うか」はゲームごとの決め事なので、ユーザーの指摘でエンジンからゲームへ移した。`target`(ハンドル、強い参照で保存される)、`offset`)を付けたアクターは、`TickOrder::Late`で相手の位置 + offset へ動く(相手が動いた同じフレームで追う)。編集中は Tick が止まるので追わない。ゲームの世界は画面の3倍(`game::WorldWidth` / `WorldHeight`)で、跳ね回るものは世界の端で跳ね返り、プレイヤーは世界の外へ出られない。

## 禁止事項

- 生の`new`/`delete`、`malloc`/`free`を使う。
- coreから他のモジュールを参照する。
- SDL3の型や呼び出しを、platformと描画層の実装の外へ出す。
- Windows固有のAPIやパスを、platformの外で使う。
- GC管理オブジェクトを、ゲームスレッド以外から触る。
- ImGuiのコードをリリースビルドに含める。
- 例外とRTTIを使う。
- ユーザーに相談せず、依存ライブラリを追加する。

## 検証

- 変更ごとにビルドとテストを回す。ビルドとテストのコマンドはM0で確定し、ここに追記する。
- ビルド(リポジトリのルートで実行。cmakeとctestはPATHに無いので、VS 2022付属の`C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`にあるものを使う):
  - 生成: `cmake --preset vs2022`(`build/vs2022/`に.slnができる)
  - ビルド: `cmake --build --preset debug`(または`release`)
  - 実行: `build/vs2022/Debug/HuskyEngineV2.exe`
  - エディタのCLI: `build/vs2022/Debug/HuskyEditorCli.exe --list`、`build/vs2022/Debug/HuskyEditorCli.exe asset.refresh`(コミットの前に流すと、`.meta`の抜けやGUIDの重なりが分かる)
- テスト(doctest、ヘッドレス。ビルドの後に実行):
  - `ctest --preset debug`(または`release`)
  - ビルドが失敗しても、ctestは前回の古いexeを実行して「passed」と出す。必ずビルドの成功を確かめてからテスト結果を見る。
  - 個別の結果を見るときは`build/vs2022/Debug/HuskyTests.exe`を直接実行する。
  - テストは`tests/`に、モジュールごとのフォルダ(`tests/engine/`など)で置き、`CMakeLists.txt`の`HuskyTests`に足す。
- テストはdtを注入したヘッドレス実行で行い、結果が毎回同じになるようにする。
- サニタイザ(実行中にメモリの誤用などを見つける仕組み)は、環境ごとに使えるものを回す(2026-11-12に決定)。
  - Windows(MSVC): ASanだけを回す。変更のたびの検証に含めてよい。
  - ASanのコマンド(MSVC、Debug構成。`build/vs2022-asan/`に普段のビルドとは別に作る):
    - 生成: `cmake --preset vs2022-asan`
    - ビルド: `cmake --build --preset asan`
    - テスト: `ctest --preset asan`(個別の結果は`build/vs2022-asan/Debug/HuskyTests.exe`)
  - ASanの実行時ライブラリ(DLL)は、ビルドのたびにexeの隣へコピーされる。
  - 自作アロケータ(`FreeListAllocator`)で返したメモリへのアクセスも、ポイズニングで検出できる(`use-after-poison`と報告される)。ポイズニングのテストはASanのビルドでだけ確認し、それ以外では何もしない。
  - UBSanとTSanは、MSVCでは使えない(UBSanはClang、TSanはWindows以外が必要)。Mac対応を始めたら、Mac(Clang)で回す。追加のツール(clang-clなど)は入れない。
  - ジョブシステムで複数のスレッドを使い始めるときに相談し、TSanなしで進めると決めた(2026-11-12)。代わりに、スレッドの間で共有するものを最小限にし(ジョブの行列は`std::mutex`、完了の印は`std::atomic`だけ)、負荷のテストをASanのビルドでも回す。Mac対応を始めたら、ClangのTSanでまとめて確かめる。WSLやclang-clは入れない。
- スマホから依頼されたときは、目視確認を前提にせず、テストと自動検証で完了を判断する。

## 未決事項

- `docs/skipped.md`(仮)の置き場所。スキップした範囲の記録先を、リポジトリ内のメモにする想定で良いか、ユーザーに確認する。(2026-11-11 時点で仮にここへ作成済み)
