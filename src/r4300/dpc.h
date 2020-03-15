
#ifndef _R4300_DPC_H_INCLUDED_
#define _R4300_DPC_H_INCLUDED_

#include <types.h>

namespace R4300 {

void write_DPC_STATUS_REG(u32 value);
void write_DPC_START_REG(u32 value);
void write_DPC_END_REG(u32 value);

};

#endif /* _R4300_DPC_H_INCLUDED_ */
