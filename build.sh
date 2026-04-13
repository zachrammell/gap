#!/bin/bash
set -eu
cd `dirname "$0"`

for arg in "$@";
do
  declare $arg='1'
done

if [ ! -v clang ];                 then gcc=1; fi
if [ ! -v release ];               then debug=1; fi
if [ -v debug ];                   then unset release; echo "[debug mode]"; fi
if [ -v release ];                 then unset debug;   compiler="clang"; echo "[release mode]"; fi
if [ -v gcc ];                     then unset clang;   compiler="gcc";   echo "[gcc compile]"; fi
if [ -v clang ];                   then unset gcc;     echo "[clang compile]"; fi
if [ $# -eq 0 ];                   then gap=1;        echo '[default mode, assuming `gap` build]'; fi
if [ $# -eq 1 ] && [ -v release ]; then gap=1;        echo '[default mode, assuming `gap` build]'; fi

# --- Unpack Command Line Build Arguments ------------------------------------
opengl_renderer=1
auto_compile_flags=""
if [ -v track_arena ]; then auto_compile_flags="${auto_compile_flags} -DBUILD_TRACK_ARENA"; echo "[arena tracking enabled]"; fi
if [ -v opengl_renderer ]; then auto_compile_flags="${auto_compile_flags} -DBUILD_OPENGL_RENDERER"; echo "[OpenGL renderer enabled]"; fi
build_assets=0
if [ -v gap ] && [ ! -v no_deps ] && [ ! -v only_compile ]; then build_assets=1; fi

# --- Compile/Link Line Definitions ------------------------------------------
inc_dirs="-I../inc/
-I../src/
-I../external/nanosvg/src/
-I../external/freetype-2.13.3/include/
-I../external/stb/
-I../external/miniz/
-I../external/xxHash/
-Igen/"

defs="-DOS_LINUX=1 ${auto_compile_flags}"

cc_common="${compiler} -Wall ${defs} ${inc_dirs} -static-libstdc++ -static-libgcc"
cxx_common="-std=c++20 -Wno-switch -Wno-subobject-linkage -Wno-unused-function -Wno-char-subscripts"
cc_debug="${cc_common} -g -O0 -DBUILD_DEBUG=1"
cc_release="${cc_common} -g -O2 -DNDEBUG"

#link_common="-lstdc++ -lm -lGL -lX11 -lXext -lXrandr"
link_common="-static-libstdc++ -static-libgcc -lpthread -lm -lGL -lX11 -lXext -lXrandr -latomic"

# --- Choose Compile/Link Lines ----------------------------------------------
if [ -v gcc ];     then compile_debug="${cc_debug}"; fi
if [ -v gcc ];     then compile_release="${cc_release}"; fi
if [ -v clang ];   then compile_debug="${cc_debug}"; fi
if [ -v clang ];   then compile_release="${cc_release}"; fi
if [ -v debug ];   then compile="${compile_debug}"; fi
if [ -v release ]; then compile="${compile_release}"; fi

# --- 3rd Party Sources ------------------------------------------------------
freetype="../external/freetype-2.13.3/freetype-unity.c"

freetype_obj="freetype-unity.o"

# --- No Unity Sources -------------------------------------------------------
no_unity_src=""

# --- Unity Source -----------------------------------------------------------
gapsrc="../src/main.cpp"

gapsrc_obj="main.o"

if [ -v no_unity ]; then gapsrc="${no_unity_src}"; compile="${compile} -DNO_UNITY"; fi

# --- Asset Tools Source -----------------------------------------------------
asset_tool="../src/asset-tool.cpp"

# --- Scratch App Source -----------------------------------------------------
scratch_app="../scratch-app/app.cpp"

scratch_app_obj="app.o"

# --- Prep Directories -------------------------------------------------------
mkdir -p build

# --- Get Current Git Commit Id ----------------------------------------------
c_compile="${compile} -c -DBUILD_GIT_HASH=\"`git describe --always --dirty`\" -DBUILD_GIT_HASH_FULL=\"`git rev-parse HEAD`\""
cxx_compile="${compile} -c ${cxx_common} -DBUILD_GIT_HASH=\"`git describe --always --dirty`\" -DBUILD_GIT_HASH_FULL=\"`git rev-parse HEAD`\""
link="g++ $freetype_obj $gapsrc_obj ${link_common} -o gap"
link_ass_tool="g++ asset-tool.o ${link_common} -o asset-tool"
link_scratch_app="g++ $scratch_app_obj ${link_common} -o app"

# --- Build & Run Assets -----------------------------------------------------
if [ "$build_assets" -eq 0 ]; then echo "[skipping asset build]";fi
if [ "$build_assets" -eq 1 ]
then
  pushd build
  $cxx_compile $asset_tool
  $link_ass_tool
  ./asset-tool
  popd
fi

# --- Build Everything (@build_targets) --------------------------------------
pushd build
if [ -v gap ]
then
  didbuild=1
  if [ -v only_compile ]
  then
    echo "[only compile]"
    $cxx_compile $gapsrc -ftime-report
    exit 0
  fi
  if [ -v no_deps ]
  then
    $cxx_compile $gapsrc
    $link
    exit 0
  fi
  $c_compile $freetype
  $cxx_compile $gapsrc
  $link
fi

if [ -v app ]
then
  didbuild=1
  $cxx_compile $scratch_app
  $link_scratch_app
fi
popd

# --- Warn On No Builds ------------------------------------------------------
if [ ! -v didbuild ]
then
  echo '[WARNING] no valid build target specified; must use build target names as arguments to this script, like `build gap` or `build gap release`.'
  exit 1
fi