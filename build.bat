@echo off
setlocal enabledelayedexpansion
cd /D "%~dp0"

:: Derived from RADDBG build.bat.

for %%a in (%*) do set "%%a=1"

if not "%msvc%"=="1" if not "%clang%"=="1" set msvc=1
if not "%release%"=="1" set debug=1
if not "%no_unity%"=="1" set no_unity=0
if "%debug%"=="1"   set release=0 && echo [debug mode]
if "%release%"=="1" set debug=0   && echo [release mode]
if "%msvc%"=="1"    set clang=0   && echo [msvc compile]
if "%clang%"=="1"   set msvc=0    && echo [clang compile]
if "%~1"==""                         echo [default mode, assuming `gap` build] && set gap=1
if "%~1"=="release" if "%~2"==""     echo [default mode, assuming `gap` build] && set gap=1

:: --- Unpack Command Line Build Arguments ------------------------------------
set opengl_renderer=1
set auto_compile_flags=
if "%asan%"=="1"             set auto_compile_flags=%auto_compile_flags% -fsanitize=address &&                              echo [asan enabled]
if "%profiled%"=="1"         set auto_compile_flags=%auto_compile_flags% /DBUILD_PROFILED /I..\..\tracy\public &&           echo [profiling enabled]
if "%track_arena%"=="1"      set auto_compile_flags=%auto_compile_flags% /DBUILD_TRACK_ARENA &&                             echo [arena tracking enabled]
if "%d3d11%"=="1"            set auto_compile_flags=%auto_compile_flags% /DBUILD_D3D11_RENDERER && set opengl_renderer=0 && echo [DX11 renderer enabled]
if "%opengl_renderer%"=="1"  set auto_compile_flags=%auto_compile_flags% /DBUILD_OPENGL_RENDERER &&                         echo [OpenGL renderer enabled]
set build_assets=0
if "%gap%"=="1" if not "%no_deps%"=="1" if not "%only_compile%"=="1" set build_assets=1

:: --- Compile/Link Line Definitions ------------------------------------------
set cl_inc_dirs=/I..\inc\ ^
/I..\src\ ^
/I..\external\nanosvg\src\ ^
/I..\external\freetype-2.13.3\include\ ^
/I..\external\stb\ ^
/I..\external\miniz\ ^
/I..\external\xxHash\ ^
/Igen\
set cl_defs=/D_ITERATOR_DEBUG_LEVEL=0 /D_MBCS /DWIN32 /D_WINDOWS
set cl_common=     /nologo /diagnostics:caret /diagnostics:color /EHsc /FC /Z7 /W4 /WX /utf-8 /MP /GS /fp:precise /std:c++20 /Zc:preprocessor /Zc:wchar_t /Zc:forScope /Zc:inline /GR /permissive- %cl_inc_dirs% %cl_defs%
set clang_common=  -I..\inc\ -gcodeview -fdiagnostics-absolute-paths -Wall -Wno-unknown-warning-option -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf -ferror-limit=10000
set cl_debug=      call cl /Od /DBUILD_DEBUG=1 /Ob1 /MTd %cl_common% %auto_compile_flags%
set cl_release=    call cl /O2 /DBUILD_DEBUG=0 /MT /DNDEBUG %cl_common% %auto_compile_flags%
set clang_debug=   call clang -g -O0 -DBUILD_DEBUG=1 %clang_common% %auto_compile_flags%
set clang_release= call clang -g -O2 -DBUILD_DEBUG=0 %clang_common% %auto_compile_flags%
set msvc_link=     /MANIFEST:EMBED /INCREMENTAL:NO /noexp /MACHINE:X64 /DEBUG
set cl_link=       /link %msvc_link%
set clang_link=    -fuse-ld=lld -Xlinker /MANIFEST:EMBED -Xlinker -Xlinker /NATVIS:"%~dp0\src\natvis\base.natvis"
set cl_out=        /out:
set clang_out=     -o
set cl_natvis=     /NATVIS:
set clang_natvis=  -Xlinker /NATVIS:

:: --- Per-Build Settings -----------------------------------------------------
set link_icon=logo.res
if "%msvc%"=="1"    set only_compile_flag=/c
if "%clang%"=="1"   set only_compile_flag=-c
if "%msvc%"=="1"    set no_aslr=/DYNAMICBASE:NO
if "%clang%"=="1"   set no_aslr=-Wl,/DYNAMICBASE:NO
if "%msvc%"=="1"    set rc=call rc
if "%clang%"=="1"   set rc=call llvm-rc

:: --- Choose Compile/Link Lines ----------------------------------------------
if "%msvc%"=="1"         set compile_debug=%cl_debug%
if "%msvc%"=="1"         set compile_release=%cl_release%
if "%msvc%"=="1"         set compile_link=%cl_link% /SUBSYSTEM:WINDOWS
if "%msvc%"=="1"         set only_link=%msvc_link% /SUBSYSTEM:WINDOWS
if "%msvc%"=="1"         set compile_link_ass_tool=%cl_link% /SUBSYSTEM:CONSOLE
if "%msvc%"=="1"         set out=%cl_out%
if "%clang%"=="1"        set compile_debug=%clang_debug%
if "%clang%"=="1"        set compile_release=%clang_release%
if "%clang%"=="1"        set compile_link=%clang_link%
if "%clang%"=="1"        set compile_link_ass_tool=%clang_link%
if "%clang%"=="1"        set out=%clang_out%
if "%debug%"=="1"        set compile=%compile_debug%
if "%release%"=="1"      set compile=%compile_release%
if "%only_compile%"=="1" set compile=%compile% %only_compile_flag%

:: --- 3rd Party Sources ------------------------------------------------------
set freetype=..\external\freetype-2.13.3\freetype-unity.c

set freetype_obj=freetype-unity.obj

:: --- No Unity Sources -------------------------------------------------------
set no_unity_src=

:: --- Unity Source -----------------------------------------------------------
set gapsrc= ..\src\main.cpp

set gapsrc_obj=main.obj

if "%no_unity%"=="1" set gapsrc=%no_unity_src% && set compile=%compile% /DNO_UNITY

:: --- Asset Tools Source -----------------------------------------------------
set asset_tool= ..\src\asset-tool.cpp

:: --- Scratch App Source -----------------------------------------------------
set scratch_app= ..\scratch-app\app.cpp

:: --- Prep Directories -------------------------------------------------------
if not exist build mkdir build

:: --- Produce Logo Icon File -------------------------------------------------
pushd build
%rc% /nologo /fo logo.res ..\res\logo.rc || exit /b 1
popd

:: --- Get Current Git Commit Id ----------------------------------------------
:: for /f %%i in ('call git describe --always --dirty')   do set compile=%compile% -DBUILD_GIT_HASH=\"%%i\"
:: for /f %%i in ('call git rev-parse HEAD')              do set compile=%compile% -DBUILD_GIT_HASH_FULL=\"%%i\"

set compile=%compile% -DBUILD_GIT_HASH=\"123\" -DBUILD_GIT_HASH_FULL=\"123\"

:: --- Build & Run Assets -----------------------------------------------------
pushd build
if not "%build_assets%"=="1" echo [skipping asset build]
if "%build_assets%"=="1" (
  %compile% %asset_tool% %compile_link_ass_tool% %out%asset-tool.exe || exit /b 1
  :: Build assets.
  .\asset-tool.exe || exit /b 1
)
popd build

:: --- Build Everything (@build_targets) --------------------------------------
pushd build
if "%gap%"=="1" (
  set didbuild=1
  if "%only_compile%"=="1" (
    echo [only compile]
    %compile% %gapsrc% /Bt+ || exit /b 1
    popd
    exit /b 0
  )
  if "%no_deps%"=="1" (
    echo [no dependencies]
    %compile% %only_compile_flag% %gapsrc% || exit /b 1
    link %only_link% %gapsrc_obj% %freetype_obj% %link_icon% %out%gap.exe || exit /b 1
    popd
    exit /b 0
  )
  %compile% %gapsrc% %freetype% %compile_link% %link_icon% %out%gap.exe || exit /b 1
)

if "%app%"=="1" (
  set didbuild=1
  %compile% %scratch_app% %compile_link_ass_tool% %out%app.exe || exit /b 1
)
popd

:: --- Warn On No Builds ------------------------------------------------------
if "%didbuild%"=="" (
  echo [WARNING] no valid build target specified; must use build target names as arguments to this script, like `build gap` or `build gap release`.
  exit /b 1
)
