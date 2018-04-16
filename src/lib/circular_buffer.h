
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
