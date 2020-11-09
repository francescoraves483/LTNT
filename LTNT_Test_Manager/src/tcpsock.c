#include "tcpsock.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

int connectWithTimeout2(int sockfd, const struct sockaddr *addr,socklen_t addrlen,int timeout_ms,bool connect_or_accept,int *accept_sock) {
	int sock_opt;
	int connect_rval=0;
	int poll_rval;
	struct pollfd socketMon;

	if(connect_or_accept==false) {
		if(accept_sock==NULL) {
			return CONNECT_ERROR_INVALID_ARGUMENT;
		}
		*accept_sock=0;
	}

	// Retrieve the socket descriptor flags
	sock_opt=fcntl(sockfd,F_GETFL,NULL);
	if(sock_opt<0) {
		return CONNECT_ERROR_FCNTL_GETFL;
	}

	// Set the non-blocking flag to the socket
	if (fcntl(sockfd,F_SETFL,sock_opt | O_NONBLOCK)<0) {
		return CONNECT_ERROR_FCNTL_SETNOBLK;
	}

	socketMon.fd=sockfd;
	socketMon.events=POLLOUT;
	socketMon.revents=0;

	if(connect_or_accept==true) {
		connect_rval=connect(sockfd,addr,addrlen);
	} else {
		if(addr!=NULL) {
			struct sockaddr accept_addr=*addr;
			*accept_sock=accept(sockfd,&accept_addr,addrlen==0 ? NULL : &addrlen);
		} else {
			*accept_sock=accept(sockfd,NULL,NULL);
		}
	}

	if(connect_rval<0 || *accept_sock<0) {
		// Check if the connect/accept operation is still in progress
		if(errno==EINPROGRESS) {
			// Run poll() multiple times if it was interrupted by some signal (i.e. if errno was set to EINTR)
			do {
				poll_rval=poll(&socketMon,1,timeout_ms);
			} while(errno==EINTR);

			// Check the poll() return value (=0: timeout occurred, <0: poll error, >0: success)
			if(poll_rval==0) {
				return CONNECT_ERROR_TIMEOUT;
			} else if(poll_rval<0) {
				return CONNECT_ERROR_POLL;
			} else {
				// If everything was ok, check if the connect()/accept() completed successfully, by looking at SO_ERROR, as reported in connect(2)
				socklen_t int_len=sizeof(int);
				int so_error_val;

              	if (getsockopt(sockfd,SOL_SOCKET,SO_ERROR,(void*)(&so_error_val),&int_len) < 0) { 
                	return CONNECT_ERROR_STATUS_UNKNOWN;
              	}

              	// The connect()/accept() operation did not complete successfully, return so_error_val, containing why this operation failed
              	if(so_error_val) {
              		return so_error_val;
              	} else {
              		connect_rval=0; // Everything is ok, as of now, as if a call to a blocking connect returned 0
              	}
			}
		} else if(errno==EISCONN) {
			// EISCONN means that the TCP socket is already connected
			return CONNECT_ERROR_ALREADY_CONNECTED;
		} else {
			// connect() immediately failed
			return CONNECT_ERROR_IMMEDIATEFAILURE;
		}
	}

	// If no error occurred, we can proceed in restoring the blocking flag of the socket
	if (fcntl(sockfd,F_SETFL,sock_opt) < 0) {
		return CONNECT_ERROR_FCNTL_RESTOREBLK;
	}

	return connect_rval;
}

char *connectWithTimeoutStrError(int retval) {
	switch(retval) {
		case CONNECT_ERROR_FCNTL_GETFL:
			return "fcntl() GETFL error";
			break;

		case CONNECT_ERROR_FCNTL_SETNOBLK:
			return "fcntl() set non-blocking socket failed";
			break;

		case CONNECT_ERROR_FCNTL_RESTOREBLK:
			return "fcntl() could not restore blocking state to socket";
			break;

		case CONNECT_ERROR_TIMEOUT:
			return "TCP connection timed out";
			break;

		case CONNECT_ERROR_IMMEDIATEFAILURE:
			return "TCP connection could not be established on first attempt";
			break;

		case CONNECT_ERROR_POLL:
			return strerror(retval);
			break;

		case CONNECT_ERROR_STATUS_UNKNOWN:
			return "Unknown connect()/accept() status after trying to connect/accept; cannot run getsockopt() to retrieve socket status";
			break;

		case CONNECT_ERROR_ALREADY_CONNECTED:
			return "TCP socket is already connected";
			break;

		case CONNECT_ERROR_INVALID_ARGUMENT:
			return "Invalid argument: unexpected NULL pointer";
			break;

		default:
			return strerror(retval);
			break;
	}

	return "This point should have never been reached; please report this bug to the LTNT developers";
}