/*++
/* NAME
/*	sock_empty_wait 3
/* SUMMARY
/*	wait until socket send buffer is near empty
/* SYNOPSIS
/*	#include <iostuff.h>
/*
/*	int	sock_empty_wait(fd, timeout)
/*	int	fd;
/*	int	timeout;
/* AUXILIARY ROUTINES
/*	int	sock_maximize_send_lowat(fd)
/*	int	fd;
/*
/*	void	sock_set_send_lowat(fd, send_lowat)
/*	int	fd;
/*	int	send_lowat;
/* DESCRIPTION
/*	sock_empty_wait() blocks the process until the specified socket's
/*	send buffer is near empty, in the hope that the contents of the
/*	next write() operation will not be merged with preceding data.
/*
/*	sock_maximize_send_lowat() maximizes the socket send buffer
/*	low-water mark, which controls how much free buffer space must
/*	be available before the socket is considered writable.
/*	The result value is the old low-water mark value.
/*
/*	sock_set_send_lowat() sets the socket send buffer low-water mark
/*	to the specified value.
/*
/*	Arguments:
/* .IP fd
/*	File descriptor in the range 0..FD_SETSIZE.
/* .IP timeout
/*	If positive, deadline in seconds. A zero value effects a poll.
/*	A negative value means wait until something happens.
/* DIAGNOSTICS
/*	sock_maximize_send_lowat() returns -1 if the kernel does not
/*	support access to or control over the send buffer low-water mark.
/*	Otherwise, all system call errors are fatal.
/*
/*	A zero result means success.  When the specified deadline is
/*	exceeded, sock_empty_wait() returns -1 and sets errno to ETIMEDOUT.
/* BUGS
/*	Linux and Solaris do not provide the necessary support. Until
/*	they become better citizens they are punished by having to sleep
/*	for 10 seconds, and even then there is no reasonable way to find
/*	out if all the written data was acknowledged by the remote TCP.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

/* Utility library. */

#include <msg.h>
#include <iostuff.h>

/* sock_maximize_send_lowat - maximize the send buffer low-water mark */

int     sock_maximize_send_lowat(int fd)
{
    char   *myname = "sock_maximize_send_lowat";
    int     send_buffer_size;
    int     saved_low_water_mark;
    int     want_low_water_mark;
    int     got_low_water_mark;
    SOCKOPT_SIZE optlen;

    /*
     * Get the send buffer size. If this succeeds then we know the file
     * handle is a socket.
     */
    optlen = sizeof(send_buffer_size);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF,
		   (char *) &send_buffer_size, &optlen) < 0)
	msg_fatal("%s: getsockopt SO_SNDBUF: %m", myname);

    /*
     * Save the send buffer low-water mark. XXX Solaris 8 does not support
     * this operation.
     */
    optlen = sizeof(saved_low_water_mark);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDLOWAT,
		   (char *) &saved_low_water_mark, &optlen) < 0) {
	if (msg_verbose)
	    msg_info("%s: getsockopt SO_SNDLOWAT: %m", myname);
	return (-1);
    }

    /*
     * Max out the send buffer low-water mark. XXX Linux 2.4.14 does not
     * support this operation.
     */
    want_low_water_mark = send_buffer_size;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDLOWAT,
		   (char *) &want_low_water_mark,
		   sizeof(want_low_water_mark)) < 0) {
	if (msg_verbose)
	    msg_info("%s: setsockopt SO_SNDLOWAT: %m", myname);
	return (-1);
    }

    /*
     * Make sure the kernel did not cheat.
     */
    optlen = sizeof(got_low_water_mark);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDLOWAT,
		   (char *) &got_low_water_mark, &optlen) < 0)
	msg_fatal("%s: getsockopt SO_SNDLOWAT: %m", myname);
    if (got_low_water_mark == 1)
	return (-1);

    /*
     * Make debugging a bit easier.
     */
    if (msg_verbose) {
	msg_info("%s: send buffer %d, low-water mark was %d, wanted %d, got %d",
		 myname, send_buffer_size, saved_low_water_mark,
		 want_low_water_mark, got_low_water_mark);

    }
    return (saved_low_water_mark);
}

/* sock_set_send_lowat - restore socket send buffer low-water mark */

void    sock_set_send_lowat(int fd, int want_low_water_mark)
{
    char   *myname = "sock_set_send_lowat";
    int     got_low_water_mark;
    SOCKOPT_SIZE optlen;

    /*
     * Set the send buffer low-water mark.
     */
    if (setsockopt(fd, SOL_SOCKET, SO_SNDLOWAT,
		   (char *) &want_low_water_mark,
		   sizeof(want_low_water_mark)) < 0)
	msg_fatal("%s: setsockopt SO_SNDLOWAT: %m", myname);

    /*
     * Make debugging a bit easier.
     */
    if (msg_verbose) {
	optlen = sizeof(got_low_water_mark);
	if (getsockopt(fd, SOL_SOCKET, SO_SNDLOWAT,
		       (char *) &got_low_water_mark, &optlen) < 0)
	    msg_fatal("%s: getsockopt SO_SNDLOWAT: %m", myname);
	msg_info("%s: low-water mark wanted %d, got %d",
		 myname, want_low_water_mark, got_low_water_mark);
    }
}

/* sock_empty_wait - wait until socket send buffer is near empty */

int     sock_empty_wait(int fd, int timeout)
{
    int     saved_errno;
    int     saved_low_water_mark;
    int     result;

    /*
     * Max out the send buffer low-water mark.
     */
    saved_low_water_mark = sock_maximize_send_lowat(fd);

    /*
     * Wait until the socket is considered writable.
     */
    result = write_wait(fd, timeout);

    /*
     * Work around systems that have no functional SO_SNDLOWAT control.
     */
    if (result == 0 && saved_low_water_mark <= 0)
	sleep(10);

    /*
     * Restore the send buffer low-water mark.
     */
    if (saved_low_water_mark > 0) {
	saved_errno = errno;
	sock_set_send_lowat(fd, saved_low_water_mark);
	errno = saved_errno;
    }

    /*
     * Done.
     */
    return (result);
}
