
#ifndef __R4300_HW_INCLUDED__
#define __R4300_HW_INCLUDED__

#include <memory.h>

namespace R4300 {

extern Memory::AddressSpace physmem;

/**
 * @param Create physical address space.
 */
void init(std::string romFile);

};

#endif /* __R4300_HW_INCLUDED__ */
