```yaml
# ============================================================
# Common C++ Development Prompt
# ============================================================

language:
  name: "C++"
  standard: "C++20"

development_environment:
  os: "Windows"
  ide: "Visual Studio 2026"
  compiler: "MSVC"
  architecture: "x64"
  configuration:
    - "Debug"
    - "Release"

# ============================================================
# General implementation policy
# ============================================================

implementation:
  priority:
    - "correctness"
    - "simplicity"
    - "execution speed"
    - "maintainability"

  use_standard_library_first: true
  avoid_unnecessary_dependencies: true
  avoid_overengineering: true

  modern_cpp:
    enabled: true
    use_raii: true
    use_smart_pointers_when_needed: true
    use_std_filesystem: true
    use_std_string: true
    use_constexpr_when_appropriate: true
    use_auto_when_readability_is_improved: true

  avoid:
    - "unnecessary global variables"
    - "unsafe C functions"
    - "unnecessary dynamic allocation"
    - "unnecessary memory copies"
    - "unnecessary file access"
    - "unnecessary abstraction"
    - "unnecessary classes"
    - "unnecessary third-party libraries"

# ============================================================
# Windows
# ============================================================

windows:
  unicode: true
  character_set: "Unicode"

  path_handling:
    support_long_paths_when_possible: true
    use_std_filesystem_path: true
    preserve_unicode_paths: true

  api:
    prefer_wide_api: true
    avoid_ansi_win32_api_when_possible: true

# ============================================================
# Source encoding
# ============================================================

encoding:
  source_code: "UTF-8"
  source_code_bom: "preserve-existing"

  console_output:
    stdout: "UTF-8"
    stderr: "UTF-8"

  text_files:
    default: "UTF-8"
    bom: "preserve-existing"

# ============================================================
# Libraries
# ============================================================

libraries:
  standard_library_first: true

  required: []

  optional: []

  forbidden: []

  dependency_policy:
    - "Do not add a third-party library unless it is necessary."
    - "If the standard library or Windows API can implement the feature simply, prefer it."
    - "Do not replace an existing working library without a clear reason."

# ============================================================
# Coding style
# ============================================================

coding_style:
  style: "simple modern C++"

  naming:
    classes: "PascalCase"
    structs: "PascalCase"
    functions: "camelCase"
    methods: "camelCase"
    variables: "camelCase"
    constants: "UPPER_SNAKE_CASE"

  braces:
    style: "allman"

  comments:
    language: "Japanese"
    preserve_existing_comments: true
    add_only_when_useful: true
    avoid_excessive_comments: true

  formatting:
    readable: true
    avoid_excessive_blank_lines: true
    avoid_excessively_long_functions_when_reasonable: true

# ============================================================
# Error handling
# ============================================================

error_handling:
  handle_errors_explicitly: true

  check:
    - "invalid arguments"
    - "file not found"
    - "directory not found"
    - "access denied"
    - "file open failure"
    - "file write failure"
    - "invalid input data"

  exceptions:
    catch_when_recovery_or_error_reporting_is_possible: true
    do_not_silently_ignore: true

  messages:
    include_relevant_filename_or_path: true
    include_reason_when_available: true

# ============================================================
# File processing
# ============================================================

file_processing:
  use_std_filesystem: true

  path:
    preserve_original_path: true
    support_unicode: true

  performance:
    skip_unnecessary_processing: true
    skip_existing_output_when_specified: true
    avoid_reopening_files: true
    avoid_duplicate_scans: true

# ============================================================
# Performance
# ============================================================

performance:
  optimization:
    enabled: true
    priority: "practical"

  rules:
    - "Avoid unnecessary repeated processing."
    - "Avoid scanning the same file or directory multiple times without reason."
    - "Avoid unnecessary memory copies."
    - "Avoid loading an entire file into memory when streaming is sufficient."
    - "Prefer simple efficient algorithms."

  multithreading:
    enabled_by_default: false
    use_only_when_beneficial: true
    avoid_when_complexity_outweighs_gain: true

# ============================================================
# Existing code modification rules
# ============================================================

existing_code:
  preserve_existing_behavior: true
  preserve_existing_comments: true
  preserve_existing_structure_when_possible: true
  preserve_existing_naming_when_possible: true

  modification:
    modify_only_required_parts: true
    do_not_rewrite_unrelated_code: true
    do_not_remove_existing_features: true
    do_not_change_existing_behavior_without_instruction: true
    do_not_change_ui_without_instruction: true
    do_not_change_output_format_without_instruction: true
    do_not_change_command_line_interface_without_instruction: true

  compatibility:
    maintain_backward_compatibility_when_possible: true

# ============================================================
# AI behavior
# ============================================================

ai:
  follow_specification_exactly: true

  rules:
    - "Do not add features that were not requested."
    - "Do not remove features that were not requested to be removed."
    - "Do not change specifications based on assumptions."
    - "Do not simplify away required behavior."
    - "Do not invent unknown APIs, methods, classes, files, or libraries."
    - "Do not assume missing code exists."
    - "If existing code is provided, base modifications on that code."
    - "Prefer a direct implementation over an unnecessarily complex design."

# ============================================================
# Code output
# ============================================================

code_output:
  full_source_code: true
  no_placeholders: true
  no_ellipsis: true
  complete_methods: true

  requirements:
    - "Output complete compilable code."
    - "Do not use TODO as a replacement for implementation."
    - "Do not omit unchanged sections with comments such as 'same as above'."
    - "Do not use '...' to omit code."
    - "Do not output incomplete methods."
    - "Include all required headers."
    - "Include complete class and function implementations."
    - "When multiple source files are required, output every required file."

  existing_code:
    keep_original_comments: true
    keep_unmodified_code: true

# ============================================================
# Build
# ============================================================

build:
  must_compile: true

  host_tools:
    architecture: "x64"
    preferred_tool_architecture: "x64"
    msbuild_argument: "/p:PreferredToolArchitecture=x64"
    project_setting: "<PreferredToolArchitecture>x64</PreferredToolArchitecture>"
    project_setting_location: "cdaf.vcxproj の UserMacros の直後にある PropertyGroup 内"
    rules:
      - "MSBuild でビルドするときは /p:PreferredToolArchitecture=x64 を指定する。"
      - "プロジェクトに設定を永続化する場合は、UserMacros の直後に PropertyGroup を追加して PreferredToolArchitecture を x64 にする。"
      - "この設定はビルド用ホストツールの指定であり、対象プラットフォームや標準の出力先は変更しない。"
    reason: "この環境では x86 ホストツールで LNK1101（MSPDB140.DLL バージョン不整合）が再現し、x64 ホストツールで Debug / Release のビルド成功を確認済み。"

  output_paths:
    use_visual_studio_defaults: true
    intermediate_directory: "$(IntDir)"
    output_directory: "$(OutDir)"
    do_not_override_with_custom_paths: true

  compiler:
    warnings_should_be_minimized: true

  requirements:
    - "Code must compile with Visual Studio 2026 and MSVC unless otherwise specified."
    - "Use Visual Studio standard intermediate and output directories."
    - "Do not introduce custom build output paths unless explicitly requested."

# ============================================================
# Documentation policy
# ============================================================

documentation:
  root_readme:
    audience: "end users"
    purpose: "Public-facing application guide suitable for GitHub publication."
    include:
      - "Application overview and user-facing features."
      - "Supported environments, launch instructions, and usage."
      - "User data locations, troubleshooting, and relevant license information."
    exclude:
      - "Build commands, compiler settings, and development environment setup."
      - "Test commands, test results, and verification logs."
      - "Internal implementation details, architecture, and dependency versions."
      - "Asset generation procedures and design iteration history."
      - "AI conversation history, work reports, and developer-only notes."
  development_document:
    default_file: "DEVELOPMENT.md"
    rules:
      - "Keep development information in a separate document, not the root README.md."
      - "Preserve useful technical information by moving it to the development document."
      - "Keep the root README concise, polished, and understandable without development knowledge."
      - "Review README changes from an end user's perspective before publishing."

# ============================================================
# Response style
# ============================================================

response:
  language: "Japanese"

  explanation:
    enabled: true
    concise: true

  when_code_is_requested:
    code_first: true
    avoid_unnecessary_explanation: true

  when_full_code_is_requested:
    explanations_before_code: false
    explanations_after_code: false

# ============================================================
# Restrictions
# ============================================================

restrictions:
  - "Do not output pseudocode when implementation is requested."
  - "Do not provide only partial code when full code is requested."
  - "Do not silently change requirements."
  - "Do not rename files, classes, functions, or variables without a reason."
  - "Do not introduce unnecessary frameworks."
  - "Do not replace working code merely for stylistic reasons."
```
