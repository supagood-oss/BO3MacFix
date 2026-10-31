# Modified by Mickael DE LA ROQUE on 2026-09-08 to build and test the
# unofficial custom-map stream-stall workaround.
TARGET = BO3MacFix.dylib
TEST_TARGET = tests/test_stream_stall_recovery

DEBUG   ?= 0
ARCH    ?= x86_64
SDK     ?= macosx
OSXVER  ?= 10.13

SYSROOT  ?= $(shell xcrun --sdk $(SDK) --show-sdk-path 2>/dev/null)
CLANG    := clang
HOST_CC  ?= cc
PYTHON   ?= python3
DEV_MOD_HOOKS ?= 0
DEFINES += -DT7O_DEV_NATIVE=$(DEV_MOD_HOOKS)
EXPERIMENTAL_MOD_TEST ?= 0
DEFINES += -DT7O_EXPERIMENTAL_MOD_TEST=$(EXPERIMENTAL_MOD_TEST)
CC       := $(CLANG) -isysroot $(SYSROOT) -arch $(ARCH) -mmacosx-version-min=$(OSXVER)

CFLAGS  = -O1 -Wall -g -I include/ -I src/ -fvisibility=hidden
LDFLAGS = -shared -Wl,-undefined -Wl,dynamic_lookup -Llib -ldobby

FRAMEWORKS = -framework CoreFoundation -framework IOKit -framework CoreGraphics -framework ImageIO

SOURCES = source/bo3macfix.c source/bo3macnative.m source/stream_stall_recovery.c source/utilities.c source/steam.c source/exports.c source/havok.c source/t7overcharged.c source/cpu_tuning.c
SOURCES += source/controllers.c source/gpu_tweaks.c
# PlayStation controllers through macOS's GameController framework (gc_bridge.m), weak-linked because it needs macOS 11.3
SOURCES += source/gc_bridge.m
FRAMEWORKS += -framework Foundation -weak_framework GameController -weak_framework CoreHaptics

# MetalFX upscaling (metalfx.m), MetalFX is weak-linked because it needs macOS 13
SOURCES += source/metalfx.m

# the pre-patched executable (prepatch.c), the launcher builds it with BO3MacFix_BuildPrepatched
SOURCES += source/prepatch.c
SOURCES += source/t7o_core.c source/t7o_mac_fx.c source/t7o_mac_script.c source/t7o_mac_pause.c source/t7o_mac_movement.c
SOURCES += source/t7o_codegen.c
SOURCES += source/t7o_movement.c
SOURCES += source/t7o_mac_lifecycle.c
SOURCES += source/t7o_inline.S
SOURCES += source/t7o_mac_presentation.c
SOURCES += source/t7o_mac_memory.c
SOURCES += source/t7o_mac_traversal.c
SOURCES += source/t7o_mac_lean.c
SOURCES += source/t7o_mac_migration.c
SOURCES += source/t7o_mac_ui.c
SOURCES += source/t7o_mac_checkpoint.c
SOURCES += source/t7o_checkpoint_store.c source/t7o_mac_checkpoint_disk.c
SOURCES += source/t7o_mac_arc.c
SOURCES += source/t7o_cooppause.c source/t7o_mac_cooppause.c
SOURCES += source/t7o_mac_camera.c
SOURCES += source/t7o_freeaim.c source/t7o_mac_freeaim.c
SOURCES += source/t7o_prompt.c source/t7o_mac_prompt.c
SOURCES += source/t7o_mantle.c source/t7o_mac_mantle.c
SOURCES += source/converter_process.c
SOURCES += source/xpak_fallback.c source/mod_fileio.c
SOURCES += source/t7o_save_codec.c
SOURCES += source/steam_auth.c
NATIVE_OBJECTS = source/t7o_platform-$(EXPERIMENTAL_MOD_TEST).o source/t7o_json.o
FRAMEWORKS += -framework Metal -framework QuartzCore -framework AppKit -weak_framework MetalFX

all: $(TARGET)

test: $(TEST_TARGET) test-core test-codegen test-codegen-machine test-fx-adapter test-script-adapter test-pause-adapter test-movement-adapter test-lifecycle-adapter test-presentation-adapter test-memory-adapter test-traversal-adapter test-lean-adapter test-migration-adapter test-ui-adapter test-checkpoint-adapter test-arc-adapter test-cooppause test-cooppause-adapter test-camera-adapter test-freeaim test-freeaim-adapter test-prompt-adapter test-mantle-adapter test-converter-process test-steam-auth test-lua
	./$(TEST_TARGET)

test-converter-process:
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror tests/test_converter_process.c source/converter_process.c -o tests/test_converter_process
	./tests/test_converter_process

test-steam-auth:
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -pthread tests/test_steam_auth.c source/steam_auth.c -o tests/test_steam_auth
	./tests/test_steam_auth

test-core: tests/test_t7o_core
	./tests/test_t7o_core

test-codegen: tests/test_t7o_codegen
	./tests/test_t7o_codegen

test-codegen-machine:
	$(PYTHON) tools/verify_codegen.py --compiler $(HOST_CC) --output build/codegen-machine-checks.json

tests/test_t7o_codegen: tests/test_t7o_codegen.c source/t7o_codegen.c source/t7o_codegen.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_codegen.c source/t7o_codegen.c

tests/test_t7o_core: tests/test_t7o_core.c source/t7o_core.c source/t7o_core.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_core.c source/t7o_core.c

test-lua:
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

test-fx-adapter: tests/test_t7o_mac_fx_adapter
	./tests/test_t7o_mac_fx_adapter

tests/test_t7o_mac_fx_adapter: tests/test_t7o_mac_fx_adapter.c source/t7o_mac_fx.c source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_fx_adapter.c source/t7o_core.c

test-script-adapter: tests/test_t7o_mac_script_adapter
	./tests/test_t7o_mac_script_adapter

test-pause-adapter: tests/test_t7o_mac_pause_adapter
	./tests/test_t7o_mac_pause_adapter

tests/test_t7o_mac_pause_adapter: tests/test_t7o_mac_pause_adapter.c source/t7o_mac_pause.c source/t7o_mac_pause.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_pause_adapter.c

test-movement-adapter: tests/test_t7o_mac_movement_adapter
	./tests/test_t7o_mac_movement_adapter

test-lifecycle-adapter: tests/test_t7o_mac_lifecycle_adapter
	./tests/test_t7o_mac_lifecycle_adapter

tests/test_t7o_mac_lifecycle_adapter: tests/test_t7o_mac_lifecycle_adapter.c source/t7o_mac_lifecycle.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_lifecycle_adapter.c

test-presentation-adapter: tests/test_t7o_mac_presentation_adapter
	./tests/test_t7o_mac_presentation_adapter

test-memory-adapter: tests/test_t7o_mac_memory_adapter
	./tests/test_t7o_mac_memory_adapter

test-traversal-adapter: tests/test_t7o_mac_traversal_adapter
	./tests/test_t7o_mac_traversal_adapter

test-lean-adapter: tests/test_t7o_mac_lean_adapter
	./tests/test_t7o_mac_lean_adapter

test-migration-adapter: tests/test_t7o_mac_migration_adapter
	./tests/test_t7o_mac_migration_adapter

test-ui-adapter: tests/test_t7o_mac_ui_adapter
	./tests/test_t7o_mac_ui_adapter

test-camera-adapter: tests/test_t7o_mac_camera_adapter
	./tests/test_t7o_mac_camera_adapter

test-freeaim: tests/test_t7o_freeaim
	./tests/test_t7o_freeaim

test-freeaim-adapter: tests/test_t7o_mac_freeaim_adapter
	./tests/test_t7o_mac_freeaim_adapter

test-prompt-adapter: tests/test_t7o_mac_prompt_adapter
	./tests/test_t7o_mac_prompt_adapter

tests/test_t7o_mac_prompt_adapter: tests/test_t7o_mac_prompt_adapter.c source/t7o_mac_prompt.c source/t7o_mac_prompt.h source/t7o_mac_offsets.h source/t7o_prompt.c source/t7o_prompt.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_prompt_adapter.c source/t7o_prompt.c -pthread -lm

test-mantle-adapter: tests/test_t7o_mac_mantle_adapter
	./tests/test_t7o_mac_mantle_adapter

# Header changes must rebuild fixtures that embed the production adapter.
tests/test_t7o_mac_movement_adapter tests/test_t7o_mac_traversal_adapter tests/test_t7o_mac_ui_adapter tests/test_t7o_mac_freeaim_adapter tests/test_t7o_mac_prompt_adapter tests/test_t7o_mac_mantle_adapter: source/t7o_inline.h source/t7o_mac_offsets.h

tests/test_t7o_mac_mantle_adapter: tests/test_t7o_mac_mantle_adapter.c source/t7o_mac_mantle.c source/t7o_mac_mantle.h source/t7o_mac_offsets.h source/t7o_mantle.c source/t7o_mantle.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_mantle_adapter.c source/t7o_mantle.c -pthread -lm

tests/test_t7o_mac_freeaim_adapter: tests/test_t7o_mac_freeaim_adapter.c source/t7o_mac_freeaim.c source/t7o_mac_freeaim.h source/t7o_mac_offsets.h source/t7o_freeaim.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_freeaim_adapter.c source/t7o_freeaim.c -pthread -lm

tests/test_t7o_freeaim: tests/test_t7o_freeaim.c source/t7o_freeaim.c source/t7o_freeaim.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_freeaim.c source/t7o_freeaim.c -lm

tests/test_t7o_mac_camera_adapter: tests/test_t7o_mac_camera_adapter.c source/t7o_mac_camera.c source/t7o_mac_camera.h source/t7o_mac_offsets.h source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_camera_adapter.c source/t7o_core.c -pthread -lm

test-checkpoint-adapter: tests/test_t7o_mac_checkpoint_adapter tests/test_t7o_mac_checkpoint_engine tests/test_t7o_checkpoint_store tests/test_t7o_mac_checkpoint_disk
	./tests/test_t7o_mac_checkpoint_adapter
	./tests/test_t7o_mac_checkpoint_engine
	./tests/test_t7o_checkpoint_store
	./tests/test_t7o_mac_checkpoint_disk

tests/test_t7o_checkpoint_store: tests/test_t7o_checkpoint_store.c source/t7o_checkpoint_store.c source/t7o_checkpoint_store.h source/mod_fileio.c
	$(HOST_CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_checkpoint_store.c source/t7o_checkpoint_store.c source/mod_fileio.c

tests/test_t7o_mac_checkpoint_disk: tests/test_t7o_mac_checkpoint_disk.c source/t7o_mac_checkpoint_disk.c source/t7o_checkpoint_store.c source/t7o_checkpoint_store.h source/t7o_mac_offsets.h source/mod_fileio.c
	$(HOST_CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_checkpoint_disk.c source/t7o_checkpoint_store.c source/mod_fileio.c -pthread

tests/test_t7o_mac_checkpoint_engine: tests/test_t7o_mac_checkpoint_engine.c source/t7o_mac_checkpoint.c source/t7o_mac_checkpoint.h source/t7o_mac_offsets.h source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_checkpoint_engine.c source/t7o_core.c -pthread

tests/test_t7o_mac_checkpoint_adapter: tests/test_t7o_mac_checkpoint_adapter.c source/t7o_mac_checkpoint.c source/t7o_mac_checkpoint.h source/t7o_mac_offsets.h source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_checkpoint_adapter.c source/t7o_core.c -pthread

test-arc-adapter: tests/test_t7o_mac_arc_adapter
	./tests/test_t7o_mac_arc_adapter

test-cooppause: tests/test_t7o_cooppause
	./tests/test_t7o_cooppause

tests/test_t7o_cooppause: tests/test_t7o_cooppause.c source/t7o_cooppause.c source/t7o_cooppause.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_cooppause.c source/t7o_cooppause.c

test-cooppause-adapter: tests/test_t7o_mac_cooppause_adapter
	./tests/test_t7o_mac_cooppause_adapter

tests/test_t7o_mac_cooppause_adapter: tests/test_t7o_mac_cooppause_adapter.c source/t7o_mac_cooppause.c source/t7o_cooppause.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_cooppause_adapter.c source/t7o_cooppause.c -pthread

tests/test_t7o_mac_arc_adapter: tests/test_t7o_mac_arc_adapter.c source/t7o_mac_arc.c source/t7o_mac_arc.h source/t7o_mac_offsets.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_arc_adapter.c -lm

tests/test_t7o_mac_ui_adapter: tests/test_t7o_mac_ui_adapter.c source/t7o_mac_ui.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_ui_adapter.c -pthread -lm

tests/test_t7o_mac_migration_adapter: tests/test_t7o_mac_migration_adapter.c source/t7o_mac_migration.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_migration_adapter.c

tests/test_t7o_mac_lean_adapter: tests/test_t7o_mac_lean_adapter.c source/t7o_mac_lean.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_lean_adapter.c -lm

tests/test_t7o_mac_traversal_adapter: tests/test_t7o_mac_traversal_adapter.c source/t7o_mac_traversal.c source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_traversal_adapter.c source/t7o_core.c -lm

tests/test_t7o_mac_memory_adapter: tests/test_t7o_mac_memory_adapter.c source/t7o_mac_memory.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_memory_adapter.c

tests/test_t7o_mac_presentation_adapter: tests/test_t7o_mac_presentation_adapter.c source/t7o_mac_presentation.c source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_presentation_adapter.c source/t7o_core.c

tests/test_t7o_mac_movement_adapter: tests/test_t7o_mac_movement_adapter.c source/t7o_mac_movement.c source/t7o_core.c source/t7o_movement.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_movement_adapter.c source/t7o_core.c source/t7o_movement.c -lm

tests/test_t7o_mac_script_adapter: tests/test_t7o_mac_script_adapter.c source/t7o_mac_script.c source/t7o_core.c
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_t7o_mac_script_adapter.c source/t7o_core.c

test-macos: check-sdk
	$(CLANG) -isysroot $(SYSROOT) -fobjc-arc -fblocks -c source/t7o_json.m -o source/t7o_json_test.o
	$(CLANG) -isysroot $(SYSROOT) -fobjc-arc -framework Foundation -Wall -Wextra -o tests/test_t7o_json tests/test_t7o_json.m source/t7o_json_test.o
	./tests/test_t7o_json
	$(CLANG) -isysroot $(SYSROOT) -std=c11 -O1 -c source/t7o_save_codec.c -o source/t7o_save_codec_test.o
	$(CLANG) -isysroot $(SYSROOT) -fobjc-arc -fblocks -DT7O_TESTING -DT7O_EXPERIMENTAL_MOD_TEST=1 -framework Foundation -framework IOKit -Wall -Wextra -Wno-unused-parameter -o tests/test_t7o_platform tests/test_t7o_platform.m source/t7o_platform.m source/t7o_save_codec_test.o source/t7o_json_test.o
	./tests/test_t7o_platform

$(TEST_TARGET): tests/test_stream_stall_recovery.c source/stream_stall_recovery.c source/stream_stall_recovery.h
	$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ tests/test_stream_stall_recovery.c source/stream_stall_recovery.c

$(TARGET): check-sdk $(SOURCES) $(NATIVE_OBJECTS) source/t7overcharged_lua.h
	$(CC) $(CFLAGS) $(FRAMEWORKS) $(DEFINES) $(LDFLAGS) -o $@ $(SOURCES) $(NATIVE_OBJECTS)

source/t7o_platform-$(EXPERIMENTAL_MOD_TEST).o: source/t7o_platform.m source/t7o_platform.h source/t7o_save_codec.h source/t7o_json.h
	$(CC) $(CFLAGS) $(DEFINES) -fobjc-arc -fblocks -c -o $@ source/t7o_platform.m

source/t7o_json.o: source/t7o_json.m source/t7o_json.h
	$(CC) $(CFLAGS) -fobjc-arc -fblocks -c -o $@ source/t7o_json.m

check-sdk:
	@test -n "$(SYSROOT)" || (echo 'A macOS SDK is required for the dylib build.'; exit 1)

# the Lua half of our T7Overcharged support, embedded as a byte array
source/t7overcharged_lua.h: source/t7overcharged.lua
	cd source && xxd -i t7overcharged.lua > t7overcharged_lua.h

clean:
	rm -f -- tests/test_t7o_freeaim tests/test_t7o_mac_freeaim_adapter
	rm -f -- tests/test_t7o_mac_prompt_adapter
	rm -f -- tests/test_t7o_mac_mantle_adapter
	rm -f -- tests/test_t7o_mac_memory_adapter
	rm -f -- tests/test_t7o_mac_traversal_adapter
	rm -f -- tests/test_t7o_mac_lean_adapter
	rm -f -- tests/test_t7o_mac_migration_adapter
	rm -f -- tests/test_t7o_mac_ui_adapter
	rm -f -- tests/test_steam_auth source/t7o_save_codec_test.o tests/test_t7o_json source/t7o_json_test.o
	rm -f -- $(TARGET) $(TEST_TARGET) tests/test_t7o_core tests/test_t7o_codegen tests/test_t7o_mac_fx_adapter tests/test_t7o_mac_script_adapter tests/test_t7o_mac_pause_adapter tests/test_t7o_mac_movement_adapter tests/test_t7o_mac_lifecycle_adapter tests/test_t7o_mac_presentation_adapter tests/test_converter_process $(NATIVE_OBJECTS) source/t7overcharged_lua.h

.PHONY: all clean test test-core test-codegen test-codegen-machine test-fx-adapter test-script-adapter test-pause-adapter test-movement-adapter test-lifecycle-adapter test-presentation-adapter test-converter-process test-lua test-macos check-sdk
.PHONY: test-steam-auth test-memory-adapter test-traversal-adapter test-lean-adapter test-migration-adapter test-ui-adapter test-checkpoint-adapter test-arc-adapter test-cooppause test-cooppause-adapter test-camera-adapter
.PHONY: test-freeaim test-freeaim-adapter
.PHONY: test-prompt-adapter
.PHONY: test-mantle-adapter
