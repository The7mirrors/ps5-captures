#!/bin/bash
# FFmpeg + x264 for the helper's WebM -> MP4 converter (ProsperoConvert.cpp): static, PIC and minimal (WebM with
# VP9/VP8 + Opus/Vorbis in, MP4 with H.264 by x264 + AAC out), built with the PS5 payload SDK's clang into deps/prefix.
#
#   deps/build-deps.sh            (PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk unless set; needs curl, make, nasm, clang 18)
#
# Debug info is left out and FFmpeg's configure line is blanked, so no build paths end up in the payload.
set -e
export PATH=/usr/lib/llvm-18/bin:$PATH
DEPS="$(cd "$(dirname "$0")" && pwd)"
SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
PREFIX="$DEPS/prefix"
STUB="$DEPS/stub"
SRC="$DEPS/src"
FFMPEG_VER=7.1.5
FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
X264_COMMIT=b35605ace3ddf7c1a5d67a2eb553f034aef41d55 # (the stable branch on 2026-10-11, X264_BUILD 165)
X264=x264-$X264_COMMIT
JOBS=$(nproc 2>/dev/null || echo 4)

mkdir -p "$SRC" "$STUB" "$PREFIX/lib/pkgconfig"
cd "$SRC"
[ -f ffmpeg-$FFMPEG_VER.tar.xz ] || curl -fLO https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz
echo "$FFMPEG_SHA256  ffmpeg-$FFMPEG_VER.tar.xz" | sha256sum -c -
[ -f $X264.tar.bz2 ] || curl -fLO https://code.videolan.org/videolan/x264/-/archive/$X264_COMMIT/$X264.tar.bz2
[ -d ffmpeg-$FFMPEG_VER ] || tar xf ffmpeg-$FFMPEG_VER.tar.xz
[ -d $X264 ] || tar xf $X264.tar.bz2
[ -f "$STUB/libm.a" ] || "$SDK/bin/llvm-ar" rc "$STUB/libm.a" # (an empty libm: the SDK's libc has the math)

echo "== x264"
cd "$SRC/$X264"
make distclean >/dev/null 2>&1 || true
CC="$SDK/bin/prospero-clang" AR="$SDK/bin/llvm-ar" RANLIB="$SDK/bin/llvm-ranlib" STRINGS=llvm-strings \
./configure --host=x86_64-unknown-freebsd --prefix="$PREFIX" --enable-static --enable-pic --disable-cli \
	--disable-opencl --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash \
	--bit-depth=8 --chroma-format=420 --extra-ldflags="-L$STUB" >configure.out 2>&1 || { tail -30 configure.out; exit 1; }
make -j$JOBS >make.out 2>&1 || { grep error make.out | head -20; exit 1; }
make install-lib-static >/dev/null

# x264's pkg-config file (install-lib-static writes none)
cat >"$PREFIX/lib/pkgconfig/x264.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: x264
Description: H.264 encoder
Version: 0.$(sed -n 's/^#define X264_BUILD \([0-9]*\).*/\1/p' x264.h)
Libs: -L\${libdir} -lx264 -lpthread
Cflags: -I\${includedir}
EOF

echo "== FFmpeg $FFMPEG_VER"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" PKG_CONFIG_PATH=
cd "$SRC/ffmpeg-$FFMPEG_VER"
make distclean >/dev/null 2>&1 || true
./configure --prefix="$PREFIX" --enable-cross-compile --target-os=freebsd --arch=x86_64 \
	--cc="$SDK/bin/prospero-clang" --cxx="$SDK/bin/prospero-clang++" --ar="$SDK/bin/llvm-ar" \
	--ranlib="$SDK/bin/llvm-ranlib" --nm="$SDK/bin/llvm-nm" --strip=llvm-strip --pkg-config=pkg-config \
	--extra-cflags="-I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib -L$STUB" --extra-libs="-lpthread" \
	--enable-static --disable-shared --enable-pic --enable-gpl --disable-autodetect --enable-pthreads --disable-debug \
	--disable-programs --disable-doc --disable-network --disable-avdevice --disable-avfilter --disable-postproc \
	--disable-everything \
	--enable-libx264 --enable-encoder=libx264,aac \
	--enable-decoder=vp9,vp8,opus,vorbis --enable-parser=vp9,vp8,opus,vorbis \
	--enable-demuxer=matroska --enable-muxer=mp4 --enable-protocol=file \
	--enable-swscale --enable-swresample >configure.out 2>&1 || { tail -25 configure.out; tail -40 ffbuild/config.log; exit 1; }
sed -i 's|^#define FFMPEG_CONFIGURATION .*|#define FFMPEG_CONFIGURATION ""|' config.h
make -j$JOBS >make.out 2>&1 || { grep error make.out | head -30; exit 1; }
make install >/dev/null
ls -la "$PREFIX"/lib/*.a
