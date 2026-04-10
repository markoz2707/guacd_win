/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifdef _WIN32
#include "config.win32.h"
#else
#include "config.h"
#endif
#include "move-fd.h"

#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include "win32-compat.h"
#else
/* Required for CMSG_* macros on BSD */
#define __BSD_VISIBLE 1
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <guacamole/error.h>

#ifdef _WIN32

int guacd_send_fd(int sock, int fd) {
    /* On Windows with thread model, threads share the same fd space.
     * Just send the fd number as an integer over the socket. */
    char marker = 'G';
    int total = 0;

    /* Send marker byte first */
    total = send(sock, &marker, 1, 0);
    if (total != 1) return 0;

    /* Send fd value */
    total = send(sock, (const char*)&fd, sizeof(fd), 0);
    return (total == sizeof(fd));
}

int guacd_recv_fd(int sock) {
    char marker;
    int fd;

    /* Receive marker byte */
    int total = recv(sock, &marker, 1, 0);
    if (total != 1 || marker != 'G')
        return -1;

    /* Receive fd value */
    total = recv(sock, (char*)&fd, sizeof(fd), 0);
    if (total != sizeof(fd))
        return -1;

    return fd;
}

#else
/* Original UNIX implementation below */

int guacd_send_fd(int sock, int fd) {

    struct msghdr message = {0};
    char message_data[] = {'G'};

    /* Assign data buffer */
    struct iovec io_vector[1];
    io_vector[0].iov_base = message_data;
    io_vector[0].iov_len  = sizeof(message_data);
    message.msg_iov    = io_vector;
    message.msg_iovlen = 1;

    /* Assign ancillary data buffer */
    char buffer[CMSG_SPACE(sizeof(fd))] = {0};
    message.msg_control = buffer;
    message.msg_controllen = sizeof(buffer);

    /* Set fields of control message header */
    struct cmsghdr* control = CMSG_FIRSTHDR(&message);
    control->cmsg_level = SOL_SOCKET;
    control->cmsg_type  = SCM_RIGHTS;
    control->cmsg_len   = CMSG_LEN(sizeof(fd));

    /* Add file descriptor to message data */
    memcpy(CMSG_DATA(control), &fd, sizeof(fd));

    /* Send file descriptor */
    ssize_t result;
    GUAC_RETRY_EINTR(result, sendmsg(sock, &message, 0));

    return (result == sizeof(message_data));

}

int guacd_recv_fd(int sock) {

    int fd;

    struct msghdr message = {0};
    char message_data[1];

    /* Assign data buffer */
    struct iovec io_vector[1];
    io_vector[0].iov_base = message_data;
    io_vector[0].iov_len  = sizeof(message_data);
    message.msg_iov    = io_vector;
    message.msg_iovlen = 1;

    /* Assign ancillary data buffer */
    char buffer[CMSG_SPACE(sizeof(fd))];
    message.msg_control = buffer;
    message.msg_controllen = sizeof(buffer);

    /* Receive file descriptor */
    ssize_t result;
    GUAC_RETRY_EINTR(result, recvmsg(sock, &message, 0));

    if (result == sizeof(message_data)) {

        /* Validate payload */
        if (message_data[0] != 'G') {
            errno = EPROTO;
            return -1;
        }

        /* Iterate control headers, looking for the sent file descriptor */
        struct cmsghdr* control;
        for (control = CMSG_FIRSTHDR(&message); control != NULL; control = CMSG_NXTHDR(&message, control)) {

            /* Pull file descriptor from data */
            if (control->cmsg_level == SOL_SOCKET && control->cmsg_type == SCM_RIGHTS) {
                memcpy(&fd, CMSG_DATA(control), sizeof(fd));
                return fd;
            }

        }

    } /* end if recvmsg() success */

    /* Failed to receive file descriptor */
    return -1;

}

#endif /* _WIN32 */

