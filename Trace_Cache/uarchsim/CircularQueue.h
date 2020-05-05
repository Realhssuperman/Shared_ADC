//
// Created by s117 on 2018-12-01.
// Modify from: https://github.com/embeddedartistry/embedded-resources/blob/master/examples/cpp/circular_buffer.cpp
//

#ifndef __CIRCULARQUEUE_H
#define __CIRCULARQUEUE_H

#include <memory>
#include <stdexcept>
#include <cassert>

template<typename T>
class CircularQueue {
 public:
  explicit CircularQueue(size_t size) :
      _buf(std::unique_ptr<T[]>(new T[size])),
      _max_size(size) {}

  size_t push(const T &item) {
    size_t curr_item_idx = _tail;

    if (full()) {
      throw std::runtime_error("Circular FIFO Overflow");
    }

    _buf[_tail] = item;

    if (_full) {
      _head = (_head + 1) % _max_size;
    }

    _tail = (_tail + 1) % _max_size;

    _full = _tail == _head;

    return curr_item_idx;
  }

  T pop() {

    if (empty()) {
      throw std::runtime_error("Circular FIFO Underflow");
    }

    //Read data and advance the head (we now have a free space)
    auto val = _buf[_head];
    _full = false;
    _head = (_head + 1) % _max_size;

    return val;
  }

  size_t head_idx() {
    return _head;
  }

  void restore_head_idx(size_t restore_head) {
    size_t old_size = size();
    _head = restore_head;
    _full = _tail == _head;
    size_t new_size = size();

    assert(old_size <= new_size);
  }

  void drop_newer(size_t pos) {
    do_bound_check(pos);
    size_t new_tail = (pos + 1) % _max_size;
    if (new_tail == _tail) {
      return;
    } else {
      _tail = new_tail;
      _full = false;
    }
  }

  size_t tail_idx() {
    return _tail;
  }

  void reset() {
    _tail = _head;
    _full = false;
  }

  bool empty() const {
    //if tail and head are equal, we are empty
    return (!_full && (_tail == _head));
  }

  bool full() const {
    //If head is ahead the tail by 1, we are full
    return _full;
  }

  size_t capacity() const {
    return _max_size;
  }

  size_t size() const {
    size_t size = _max_size;

    if (!_full) {
      if (_tail >= _head) {
        size = _tail - _head;
      } else {
        size = _max_size + _tail - _head;
      }
    }

    return size;
  }

  size_t available() const {
    return capacity() - size();
  }

  T &operator[](size_t n) {
    do_bound_check(n);
    return _buf[n];
  }

  const T &operator[](size_t n) const {
    do_bound_check(n);
    return _buf[n];
  }

  T &at(size_t n) {
    do_bound_check(n);
    return _buf[n];
  }

  const T &at(size_t n) const {
    do_bound_check(n);
    return _buf[n];
  }

 private:
  std::unique_ptr<T[]> _buf;
  size_t _tail = 0;
  size_t _head = 0;
  const size_t _max_size;
  bool _full = false;

  bool do_bound_check(size_t n) const {
    if (n >= _max_size) {
      throw std::out_of_range("Access out of bound");
    } else {
      if (!_full) {
        if (_tail >= _head) {
          if (!(_head <= n && n < _tail)) {
            throw std::out_of_range("Access out of valid range");
          }
        } else {
          if (!((_head <= n && n < _max_size) || (0 <= n && n < _tail))) {
            throw std::out_of_range("Access out of valid range");
          }
        }
      }
    }
    return true;
  }
};

#endif //__CIRCULARQUEUE_H
