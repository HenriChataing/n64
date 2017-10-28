CC        := g++
LD        := g++

SRCDIR    := src
OBJDIR    := obj
BINDIR    := bin
EXE       := n64

CXXFLAGS  := -Wall -Wno-unused-function -m32 -std=c++11
CXXFLAGS  += -I$(SRCDIR) -I$(SRCDIR)
LDFLAGS   := -m32
LIBS      := -lpthread

ifneq ($(DEBUG),)
CXXFLAGS  += -g -pg -DDEBUG=$(DEBUG)
LDFLAGS   += -pg
endif

SRC       := main.cc memory.cc core.cc
SRC       += r4300/cpu.cc r4300/eval.cc r4300/mmu.cc

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
