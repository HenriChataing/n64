/*
 * Copyright (c) 2017-2017 Prove & Run S.A.S
 * All Rights Reserved.
 *
 * This software is the confidential and proprietary information of
 * Prove & Run S.A.S ("Confidential Information"). You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered
 * into with Prove & Run S.A.S
 *
 * PROVE & RUN S.A.S MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE
 * SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT. PROVE & RUN S.A.S SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING,
 * MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 */
/**
 * @file
 * @brief
 * @author Henri Chataing
 * @date July 04th, 2017 (creation)
 * @copyright (c) 2017-2017, Prove & Run and/or its affiliates.
 *   All rights reserved.
 */

#ifndef _TYPE_H_INCLUDED_
#define _TYPE_H_INCLUDED_

#include <cstdint>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned int uint;
typedef unsigned long ulong;

#define U64_2GB     (0x80000000llu)
#define U64_2_5GB   (0xa0000000llu)
#define U64_3GB     (0xc0000000llu)
#define U64_1TB     (0x10000000000llu)

#endif /* _TYPE_H_INCLUDED_ */
