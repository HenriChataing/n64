CC        := g++
LD        := g++

SRCDIR    := src
OBJDIR    := obj
BINDIR    := bin
EXE       := n64

CXXFLAGS  := -Wall -Wno-unused-function -std=c++11
CXXFLAGS  += -I$(SRCDIR) -I$(SRCDIR)/lib -DTARGET_BIGENDIAN
LDFLAGS   :=
LIBS      := -lpthread -lcurses

ifneq ($(DEBUG),)
CXXFLAGS  += -g -pg -DDEBUG=$(DEBUG)
LDFLAGS   += -pg
endif

SRC       := main.cc memory.cc core.cc terminal.cc debugger.cc
SRC       += r4300/cpu.cc r4300/cop0.cc r4300/cop1.cc r4300/hw.cc r4300/eval.cc r4300/mmu.cc
SRC       += mips/disas.cc rsp/server.cc rsp/commands.cc rsp/buffer.cc

OBJS      := $(patsubst %.cc,$(OBJDIR)/%.o, $(SRC))
DEPS      := $(patsubst %.cc,$(OBJDIR)/%.d, $(SRC))

Q = @

all: $(EXE)

-include $(DEPS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc
	@echo "  CXX     $*.cc"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CXXFLAGS) -c $< -o $@
	$(Q)$(CC) $(CXXFLAGS) -MT "$@" -MM $< > $(OBJDIR)/$*.d

$(EXE): $(OBJS)
	@echo "  LD      $@"
	$(Q)$(LD) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

clean:
	@rm -rf $(OBJDIR) $(EXE)

.PHONY: clean all
