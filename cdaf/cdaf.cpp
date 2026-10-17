#include <windows.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace fs = std::filesystem;


//------------------------------------------------------------
// 小文字化
//------------------------------------------------------------
std::wstring ToLower(std::wstring s)
{
	std::transform(
		s.begin(),
		s.end(),
		s.begin(),
		[](wchar_t c)
		{
			return static_cast<wchar_t>(std::towlower(c));
		});

	return s;
}


//------------------------------------------------------------
// マルチバイト文字列 → Unicode
//------------------------------------------------------------
std::wstring MultiByteToWide(
	const std::string& text,
	UINT codePage,
	DWORD flags = 0)
{
	if (text.empty())
		return L"";

	int len = MultiByteToWideChar(
		codePage,
		flags,
		text.data(),
		static_cast<int>(text.size()),
		nullptr,
		0);

	if (len <= 0)
		return L"";

	std::wstring result(len, L'\0');

	MultiByteToWideChar(
		codePage,
		flags,
		text.data(),
		static_cast<int>(text.size()),
		result.data(),
		len);

	return result;
}


//------------------------------------------------------------
// 前後の空白・改行・引用符を除去
//------------------------------------------------------------
std::wstring Trim(std::wstring s)
{
	const wchar_t* spaces = L" \t\r\n";

	size_t first = s.find_first_not_of(spaces);

	if (first == std::wstring::npos)
		return L"";

	size_t last = s.find_last_not_of(spaces);

	s = s.substr(first, last - first + 1);

	if (s.size() >= 2 &&
		s.front() == L'"' &&
		s.back() == L'"')
	{
		s = s.substr(1, s.size() - 2);
	}

	return s;
}


//------------------------------------------------------------
// cdw.exe のあるディレクトリ取得
//------------------------------------------------------------
fs::path GetExeDirectory()
{
	std::wstring buffer(32768, L'\0');

	DWORD len = GetModuleFileNameW(
		nullptr,
		buffer.data(),
		static_cast<DWORD>(buffer.size()));

	if (len == 0)
		return fs::current_path();

	buffer.resize(len);

	return fs::path(buffer).parent_path();
}


//------------------------------------------------------------
// cdaf.def からファイラーのパスを読み込む
//------------------------------------------------------------
bool LoadFileManager(fs::path& fileManager)
{
	const fs::path exeDirectory = GetExeDirectory();
	const fs::path defFile = exeDirectory / L"cdaf.def";

	//--------------------------------------------------------
	// まず cdaf.def に指定されたファイラーを確認
	//--------------------------------------------------------
	std::ifstream ifs(defFile, std::ios::binary);

	if (ifs)
	{
		std::string line;
		std::getline(ifs, line);

		//--------------------------------------------------------
		// UTF-8 BOM除去
		//--------------------------------------------------------
		if (line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF)
		{
			line.erase(0, 3);
		}

		//--------------------------------------------------------
		// UTF-8として変換
		//--------------------------------------------------------
		std::wstring wline =
			MultiByteToWide(
				line,
				CP_UTF8,
				MB_ERR_INVALID_CHARS);

		//--------------------------------------------------------
		// UTF-8でなければANSI
		//--------------------------------------------------------
		if (wline.empty() && !line.empty())
		{
			wline = MultiByteToWide(line, CP_ACP);
		}

		wline = Trim(wline);

		//--------------------------------------------------------
		// ファイラー存在確認
		// 無効なら同じフォルダ内の実行ファイルを探す
		//--------------------------------------------------------
		if (!wline.empty())
		{
			fs::path candidate(wline);
			std::error_code ec;

			if (fs::is_regular_file(candidate, ec))
			{
				fileManager = candidate;
				return true;
			}
		}
	}

	//--------------------------------------------------------
	// cdaf.def が無い・空・指定パスが無効な場合
	// cdaf.exe と同じフォルダで afw.exe を探す
	//--------------------------------------------------------
	std::error_code ec;
	fs::path candidate = exeDirectory / L"afw.exe";

	if (fs::is_regular_file(candidate, ec))
	{
		fileManager = candidate;
		return true;
	}

	//--------------------------------------------------------
	// あふｗの通常の実行ファイル名 AFXW.EXE も探す
	//--------------------------------------------------------
	ec.clear();
	candidate = exeDirectory / L"AFXW.EXE";

	if (fs::is_regular_file(candidate, ec))
	{
		fileManager = candidate;
		return true;
	}

	//--------------------------------------------------------
	// すべて見つからなかった場合だけエラー
	//--------------------------------------------------------
	std::wcerr
		<< L"ファイラーが見つかりません。\n"
		<< L"設定ファイル: " << defFile.wstring() << L"\n"
		<< L"検索先: " << (exeDirectory / L"afw.exe").wstring() << L"\n"
		<< L"検索先: " << (exeDirectory / L"AFXW.EXE").wstring() << L"\n";

	return false;
}


//------------------------------------------------------------
// -l / -r オプション判定
//
// 戻り値:
//   L'l' = 左ウインドウ
//   L'r' = 右ウインドウ
//   0    = 不正
//------------------------------------------------------------
wchar_t GetWindowOption(const std::wstring& option)
{
	if (option.size() != 2)
		return 0;

	//--------------------------------------------------------
	// 先頭は - または /
	//--------------------------------------------------------
	if (option[0] != L'-' &&
		option[0] != L'/')
	{
		return 0;
	}

	//--------------------------------------------------------
	// l / L
	//--------------------------------------------------------
	if (option[1] == L'l' ||
		option[1] == L'L')
	{
		return L'l';
	}

	//--------------------------------------------------------
	// r / R
	//--------------------------------------------------------
	if (option[1] == L'r' ||
		option[1] == L'R')
	{
		return L'r';
	}

	return 0;
}


//------------------------------------------------------------
// あふｗ起動
//
// 左:
//   AFXW.EXE -s -l"C:\xxxx"
//
// 右:
//   AFXW.EXE -s -r"C:\xxxx"
//------------------------------------------------------------
bool LaunchFileManager(
	const fs::path& fileManager,
	const fs::path& targetDirectory,
	wchar_t windowSide)
{
	STARTUPINFOW si{};
	PROCESS_INFORMATION pi{};

	si.cb = sizeof(si);

	//--------------------------------------------------------
	// AFXW側へ渡すオプション
	//--------------------------------------------------------
	std::wstring afxOption;

	if (windowSide == L'r')
	{
		afxOption = L"-r";
	}
	else
	{
		afxOption = L"-l";
	}

	//--------------------------------------------------------
	// コマンドライン作成
	//
	// 例:
	// "C:\Bin\afx\AFXW.EXE" -s -r"D:\Work\Test"
	//--------------------------------------------------------
	std::wstring commandLine =
		L"\"" +
		fileManager.wstring() +
		L"\" -s " +
		afxOption +
		L"\"" +
		targetDirectory.wstring() +
		L"\"";

	//--------------------------------------------------------
	// CreateProcessW用
	//--------------------------------------------------------
	std::vector<wchar_t> commandBuffer(
		commandLine.begin(),
		commandLine.end());

	commandBuffer.push_back(L'\0');

	//--------------------------------------------------------
	// 起動
	//--------------------------------------------------------
	BOOL result = CreateProcessW(
		fileManager.c_str(),
		commandBuffer.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		targetDirectory.c_str(),
		&si,
		&pi);

	if (!result)
	{
		std::wcerr
			<< L"ファイラーを起動できません。\n"
			<< L"Error = "
			<< GetLastError()
			<< L"\n";

		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	return true;
}


//------------------------------------------------------------
// 使用方法表示
//------------------------------------------------------------
void ShowUsage()
{
	std::wcout
		<< L"使い方:\n"
		<< L"\n"
		<< L"  cdaf -l [文字列] [path]\n"
		<< L"  cdaf -r [文字列] [path]\n"
		<< L"\n"
		<< L"  -l / -L / /l / /L : AFXW 左ウインドウ\n"
		<< L"  -r / -R / /r / /R : AFXW 右ウインドウ\n"
		<< L"\n"
		<< L"例:\n"
		<< L"\n"
		<< L"  cdaf -l\n"
		<< L"  cdaf -r abc\n"
		<< L"  cdaf -l abc D:\\Work\n";
}


//------------------------------------------------------------
// main
//------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
	//--------------------------------------------------------
	// Unicodeコンソール
	//--------------------------------------------------------
	_setmode(
		_fileno(stdout),
		_O_U16TEXT);

	_setmode(
		_fileno(stderr),
		_O_U16TEXT);

	_setmode(
		_fileno(stdin),
		_O_U16TEXT);


	//--------------------------------------------------------
	// 引数
	//
	// cdaf -l
	// cdaf -r
	// cdaf -l <word>
	// cdaf -r <word>
	// cdaf -l <word> <path>
	// cdaf -r <word> <path>
	//--------------------------------------------------------
	if (argc < 2 ||
		argc > 4)
	{
		ShowUsage();
		return 1;
	}


	//--------------------------------------------------------
	// 第1引数 -l / -r
	//--------------------------------------------------------
	wchar_t windowSide =
		GetWindowOption(argv[1]);

	if (windowSide == 0)
	{
		ShowUsage();
		return 1;
	}


	//--------------------------------------------------------
	// 検索文字列
	//--------------------------------------------------------
	std::wstring searchText;

	if (argc == 2)
	{
		//----------------------------------------------------
		// 検索文字列無し
		// コンソールから入力
		//----------------------------------------------------
		std::wcout
			<< L"input word $ ";

		std::getline(
			std::wcin,
			searchText);

		searchText =
			Trim(searchText);

		//----------------------------------------------------
		// Enterのみなら終了
		//----------------------------------------------------
		if (searchText.empty())
		{
			return 0;
		}
	}
	else
	{
		//----------------------------------------------------
		// 第2引数を検索文字列とする
		//----------------------------------------------------
		searchText =
			argv[2];
	}


	//--------------------------------------------------------
	// 検索文字列を小文字化
	//--------------------------------------------------------
	std::wstring searchLower =
		ToLower(searchText);


	//--------------------------------------------------------
	// 検索対象ディレクトリ
	//--------------------------------------------------------
	fs::path targetDirectory;

	if (argc >= 4)
	{
		//----------------------------------------------------
		// path指定あり
		//----------------------------------------------------
		targetDirectory =
			fs::path(argv[3]);
	}
	else
	{
		//----------------------------------------------------
		// path省略
		//----------------------------------------------------
		targetDirectory =
			fs::current_path();
	}


	//--------------------------------------------------------
	// 絶対パスへ変換
	//--------------------------------------------------------
	std::error_code ec;

	targetDirectory =
		fs::absolute(
			targetDirectory,
			ec);

	if (ec)
	{
		std::wcerr
			<< L"パスを取得できません。\n";

		return 1;
	}


	//--------------------------------------------------------
	// ディレクトリ存在確認
	//--------------------------------------------------------
	if (!fs::exists(
		targetDirectory,
		ec) ||
		!fs::is_directory(
			targetDirectory,
			ec))
	{
		std::wcerr
			<< L"指定されたディレクトリが存在しません。\n"
			<< targetDirectory.wstring()
			<< L"\n";

		return 1;
	}


	//--------------------------------------------------------
	// 検索結果
	//--------------------------------------------------------
	std::vector<fs::path> found;


	//--------------------------------------------------------
	// ディレクトリ列挙
	//--------------------------------------------------------
	fs::directory_iterator it(
		targetDirectory,
		fs::directory_options::skip_permission_denied,
		ec);

	fs::directory_iterator end;

	if (ec)
	{
		std::wcerr
			<< L"ディレクトリを開けません。\n"
			<< targetDirectory.wstring()
			<< L"\n";

		return 1;
	}


	//--------------------------------------------------------
	// 直下のディレクトリだけ検索
	//--------------------------------------------------------
	while (it != end)
	{
		std::error_code entryEc;

		if (it->is_directory(entryEc))
		{
			std::wstring name =
				it->path()
				.filename()
				.wstring();

			std::wstring nameLower =
				ToLower(name);


			//------------------------------------------------
			// 大文字・小文字を区別せず部分一致
			//------------------------------------------------
			if (nameLower.find(searchLower) !=
				std::wstring::npos)
			{
				found.push_back(
					it->path());


				//--------------------------------------------
				// 10個以上なら終了
				//--------------------------------------------
				if (found.size() >= 10)
				{
					std::wcout
						<< L"多すぎ\n";

					return 0;
				}
			}
		}


		//----------------------------------------------------
		// 次へ
		//----------------------------------------------------
		it.increment(ec);

		if (ec)
		{
			ec.clear();
		}
	}


	//--------------------------------------------------------
	// 0件
	//--------------------------------------------------------
	if (found.empty())
	{
		std::wcout
			<< L"フォルダが見つかりません。\n"
			<< L"検索文字列: " << searchText << L"\n"
			<< L"検索先: " << targetDirectory.wstring() << L"\n"
			<< L"※ 検索先の直下にあるサブフォルダだけを検索します。\n";

		return 0;
	}


	//--------------------------------------------------------
	// 名前順ソート
	//--------------------------------------------------------
	std::sort(
		found.begin(),
		found.end(),
		[](const fs::path& a,
			const fs::path& b)
		{
			return
				ToLower(
					a.filename().wstring()) <
				ToLower(
					b.filename().wstring());
		});


	//--------------------------------------------------------
	// 一覧表示
	//--------------------------------------------------------
	for (size_t i = 0;
		i < found.size();
		++i)
	{
		std::wcout
			<< L"["
			<< (i + 1)
			<< L"] "
			<< found[i]
			.filename()
			.wstring()
			<< L"\n";
	}


	//--------------------------------------------------------
	// cdaf.def 読み込み
	//--------------------------------------------------------
	fs::path fileManager;

	if (!LoadFileManager(
		fileManager))
	{
		return 1;
	}


	//--------------------------------------------------------
	// 1件なら入力を待たずに起動
	//--------------------------------------------------------
	if (found.size() == 1)
	{
		return LaunchFileManager(
			fileManager,
			found.front(),
			windowSide) ? 0 : 1;
	}


	//--------------------------------------------------------
	// キー待ち
	//--------------------------------------------------------
	for (;;)
	{
		int ch =
			_getwch();


		//----------------------------------------------------
		// ESC
		//----------------------------------------------------
		if (ch == 27)
		{
			return 0;
		}


		//----------------------------------------------------
		// 0
		//----------------------------------------------------
		if (ch == L'0')
		{
			return 0;
		}


		//----------------------------------------------------
		// 1 ～ 9
		//----------------------------------------------------
		if (ch >= L'1' &&
			ch <= L'9')
		{
			int index =
				ch - L'1';


			//------------------------------------------------
			// 存在する番号
			//------------------------------------------------
			if (index >= 0 &&
				index <
				static_cast<int>(
					found.size()))
			{
				//--------------------------------------------
				// AFXW起動
				//--------------------------------------------
				if (!LaunchFileManager(
					fileManager,
					found[index],
					windowSide))
				{
					return 1;
				}

				return 0;
			}
		}
	}
}