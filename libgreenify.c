#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif
#include "libgreenify.h"

#define EVENT_READ 0x01
#define EVENT_WRITE 0x02

static greenify_wait_callback_func_t g_wait_callback = NULL;

/* return 1 means the flags changed */
static int
set_nonblock(int fd, int *old_flags)
{
	*old_flags = fcntl(fd, F_GETFL, 0);
	if ((*old_flags) & O_NONBLOCK) {
		return 0;
	} else {
		fcntl(fd, F_SETFL, *old_flags | O_NONBLOCK);
		return 1;
	}
}

static void
restore_flags(int fd, int flags)
{
	fcntl(fd, F_SETFL, flags);
}

void greenify_set_wait_callback(greenify_wait_callback_func_t callback)
{
	g_wait_callback = callback;
}

int callback_single_watcher(int fd, int events)
{
	struct greenify_watcher watchers[1];

	assert(g_wait_callback != NULL);

	watchers[0].fd = fd;
	watchers[0].events = events;
	return g_wait_callback(watchers, 1, -1);
}

int
green_connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
	int flags_changed, flags, s_err;
	ssize_t retval;

	if (g_wait_callback == NULL || !set_nonblock(socket, &flags))
		return connect(socket, address, address_len);

	do {
		retval = connect(socket, address, address_len);
		s_err = errno;
	} while(retval < 0 && (s_err == EWOULDBLOCK || s_err == EALREADY || s_err == EINPROGRESS)
			&& !(retval = callback_single_watcher(socket, EVENT_READ)));

	restore_flags(socket, flags);
	errno = s_err;
	return retval;
}

ssize_t
green_read(int fildes, void *buf, size_t nbyte)
{
	int flags_changed, flags, s_err;
	ssize_t retval;

	if (g_wait_callback == NULL || !set_nonblock(fildes, &flags))
		return read(fildes, buf, nbyte);

	do {
		retval = read(fildes, buf, nbyte);
		s_err = errno;
	} while(retval < 0 && (s_err == EWOULDBLOCK || s_err == EAGAIN)
			&& !(retval = callback_single_watcher(fildes, EVENT_READ)));

	restore_flags(fildes, flags);
	errno = s_err;
	return retval;
}

ssize_t
green_write(int fildes, const void *buf, size_t nbyte)
{
	int flags, flags_changed, s_err;
	ssize_t retval;

	if (g_wait_callback == NULL || !set_nonblock(fildes, &flags))
		return write(fildes, buf, nbyte);

	do {
		retval = write(fildes, buf, nbyte);
		s_err = errno;
	} while(retval < 0 && (s_err == EWOULDBLOCK || s_err == EAGAIN)
			&& !(retval = callback_single_watcher(fildes, EVENT_WRITE)));

	restore_flags(fildes, flags);
	errno = s_err;
	return retval;
}

ssize_t
green_recv(int socket, void *buffer, size_t length, int flags)
{
	int sock_flags, sock_flags_changed, s_err;
	ssize_t retval;

	if (g_wait_callback == NULL || !set_nonblock(socket, &sock_flags))
		return recv(socket, buffer, length, flags);

	do {
		retval = recv(socket, buffer, length, flags);
		s_err = errno;
	} while(retval < 0 && (s_err == EWOULDBLOCK || s_err == EAGAIN)
			&& !(retval = callback_single_watcher(socket, EVENT_READ)));

	restore_flags(socket, sock_flags);
	errno = s_err;
	return retval;
}

ssize_t
green_send(int socket, const void *buffer, size_t length, int flags)
{
	int sock_flags, sock_flags_changed, s_err;
	ssize_t retval;

	if (g_wait_callback == NULL || !set_nonblock(socket, &sock_flags))
		return send(socket, buffer, length, flags);

	do {
		retval = send(socket, buffer, length, flags);
		s_err = errno;
	} while(retval < 0 && (s_err == EWOULDBLOCK || s_err == EAGAIN)
			&& !(retval = callback_single_watcher(socket, EVENT_WRITE)));

	restore_flags(socket, sock_flags);
	errno = s_err;
	return retval;
}

#ifdef HAVE_POLL
int
green_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
	int retval;
	int events = 0;
	struct pollfd *fd;

	if (g_wait_callback == NULL)
		return poll(fds, nfds, timeout);

	if (nfds != 1) {
		fprintf(stderr, "[greenify] currently only support 1 fd.  May block.\n");
		return poll(fds, nfds, timeout);
	}

	fd = &fds[0];

	if (fd->events & ~(POLLIN | POLLPRI | POLLOUT)) {
		fprintf(stderr, "[greenify] support POLLIN|POLLPRI|POLLOUT only, got 0x%x, may block.\n",
				fd->events);
		return poll(fds, nfds, timeout);
	}

	if (fd->events & POLLIN || fd->events & POLLPRI) {
		events |= EVENT_READ;
	}
	if (fd->events & POLLOUT) {
		events |= EVENT_WRITE;
	}

	callback_single_watcher(fd->fd, events, timeout);
	return poll(fds, nfds, 0);
}
#endif