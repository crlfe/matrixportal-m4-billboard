#ifndef BASE64ENCODER_HH_
#define BASE64ENCODER_HH_

#include <Arduino.h>

class Base64Encoder
{
  public:
    Base64Encoder(String &dst) : dst(dst)
    {
    }

    ~Base64Encoder()
    {
        flush();
    }

    void putc(int x)
    {
        buf[pos++] = (uint8_t)x;
        if (pos == sizeof(buf))
        {
            flush();
        }
    }

    void puts(const char *str)
    {
        if (str)
        {
            for (const char *p = str; *p; p++)
            {
                putc(*p);
            }
        }
    }

    void flush()
    {
        // Array has 65 elements because it includes the trailing NUL.
        static const char BASE64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789"
                                       "+/";

        if (pos > 0)
        {
            dst += BASE64[63u & (buf[0] >> 2)];
            dst += BASE64[63u & ((buf[0] << 4) | (buf[1] >> 4))];
            dst += pos > 0 ? BASE64[63u & ((buf[1] << 2) | (buf[2] >> 6))] : '=';
            dst += pos > 1 ? BASE64[63u & buf[2]] : '=';
        }

        bzero(buf, sizeof(buf));
        pos = 0;
    }

  private:
    String &dst;

    uint8_t buf[3];
    size_t pos = 0;
};

#endif // BASE64ENCODER_HH_
