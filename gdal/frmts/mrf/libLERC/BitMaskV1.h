/*
Copyright 2015 Esri
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
A local copy of the license and additional notices are located with the
source distribution at:
http://github.com/Esri/lerc/
Contributors:  Thomas Maurer
               Lucian Plesea
*/

#ifndef BITMASKV1_H
#define BITMASKV1_H
#include "DefinesV1.h"
#include <vector>

NAMESPACE_LERC_START

/** BitMaskV1 - Convenient and fast access to binary mask bits
* includes RLE compression and decompression, in BitMaskV1.cpp
*
*/

class BitMaskV1
{
public:
    BitMaskV1(int nCols, int nRows) : m_nRows(nRows), m_nCols(nCols) {
        bits.resize(Size(), 0);
    }

    Byte  IsValid(int k) const { return (bits[k >> 3] & Bit(k)) != 0; }
    void  SetValid(int k) { bits[k >> 3] |= Bit(k); }
    void  SetInvalid(int k) { bits[k >> 3] &= ~Bit(k); }
    int   Size() const { return (m_nCols * m_nRows - 1) / 8 + 1; }

    // max RLE compressed size is n + 4 + 2 * (n - 1) / 32767
    // Returns encoded size
    int RLEcompress(Byte* aRLE) const;
    // current encoded size
    int RLEsize() const;
    // Decompress a RLE bitmask, bitmask size should be already set
    // Returns false if input seems wrong
    bool RLEdecompress(const Byte* src, size_t n);

private:
    int m_nRows, m_nCols;
    std::vector<Byte> bits;
    static Byte  Bit(int k) { return static_cast<Byte>(0x80 >> (k & 7)); }

    // Disable assignment op, default and copy constructor
    BitMaskV1();
    BitMaskV1(const BitMaskV1& copy);
    BitMaskV1& operator=(const BitMaskV1& m);
};

NAMESPACE_LERC_END
#endif
