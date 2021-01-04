
#include <r4300/rsp.h>
#include <interpreter.h>

namespace R4300::RSP {

/**
 * @brief Fetch and interpret a single instruction from memory.
 * @return true if the instruction caused an exception
 */
void step()
{
    // Nothing done if RSP is halted.
    if (state.hwreg.SP_STATUS_REG & SP_STATUS_HALT)
        return;

    switch (state.rsp.nextAction) {
        case State::Action::Continue:
            state.rspreg.pc += 4;
            interpreter::rsp::eval();
            break;

        case State::Action::Delay:
            state.rspreg.pc += 4;
            state.rsp.nextAction = State::Action::Jump;
            interpreter::rsp::eval();
            break;

        case State::Action::Jump:
            state.rspreg.pc = state.rsp.nextPc;
            state.rsp.nextAction = State::Action::Continue;
            interpreter::rsp::eval();
            break;
    }
}

}; /* namespace R4300::RSP */
