
#ifndef _R4300_EXPORT_H_INCLUDED_
#define _R4300_EXPORT_H_INCLUDED_

#include <r4300/state.h>

namespace R4300 {

void serializeCpuRegisters(FILE *of, struct cpureg &reg);
void serializeCp0Registers(FILE *of, struct cp0reg &reg);
void serializeCp1Registers(FILE *of, struct cp1reg &reg);

size_t serializedCpuRegistersSize(void);
size_t serializedCp0RegistersSize(void);
size_t serializedCp1RegistersSize(void);

void deserializeCpuRegisters(FILE *of, struct cpureg &reg);
void deserializeCp0Registers(FILE *of, struct cp0reg &reg);
void deserializeCp1Registers(FILE *of, struct cp1reg &reg);

}; /* R4300 */

#endif /* _R4300_EXPORT_H_INCLUDED_ */
