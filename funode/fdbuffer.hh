// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

template< unsigned int SizeT >
struct FdBuffer {
    enum { Size = SizeT };

    //

    char buffer[Size];
    unsigned int filled = 0;

    //

    unsigned int space() const { return Size - filled;  }
    bool empty() const         { return filled == 0;    }
    bool full() const          { return filled >= Size; }

    void consume(unsigned int bytes);
    unsigned int produce(const char* data,
                         unsigned int bytes);

    int read(int fd);
    int write(int fd);
};
