#ifndef LTNT_TCPSOCK_H_INCLUDED
#define LTNT_TCPSOCK_H_INCLUDED

#include <stdbool.h>
#include <sys/socket.h>

// connectWithTimeout() errors
#define CONNECT_ERROR_FCNTL_GETFL -2
#define CONNECT_ERROR_FCNTL_SETNOBLK -3
#define CONNECT_ERROR_FCNTL_RESTOREBLK -4
#define CONNECT_ERROR_TIMEOUT -5
#define CONNECT_ERROR_IMMEDIATEFAILURE -6
#define CONNECT_ERROR_POLL -7
#define CONNECT_ERROR_STATUS_UNKNOWN -8
#define CONNECT_ERROR_ALREADY_CONNECTED -9
#define CONNECT_ERROR_INVALID_ARGUMENT -10

// Same function code as connectWithTimeout() included in LaTe 0.1.6-beta, with the addition of the possibility to pass 'false' as last argument
// and perform an "accept()" instead of a "connect()"
// "accept_sock" should be NULL when connect_or_accept is "true" (i.e. when a connect() operation is performed)
int connectWithTimeout2(int sockfd, const struct sockaddr *addr,socklen_t addrlen,int timeout_ms,bool connect_or_accept,int *accept_sock);
char *connectWithTimeoutStrError(int retval);

#endif