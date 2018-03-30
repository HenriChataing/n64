/*
 * Copyright (c) 2018-2018 Prove & Run S.A.S
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
 * @date March 28th, 2018 (creation)
 * @copyright (c) 2018-2018, Prove & Run and/or its affiliates.
 *   All rights reserved.
 */

#ifndef _CIRCULAR_BUFFER_H_INCLUDED_
#define _CIRCULAR_BUFFER_H_INCLUDED_

#include <cstdlib>
#include <memory>

template <class T>
class circular_buffer
{
public:
    circular_buffer(size_t size)
        : _size(size), _buf(std::unique_ptr<T[]>(new T[size]))
    {}

    ~circular_buffer() {}

    void reset(void) {
        _head = _tail = 0;
    }

    bool empty(void) {
        return _head == _tail;
    }

    bool full(void) {
        return (_head + 1) % _size == _tail;
    }

    void put(T item)
    {
        _buf[_head] = item;
        _head = (_head + 1) % _size;

        if (_head == _tail)
            _tail = (_tail + 1) % _size;
    }

    T get(void)
    {
        if (empty())
            return T();

        T ret = _buf[_tail];
        _tail = (_tail + 1) % _size;
        return ret;
    }

private:
    size_t _head = 0;
    size_t _tail = 0;
    size_t _size;
    std::unique_ptr<T[]> _buf;
};

#endif /* _CIRCULAR_BUFFER_H_INCLUDED_ */
