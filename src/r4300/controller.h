
#ifndef _CONTROLLER_H_INCLUDED_
#define _CONTROLLER_H_INCLUDED_

#include <iostream>

#include "types.h"

namespace R4300 {

struct extension_pak {
    void read(uint16_t address, uint8_t data[32]);
    void write(uint16_t address, uint8_t data[32]);
};

struct rumble_pak: extension_pak {
    uint8_t test_value;
    bool rumble_on;

    ~rumble_pak();
    virtual void read(uint16_t address, uint8_t data[32]);
    virtual void write(uint16_t address, uint8_t data[32]);
};

struct transfer_pak: extension_pak {
    ~transfer_pak();
    virtual void read(uint16_t address, uint8_t data[32]);
    virtual void write(uint16_t address, uint8_t data[32]);
};

struct controller_pak: extension_pak {
    uint8_t memory[32768];
    uint8_t test_block[256];

    controller_pak();
    ~controller_pak();
    virtual void read(uint16_t address, uint8_t data[32]);
    virtual void write(uint16_t address, uint8_t data[32]);
};

struct controller {
    unsigned A : 1;
    unsigned B : 1;
    unsigned Z : 1;
    unsigned start : 1;
    unsigned direction_up : 1;
    unsigned direction_down : 1;
    unsigned direction_left : 1;
    unsigned direction_right : 1;
    unsigned : 2;
    unsigned L : 1;
    unsigned R : 1;
    unsigned camera_up : 1;
    unsigned camera_down : 1;
    unsigned camera_left : 1;
    unsigned camera_right : 1;
    signed direction_x : 8;
    signed direction_y : 8;

    struct extension_pak *mempak;

    controller();
    ~controller();
    void set_mempak(struct extension_pak *mempak);
};

};

#endif /* _CONTROLLER_H_INCLUDED_ */
