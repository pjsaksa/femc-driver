/* Femc Driver - babysitter demo
 * Copyright (C) 2017-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_DEMOS_BABYSITTER_HEADER
#define FEMC_DRIVER_DEMOS_BABYSITTER_HEADER

#include <stdint.h>
#include <sys/types.h>

enum {
    Babysitter_NameSize    = 50,
    Babysitter_FileEntries = 30,
};

typedef struct {
    char name[Babysitter_NameSize];
    pid_t pid;
    int fd;
    int wd;
} file_entry_t;

typedef struct {
    file_entry_t entries[Babysitter_FileEntries];
    uint32_t num_of_entries;
} babysitter_public_t;

typedef void (*babysitter_callback)(const babysitter_public_t* api,
                                    const char* name,
                                    pid_t pid);

//

babysitter_public_t* new_babysitter_service(babysitter_callback alive,
                                            babysitter_callback dead);

#endif
