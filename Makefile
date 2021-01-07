
CC        := gcc
CXX       := g++
LD        := g++

Q ?= @

EXTDIR    := external
SRCDIR    := src
OBJDIR    := obj
EXE       := n64

# Enable gcc profiling.
PROFILE   ?= 0

# Select optimisation level.
OPTIMISE  ?= 2

# Enable address sanitizer.
ENABLE_SANITIZER ?= 0

# Enable recompiler.
ENABLE_RECOMPILER ?= 1

# Enable trace capture. Traces are saved to test/recompiler to be
# used as recompiler test inputs.
ENABLE_CAPTURE := 0

INCLUDE   := $(SRCDIR) $(SRCDIR)/lib $(SRCDIR)/gui $(SRCDIR)/interpreter
INCLUDE   += $(EXTDIR)/fmt/include $(EXTDIR)/imgui $(EXTDIR)/cxxopts/include
DEFINE    := TARGET_BIGENDIAN ENABLE_RECOMPILER=$(ENABLE_RECOMPILER)

CFLAGS    := -Wall -Wno-unused-function -std=gnu11 -g
CFLAGS    += -O$(OPTIMISE) $(addprefix -I,$(INCLUDE)) $(addprefix -D,$(DEFINE))

CXXFLAGS  := -Wall -Wno-unused-function -std=c++11 -g
CXXFLAGS  += -O$(OPTIMISE) $(addprefix -I,$(INCLUDE)) $(addprefix -D,$(DEFINE))

LDFLAGS   :=

# Enable address sanitizer.
ifeq ($(ENABLE_SANITIZER),1)
CFLAGS    += -fsanitize=address
CXXFLAGS  += -fsanitize=address
LIBS      += -lasan
endif

# Enable gprof profiling.
# Output file gmon.out can be formatted with make gprof2dot
ifeq ($(PROFILE),1)
CFLAGS    += -pg
CXXFLAGS  += -pg
LDFLAGS   += -pg
endif

# Options for multithreading.
LIBS      += -lpthread

# Additional library dependencies.
LIBS      += -lpng

# Options for linking imgui with opengl3 and glfw3
LIBS      += -lGL -lGLEW `pkg-config --static --libs glfw3`
CFLAGS    += `pkg-config --cflags glfw3`
CXXFLAGS  += `pkg-config --cflags glfw3`

.PHONY: all
all: $(EXE)

RECOMPILER_OBJS := \
    $(OBJDIR)/src/recompiler/ir.o \
    $(OBJDIR)/src/recompiler/backend.o \
    $(OBJDIR)/src/recompiler/code_buffer.o \
    $(OBJDIR)/src/recompiler/passes/typecheck.o \
    $(OBJDIR)/src/recompiler/passes/run.o \
    $(OBJDIR)/src/recompiler/passes/optimize.o \
    $(OBJDIR)/src/recompiler/target/mips/disassembler.o \
    $(OBJDIR)/src/recompiler/target/x86_64/assembler.o \
    $(OBJDIR)/src/recompiler/target/x86_64/emitter.o

UI_OBJS := \
    $(OBJDIR)/src/gui/gui.o \
    $(OBJDIR)/src/gui/graphics.o \
    $(OBJDIR)/src/gui/imgui_impl_glfw.o \
    $(OBJDIR)/src/gui/imgui_impl_opengl3.o

HW_OBJS := \
    $(OBJDIR)/src/r4300/rdp.o \
    $(OBJDIR)/src/r4300/hw/ai.o \
    $(OBJDIR)/src/r4300/hw/cart.o \
    $(OBJDIR)/src/r4300/hw/dpc.o \
    $(OBJDIR)/src/r4300/hw/mi.o \
    $(OBJDIR)/src/r4300/hw/pi.o \
    $(OBJDIR)/src/r4300/hw/pif.o \
    $(OBJDIR)/src/r4300/hw/ri.o \
    $(OBJDIR)/src/r4300/hw/sp.o \
    $(OBJDIR)/src/r4300/hw/vi.o \

EXTERNAL_OBJS := \
    $(OBJDIR)/external/fmt/src/format.o \
    $(OBJDIR)/external/imgui/imgui.o \
    $(OBJDIR)/external/imgui/imgui_draw.o \
    $(OBJDIR)/external/imgui/imgui_widgets.o

OBJS      := \
    $(OBJDIR)/src/main.o \
    $(OBJDIR)/src/memory.o \
    $(OBJDIR)/src/debugger.o \
    $(OBJDIR)/src/core.o \
    $(OBJDIR)/src/trace.o \
    $(OBJDIR)/src/interpreter/cpu.o \
    $(OBJDIR)/src/interpreter/cop0.o \
    $(OBJDIR)/src/interpreter/cop1.o \
    $(OBJDIR)/src/interpreter/rsp.o \
    $(OBJDIR)/src/assembly/disassembler.o \
    $(OBJDIR)/src/r4300/mmu.o \
    $(OBJDIR)/src/r4300/cpu.o \
    $(OBJDIR)/src/r4300/rsp.o \
    $(OBJDIR)/src/r4300/state.o \
    $(OBJDIR)/src/r4300/export.o \

ifeq ($(ENABLE_CAPTURE),1)
OBJS      += $(OBJDIR)/src/interpreter/trace.o
endif

OBJS      += $(RECOMPILER_OBJS)
OBJS      += $(HW_OBJS)
OBJS      += $(UI_OBJS)
OBJS      += $(EXTERNAL_OBJS)

DEPS      := $(patsubst %.o,%.d,$(OBJS))

-include $(DEPS)

$(OBJDIR)/%.o: %.cc
	@echo "  CXX     $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -MMD -MF $(@:.o=.d) -o $@

$(OBJDIR)/%.o: %.cpp
	@echo "  CXX     $*.cpp"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -MMD -MF $(@:.o=.d) -o $@

$(OBJDIR)/%.o: %.c
	@echo "  CC      $*.c"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -MMD -MF $(@:.o=.d) -o $@

$(EXE): $(OBJS)
	@echo "  LD      $@"
	$(Q)$(LD) -o $@ $(LDFLAGS) $^ $(LIBS)

test: bin/recompiler_test_suite
	@./bin/recompiler_test_suite

bin/rsp_test_suite: CXXFLAGS += \
    -std=c++17 \
    -I$(SRCDIR) \
    -I$(SRCDIR)/lib \
    -I$(EXTDIR)/tomlplusplus/include \
    -I$(EXTDIR)/fmt/include

bin/rsp_test_suite: \
    $(OBJDIR)/test/rsp_test_suite.o \
    $(OBJDIR)/src/interpreter/rsp.o \
    $(OBJDIR)/src/memory.o \
    $(OBJDIR)/src/debugger.o \
    $(OBJDIR)/src/r4300/rsp.o \
    $(OBJDIR)/src/r4300/hw/sp.o \
    $(OBJDIR)/src/assembly/disassembler.o \
    $(OBJDIR)/external/fmt/src/format.o

bin/rsp_test_suite:
	@echo "  LD      $@"
	@mkdir -p bin
	$(Q)$(LD) -o $@ $(LDFLAGS) $^

bin/recompiler_test_suite: CFLAGS += \
    -std=c11 \
    -I$(SRCDIR)/src

bin/recompiler_test_suite: CXXFLAGS += \
    -std=c++17 \
    -I$(SRCDIR)/src \
    -I$(EXTDIR)/tomlplusplus/include \
    -I$(EXTDIR)/fmt/include

bin/recompiler_test_suite: \
    $(OBJDIR)/src/debugger.o \
    $(OBJDIR)/src/memory.o \
    $(OBJDIR)/src/r4300/export.o \
    $(OBJDIR)/src/r4300/mmu.o \
    $(OBJDIR)/src/r4300/cpu.o \
    $(OBJDIR)/src/interpreter/cpu.o \
    $(OBJDIR)/src/interpreter/cop0.o \
    $(OBJDIR)/src/interpreter/cop1.o \
    $(OBJDIR)/test/recompiler_test_suite.o \
    $(OBJDIR)/src/assembly/disassembler.o

bin/recompiler_test_suite: $(RECOMPILER_OBJS)
bin/recompiler_test_suite: $(EXTERNAL_OBJS)

bin/recompiler_test_suite:
	@echo "  LD      $@"
	@mkdir -p bin
	$(Q)$(LD) -o $@ $(LDFLAGS) $^

bin/recompiler_test_server: CFLAGS += \
    -std=c11 \
    -I$(SRCDIR)/src

bin/recompiler_test_server: CXXFLAGS += \
    -std=c++17 \
    -I$(SRCDIR)/src \
    -I$(EXTDIR)/tomlplusplus/include \
    -I$(EXTDIR)/fmt/include

bin/recompiler_test_server: \
    $(OBJDIR)/src/core.o \
    $(OBJDIR)/src/debugger.o \
    $(OBJDIR)/src/memory.o \
    $(OBJDIR)/src/trace.o \
    $(OBJDIR)/src/r4300/export.o \
    $(OBJDIR)/src/r4300/mmu.o \
    $(OBJDIR)/src/r4300/cpu.o \
    $(OBJDIR)/src/r4300/state.o \
    $(OBJDIR)/src/r4300/rsp.o \
    $(OBJDIR)/src/interpreter/cpu.o \
    $(OBJDIR)/src/interpreter/cop0.o \
    $(OBJDIR)/src/interpreter/cop1.o \
    $(OBJDIR)/test/recompiler_test_server.o \
    $(OBJDIR)/src/assembly/disassembler.o

bin/recompiler_test_server: $(RECOMPILER_OBJS)
bin/recompiler_test_server: $(HW_OBJS)
bin/recompiler_test_server: $(UI_OBJS)
bin/recompiler_test_server: $(EXTERNAL_OBJS)

bin/recompiler_test_server:
	@echo "  LD      $@"
	@mkdir -p bin
	$(Q)$(LD) -o $@ $(LDFLAGS) $^ $(LIBS)

.PHONY: gprof2dot
gprof2dot:
	gprof $(EXE) | gprof2dot | dot -Tpng -o n64-prof.png

.PHONY: clean
clean:
	@rm -rf $(OBJDIR) $(EXE)
