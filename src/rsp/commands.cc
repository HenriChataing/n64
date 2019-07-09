
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "commands.h"

namespace RSP {

typedef int (*commandFunc)(char *, size_t, char *, size_t);

namespace Command {

int unsupported(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int reportHalted(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int continue_(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int step(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
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

int generalQuery(char *in, size_t inLen, char *out, size_t outLen)
{
    // Find the substring representing the query object : until the first
    // colon ':' or the fullstring.
    size_t commandLen = 0;
    for (; commandLen < inLen; commandLen++) {
        if (in[commandLen] == ':')
            break;
    }

    if (startsWith("qC", in, commandLen)) {
        // Return the current thread ID.
        // Always first thread since multiprocessing is disabled.
        return snprintf(out, outLen, "QC1");
    } else if (startsWith("qSupported", in, commandLen)) {
        // Give our working packet size, disable multiprocessing.
        return snprintf(out, outLen, "packetSize=%zu;multiprocess-", outLen);
    } else if (startsWith("qTStatus", in, commandLen)) {
        return 0;
    } else if (startsWith("qfThreadInfo", in, commandLen)) {
        // Query the active threads. Since no multiprocess, only one to return.
        return snprintf(out, outLen, "m1");
    } else if (startsWith("qsThreadInfo", in, commandLen)) {
        // Only one thread, reply with end of list.
        return snprintf(out, outLen, "l");
    } else if (startsWith("qAttached", in, commandLen)) {
        // The remote server (us) is always attached to the parent simulator
        // thread.
        return snprintf(out, outLen, "1");
    }

    std::cerr << "Unrecognized general query: " << in << std::endl;
    return 0;
}

int generalSet(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int readGeneralRegisters(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int writeGeneralRegisters(char *in, size_t inLen, char *out, size_t outLen)
{
    return 0;
}

int setThread(char *in, size_t inLen, char *out, size_t outLen)
{
    if (strncmp("Hg0", in, inLen) == 0 ||
        strncmp("Hg1", in, inLen) == 0) {
        // Indicate that register commands apply to a the selected thread only.
        // Multiprocess not supported; any thread is fine.
        return snprintf(out, outLen, "OK");
    } else if (strncmp("Hc-1", in, inLen) == 0) {
        // Indicate that subsequent 'continue' and 'step' operations apply
        // to all threads simultanously _ which is already the case.
        return snprintf(out, outLen, "OK");
    }

    std::cerr << "Unrecognized thread command: " << in << std::endl;
    return 0;
}

};

int handlePacket(char *in, size_t inLen, char *out, size_t outLen)
{
    if (inLen == 0) {
        // Dismiss empty packets.
        return 0;
    }

    switch (in[0]) {
        case '?': return Command::reportHalted(in, inLen, out, outLen);
        case 'c': return Command::continue_(in, inLen, out, outLen);
        case 'C': return Command::continue_(in, inLen, out, outLen);
        case 'g': return Command::readGeneralRegisters(in, inLen, out, outLen);
        case 'G': return Command::writeGeneralRegisters(in, inLen, out, outLen);
        case 'H': return Command::setThread(in, inLen, out, outLen);
        case 'q': return Command::generalQuery(in, inLen, out, outLen);
        case 'Q': return Command::generalSet(in, inLen, out, outLen);
        case 's': return Command::step(in, inLen, out, outLen);
        case 'S': return Command::step(in, inLen, out, outLen);
        default:  return Command::unsupported(in, inLen, out, outLen);
    }
}

};
