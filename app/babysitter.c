/* Femc Driver - babysitter app
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "babysitter.h"

#include "../dispatcher.h"
#include "../error_stack.h"
#include "../utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

enum { this_error_context = fda_babysitter };

//

enum {
    OneBufferSize   = sizeof(struct inotify_event) + NAME_MAX + 1,
    BufferSize      = 15 * OneBufferSize,
};

typedef struct {
    fda_babysitter_public user;
    //
    fda_babysitter_process_callback alive;
    fda_babysitter_process_callback dead;
    //
    fdd_service_input grab_service;
    fdd_service_input drop_service;
    //
    int drop_fd;
} babysitter_service_t;

//

enum { NoEntry = 0xFFFFFFFFu };

// ------------------------------------------------------------

static uint32_t find_entry_by_name(const fda_babysitter_public* const user,
                                   const char* name)
{
    for (uint32_t i = 0;
         i < user->num_of_entries;
         ++i)
    {
        if (strncmp(name, user->entries[i].name, FdaBabysitter_NameSize) == 0)
            return i;
    }

    return NoEntry;
}

static uint32_t find_entry_by_wd(const fda_babysitter_public* const user,
                                 const int wd)
{
    if (wd < 0)
        return NoEntry;

    for (uint32_t i = 0;
         i < user->num_of_entries;
         ++i)
    {
        if (wd == user->entries[i].wd)
            return i;
    }

    return NoEntry;
}

static uint32_t store_file_entry(fda_babysitter_public* const user,
                                 const char* name)
{
    if (user->num_of_entries == FdaBabysitter_FileEntries) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return NoEntry;
    }

    fda_babysitter_file_entry* file_entry = &user->entries[user->num_of_entries];

    strncpy(file_entry->name, name, FdaBabysitter_NameSize);
    file_entry->pid = -1;
    file_entry->fd  = -1;
    file_entry->wd  = -1;

    return user->num_of_entries++;
}

static void clear_file_entry(fda_babysitter_public* const user,
                             uint32_t idx)
{
    if (idx >= user->num_of_entries) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return;
    }

    --user->num_of_entries;
    if (idx != user->num_of_entries) {
        memcpy(&user->entries[idx],
               &user->entries[user->num_of_entries],
               sizeof(fda_babysitter_file_entry));
    }
}

// ------------------------------------------------------------

static bool check_file_is_locked(fda_babysitter_file_entry* file_entry)
{
    int fd = file_entry->fd;

    if (fd < 0
        && (fd =open(file_entry->name, O_RDONLY|O_CLOEXEC)) < 0)
    {
        return false;
    }

    //

    file_entry->pid = -1;
    file_entry->fd  = -1;

    //

    struct flock flock;

    flock.l_type   = F_WRLCK;
    flock.l_whence = SEEK_SET;
    flock.l_start  = 0;
    flock.l_len    = 0;
    flock.l_pid    = 0;

    if (fcntl(fd, F_GETLK, &flock) < 0) {
        fde_push_stdlib_error("fcntl", errno);
        fdu_safe_close(fd);
        return false;
    }

    if (flock.l_type == F_UNLCK) {
        fdu_safe_close(fd);
        return false;
    }

    file_entry->pid = flock.l_pid;
    file_entry->fd = fd;
    return true;
}

// ------------------------------------------------------------

static bool drop_process(babysitter_service_t* const service,
                         const uint32_t idx,
                         const pid_t pid)
{
    if (inotify_rm_watch(service->drop_fd, service->user.entries[idx].wd) < 0)
        fde_push_stdlib_error("inotify_rm_watch", errno);

    //

    if (service->dead) {
        (*service->dead)(&service->user,
                         service->user.entries[idx].name,
                         pid);
    }

    clear_file_entry(&service->user, idx);
    return true;
}

// ------------------------------------------------------------

static bool handle_drop_event(babysitter_service_t* const service,
                              const struct inotify_event* const event)
{
    if (event->len
        || event->wd < 0)
    {
        return true;
    }

    uint32_t idx = find_entry_by_wd(&service->user, event->wd);

    if (idx >= service->user.num_of_entries)
        return true;    // already not being watched

    const pid_t pid = service->user.entries[idx].pid;

    return check_file_is_locked(&service->user.entries[idx])
        || drop_process(service, idx, pid);
}

// ------------------------------------------------------------

static bool refresh_file(babysitter_service_t* const service,
                         const char* name)
{
    uint32_t idx = find_entry_by_name(&service->user, name);

    if (idx < service->user.num_of_entries)
    {
        // already being watched

        const pid_t pid = service->user.entries[idx].pid;

        return check_file_is_locked(&service->user.entries[idx])
            || drop_process(service, idx, pid);
    }

    idx = store_file_entry(&service->user, name);

    if (idx == NoEntry) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    if (!check_file_is_locked(&service->user.entries[idx])) {
        clear_file_entry(&service->user, idx);
        return true;
    }

    int wd = -1;

    if ((wd =inotify_add_watch(service->drop_fd, service->user.entries[idx].name, IN_CLOSE)) < 0) {
        fde_push_stdlib_error("inotify_add_watch", errno);
        clear_file_entry(&service->user, idx);
        return false;
    }

    if (!check_file_is_locked(&service->user.entries[idx])) {
        inotify_rm_watch(service->drop_fd, wd);
        clear_file_entry(&service->user, idx);
        return true;
    }

    //

    service->user.entries[idx].wd = wd;

    if (service->alive) {
        (*service->alive)(&service->user,
                          service->user.entries[idx].name,
                          service->user.entries[idx].pid);
    }

    return true;
}

static bool handle_grab_event(babysitter_service_t* const service,
                              const struct inotify_event* const event)
{
    return (!event->len
            && event->len >= FdaBabysitter_NameSize
            && !*event->name)
        || refresh_file(service, event->name);
}

// ------------------------------------------------------------

typedef bool (*event_handler_func)(babysitter_service_t*,
                                   const struct inotify_event*);

static bool read_and_handle_event(babysitter_service_t* service,
                                  int fd,
                                  event_handler_func handler)
{
    char buffer[BufferSize]
        __attribute__ ((aligned(__alignof__(struct inotify_event))));

    const int i = read(fd, buffer, BufferSize);

    if (i > 0) {
        const char* event_ptr = buffer;
        const char* const event_end = &buffer[i];

        while (event_ptr < event_end)
        {
            const struct inotify_event* event = (const struct inotify_event*) event_ptr;
            event_ptr += sizeof(struct inotify_event) + event->len;

            if (!handler(service, event))
                return false;
        }

        FDE_ASSERT( event_ptr == event_end , "event_ptr != event_end" , false );

        return true;
    }
    else if (!i) {
        fde_push_message("fd closed");
        return false;
    }
    else /* if (i < 0) */
    {
        fde_push_stdlib_error("read", errno);
        return false;
    }
}

// ------------------------------------------------------------

static bool got_file_dropped_event(void* const service_v,
                                   const int fd)
{
    babysitter_service_t* const service = (babysitter_service_t* const) service_v;

    const fde_node_t* ectx = 0;

    return (ectx =fde_push_context(this_error_context))
        && read_and_handle_event(service, fd, &handle_drop_event)
        && fde_safe_pop_context(this_error_context, ectx);
}

static bool got_file_grabbed_event(void* const service_v,
                                   const int fd)
{
    babysitter_service_t* const service = (babysitter_service_t* const) service_v;

    const fde_node_t* ectx = 0;

    return (ectx =fde_push_context(this_error_context))
        && read_and_handle_event(service, fd, &handle_grab_event)
        && fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

static bool scan_directory(babysitter_service_t* service)
{
    const fde_node_t* ectx = 0;
    DIR* dir;
    struct dirent* dirent;
    struct stat st;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!(dir =opendir("."))) {
        fde_push_stdlib_error("opendir", errno);
        return false;
    }

    while ((dirent =readdir(dir)))
    {
        if (dirent->d_name[0] == '.')
            continue;

        if (stat(dirent->d_name, &st) != 0) {
            fprintf(FDD_ACTIVE_LOGFILE, "stat(\"%s\"): %s\n", dirent->d_name, strerror(errno));
            continue;
        }

        if (!S_ISREG(st.st_mode))       // skip non-regular files
            continue;

        if (!refresh_file(service, dirent->d_name)) {
            if (closedir(dir) < 0)
                fde_push_stdlib_error("closedir", errno);
            return false;
        }
    }

    if (closedir(dir) < 0) {
        fde_push_stdlib_error("closedir", errno);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}

// ------------------------------------------------------------

fda_babysitter_public* fda_babysitter_new_service(fda_babysitter_process_callback alive,
                                                  fda_babysitter_process_callback dead)
{
    if (OneBufferSize != 272    // if these change, make sure new values make sense
        || BufferSize != 4080)
    {
        fprintf(FDD_ACTIVE_LOGFILE, "babysitter: ASSERT: OneBufferSize == 272\n");
        fprintf(FDD_ACTIVE_LOGFILE, "babysitter: ASSERT: BufferSize == 4080\n");
        fprintf(FDD_ACTIVE_LOGFILE, "babysitter: OneBufferSize = %u, BufferSize = %u\n", OneBufferSize, BufferSize);
        abort();
    }

    //

    const fde_node_t* ectx = 0;

    if (!(ectx =fde_push_context(this_error_context)))
        return 0;

    babysitter_service_t* service = malloc(sizeof(babysitter_service_t));

    if (!service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        goto error_1;
    }

    // init basic fields

    service->alive = alive;
    service->dead = dead;
    service->user.num_of_entries = 0;

    //

    const int grab_fd = inotify_init1(IN_CLOEXEC);

    if (grab_fd < 0) {
        fde_push_stdlib_error("inotify_init/grab_fd", errno);
        goto error_2;
    }

    const int drop_fd = inotify_init1(IN_CLOEXEC);

    if (drop_fd < 0) {
        fde_push_stdlib_error("inotify_init/drop_fd", errno);
        goto error_3;
    }

    service->drop_fd = drop_fd;

    int wd = -1;

    if ((wd =inotify_add_watch(grab_fd, ".", IN_MODIFY)) < 0) {
        fde_push_stdlib_error("inotify_add_watch", errno);
        goto error_4;
    }

    //

    fdd_init_service_input(&service->grab_service, service, &got_file_grabbed_event);
    fdd_init_service_input(&service->drop_service, service, &got_file_dropped_event);

    if (fdd_add_input(grab_fd, &service->grab_service)
        && fdd_add_input(drop_fd, &service->drop_service)
        && scan_directory(service)
        && fde_safe_pop_context(this_error_context, ectx)
        )
    {
        return &service->user;
    }

    //
 error_4:
    fdu_safe_close(drop_fd);
 error_3:
    fdu_safe_close(grab_fd);
 error_2:
    free(service);
 error_1:
    return 0;
}
