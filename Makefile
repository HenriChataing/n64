CC        := gcc
CXX       := g++
LD        := g++

SRCDIR    := src
OBJDIR    := obj
BINDIR    := bin
EXE       := n64

CFLAGS    := -Wall -Wno-unused-function -std=gnu11
CXXFLAGS  := -Wall -Wno-unused-function -std=c++11
CXXFLAGS  += -I$(SRCDIR) -I$(SRCDIR)/lib -DTARGET_BIGENDIAN
LDFLAGS   :=
LIBS      := -lpthread -lpng

ifneq ($(DEBUG),)
CXXFLAGS  += -g -pg -DDEBUG=$(DEBUG)
LDFLAGS   += -pg
endif

SRC       := \
    main.cc \
    memory.cc \
    core.cc \
    terminal.cc \
    debugger.cc

SRC       += \
    mips/cpu-disas.cc \
    mips/rsp-disas.cc \
    r4300/cpu.cc \
    r4300/state.cc \
    r4300/cop0.cc \
    r4300/cop1.cc \
    r4300/hw.cc \
    r4300/rsp.cc \
    r4300/rdp.cc \
    r4300/mmu.cc

# GDB RSP server sources
SRC       += \
    rsp/server.cc \
    rsp/commands.cc \
    rsp/buffer.cc

# Options for linking imgui with opengl3 and glfw3
LIBS      += -lGL -lGLEW `pkg-config --static --libs glfw3`
CFLAGS    += `pkg-config --cflags glfw3` -I$(SRCDIR)/gui
CXXFLAGS  += `pkg-config --cflags glfw3` -I$(SRCDIR)/gui
SRC       += \
    gui/imgui.cpp \
    gui/imgui_draw.cpp \
    gui/imgui_impl_glfw.cpp \
    gui/imgui_impl_opengl3.cpp \
    gui/imgui_widgets.cpp \
    gui/gui.cpp \
    gui/graphics.cc

OBJS      := $(filter %.o, \
                 $(patsubst %.c,$(OBJDIR)/%.o, $(SRC)) \
                 $(patsubst %.cc,$(OBJDIR)/%.o, $(SRC)) \
                 $(patsubst %.cpp,$(OBJDIR)/%.o, $(SRC)))
DEPS      := $(filter %.d, \
                 $(patsubst %.c,$(OBJDIR)/%.d, $(SRC)) \
                 $(patsubst %.cc,$(OBJDIR)/%.d, $(SRC)) \
                 $(patsubst %.cpp,$(OBJDIR)/%.d, $(SRC)))

Q = @


all: $(EXE)

-include $(DEPS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc
	@echo "  CXX     $*.cc"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@
	$(Q)$(CXX) $(CXXFLAGS) -MT "$@" -MM $< > $(OBJDIR)/$*.d

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "  CXX     $*.cpp"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@
	$(Q)$(CXX) $(CXXFLAGS) -MT "$@" -MM $< > $(OBJDIR)/$*.d

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "  CC      $*.c"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
	$(Q)$(CC) $(CFLAGS) -MT "$@" -MM $< > $(OBJDIR)/$*.d

$(EXE): $(OBJS)
	@echo "  LD      $@"
	$(Q)$(LD) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

clean:
	@rm -rf $(OBJDIR) $(EXE)

.PHONY: clean all
