
#ifndef _CIRCULAR_BUFFER_H_INCLUDED_
#define _CIRCULAR_BUFFER_H_INCLUDED_

#include <iostream>

#include <cstdlib>
#include <memory>

template <class T>
class circular_buffer
{
public:
    circular_buffer(size_t size)
        : _head(0), _tail(0), _size(size),
          _buf(std::unique_ptr<T[]>(new T[size]))
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

    size_t length(void) const {
        return ((_head + _size) - _tail) % _size;
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

    T peek_front(size_t at) const
    {
        if (at >= length())
            throw std::out_of_range("peek out of bound access");

        size_t offset = (_head + _size - at - 1) % _size;
        return _buf[offset];
    }

    T peek_back(size_t at) const
    {
        if (at >= length())
            throw std::out_of_range("peek out of bound access");

        size_t offset = (_tail + at) % _size;
        return _buf[offset];
    }

private:
    size_t _head = 0;
    size_t _tail = 0;
    size_t _size;
    std::unique_ptr<T[]> _buf;
};

#endif /* _CIRCULAR_BUFFER_H_INCLUDED_ */
