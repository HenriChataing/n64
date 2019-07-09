
#ifndef _RSP_COMMANDS_H_INCLUDED_
#define _RSP_COMMANDS_H_INCLUDED_

#include <cstdint>
#include <cstdlib>

namespace RSP {

int handlePacket(char *in, size_t inLen, char *out, size_t outLen);

};

#endif /* _RSP_COMMANDS_H_INCLUDED_ */
