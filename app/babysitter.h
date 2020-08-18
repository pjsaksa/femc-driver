// Femc Driver - babysitter app
// Copyright (C) 2017-2020 Pauli Saksa
//
// Licensed under The MIT License, see file LICENSE.txt in this source tree.

#pragma once

#include <stdint.h>
#include <sys/types.h>

enum {
    FdaBabysitter_NameSize    = 50,
    FdaBabysitter_FileEntries = 30,
};

typedef struct {
    char name[FdaBabysitter_NameSize];
    pid_t pid;
    int fd;
    int wd;
} fda_babysitter_file_entry;

typedef struct {
    fda_babysitter_file_entry entries[FdaBabysitter_FileEntries];
    uint32_t num_of_entries;
} fda_babysitter_public;

typedef void (*fda_babysitter_process_callback)(const fda_babysitter_public* api,
                                                const char* name,
                                                pid_t pid);

//

fda_babysitter_public* fda_babysitter_new_service(fda_babysitter_process_callback alive,
                                                  fda_babysitter_process_callback dead);
