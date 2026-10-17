# 開発

Visual Studio 2026、MSVC v145、Windows SDKを使用します。C++20、Unicode、x64のDebug／Releaseが対象です。

`cdaf.slnx`にはコンソール版のcdafとWindows版のfindCDが含まれます。出力先はVisual Studioの標準設定です。

Visual Studioで`cdaf.slnx`を開き、x64のDebugまたはReleaseを選択してビルドします。実行ファイルはソリューション直下の`x64\Debug\`または`x64\Release\`に生成されます。`OutDir`と`IntDir`は上書きせず、Visual Studioの既定値を使用します。両プロジェクトの`UserMacros`直後の`PropertyGroup`で、ビルド用ホストツールをx64に指定しています。

```powershell
MSBuild cdaf.slnx /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64
MSBuild cdaf.slnx /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64
```

findCDはWin32ダイアログを使用します。設定は`HKEY_CURRENT_USER\Software\bry-ful\findCD`の`AFXW`と`Side`に保存します。

確認項目は、直下のみの検索、大文字小文字を区別しないAND検索、0／1／10／11件の結果、Enter／Escape／上下キー／数字キー／クリック、左右選択の保存、AFXW.EXEの検出とファイル選択、各ダイアログの中央表示です。
