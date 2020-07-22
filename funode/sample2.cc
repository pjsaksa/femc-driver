// Femc Driver
// Copyright (C) 2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

extern "C" {
#include "../dispatcher.h"
#include "../error_stack.h"
}

#include <iostream>

class CheeseBalls {
public:
    CheeseBalls()
    {
        fdd_add_timer([] (void* void_object, int)
                      {
                          if (CheeseBalls* object = static_cast<CheeseBalls*>( void_object );
                              object != nullptr)
                          {
                              object->cheese();
                              return object->toggleTimer();
                          }
                          return true;
                      },
                      this, 0, 1500, 1500);

        fdd_add_timer([] (void* void_object, int)
                      {
                          if (CheeseBalls* object = static_cast<CheeseBalls*>( void_object );
                              object != nullptr)
                          {
                              object->balls();
                              return object->toggleTimer();
                          }
                          return true;
                      },
                      this, 0, 5000, 5000);
    }

private:
    int cheese_calls_left = 10;

    //

    void cheese()
    {
        std::cout << "cheese " << std::flush;

        --cheese_calls_left;
    }

    void balls()
    {
        std::cout << "BALLS\n";
    }

    bool toggleTimer()
    {
        if (cheese_calls_left > 0) {
            return true;
        }
        else {
            fde_push_consistency_failure_id(fde_consistency_kill_recurring_timer);
            return false;
        }
    }
};

// ------------------------------------------------------------

int main()
{
    CheeseBalls cb1;

    fdd_main(FDD_INFINITE);
}
