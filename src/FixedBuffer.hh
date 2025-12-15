#ifndef FIXED_BUFFER_HH_
#define FIXED_BUFFER_HH_

#include <Arduino.h>

template <unsigned long kBufferSize>
class FixedBuffer final : public Print {
 public:
  FixedBuffer() {}

  virtual size_t write(uint8_t x) override {
    if (end_ >= kBufferSize) {
      setWriteError();
      return 0;
    }
    data_[end_] = x;
    end_ += 1;
    return 1;
  }

  virtual size_t write(const uint8_t* ptr, size_t size) override {
    if (end_ >= kBufferSize && size > 0) {
      setWriteError();
      return 0;
    }
    size_t n = min(remaining(), size);
    memcpy(end(), ptr, n);
    end_ += n;
    return n;
  }

  void advanceBegin(size_t size) {
    if (size >= kBufferSize || begin_ + size >= kBufferSize) {
      begin_ = kBufferSize;
      end_ = kBufferSize;
    } else {
      begin_ += size;
      if (begin_ > end_) {
        end_ = begin_;
      }
    }
  }

  void advanceEnd(size_t size) {
    if (size >= kBufferSize || end_ + size >= kBufferSize) {
      end_ = kBufferSize;
    } else {
      end_ += size;
    }
  }

  void clear() {
    clearWriteError();
    bzero(data_, kBufferSize);
    begin_ = 0;
    end_ = 0;
  }

  size_t size() const { return end_ - begin_; }

  size_t capacity() const { return kBufferSize; }

  size_t remaining() const { return kBufferSize - end_; }

  uint8_t* begin() { return data_ + begin_; }

  uint8_t* end() { return data_ + end_; }

  const uint8_t* begin() const { return data_ + begin_; }

  const uint8_t* end() const { return data_ + end_; }

  const uint8_t* cbegin() const { return data_ + begin_; }

  const uint8_t* cend() const { return data_ + end_; }

  uint8_t get(size_t pos) const {
    if (pos >= size()) {
      return '\0';
    }
    return data_[begin_ + pos];
  }

  void set(size_t pos, uint8_t x) {
    if (pos >= size()) {
      setWriteError();
    } else {
      data_[begin_ + pos] = x;
    }
  }

 private:
  uint8_t data_[kBufferSize];
  size_t begin_ = 0;
  size_t end_ = 0;
};

#endif  // FIXED_BUFFER_HH_
