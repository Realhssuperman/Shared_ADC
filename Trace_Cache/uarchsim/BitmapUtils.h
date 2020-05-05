//
// Created by s117 on 1/30/19.
//

#ifndef INC_721SIM_BITMAPUTILS_H
#define INC_721SIM_BITMAPUTILS_H

#include <stddef.h>
#include <stdint.h>

class Bitmap {
 public:
  Bitmap();

  explicit Bitmap(uint8_t len);

  uint64_t GetBitmap();

  void ClearBitmap();

  uint8_t GetAvail();

  // Index rule: MSB - 63, LSB - 0
  uint64_t SetBit(uint8_t pos);

  void SetBitWithMask(uint64_t mask);

  // Index rule: MSB - 63, LSB - 0
  uint64_t UnsetBit(uint8_t pos);

  void UnsetBitWithMask(uint64_t mask);

  // Index rule: MSB - 63, LSB - 0
  bool TestBit(uint8_t pos);

  bool TestBitWithMask(uint64_t mask);

  // Index rule: MSB - 63, LSB - 0
  uint8_t GetFirstFreeBitPos(uint8_t start_pos = 0);

  uint64_t GetFirstFreeBitMask(uint8_t start_pos = 0);

  // Index rule: MSB - 63, LSB - 0
  uint8_t GetFirstSetBitPos(uint8_t start_pos = 0);

  uint64_t GetFirstSetBitMask(uint8_t start_pos = 0);

 private:
  uint64_t bitmap;
  uint8_t len;
  uint8_t avail;

  uint8_t SearchFirstBitPos(uint8_t search_target, uint8_t start_pos = 0);

  uint64_t SearchFirstBitPosMask(uint8_t search_target, uint8_t start_pos = 0);
};

#endif //INC_721SIM_BITMAPUTILS_H
