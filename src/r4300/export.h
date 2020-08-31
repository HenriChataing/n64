
#ifndef _R4300_EXPORT_H_INCLUDED_
#define _R4300_EXPORT_H_INCLUDED_

#include <iostream>
#include <r4300/state.h>

namespace R4300 {

void serialize(std::ostream &os, const struct cpureg &reg);
void serialize(std::ostream &os, const struct cp0reg &reg);
void serialize(std::ostream &os, const struct cp1reg &reg);

void deserialize(std::istream &is, struct cpureg &reg);
void deserialize(std::istream &is, struct cp0reg &reg);
void deserialize(std::istream &is, struct cp1reg &reg);

}; /* R4300 */

#endif /* _R4300_EXPORT_H_INCLUDED_ */
