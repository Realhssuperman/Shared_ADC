//
// Created by joshmosh on 11/26/2019.
//
#include <xinu.h>

void SetBit(uint32 A[], uint32 k) {
    A[k / 32] |= 1 << (k % 32);  // Set the bit at the k-th position in A[i]
}

void ClearBit(uint32 A[], uint32 k) {
    A[k / 32] &= ~(1 << (k % 32));
}

int TestBit(uint32 A[], uint32 k) {
    return ((A[k / 32] & (1 << (k % 32))) != 0);
}