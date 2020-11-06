
#include <iostream>
#include <iomanip>
#include <cstring>

#include <r4300/rdp.h>
#include <r4300/hw.h>
#include <r4300/state.h>

#include <core.h>
#include <debugger.h>

namespace R4300 {

/*
 * Note: the game ROM is accessed in cartridge domain 1, address 2;
 * This domain is not represented here, as it is created as a special
 * ROM region.
 */

bool read_CART_1_1(uint bytes, u64 addr, u64 *value) {
    debugger::info(Debugger::Cart, "[cart 1:1] {:08x} -> ?", addr);
    *value = 0;
    return true;
}

bool write_CART_1_1(uint bytes, u64 addr, u64 value) {
    debugger::info(Debugger::Cart, "[cart 1:1] {:08x} <- {:08x}", addr, value);
    core::halt("Cart_1_1 write access");
    return true;
}

bool read_CART_1_3(uint bytes, u64 addr, u64 *value) {
    debugger::info(Debugger::Cart, "[cart 1:3] {:08x} -> ?", addr);
    *value = 0;
    return true;
}

bool write_CART_1_3(uint bytes, u64 addr, u64 value) {
    debugger::info(Debugger::Cart, "[cart 1:3] {:08x} <- {:08x}", addr, value);
    core::halt("Cart_1_3 write access");
    return true;
}

bool read_CART_2_1(uint bytes, u64 addr, u64 *value) {
    debugger::info(Debugger::Cart, "[cart 2:1] {:08x} -> ?", addr);
    *value = 0;
    return true;
}

bool write_CART_2_1(uint bytes, u64 addr, u64 value) {
    debugger::info(Debugger::Cart, "[cart 2:1] {:08x} <- {:08x}", addr, value);
    core::halt("Cart_2_1 write access");
    return true;
}

bool read_CART_2_2(uint bytes, u64 addr, u64 *value) {
    debugger::info(Debugger::Cart, "[cart 2:2] {:08x} -> ?", addr);
    *value = 0;
    return true;
}

bool write_CART_2_2(uint bytes, u64 addr, u64 value) {
    debugger::info(Debugger::Cart, "[cart 2:2] {:08x} <- {:08x}", addr, value);
    core::halt("Cart_2_2 write access");
    return true;
}

}; /* namespace R4300 */
