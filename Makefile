# Captures -- PS5 screenshots and videos to a phone over Wi-Fi (see ../README.md)
#
#   make        -> build/ps5/CapturesPS5.elf   the payload to send with a payload loader: it installs the app
#                  (/data/homebrew/$(TITLE_ID)) and stays running as its helper (sandbox escape + the phone gallery)
#   make app    -> build/app/$(TITLE_ID)/       the app folder on its own (eboot.bin, sce_module/, sce_sys/)
#
# Needs the ps5-payload-dev SDK (https://github.com/ps5-payload-dev/sdk) in $(PS5_PAYLOAD_SDK) and clang/lld 18.
#
# Three steps, as in PS5SX2 and genplusgxPS5:
#   1. build/ps5/CapturesPS5-helper.elf  the helper alone (a payload): the app carries it and starts it when none runs
#   2. build/app/$(TITLE_ID)/            eboot.bin = the app (a native PS5 title, signed), sce_module/libc.prx, sce_sys/
#   3. build/ps5/CapturesPS5.elf         the installer + helper, with the app folder from step 2 inside

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
VERSION ?= 1.2
TITLE_ID ?= PPSA99233
APP_OUT := build/app/$(TITLE_ID)

# the app (eboot.bin)
APP_SRC := coreorbis/main-captures.cpp \
	coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoVideo.cpp coreorbis/orbis-shims/ProsperoInput.cpp \
	coreorbis/orbis-shims/ProsperoJailbreak.cpp coreorbis/orbis-shims/ProsperoCrash.cpp \
	coreorbis/orbis-shims/ProsperoCrt.cpp frontend/fe_text.cpp
APP_C_SRC := frontend/third_party/qrcodegen/qrcodegen.c
# the helper payload, and the installer (= helper + install)
HELPER_SRC := coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoHelper.cpp coreorbis/orbis-shims/ProsperoCrash.cpp \
	coreorbis/orbis-shims/ProsperoCaptures.cpp coreorbis/orbis-shims/ProsperoConvert.cpp
INSTALLER_SRC := installer/installer_main.cpp $(HELPER_SRC) coreorbis/orbis-shims/ProsperoInstall.cpp

INCS := -Icoreorbis/include-orbis -Icoreorbis/orbis-shims -Ifrontend
DEFS := -DGENPLUS_PS5_VERSION='"$(VERSION)"' -DGENPLUS_TITLE_ID='"$(TITLE_ID)"'
WARN := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers

PS5_CC  := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
PS5_OUT := build/ps5
PS5_OPT := -O2 -fPIC -march=znver2 -fno-strict-aliasing
# the helper's WebM -> MP4 converter: FFmpeg + x264 for the PS5, built by deps/build-deps.sh into deps/prefix
FFMPEG ?= $(abspath deps/prefix)
FFMPEG_LIBS := $(FFMPEG)/lib/libavformat.a $(FFMPEG)/lib/libavcodec.a $(FFMPEG)/lib/libswscale.a \
	$(FFMPEG)/lib/libswresample.a $(FFMPEG)/lib/libavutil.a $(FFMPEG)/lib/libx264.a
PS5_PAYLOAD_LIBS := $(FFMPEG_LIBS) -lkernel_sys -lSceNet -lSceNetCtl -lpthread -Wl,--wrap=pthread_create
%/orbis-shims/ProsperoConvert.o: EXTRA_FLAGS = -I$(FFMPEG)/include

# the UI font (Roboto) and the button glyphs (PromptFont) are built into fe_text.o
%/frontend/fe_text.o: EXTRA_FLAGS = -DFE_FONT_DIR='"$(abspath frontend/assets/fonts)"' -Wno-error -Wno-sign-compare -Wno-unused-function
%/frontend/fe_text.o: frontend/assets/fonts/Roboto-Regular.ttf frontend/assets/fonts/promptfont.otf frontend/assets/fonts/fa-brands-400.otf
# the gallery page is built into the helper
%/orbis-shims/ProsperoCaptures.o: coreorbis/orbis-shims/captures_page.inc
# the helper alone is installer_main.cpp without the install step
$(PS5_OUT)/installer/helper_main.o: EXTRA_FLAGS = -DGENPLUS_HELPER_ONLY

APP_OBJ := $(patsubst %.cpp,$(PS5_OUT)/%.o,$(APP_SRC)) $(patsubst %.c,$(PS5_OUT)/%.o,$(APP_C_SRC))
HELPER_OBJ := $(PS5_OUT)/installer/helper_main.o $(patsubst %.cpp,$(PS5_OUT)/%.o,$(HELPER_SRC))
INSTALLER_OBJ := $(patsubst %.cpp,$(PS5_OUT)/%.o,$(INSTALLER_SRC))

all: $(PS5_OUT)/CapturesPS5.elf

$(PS5_OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) -w -c -o $@ $<

$(PS5_OUT)/%.o: %.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h frontend/*.h
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(WARN) $(EXTRA_FLAGS) -c -o $@ $<

$(PS5_OUT)/installer/helper_main.o: installer/installer_main.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(WARN) $(EXTRA_FLAGS) -c -o $@ $<

# 1. the helper
$(PS5_OUT)/CapturesPS5-helper.elf: $(HELPER_OBJ)
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)

$(PS5_OUT)/helper_data.o: coreorbis/orbis-shims/helper_data.cpp coreorbis/orbis-shims/ProsperoJailbreak.h $(PS5_OUT)/CapturesPS5-helper.elf
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(WARN) -DHELPER_ELF='"$(abspath $(PS5_OUT)/CapturesPS5-helper.elf)"' -c -o $@ $<

# 3. the installer + helper, with the app folder inside
APP_FILES = $(APP_OUT)/eboot.bin $(APP_OUT)/sce_sys/param.json $(APP_OUT)/sce_sys/icon0.png $(APP_OUT)/sce_sys/pic0.dds \
	$(APP_OUT)/sce_sys/pic1.dds $(APP_OUT)/sce_module/libc.prx
APP_DATA_DEFS = -DAPP_EBOOT='"$(abspath $(APP_OUT)/eboot.bin)"' -DAPP_PARAM='"$(abspath $(APP_OUT)/sce_sys/param.json)"' \
	-DAPP_ICON0='"$(abspath $(APP_OUT)/sce_sys/icon0.png)"' -DAPP_PIC0='"$(abspath $(APP_OUT)/sce_sys/pic0.dds)"' \
	-DAPP_PIC1='"$(abspath $(APP_OUT)/sce_sys/pic1.dds)"' -DAPP_LIBC='"$(abspath $(APP_OUT)/sce_module/libc.prx)"'

$(PS5_OUT)/install_data.o: coreorbis/orbis-shims/install_data.cpp coreorbis/orbis-shims/ProsperoInstall.h $(APP_FILES)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(WARN) $(APP_DATA_DEFS) -c -o $@ $<

$(PS5_OUT)/CapturesPS5.elf: $(INSTALLER_OBJ) $(PS5_OUT)/install_data.o
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)
	@echo "built $@ ($$(stat -c %s $@) bytes)"

# 2. the app: a native PS5 title linked the way PS5SX2 links its eboot -- ps5-native-app-boilerplate's startup
# (app_crt.cpp, app_cpp_runtime.cpp) and the objects above, a PIE laid out by ps5-pie.ld against the SDK's
# system-module stubs and its static libc++/libc, then ps5-native-tool link (SCE dynamic tables) and self --sign.
NATIVE := proto/native
NATIVE_TOOL := $(NATIVE)/build/host/ps5-native-tool
LIBC_BUILDER := $(NATIVE)/build/host/libc-builder
LLD ?= ld.lld
HOST_CXX ?= clang++
NATIVE_CLANG = PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK) sh $(NATIVE)/tooling/prospero-clang18
SDK_LIB := $(PS5_PAYLOAD_SDK)/target/lib
APP_STUBS = $(filter-out %/libScePosixForWebKit.so %/libSceGLSlimVSH.so,$(sort $(wildcard $(SDK_LIB)/*.so)))

$(NATIVE_TOOL): $(NATIVE)/tooling/native/native_app_builder.cpp $(NATIVE)/tooling/native/elf_object.cpp \
		$(NATIVE)/tooling/native/self_container.cpp $(NATIVE)/tooling/native/sce_module_writer.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++20 -O2 -o $@ $^ -lz

$(LIBC_BUILDER): $(NATIVE)/tooling/native/libc_builder.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror -o $@ $<

# sce_module/libc.prx, deterministic: it must match the published digest (proto/native/libc.prx.sha256)
build/app/libc.prx: $(LIBC_BUILDER) $(NATIVE_TOOL) $(wildcard $(NATIVE)/tooling/native/runtime/*.txt)
	@mkdir -p $(dir $@)
	$(LIBC_BUILDER) $(NATIVE)/tooling/native/runtime/api-surface.txt $(NATIVE)/tooling/native/runtime/imports.txt build/app/libc.raw.elf
	$(NATIVE_TOOL) self --sign --in build/app/libc.raw.elf --out $@
	cd build/app && sha256sum --check --strict $(abspath $(NATIVE)/libc.prx.sha256)

$(APP_OUT)/sce_module/libc.prx: build/app/libc.prx
	@mkdir -p $(dir $@)
	cp $< $@

app: $(APP_FILES)

build/app/app_crt.o build/app/app_cpp_runtime.o: build/app/%.o: $(NATIVE)/tooling/native/%.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CLANG) -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -c -o $@ $<

EBOOT_OBJS = build/app/app_crt.o build/app/app_cpp_runtime.o $(APP_OBJ) $(PS5_OUT)/helper_data.o
build/app/captures-pie.elf: $(EBOOT_OBJS) $(NATIVE)/tooling/native/ps5-pie.ld $(NATIVE)/tooling/native/ehframe.ld \
		$(NATIVE)/tooling/native/app-symbols.map $(NATIVE)/tooling/native/localize.map
	$(LLD) -m elf_x86_64 -pie -z max-page-size=0x4000 --hash-style=gnu \
		-T $(NATIVE)/tooling/native/ps5-pie.ld -T $(NATIVE)/tooling/native/ehframe.ld --eh-frame-hdr \
		--version-script $(NATIVE)/tooling/native/app-symbols.map --version-script $(NATIVE)/tooling/native/localize.map \
		-e _start --no-dynamic-linker -o $@ $(EBOOT_OBJS) \
		--as-needed $(APP_STUBS) -L $(SDK_LIB) \
		$(SDK_LIB)/libc++.a $(SDK_LIB)/libc++abi.a $(SDK_LIB)/libunwind.a $(SDK_LIB)/libc.a -lpthread

build/app/eboot.elf: build/app/captures-pie.elf $(NATIVE_TOOL)
	$(NATIVE_TOOL) link --in $< --out $@ --stub-dir $(SDK_LIB) --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
	python3 tools/prepare_console_elf.py --input $@ --output build/app/eboot-console-ready.elf

$(APP_OUT)/eboot.bin: build/app/eboot.elf $(NATIVE_TOOL)
	@mkdir -p $(dir $@)
	$(NATIVE_TOOL) self --sign --in $< --out $@ --magic 0x1D3D154F

$(APP_OUT)/sce_sys/param.json: app/sce_sys/param.json
	@mkdir -p $(dir $@)
	sed 's/PPSA99232/$(TITLE_ID)/g' $< > $@

$(APP_OUT)/sce_sys/%: app/sce_sys/%
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf build $(NATIVE)/build

.PHONY: all app clean
.SECONDARY:
