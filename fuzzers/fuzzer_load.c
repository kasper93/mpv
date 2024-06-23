/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <unistd.h>

#include <libmpv/client.h>

#include "common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
#ifdef MPV_LOAD_CONFIG_FILE
    // config file size limit, see m_config_parse_config_file()
    if (size > 1000000000)
        return 0;
#endif

#ifdef MPV_LOAD_INPUT_CONF
    // input config file size limit, see parse_config_file() in input.c
    if (size > 1000000)
        return 0;
#endif

    // fmemopen doesn't have associated file descriptor, so we do copy.
    int fd = memfd_create("fuzz_mpv_load", 0);
    if (fd == -1)
        exit(1);
    ssize_t written = 0;
    while (written < size) {
        ssize_t result = write(fd, data + written, size - written);
        if (result == -1)
            exit(1);
        written += result;
    }
    if (lseek(fd, 0, SEEK_SET) != 0)
        exit(1);
    char filename[5 + 10 + 1];
    if (sprintf(filename, "fd://%d", fd) <= 5)
        exit(1);

    mpv_handle *ctx = mpv_create();
    if (!ctx)
        exit(1);

    check_error(mpv_set_option_string(ctx, "vo", "null"));
    check_error(mpv_set_option_string(ctx, "ao", "null"));
    check_error(mpv_set_option_string(ctx, "ao-null-untimed", "yes"));
    check_error(mpv_set_option_string(ctx, "untimed", "yes"));
    check_error(mpv_set_option_string(ctx, "video-osd", "no"));
    check_error(mpv_set_option_string(ctx, "msg-level", "all=trace"));
    check_error(mpv_set_option_string(ctx, "network-timeout", "1"));
#ifdef MPV_DEMUXER
    check_error(mpv_set_option_string(ctx, "demuxer", MPV_DEMUXER));
#endif

    check_error(mpv_initialize(ctx));

    const char *cmd[] = {"load" MPV_LOAD, filename, NULL};
    check_error(mpv_command(ctx, cmd));

#ifdef MPV_LOADFILE
    while (1) {
        mpv_event *event = mpv_wait_event(ctx, 10000);
        if (event->event_id == MPV_EVENT_IDLE)
            break;
    }
#endif

    mpv_terminate_destroy(ctx);
    close(fd);

    return 0;
}
