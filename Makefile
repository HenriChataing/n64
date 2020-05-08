
CC        := gcc
CXX       := g++
LD        := g++

Q ?= @

EXTDIR    := external
SRCDIR    := src
OBJDIR    := obj
EXE       := n64

# Select optimisation level.
OPTIMISE  ?= 2

INCLUDE   := $(SRCDIR) $(SRCDIR)/lib $(SRCLIB) $(EXTDIR)/fmt/include
DEFINE    := TARGET_BIGENDIAN

CFLAGS    := -Wall -Wno-unused-function -std=gnu11 -g
CFLAGS    += -O$(OPTIMISE) $(addprefix -I,$(INCLUDE)) $(addprefix -D,$(DEFINE))

CXXFLAGS  := -Wall -Wno-unused-function -std=c++11 -g
CXXFLAGS  += -O$(OPTIMISE) $(addprefix -I,$(INCLUDE)) $(addprefix -D,$(DEFINE))

LDFLAGS   :=
LIBS      := -lpthread -lpng

# Enable gprof profiling.
# Output file gmon.out can be formatted with make gprof2dot
ifneq ($(PROFILE),)
CXXFLAGS  += -pg
LDFLAGS   += -pg
endif

# Options for linking imgui with opengl3 and glfw3
LIBS      += -lGL -lGLEW `pkg-config --static --libs glfw3`
CFLAGS    += `pkg-config --cflags glfw3` -I$(SRCDIR)/gui
CXXFLAGS  += `pkg-config --cflags glfw3` -I$(SRCDIR)/gui

.PHONY: all
all: $(EXE)

OBJS      := \
    src/main.o \
    src/memory.o \
    src/debugger.o \
    src/mips/cpu-disas.o \
    src/mips/rsp-disas.o \
    src/r4300/cpu.o \
    src/r4300/state.o \
    src/r4300/cop0.o \
    src/r4300/cop1.o \
    src/r4300/hw.o \
    src/r4300/rsp.o \
    src/r4300/rdp.o \
    src/r4300/mmu.o \
    src/gui/gui.o \
    src/gui/graphics.o \
    src/gui/imgui.o \
    src/gui/imgui_draw.o \
    src/gui/imgui_impl_glfw.o \
    src/gui/imgui_impl_opengl3.o \
    src/gui/imgui_widgets.o

OBJS      += \
    external/fmt/src/format.o

DEPS      := $(patsubst %.o,$(OBJDIR)/%.d,$(OBJS))

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

$(EXE): $(addprefix $(OBJDIR)/,$(OBJS))
	@echo "  LD      $@"
	$(Q)$(LD) -o $@ $(LDFLAGS) $^ $(LIBS)

.PHONY: gprof2dot
gprof2dot:
	gprof $(EXE) | gprof2dot | dot -Tpng -o n64-prof.png

clean:
	@rm -rf $(OBJDIR) $(EXE)

.PHONY: clean all
