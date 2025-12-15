#ifndef BASE64ENCODER_HH_
#define BASE64ENCODER_HH_

#include <Arduino.h>

class Base64Encoder : public Print {
 public:
  Base64Encoder(String& dst) : dst_(dst) {}

  ~Base64Encoder() {
    // The Base64Encoder is tiny, so will often be a short-lived
    // stack-allocated object while part of a message is being formatted.
    // To avoid the common (and hard to debug) mistake of forgetting to flush
    // the last few characters, we automatically flush when destroyed.
    flush();
  }

  virtual size_t write(uint8_t x) override {
    buf_[pos_++] = x;
    if (pos_ == sizeof(buf_)) {
      flush();
    }
    return 1;
  }

  virtual void flush() override {
    static const char BASE64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/";

    if (pos_ > 0) {
      char output[] = {
          BASE64[63u & (buf_[0] >> 2)],
          BASE64[63u & ((buf_[0] << 4) | (buf_[1] >> 4))],
          pos_ > 0 ? BASE64[63u & ((buf_[1] << 2) | (buf_[2] >> 6))] : '=',
          pos_ > 1 ? BASE64[63u & buf_[2]] : '=', '\0'};

      if (!dst_.concat(output)) {
        setWriteError();
      }
    }

    bzero(buf_, sizeof(buf_));
    pos_ = 0;
  }

 private:
  String& dst_;

  uint8_t buf_[3];
  size_t pos_ = 0;
};

#endif  // BASE64ENCODER_HH_
