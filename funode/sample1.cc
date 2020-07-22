// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

extern "C" {
#include "../dispatcher.h"
}

#include <iostream>

namespace
{
    bool cheese(void*, int)
    {
        std::cout << "cheese " << std::flush;
        return true;
    }

    bool balls(void*, int)
    {
        std::cout << "BALLS\n";
        return true;
    }
}

// ------------------------------------------------------------

int main()
{
    fdd_add_timer(cheese,
                  nullptr,
                  0,
                  1500,
                  1500);

    fdd_add_timer(balls,
                  nullptr,
                  0,
                  5000,
                  5000);

    fdd_main(FDD_INFINITE);
}
