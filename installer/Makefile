TARGET = Install_BO3MacFix

DEBUG   ?= 0
ARCH    ?= x86_64
SDK     ?= macosx
OSXVER  ?= 10.13

SYSROOT  := $(shell xcrun --sdk $(SDK) --show-sdk-path)
ifeq ($(SYSROOT),)
$(error Could not find SDK "$(SDK)")
endif
CLANG    := clang
CC       := $(CLANG) -isysroot $(SYSROOT) -arch $(ARCH) -mmacosx-version-min=$(OSXVER)

CFLAGS  = -O1 -Wall -g -I include/ -I src/
LDFLAGS = 

FRAMEWORKS = -framework CoreFoundation -framework Foundation

SOURCES = installer.c utilities.c native.m

all: $(TARGET)

# Build on an Intel or Apple Silicon Mac. The game payload remains x86_64.
APP = BO3MacFix Setup.app
APP_ARCHS ?= -arch x86_64 -arch arm64
gui:
	mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	$(CLANG) -isysroot $(SYSROOT) $(APP_ARCHS) -mmacosx-version-min=10.15 -fobjc-arc -fblocks -Wall -Wextra -Wno-unused-parameter -framework AppKit -framework Foundation SetupApp.m SetupEngine.m SetupDependencies.m -o "$(APP)/Contents/MacOS/BO3MacFixSetup"
	cp SetupInfo.plist "$(APP)/Contents/Info.plist"
	mkdir -p "$(APP)/Contents/Resources/Rosetta Check.app/Contents/MacOS"
	$(CLANG) -isysroot $(SYSROOT) -arch x86_64 -mmacosx-version-min=10.15 RosettaCheck.c -o "$(APP)/Contents/Resources/Rosetta Check.app/Contents/MacOS/RosettaCheck"
	cp RosettaInfo.plist "$(APP)/Contents/Resources/Rosetta Check.app/Contents/Info.plist"
	codesign --force --sign - "$(APP)/Contents/Resources/Rosetta Check.app"

test:
	$(CLANG) -isysroot $(SYSROOT) -fobjc-arc -fblocks -DBO3SETUP_TESTING -framework Foundation -framework AppKit -Wall -Wextra -Wno-unused-parameter -o ../tests/test_setup_engine ../tests/test_setup_engine.m SetupEngine.m SetupDependencies.m
	../tests/test_setup_engine

dependency-check:
	$(CLANG) -isysroot $(SYSROOT) -fobjc-arc -fblocks -framework Foundation -framework AppKit -Wall -Wextra -Wno-unused-parameter -o ../build/prepare_dependencies ../tools/prepare_dependencies.m SetupEngine.m SetupDependencies.m

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(FRAMEWORKS) $(DEFINES) $(LDFLAGS) -o $@ $(SOURCES)

clean:
	rm -f -- $(TARGET)

.PHONY: all clean gui test
