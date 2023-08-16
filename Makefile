CXX = clang++
LD = lld-link
CXX_FLAGS = -DPLATFORM_WIN32 -DPLATFORM_X64 -DTOOL_VERSION=1 -DAPI_VERSION=1 -DDEV_BUILD -I.. -std=c++20 -O0 -g -gcodeview -march=native -masm=intel -fno-exceptions -fdiagnostics-absolute-paths -Wno-switch -Wno-deprecated-declarations -Wno-inconsistent-dllimport
LD_FLAGS = /def:./cbuild.def kernel32.lib libcmt.lib shell32.lib Advapi32.lib /out:./out/cbuild.exe /debug:full /subsystem:console
OUT_DIR = ./out
SOURCES = $(wildcard ./code/*.cpp)
OBJECTS = $(addprefix $(OUT_DIR)/, $(notdir $(SOURCES:.cpp=.obj)))

all: $(OUT_DIR)/cbuild.exe

$(OUT_DIR)/cbuild.exe: $(OBJECTS)
	@echo "Linking..."
	@$(LD) $(LD_FLAGS) $(OBJECTS)

$(OUT_DIR)/%.obj: ./code/%.cpp | $(OUT_DIR)
	@echo "Compiling $<"
	@$(CXX) $(CXX_FLAGS) -c $< -o $@

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

clean:
	rm $(OUT_DIR)/*.obj $(OUT_DIR)/cbuild.exe

.PHONY: all clean
