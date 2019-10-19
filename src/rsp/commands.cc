
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <r4300/state.h>

#include "buffer.h"
#include "commands.h"

namespace RSP {
namespace Command {

void unsupported(char *in, RSP::buffer &out)
{
}

void reportHalted(char *in, RSP::buffer &out)
{
}

void continue_(char *in, RSP::buffer &out)
{
}

void step(char *in, RSP::buffer &out)
{
}

static bool startsWith(char const *prefix, char const *str, size_t strLen)
{
    size_t prefixLen = strlen(prefix);
    if (strLen < prefixLen) {
        return false;
    }
    for (size_t i = 0; i < prefixLen; i++) {
        if (str[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

void generalQuery(char *in, RSP::buffer &out)
{
    // Find the substring representing the query object : until the first
    // colon ':' or the fullstring.
    size_t commandLen = 0;
    for (; in[commandLen] != '\0'; commandLen++) {
        if (in[commandLen] == ':')
            break;
    }

    if (startsWith("qC", in, commandLen)) {
        // Return the current thread ID.
        // Always first thread since multiprocessing is disabled.
        out.append("QC1");
    } else if (startsWith("qSupported", in, commandLen)) {
        // Give our working packet size, disable multiprocessing.
        out.append("packetSize=4096;multiprocess-");
    } else if (startsWith("qTStatus", in, commandLen)) {
    } else if (startsWith("qfThreadInfo", in, commandLen)) {
        // Query the active threads. Since no multiprocess, only one to return.
        out.append("m1");
    } else if (startsWith("qsThreadInfo", in, commandLen)) {
        // Only one thread, reply with end of list.
        out.append("l");
    } else if (startsWith("qAttached", in, commandLen)) {
        // The remote server (us) is always attached to the parent simulator
        // thread.
        out.append("1");
    } else {
        // Not supported. Default is to return an empty response.
        std::cerr << "Unrecognized general query: " << in << std::endl;
    }
}

void generalSet(char *in, RSP::buffer &out)
{
}

void readGeneralRegisters(char *in, RSP::buffer &out)
{
    for (size_t r = 0; r < 32; r++)
        out.appendU64(R4300::state.reg.gpr[r]);
    out.appendU64(R4300::state.cp0reg.sr);
    out.appendU64(R4300::state.reg.multLo);
    out.appendU64(R4300::state.reg.multHi);
    out.appendU64(R4300::state.cp0reg.badvaddr);
    out.appendU64(R4300::state.cp0reg.cause);
    out.appendU64(R4300::state.reg.pc);

    for (size_t nr = 0; nr < 100; nr++)
        out.appendU64(0);
}

void writeGeneralRegisters(char *in, RSP::buffer &out)
{
    out.append("E00");
}

void setThread(char *in, RSP::buffer &out)
{
    if (strcmp("Hg0", in) == 0 ||
        strcmp("Hg1", in) == 0) {
        // Indicate that register commands apply to a the selected thread only.
        // Multiprocess not supported; any thread is fine.
        out.append("OK");
    } else if (strcmp("Hc-1", in) == 0) {
        // Indicate that subsequent 'continue' and 'step' operations apply
        // to all threads simultanously _ which is already the case.
        out.append("OK");
    } else {
        // Not supported. Default is to return an empty response.
        std::cerr << "Unrecognized thread command: " << in << std::endl;
    }
}

};

int handlePacket(char *in, size_t inLen, char *out, size_t outLen)
{
    if (inLen == 0) {
        // Dismiss empty packets.
        return 0;
    }

    RSP::buffer buf((unsigned char *)out, outLen);

    switch (in[0]) {
        case '?': Command::reportHalted(in, buf); break;
        case 'c': Command::continue_(in, buf); break;
        case 'C': Command::continue_(in, buf); break;
        case 'g': Command::readGeneralRegisters(in, buf); break;
        case 'G': Command::writeGeneralRegisters(in, buf); break;
        case 'H': Command::setThread(in, buf); break;
        case 'q': Command::generalQuery(in, buf); break;
        case 'Q': Command::generalSet(in, buf); break;
        case 's': Command::step(in, buf); break;
        case 'S': Command::step(in, buf); break;
        default:  Command::unsupported(in, buf); break;
    }

    return buf.len;
}

};
