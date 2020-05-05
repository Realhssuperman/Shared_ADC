//
// Created by s117 on 1/30/19.
//

#include <assert.h>
#include "BitmapUtils.h"

uint64_t Bitmap::SetBit(uint8_t pos) {
  assert(pos < len);
  uint64_t mask = (((uint64_t) 1) << pos);

  if (!(bitmap & mask)) {
    avail -= 1;
  }

  bitmap = bitmap | mask;
  return mask;
}

void Bitmap::SetBitWithMask(uint64_t mask) {
  // TODO: WARN - haven't handle the case that there are multiple bits are 1 in the mask
  if (!(bitmap & mask)) {
    avail -= 1;
  }

  bitmap = bitmap | mask;
}

uint64_t Bitmap::UnsetBit(uint8_t pos) {
  assert(pos < len);
  uint64_t mask = (((uint64_t) 1) << pos);

  if (bitmap & mask) {
    avail += 1;
  }

  bitmap = bitmap & (~mask);
  return mask;
}

void Bitmap::UnsetBitWithMask(uint64_t mask) {
  // TODO: WARN - haven't handle the case that there are multiple bits are 1 in the mask
  if (bitmap & mask) {
    avail += 1;
  }

  bitmap = bitmap & (~mask);
}

bool Bitmap::TestBit(uint8_t pos) {
  assert(pos < len);
  uint64_t mask = (((uint64_t) 1) << pos);
  return (bitmap & mask) != 0;
}

bool Bitmap::TestBitWithMask(uint64_t mask) {
  return (bitmap & mask) != 0;
}

uint8_t Bitmap::GetFirstFreeBitPos(uint8_t start_pos) {
  return SearchFirstBitPos(0, start_pos);
}

uint64_t Bitmap::GetFirstFreeBitMask(uint8_t start_pos) {
  return SearchFirstBitPosMask(0, start_pos);
}

uint8_t Bitmap::GetFirstSetBitPos(uint8_t start_pos) {
  return SearchFirstBitPos(1, start_pos);
}

uint64_t Bitmap::GetFirstSetBitMask(uint8_t start_pos) {
  return SearchFirstBitPosMask(1, start_pos);
}

Bitmap::Bitmap(uint8_t len) {
  assert(len < 64);
  this->bitmap = 0;
  this->len = len;
  this->avail = len;
}

uint64_t Bitmap::GetBitmap() {
  return bitmap;
}

uint8_t Bitmap::GetAvail() {
  return this->avail;
}

Bitmap::Bitmap() {
  this->bitmap = 0;
  this->len = 0;
  this->avail = 0;
}

void Bitmap::ClearBitmap() {
  this->bitmap = 0;
  this->avail = len;
}

uint8_t Bitmap::SearchFirstBitPos(uint8_t search_target, uint8_t start_pos) {
  if (start_pos >= len) {
    return len;
  }

  uint64_t search_map = bitmap, search_mask = 1u << start_pos;
  uint8_t i;

  if (search_target == 0) {
    search_map = ~search_map;
  }

  for (i = start_pos; i < len; i++) {
    if (search_map & search_mask)
      break;
    search_mask = search_mask << 1;
  }
  return i;
}

uint64_t Bitmap::SearchFirstBitPosMask(uint8_t search_target, uint8_t start_pos) {
  if (start_pos >= len) {
    return len;
  }

  uint64_t search_map = bitmap, search_mask = 1u << start_pos;
  uint8_t i;

  if (search_target == 0) {
    search_map = ~search_map;
  }

  for (i = start_pos; i < len; i++) {
    if (search_map & search_mask)
      return search_mask;
    search_mask = search_mask << 1;
  }

  return 0;
}

