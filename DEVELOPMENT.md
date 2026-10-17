# 開発

Visual Studio 2026、MSVC v145、Windows SDKを使用します。C++20、Unicode、x64のDebug／Releaseが対象です。

`cdaf.slnx`にはコンソール版のcdafとWindows版のfindCDが含まれます。出力先はVisual Studioの標準設定です。

Visual Studioで`cdaf.slnx`を開き、x64のDebugまたはReleaseを選択してビルドします。実行ファイルはソリューション直下の`x64\Debug\`または`x64\Release\`に生成されます。`OutDir`と`IntDir`は上書きせず、Visual Studioの既定値を使用します。両プロジェクトの`UserMacros`直後の`PropertyGroup`で、ビルド用ホストツールをx64に指定しています。

```powershell
MSBuild cdaf.slnx /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
MSBuild cdaf.slnx /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

findCDはWin32ダイアログを使用します。設定は`HKEY_CURRENT_USER\Software\bry-ful\findCD`の`AFXW`と`Side`に保存します。

起動時のL／R初期値は、起動中のあふのCOMオブジェクト`afxw.obj`の`SrcWin()`（0＝左、1＝右）から取得します。COM未登録・接続失敗・不正な戻り値などの場合は保存済み設定を維持します。取得した初期値だけでは設定を書き換えず、ユーザーの切り替え操作時に保存します。COM初期化とインターフェイスの解放は取得処理内で完結させます。

確認項目は、直下のみの検索、大文字小文字を区別しないAND検索、0／1／10／11件の結果、Enter／Escape／上下キー／数字キー／クリック、左右選択の保存、AFXW.EXEの検出とファイル選択、各ダイアログの中央表示です。
