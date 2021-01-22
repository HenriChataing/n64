
#include <r4300/controller.h>
#include <debugger.h>

namespace R4300 {

void extension_pak::read(uint16_t address, uint8_t data[32]) {
    (void)address;
    memset(data, 0, 32);
}

void extension_pak::write(uint16_t address, uint8_t data[32]) {
    (void)address;
    (void)data;
}

rumble_pak::~rumble_pak() {
}

/*
 * References:
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/RumblePak-Format.doc
 *
 * - 0x0000 - 0x07ff: return blocks of 0x00
 * - 0x8000 - 0x80ff: (test block)
 *      return 0x80 if last value written to the same range
 *      was non-zero, 0x00 otherwise
 * - 0xc000: (rumble register)
 *      [TODO untested] return rumble status
 */
void rumble_pak::read(uint16_t address, uint8_t data[32]) {
    switch (address) {
    case 0x8000:
        memset(data, test_value ? 0x80 : 0x00, 32);
        break;
    case 0xc000:
        memset(data, rumble_on, 32);
        break;
    default:
        debugger::warn(Debugger::SI,
            "Rumble PAK: read from unknown address {:04x}", address);
        memset(data, 0, 32);
        break;
    }
}

/*
 * References:
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/RumblePak-Format.doc
 *
 * - 0x0000 - 0x07ff: write ignored
 * - 0x8000 - 0x80ff: (test block)
 *      value written determine next read value
 * - 0xc000: (rumble register)
 *      0x0 / 0x1 switches rumble off / on
 */
void rumble_pak::write(uint16_t address, uint8_t data[32]) {
    switch (address) {
    case 0x8000:
        test_value = data[0];
        break;
    case 0xc000:
        rumble_on = data[0];
        break;
    default:
        debugger::warn(Debugger::SI,
            "Rumble PAK: write to unknown address {:04x}", address);
        break;
    }
}

/**
 * Blank controller mempaks contain the value 0xff.
 */
controller_pak::controller_pak() {
    memset(memory, 0xff, sizeof(memory));
}

controller_pak::~controller_pak() {
}

/**
 * References:
 * https://sites.google.com/site/consoleprotocols/home/nintendo-joy-bus-documentation/n64-specific/controller-accessories
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/MemPak-Format.doc
 */
void controller_pak::read(uint16_t address, uint8_t data[32]) {
    if (address < 0x8000) {
        memcpy(data, memory + address, 32);
    } else if (address < 0x80ff) {
        memcpy(data, test_block + (address - 0x8000), 32);
    } else {
        memset(data, 0, 32);
    }
}

/**
 * References:
 * https://sites.google.com/site/consoleprotocols/home/nintendo-joy-bus-documentation/n64-specific/controller-accessories
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/MemPak-Format.doc
 */
void controller_pak::write(uint16_t address, uint8_t data[32]) {
    if (address < 0x8000) {
        memcpy(memory + address, data, 32);
    } else if (address < 0x80ff) {
        memcpy(test_block + (address - 0x8000), data, 32);
    }
}

transfer_pak::~transfer_pak() {
}

/**
 * References:
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/Transfer%20Pak%20Stuff/Transfer%20Pak.txt
 */
void transfer_pak::read(uint16_t address, uint8_t data[32]) {
}

/**
 * References:
 * https://sourceforge.net/p/nragev20/code/HEAD/tree/docs/Transfer%20Pak%20Stuff/Transfer%20Pak.txt
 */
void transfer_pak::write(uint16_t address, uint8_t data[32]) {
}


controller::controller() :
    A(0), B(0), Z(0), start(0),
    direction_up(0), direction_down(0), direction_left(0), direction_right(0),
    L(0), R(0), camera_up(0), camera_down(0), camera_left(0), camera_right(0),
    direction_x(0), direction_y(0), mempak(NULL) {}

controller::~controller() {
    delete mempak;
}

void controller::set_mempak(struct extension_pak *mempak) {
    delete this->mempak;
    this->mempak = mempak;
}

}; /* namespace R4300 */
