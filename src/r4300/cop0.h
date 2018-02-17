
#ifndef _COP0_H_INCLUDED_
#define _COP0_H_INCLUDED_

#include "cpu.h"

namespace R4300 {

extern Register Index;
extern Register Random;
extern Register EntryLo0;
extern Register EntryLo1;
extern Register Context;
extern Register PageMask;
extern Register Wired;
extern Register BadVAddr;
extern Register Count;
extern Register EntryHi;
extern Register Compare;
extern Register SR;
extern Register Cause;
extern Register EPC;
extern Register PrId;
extern Register Config;
extern Register LLAddr;
extern Register WatchLo;
extern Register WatchHi;
extern Register XContext;
extern Register PErr;
extern Register CacheErr;
extern Register TagLo;
extern Register TagHi;
extern Register ErrPC;

};

#endif /* _COP0_H_INCLUDED_ */
