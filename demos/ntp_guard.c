/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "ntp_guard.h"

#include "../dispatcher.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define NTP_DEBUG_PRINT(X)

#define NTP_POLL_INIT_INTERVAL  10000
#define NTP_POLL_INTERVAL       30000
#define NTP_POLL_FAST_INTERVAL  1000

#define NTP_RUN     "/etc/ntp/run"
#define NTP_IP      "/etc/ntp/ip"
#define NTP_NEXUS   "/etc/ntp/nexus"

#define NTP_BINARY  "/usr/sbin/ntpd"

#define NTP_RTC_UPDATE_INTERVAL (23*3600)
#define NTP_RTC_UPDATE_HOUR     0
#define NTP_RTC_UPDATE_COMMAND  "/sbin/hwclock -w -u"

static int ntp_guard_start_daemon_2(const char *address)
{
    int max_fd =open("/dev/null", O_WRONLY);
    if (max_fd >= 0) close(max_fd);
    else max_fd =FD_SETSIZE;

    int pid;

    fflush(stdin);
    fflush(stdout);
    fflush(stderr);

    pid =fork();
    if (pid < 0) {
        perror("ntp-guard:fork");
        return 0;
    }
    else if (!pid) // child
    {
        chdir("/");
        umask(0);

        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        while (--max_fd > 2)
            close(max_fd);

        if (address && address[0])
        {
            // "ntpd -n -l -p address"

            execl(NTP_BINARY,
                  NTP_BINARY,
                  "-n",
                  "-l",
                  "-p",
                  address,
                  (char *)0);
        }
        else {
            // "ntpd -n -l"

            execl(NTP_BINARY,
                  NTP_BINARY,
                  "-n",
                  "-l",
                  (char *)0);
        }

        exit(2);
    }

    return pid;
}

static int ntp_guard_start_daemon(const char *filename)
{
    enum { BufferSize =80 };

    char address[BufferSize +1];
    int bytes =0;
    int i;
    int fd =open(filename, O_RDONLY);

    if (fd < 0)
        return 0;

    while ((i =read(fd, &address[bytes], BufferSize-bytes)) > 0)
        bytes +=i;
    close(fd);

    if (i < 0)  // read error
        return 0;

    // scan address for incompatible characters

    for (i=0; i<bytes; ++i)
    {
        switch (address[i]) {
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '0' ... '9':
        case '_':
            break;

        case '.':   // '-' and '.' not allowed at first
        case '-':
            if (i>0)
                continue;
            // fall
        default:
            goto character_scan_finished;
        }
    }

 character_scan_finished:
    if (i < 3)  // too short
        return 0;
    address[i] =0;

    return ntp_guard_start_daemon_2(address);
}

// *********************************************************

typedef struct {
    time_t last_access;
    time_t last_rtc_update;
    int listen_pid;
    bool listen_term_sent;
} ntp_guard_service;

// *****

static ntp_guard_service *ntp_guard_new_service(void)
{
    ntp_guard_service *service =malloc(sizeof(ntp_guard_service));

    service->last_access      =0;
    service->last_rtc_update  =0;
    service->listen_pid       =0;
    service->listen_term_sent =false;

    return service;
}

// *********************************************************

static void ntp_guard_poller(ntp_guard_service *service, int pid)
{
    if (pid
        && waitpid(pid, 0, WNOHANG) == pid)
    {
        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: pid %d is dead\n", pid);)

        pid =0;
    }

    if (service
        && service->listen_pid
        && waitpid(service->listen_pid, 0, WNOHANG) == service->listen_pid)
    {
        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: (listen) pid %d is dead\n", service->listen_pid);)

        service->listen_pid =0;
        service->listen_term_sent =false;
    }

    // *****

    const time_t now            =time(0);
    const bool daemon_running   =(pid != 0);
    const bool runfile_exists   =(access(NTP_RUN, F_OK) == 0);
    const bool ipfile_exists    =(access(NTP_IP, F_OK) == 0);
    struct stat st;

    if (daemon_running != runfile_exists)
    {
        if (runfile_exists)
        {
            fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: start daemon\n");

            if (service && service->listen_pid)
            {
                NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: stop listen\n");)

                if (!service->listen_term_sent) {
                    NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: (listen) killing pid %d\n", service->listen_pid);)

                    kill(service->listen_pid, SIGTERM);
                    service->listen_term_sent =true;
                }
                else {
                    if (kill(service->listen_pid, SIGKILL) < 0)
                    {
                        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: (listen) KILLing pid %d\n", service->listen_pid);)

                        service->listen_pid =0;
                        service->listen_term_sent =false;
                    }
                }

                fdd_add_timer((fdd_notify_func)ntp_guard_poller,
                              service, pid,
                              NTP_POLL_FAST_INTERVAL, 0);
                return;
            }

            pid =ntp_guard_start_daemon(ipfile_exists ? NTP_IP : NTP_NEXUS);

            NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: started pid %d\n", pid);)

            if (service)
                service->last_access =ipfile_exists ? now : 0;
        }
        else {
            fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: stop daemon\n");
            NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: killing pid %d\n", pid);)

            kill(pid, SIGTERM);
        }
    }
    else if (service
             && daemon_running
             //
             && ((ipfile_exists         // ipfile added
                  && !service->last_access)
                 || (!ipfile_exists     // ipfile removed
                     && service->last_access)
                 || (ipfile_exists      // ipfile changed
                     && service->last_access
                     && stat(NTP_IP, &st) != 0
                     && st.st_mtime > service->last_access)
                 )
             )
    {
        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: ipfile changed, killing pid %d\n", pid);)

        kill(pid, SIGTERM);
    }
    else if (service
             && now - service->last_rtc_update > NTP_RTC_UPDATE_INTERVAL)
    {
        struct tm tm;
        localtime_r(&now, &tm);

        if (tm.tm_hour == NTP_RTC_UPDATE_HOUR) {
            service->last_rtc_update =now;
            //

            system(NTP_RTC_UPDATE_COMMAND);

            NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: RTC updated\n");)
        }
    }
    else if (service
             && !runfile_exists
             && !daemon_running
             && !service->listen_pid)
    {
        service->listen_pid =ntp_guard_start_daemon_2(0);

        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: start listen\n");)
        NTP_DEBUG_PRINT(fprintf(FDD_ACTIVE_LOGFILE, "NTP debug: (listen) started pid %d\n", service->listen_pid);)
    }

    //

    fdd_add_timer((fdd_notify_func)ntp_guard_poller,
                  service,
                  pid,
                  NTP_POLL_INTERVAL, 0);
}

// *********************************************************

void ntp_guard_start(void)
{
    fprintf(FDD_ACTIVE_LOGFILE, "starting NTP-guard\n");

    fdd_add_timer((fdd_notify_func)ntp_guard_poller,
                  ntp_guard_new_service(),
                  0,
                  NTP_POLL_INIT_INTERVAL, 0);
}
