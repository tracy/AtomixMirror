/*****************************************************************
|
|      File: AtxBsdSockets.h
|
|      Atomix - Sockets: BSD Implementation
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#if defined(WIN32)
#define STRICT
#define ATX_WIN32_USE_WINSOCK2
#ifdef ATX_WIN32_USE_WINSOCK2
/* it is important to include this in this order, because winsock.h and ws2tcpip.h */
/* have different definitions for the same preprocessor symbols, such as IP_ADD_MEMBERSHIP */
#include <winsock2.h>
#include <ws2tcpip.h> 
#else
#include <winsock.h>
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#endif /* defined(WIN32) */

#include "AtxConfig.h"
#include "AtxInterfaces.h"
#include "AtxReferenceable.h"
#include "AtxDestroyable.h"
#include "AtxTypes.h"
#include "AtxResults.h"
#include "AtxStreams.h"
#include "AtxSockets.h"
#include "AtxUtils.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define ATX_TCP_SERVER_SOCKET_DEFAULT_LISTEN_COUNT  20

/*----------------------------------------------------------------------
|       WinSock adaptation layer
+---------------------------------------------------------------------*/
#if defined(WIN32)
#define EWOULDBLOCK  WSAEWOULDBLOCK
#define EINPROGRESS  WSAEINPROGRESS
#define ECONNREFUSED WSAECONNREFUSED
#define ECONNRESET   WSAECONNRESET
#define ETIMEDOUT    WSAETIMEDOUT
#define ENETRESET    WSAENETRESET
#define EADDRINUSE   WSAEADDRINUSE
#define ENETDOWN     WSAENETDOWN
#define ENETUNREACH  WSAENETUNREACH
#define EAGAIN       WSAEWOULDBLOCK
#define EINTR		 WSAEINTR
#define ATX_BSD_INVALID_SOCKET INVALID_SOCKET
#define ATX_BSD_SOCKET_ERROR SOCKET_ERROR
typedef int    ssize_t;
typedef int    socklen_t;
typedef char*  SocketBuffer;
typedef const char*  SocketConstBuffer;
typedef char*  SocketOption;
typedef SOCKET SocketFd;
#define GetSocketError() WSAGetLastError()
static ATX_Boolean WinsockInitialized = ATX_FALSE;
#else /* defined(WIN32) */
typedef void*  SocketBuffer;
typedef const void*  SocketConstBuffer;
typedef void*  SocketOption;
typedef int    SocketFd;
#define GetSocketError() errno
#define closesocket  close
#define ioctlsocket ioctl
#define ATX_BSD_INVALID_SOCKET (-1)
#define ATX_BSD_SOCKET_ERROR   (-1)
#endif /* defined(WIN32) */

/*----------------------------------------------------------------------
|       types
+---------------------------------------------------------------------*/
typedef struct {
    ATX_Cardinal reference_count;
    SocketFd     fd;
} BsdSocketFdWrapper;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(ATX_InputStream);
    ATX_IMPLEMENTS(ATX_OutputStream);
    ATX_IMPLEMENTS(ATX_Referenceable);
    
    /* members */
    ATX_Cardinal        reference_count;
    BsdSocketFdWrapper* socket_ref;
} BsdSocketStream;

typedef struct {
    /* interfaces */
    ATX_IMPLEMENTS(ATX_Socket);
    ATX_IMPLEMENTS(ATX_Destroyable);

    BsdSocketFdWrapper* socket_ref;
    ATX_SocketInfo      info;
} BsdSocket;

typedef struct {
    /* base class */
    ATX_EXTENDS(BsdSocket);

    /* interfaces */
    ATX_IMPLEMENTS(ATX_ServerSocket);

    /* members */
    unsigned int max_clients;
} BsdTcpServerSocket;

typedef BsdSocket BsdTcpClientSocket;

typedef struct {
    /* base class */
    ATX_EXTENDS(BsdSocket);

    /* interfaces */
    ATX_IMPLEMENTS(ATX_DatagramSocket);
} BsdUdpSocket;

/*----------------------------------------------------------------------
|       BsdSockets_Init
+---------------------------------------------------------------------*/
static ATX_Result
BsdSockets_Init(void)
{
    if (WinsockInitialized == ATX_FALSE) {
#if defined(WIN32)
		WORD    wVersionRequested;
		WSADATA wsaData;
		wVersionRequested = MAKEWORD( 2, 0 );
		if (WSAStartup( wVersionRequested, &wsaData ) != 0) {
			return ATX_FAILURE;
		}
		WinsockInitialized = ATX_TRUE;
	}
#endif
	return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   MapErrorCode
+---------------------------------------------------------------------*/
static ATX_Result 
MapErrorCode(int error)
{
    switch (error) {
        case ECONNRESET:
        case ENETRESET:
            return ATX_ERROR_CONNECTION_RESET;

        case ECONNREFUSED:
            return ATX_ERROR_CONNECTION_REFUSED;

        case ETIMEDOUT:
            return ATX_ERROR_TIMEOUT;

        case EADDRINUSE:
            return ATX_ERROR_ADDRESS_IN_USE;

        case ENETDOWN:
            return ATX_ERROR_NETWORK_DOWN;

        case ENETUNREACH:
            return ATX_ERROR_NETWORK_UNREACHABLE;

        case EINPROGRESS:
        case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        case EWOULDBLOCK:
#endif
            return ATX_ERROR_WOULD_BLOCK;

        default:
            return ATX_FAILURE;
    }
}

/*----------------------------------------------------------------------
|       SocketAddressToInetAddress
+---------------------------------------------------------------------*/
static void
SocketAddressToInetAddress(const ATX_SocketAddress* socket_address, 
                           struct sockaddr_in*      inet_address)
{
    /* initialize the structure */
    int i;
    for (i=0; i<8; i++) inet_address->sin_zero[i]=0;

    /* setup the structure */
    inet_address->sin_family = AF_INET;
    inet_address->sin_port = htons(socket_address->port);
    inet_address->sin_addr.s_addr = htonl(ATX_IpAddress_AsLong(&socket_address->ip_address));
}

/*----------------------------------------------------------------------
|       InetAddressToSocketAddress
+---------------------------------------------------------------------*/
static void
InetAddressToSocketAddress(const struct sockaddr_in* inet_address,
                           ATX_SocketAddress*        socket_address)
{
    socket_address->port = ntohs(inet_address->sin_port);
    ATX_IpAddress_SetFromLong(&socket_address->ip_address, 
                              ntohl(inet_address->sin_addr.s_addr));
}

/*----------------------------------------------------------------------
|       ResolveName
+---------------------------------------------------------------------*/
ATX_Result
ATX_IpAddress_ResolveName(ATX_IpAddress* address,
                          const char*    name, 
                          ATX_Timeout    timeout)
{
    /* make sure the TCP/IP stack is initialized */
    ATX_CHECK(BsdSockets_Init());

    /* get rid of compiler warnings */
    timeout = timeout;

    /* check parameters */
    if (name == NULL || name[0] == '\0') return ATX_ERROR_HOST_UNKNOWN;

    /* handle numerical addrs */
    {
        ATX_IpAddress numerical_address;
        if (ATX_SUCCEEDED(ATX_IpAddress_Parse(&numerical_address, name))) {
            /* the name is a numerical IP addr */
            ATX_IpAddress_Copy(address, &numerical_address);
            return ATX_SUCCESS;
        }
    }

    /* do a name lookup */
    {
        struct hostent *host_entry;
        host_entry = gethostbyname(name);
        if (host_entry == NULL ||
            host_entry->h_addrtype != AF_INET) {
            return ATX_ERROR_HOST_UNKNOWN;
        }
        ATX_CopyMemory(address, host_entry->h_addr, 4);
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketFdWrapper_Create
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocketFdWrapper_Create(SocketFd fd, BsdSocketFdWrapper** wrapper)
{
    /* allocate a new object */
    (*wrapper) = ATX_AllocateZeroMemory(sizeof(BsdSocketFdWrapper));
    if (*wrapper == NULL) return ATX_ERROR_OUT_OF_MEMORY;

    /* construct the object */
    (*wrapper)->fd = fd;
    (*wrapper)->reference_count = 1;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketFdWrapper_Destroy
+---------------------------------------------------------------------*/
static void
BsdSocketFdWrapper_Destroy(BsdSocketFdWrapper* self)
{
    closesocket(self->fd);
    ATX_FreeMemory((void*)self);
}

/*----------------------------------------------------------------------
|       BsdSocketFdWrapper_AddReference
+---------------------------------------------------------------------*/
static void
BsdSocketFdWrapper_AddReference(BsdSocketFdWrapper* self)
{
    ++self->reference_count;
}

/*----------------------------------------------------------------------
|       BsdSocketFdWrapper_Release
+---------------------------------------------------------------------*/
static void
BsdSocketFdWrapper_Release(BsdSocketFdWrapper* self)
{
    if (self == NULL) return;
    if (--self->reference_count == 0) {
        BsdSocketFdWrapper_Destroy(self);
    }
}

/*----------------------------------------------------------------------
|       BsdSocketStream_Create
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocketStream_Create(BsdSocketFdWrapper* socket_ref, BsdSocketStream** stream)
{
    /* create a new object */
    (*stream) = (BsdSocketStream*)ATX_AllocateMemory(sizeof(BsdSocketStream));
    if (*stream == NULL) return ATX_ERROR_OUT_OF_MEMORY;

    /* construct the object */
    (*stream)->reference_count = 1;
    (*stream)->socket_ref = socket_ref;

    /* keep a reference */
    BsdSocketFdWrapper_AddReference(socket_ref);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BsdSocketStream, ATX_InputStream);
ATX_DECLARE_INTERFACE_MAP(BsdSocketStream, ATX_OutputStream);
ATX_DECLARE_INTERFACE_MAP(BsdSocketStream, ATX_Referenceable);

/*----------------------------------------------------------------------
|       BsdSocketInputStream_Create
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocketInputStream_Create(BsdSocketFdWrapper* socket_ref, 
                            ATX_InputStream**   stream)
{ 
    BsdSocketStream* socket_stream = NULL;
    ATX_Result       result;

    /* create the object */
    result = BsdSocketStream_Create(socket_ref, &socket_stream);
    if (ATX_FAILED(result)) {
        *stream = NULL;
        return result;
    }

    /* set the interface */
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_InputStream);
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_OutputStream);
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_Referenceable);
    *stream = &ATX_BASE(socket_stream, ATX_InputStream);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketOuputStream_Create
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocketOutputStream_Create(BsdSocketFdWrapper* socket_ref, 
                             ATX_OutputStream**  stream)
{ 
    BsdSocketStream* socket_stream = NULL;
    ATX_Result       result;

    /* create the object */
    result = BsdSocketStream_Create(socket_ref, &socket_stream);
    if (ATX_FAILED(result)) {
        *stream = NULL;
        return result;
    }

    /* set the interface */
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_InputStream);
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_OutputStream);
    ATX_SET_INTERFACE(socket_stream, BsdSocketStream, ATX_Referenceable);
    *stream = &ATX_BASE(socket_stream, ATX_OutputStream);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketStream_Destroy
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocketStream_Destroy(BsdSocketStream* self)
{
    BsdSocketFdWrapper_Release(self->socket_ref);
    ATX_FreeMemory((void*)self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketStream_Read
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketStream_Read(ATX_InputStream* _self,
                     ATX_Any          buffer, 
                     ATX_Size         bytes_to_read, 
                     ATX_Size*        bytes_read)
{
    BsdSocketStream* self = ATX_SELF(BsdSocketStream, ATX_InputStream);
    ssize_t          nb_read;
    int              watchdog = 5000;

    do {
        nb_read = recv(self->socket_ref->fd, 
                       buffer, 
                       (ssize_t)bytes_to_read, 
                       0);
        if (nb_read > 0) {
            if (bytes_read) *bytes_read = nb_read;
            return ATX_SUCCESS;
        } else {
            if (bytes_read) *bytes_read = 0;
            if (nb_read == 0) {
                return ATX_ERROR_EOS;
            } else {
                int error = GetSocketError();
                /* on linux, EAGAIN can be returned for UDP sockets */
                /* when the checksum fails                          */
                if (error == EAGAIN) {
                    continue;
                }

                /* dont return an error if we get interrupted */
                if (error != EINTR) {
                    return MapErrorCode(error);
                } 
            }
        }
    } while (watchdog--);

    if (bytes_read) *bytes_read = 0;
    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|       BsdSocketStream_Write
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketStream_Write(ATX_OutputStream* _self,
                      ATX_AnyConst      buffer, 
                      ATX_Size          bytes_to_write, 
                      ATX_Size*         bytes_written)
{
    BsdSocketStream* self = ATX_SELF(BsdSocketStream, ATX_OutputStream);
    ssize_t          nb_written;

    nb_written = send(self->socket_ref->fd, 
                      (SocketConstBuffer)buffer, 
                      (ssize_t)bytes_to_write, 
                      0);

    if (nb_written > 0) {
        if (bytes_written) *bytes_written = nb_written;
        return ATX_SUCCESS;
    } else {
        if (bytes_written) *bytes_written = 0;
        if (nb_written == 0) {
            return ATX_ERROR_DISCONNECTED;
        } else {
            return ATX_FAILURE;
        }
    }
}

/*----------------------------------------------------------------------
|       BsdSocketInputStream_Seek
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketInputStream_Seek(ATX_InputStream* self, 
                          ATX_Offset       where)
{
    /* can't seek in socket streams */
    ATX_COMPILER_UNUSED(self);
    ATX_COMPILER_UNUSED(where);
    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|       BsdSocketInputStream_Tell
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketInputStream_Tell(ATX_InputStream* self, 
                          ATX_Offset*      where)
{
    ATX_COMPILER_UNUSED(self);

    if (where) *where = 0;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketInputStream_GetSize
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketInputStream_GetSize(ATX_InputStream* self, 
                             ATX_Size*        size)
{
    ATX_COMPILER_UNUSED(self);

    if (size) *size = 0;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketInputStream_GetAvailable
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketInputStream_GetAvailable(ATX_InputStream* _self, 
                                  ATX_Size*        available)
{
    BsdSocketStream* self = ATX_SELF(BsdSocketStream, ATX_InputStream);
    unsigned long    ready = 0;
    int io_result = ioctlsocket(self->socket_ref->fd, FIONREAD, &ready); 
    if (io_result == ATX_BSD_SOCKET_ERROR) {
        *available = 0;
        return ATX_FAILURE;
    } else {
        *available = ready;
        return ATX_SUCCESS;
    }
}

/*----------------------------------------------------------------------
|       BsdSocketOutputStream_Seek
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketOutputStream_Seek(ATX_OutputStream* self, 
                           ATX_Offset        where)
{
    ATX_COMPILER_UNUSED(self);
    ATX_COMPILER_UNUSED(where);

    /* can't seek in socket streams */
    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|       BsdSocketOutputStream_Tell
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketOutputStream_Tell(ATX_OutputStream* self, 
                           ATX_Offset*       where)
{
    ATX_COMPILER_UNUSED(self);

    if (where) *where = 0;
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocketOutputStream_Flush
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocketOutputStream_Flush(ATX_OutputStream* self)
{
    ATX_COMPILER_UNUSED(self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BsdSocketStream_GetInterface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BsdSocketStream)
    ATX_GET_INTERFACE_ACCEPT(BsdSocketStream, ATX_InputStream)
    ATX_GET_INTERFACE_ACCEPT(BsdSocketStream, ATX_OutputStream)
    ATX_GET_INTERFACE_ACCEPT(BsdSocketStream, ATX_Referenceable)
ATX_END_GET_INTERFACE_IMPLEMENTATION(BsdSocketStream)

/*----------------------------------------------------------------------
|       ATX_InputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdSocketStream, ATX_InputStream)
    BsdSocketStream_Read,
    BsdSocketInputStream_Seek,
    BsdSocketInputStream_Tell,
    BsdSocketInputStream_GetSize,
    BsdSocketInputStream_GetAvailable
ATX_END_INTERFACE_MAP(BsdSocketStream, ATX_InputStream)

/*----------------------------------------------------------------------
|       ATX_OutputStream interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdSocketStream, ATX_OutputStream)
    BsdSocketStream_Write,
    BsdSocketOutputStream_Seek,
    BsdSocketOutputStream_Tell,
    BsdSocketOutputStream_Flush
ATX_END_INTERFACE_MAP(BsdSocketStream, ATX_OutputStream)

/*----------------------------------------------------------------------
|       ATX_Referenceable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_REFERENCEABLE_INTERFACE(BsdSocketStream, reference_count)

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BsdSocket, ATX_Socket);
ATX_DECLARE_INTERFACE_MAP(BsdSocket, ATX_Destroyable);
static ATX_Result BsdSocket_RefreshInfo(BsdSocket* socket);

/*----------------------------------------------------------------------
|       BsdSocket_Construct
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_Construct(BsdSocket* self, SocketFd fd)
{ 
    ATX_Result result;

    /* create a reference to the fd */
    result = BsdSocketFdWrapper_Create(fd, &self->socket_ref);
    if (ATX_FAILED(result)) return result;

    /* get initial info */
    BsdSocket_RefreshInfo(self);

    /* setup the interfaces */
    ATX_SET_INTERFACE(self, BsdSocket, ATX_Socket);
    ATX_SET_INTERFACE(self, BsdSocket, ATX_Destroyable);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocket_Destruct
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_Destruct(BsdSocket* self)
{
    BsdSocketFdWrapper_Release(self->socket_ref);
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocket_Create
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_Create(SocketFd fd, ATX_Socket** object)
{ 
    BsdSocket* socket;

    /* allocate new object */
    socket = (BsdSocket*)ATX_AllocateZeroMemory(sizeof(BsdSocket));
    *object = (ATX_Socket*)socket;
    if (socket == NULL) {
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct object */
    BsdSocket_Construct(socket, fd);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocket_Destroy
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_Destroy(ATX_Destroyable* _self)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Destroyable);
    BsdSocket_Destruct(self);
    ATX_FreeMemory((void*)self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocket_RefreshInfo
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_RefreshInfo(BsdSocket* self)
{
    struct sockaddr_in inet_address;
    socklen_t          name_length = sizeof(inet_address);

    /* check that we have a socket */
    if (self->socket_ref == NULL) return ATX_ERROR_INVALID_STATE;

    /* get the local socket addr */
    if (getsockname(self->socket_ref->fd, 
                    (struct sockaddr*)&inet_address, 
                    &name_length) == 0) {
        ATX_IpAddress_SetFromLong(&self->info.local_address.ip_address,
                                  ntohl(inet_address.sin_addr.s_addr));
        self->info.local_address.port = ntohs(inet_address.sin_port);
    }   

    /* get the peer socket addr */
    if (getpeername(self->socket_ref->fd,
                    (struct sockaddr*)&inet_address, 
                    &name_length) == 0) {
        ATX_IpAddress_SetFromLong(&self->info.remote_address.ip_address,
                                  ntohl(inet_address.sin_addr.s_addr));
        self->info.remote_address.port = ntohs(inet_address.sin_port);
    }   

    return ATX_SUCCESS;
}

#if defined(WIN32)
/*----------------------------------------------------------------------
|       BsdSocketFd_SetBlockingMode
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_SetBlockingMode(BsdSocket* self, ATX_Boolean blocking)
{
    unsigned long args = (blocking == ATX_TRUE) ? 0 : 1;
    if (ioctlsocket(self->socket_ref->fd, FIONBIO, &args)) {
        return ATX_FAILURE;
    }
    return ATX_SUCCESS;
}
#else
/*----------------------------------------------------------------------
|       BsdSocketFd_SetBlockingMode
+---------------------------------------------------------------------*/
static ATX_Result
BsdSocket_SetBlockingMode(BsdSocket* self, ATX_Boolean blocking)
{
    int flags = fcntl(self->socket_ref->fd, F_GETFL, 0);
    if (blocking == ATX_TRUE) {
        flags ^= O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    if (fcntl(self->socket_ref->fd, F_SETFL, flags)) {
        return ATX_FAILURE;
    }
    return ATX_SUCCESS;
}
#endif

/*----------------------------------------------------------------------
|       BsdSocket_Bind
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocket_Bind(ATX_Socket* _self, const ATX_SocketAddress* address)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Socket);

    /* convert the address */
    struct sockaddr_in inet_address;
    SocketAddressToInetAddress(address, &inet_address);
    
    /* bind the socket */
    if (bind(self->socket_ref->fd, 
             (struct sockaddr*)&inet_address, 
             sizeof(inet_address)) < 0) {
        return ATX_ERROR_BIND_FAILED;
    }

    /* refresh socket info */
    BsdSocket_RefreshInfo(self);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdSocket_Connect
+---------------------------------------------------------------------*/
ATX_METHOD 
BsdSocket_Connect(ATX_Socket*              self, 
                  const ATX_SocketAddress* address, 
                  ATX_Timeout              timeout)
{
    ATX_COMPILER_UNUSED(self);
    ATX_COMPILER_UNUSED(address);
    ATX_COMPILER_UNUSED(timeout);

    /* this is unsupported unless overridden in a derived class */
    return ATX_FAILURE;
}

/*----------------------------------------------------------------------
|       BsdSocket_GetInputStream
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocket_GetInputStream(ATX_Socket* _self, ATX_InputStream** stream)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Socket);

    /* check that we have a socket */
    if (self->socket_ref == NULL) return ATX_ERROR_INVALID_STATE;

    /* create a stream */
    return BsdSocketInputStream_Create(self->socket_ref, stream);
}

/*----------------------------------------------------------------------
|       BsdSocket_GetOutputStream
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocket_GetOutputStream(ATX_Socket* _self, ATX_OutputStream** stream)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Socket);

    /* check that we have a socket */
    if (self->socket_ref == NULL) return ATX_ERROR_INVALID_STATE;

    /* create a stream */
    return BsdSocketOutputStream_Create(self->socket_ref, stream);
}

/*----------------------------------------------------------------------
|       BsdSocket_GetInfo
+---------------------------------------------------------------------*/
ATX_METHOD
BsdSocket_GetInfo(ATX_Socket* _self, ATX_SocketInfo* info)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Socket);

    /* return the cached info */
    *info = self->info;
    
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BsdSocket_GetInterface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BsdSocket)
    ATX_GET_INTERFACE_ACCEPT(BsdSocket, ATX_Socket)
    ATX_GET_INTERFACE_ACCEPT(BsdSocket, ATX_Destroyable)
ATX_END_GET_INTERFACE_IMPLEMENTATION(BsdSocket)

/*----------------------------------------------------------------------
|       ATX_Socket interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdSocket, ATX_Socket)
    BsdSocket_Bind,
    BsdSocket_Connect,
    BsdSocket_GetInputStream,
    BsdSocket_GetOutputStream,
    BsdSocket_GetInfo
ATX_END_INTERFACE_MAP(BsdSocket, ATX_Socket)

/*----------------------------------------------------------------------
|       ATX_Destroyable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_DESTROYABLE_INTERFACE(BsdSocket)

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BsdUdpSocket, ATX_DatagramSocket);

/*----------------------------------------------------------------------
|       ATX_UdpSocket_Create
+---------------------------------------------------------------------*/
ATX_Result
ATX_UdpSocket_Create(ATX_DatagramSocket** object)
{ 
    BsdUdpSocket* udp_socket;

    /* make sure the TCP/IP stack is initialized */
    ATX_CHECK(BsdSockets_Init());

    /* allocate new object */
    udp_socket = (BsdUdpSocket*)ATX_AllocateZeroMemory(sizeof(BsdUdpSocket));
    if (udp_socket == NULL) {
        *object = NULL;
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct object */
    BsdSocket_Construct(&ATX_BASE(udp_socket, BsdSocket), socket(AF_INET, SOCK_DGRAM, 0));

    /* set default socket options */
    {
        int option = 1;
        setsockopt(ATX_BASE(udp_socket, BsdSocket).socket_ref->fd, 
                   SOL_SOCKET, 
                   SO_REUSEADDR, 
                   (SocketOption)&option, 
                   sizeof(option));
    }

    /* setup interfaces */
    ATX_SET_INTERFACE(udp_socket, BsdUdpSocket, ATX_DatagramSocket);
    *object = &ATX_BASE(udp_socket, ATX_DatagramSocket);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdUdpSocket_Destroy
+---------------------------------------------------------------------*/
ATX_METHOD
BsdUdpSocket_Destroy(ATX_Destroyable* _self)
{
    BsdSocket* self = ATX_SELF(BsdSocket, ATX_Socket);

    BsdSocket_Destruct(self);
    ATX_FreeMemory((void*)self);
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdUdpSocket_Connect
+---------------------------------------------------------------------*/
ATX_METHOD
BsdUdpSocket_Connect(ATX_Socket*              _self,
                     const ATX_SocketAddress* address,
                     ATX_Timeout              timeout)
{
    BsdUdpSocket*      self = ATX_SELF(BsdUdpSocket, ATX_DatagramSocket);
    struct sockaddr_in inet_address;
    int                io_result;
    
    /* avoid compiler warnings */
    timeout = timeout;

    /* setup an address structure */
    SocketAddressToInetAddress(address, &inet_address);

    /* connect so that we can have some addr bound to the socket */
    io_result = connect(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                        (struct sockaddr *)&inet_address, 
                        sizeof(inet_address));
    if (io_result == ATX_BSD_SOCKET_ERROR) { 
        return ATX_FAILURE;
    }
    
    /* set default socket options */
    {
        int option = 1;
        setsockopt(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                SOL_SOCKET, 
                SO_BROADCAST, 
                (SocketOption)&option, 
                sizeof(option));
    }

    /* refresh socket info */
    BsdSocket_RefreshInfo(&ATX_BASE(self, BsdSocket));

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdUdpSocket_Send
+---------------------------------------------------------------------*/
ATX_METHOD 
BsdUdpSocket_Send(ATX_DatagramSocket*      _self,
                  const ATX_DataBuffer*    packet, 
                  const ATX_SocketAddress* address) 
{
    BsdUdpSocket* self = ATX_SELF(BsdUdpSocket, ATX_DatagramSocket);
    int           io_result;

    /* get the packet buffer */
    const ATX_Byte* buffer        = ATX_DataBuffer_GetData(packet);
    ssize_t         buffer_length = ATX_DataBuffer_GetDataSize(packet);

    /* send the packet buffer */
    if (address != NULL) {
        /* send to the specified socket */

        /* setup an address structure */
        struct sockaddr_in inet_address;
        SocketAddressToInetAddress(address, &inet_address);
        io_result = sendto(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                           (SocketConstBuffer)buffer, 
                           buffer_length, 
                           0, 
                           (struct sockaddr *)&inet_address, 
                           sizeof(inet_address));
    } else {
        /* send to whichever addr the socket is connected */
        io_result = send(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                         (SocketConstBuffer)buffer, 
                         buffer_length,
                         0);
    }

    /* check the result */
    if (io_result == ATX_BSD_SOCKET_ERROR) {
        return ATX_FAILURE;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdUdpSocket_Receive
+---------------------------------------------------------------------*/
ATX_METHOD 
BsdUdpSocket_Receive(ATX_DatagramSocket* _self,
                     ATX_DataBuffer*     packet, 
                     ATX_SocketAddress*  address)
{
    BsdUdpSocket* self = ATX_SELF(BsdUdpSocket, ATX_DatagramSocket);
    int           io_result;

    /* get the packet buffer */
    ATX_Byte* buffer        = ATX_DataBuffer_UseData(packet);
    ssize_t   buffer_length = ATX_DataBuffer_GetDataSize(packet);;

    /* check that we have some space to receive */
    if (buffer_length == 0) {
        return ATX_ERROR_INVALID_PARAMETERS;
    }

    /* receive a packet */
    if (address != NULL) {
        struct sockaddr_in inet_address;
        socklen_t          inet_address_length = sizeof(inet_address);
        io_result = recvfrom(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                             (SocketBuffer)buffer, 
                             buffer_length, 
                             0, 
                             (struct sockaddr *)&inet_address, 
                             &inet_address_length);

        /* convert the address format */
        if (io_result != ATX_BSD_SOCKET_ERROR) {
            if (inet_address_length == sizeof(inet_address)) {
                InetAddressToSocketAddress(&inet_address, address);
            }
            ATX_DataBuffer_SetDataSize(packet, io_result);
        } else {
            ATX_DataBuffer_SetDataSize(packet, 0);
        }
    } else {
        io_result = recv(ATX_BASE(self, BsdSocket).socket_ref->fd,
                         (SocketBuffer)buffer,
                         buffer_length,
                         0);
    }

    /* check the result */
    if (io_result == ATX_BSD_SOCKET_ERROR) {
        return ATX_FAILURE;
    }

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BsdUdpSocket_GetInterface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BsdUdpSocket)
    ATX_GET_INTERFACE_ACCEPT_EX(BsdUdpSocket, BsdSocket, ATX_Socket)
    ATX_GET_INTERFACE_ACCEPT_EX(BsdUdpSocket, BsdSocket, ATX_Destroyable)
    ATX_GET_INTERFACE_ACCEPT(BsdUdpSocket, ATX_DatagramSocket)
ATX_END_GET_INTERFACE_IMPLEMENTATION(BsdUdpSocket)

/*----------------------------------------------------------------------
|       ATX_Socket interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_GET_INTERFACE_ADAPTER_EX(BsdUdpSocket, BsdSocket, ATX_Socket)
ATX_INTERFACE_MAP(BsdUdpSocket, ATX_Socket) = {
    BsdUdpSocket_ATX_Socket_GetInterface,
    BsdSocket_Bind,
    BsdUdpSocket_Connect,
    BsdSocket_GetInputStream,
    BsdSocket_GetOutputStream,
    BsdSocket_GetInfo
};

/*----------------------------------------------------------------------
|       ATX_DatagramSocket interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdUdpSocket, ATX_DatagramSocket)
    BsdUdpSocket_Send,
    BsdUdpSocket_Receive
ATX_END_INTERFACE_MAP(BsdUdpSocket, ATX_DatagramSocket)

/*----------------------------------------------------------------------
|       ATX_Destroyable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_GET_INTERFACE_ADAPTER_EX(BsdUdpSocket, BsdSocket, ATX_Destroyable)
ATX_INTERFACE_MAP(BsdUdpSocket, ATX_Destroyable) = {
    BsdUdpSocket_ATX_Destroyable_GetInterface,
    BsdSocket_Destroy
};

/*----------------------------------------------------------------------
|       ATX_TcpClientSocket_Create
+---------------------------------------------------------------------*/
ATX_Result
ATX_TcpClientSocket_Create(ATX_Socket** object)
{ 
    BsdSocket* client;

    /* make sure the TCP/IP stack is initialized */
    ATX_CHECK(BsdSockets_Init());

    /* allocate new object */
    client = (BsdSocket*)ATX_AllocateZeroMemory(sizeof(BsdSocket));
    *object = (ATX_Socket*)client;
    if (client == NULL) {
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct the object */
    BsdSocket_Construct(client, socket(AF_INET, SOCK_STREAM, 0));

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdTcpClientSocket_Connect
+---------------------------------------------------------------------*/
ATX_METHOD
BsdTcpClientSocket_Connect(ATX_Socket*              _self,
                           const ATX_SocketAddress* address,
                           ATX_Timeout              timeout)
{
    BsdSocket*         self = ATX_SELF(BsdSocket, ATX_Socket);
    struct sockaddr_in inet_address;
    SocketFd           socket_fd = self->socket_ref->fd;
    int                io_result;
    fd_set             read_set;
    fd_set             write_set;
    fd_set             except_set;
    struct timeval     timeout_value;

    /* set the socket to nonblocking so that we can timeout on connect */
    if (BsdSocket_SetBlockingMode(self, ATX_FALSE)) {
        return ATX_FAILURE;
    }

    /* convert the address */
    SocketAddressToInetAddress(address, &inet_address);

    /* initiate connection */
    io_result = connect(socket_fd, 
                        (struct sockaddr *)&inet_address, 
                        sizeof(inet_address));
    if (io_result == 0) {
        /* immediate connection */
        BsdSocket_SetBlockingMode(self, ATX_TRUE);

        /* get socket info */
        BsdSocket_RefreshInfo(self);

        return ATX_SUCCESS;
    }
    if (io_result == ATX_BSD_SOCKET_ERROR && GetSocketError() != EINPROGRESS) {
        /* error */
        return MapErrorCode(GetSocketError());
    }

    /* wait for connection to succeed or fail */
    FD_ZERO(&read_set);
    FD_SET(socket_fd, &read_set);
    FD_ZERO(&write_set);
    FD_SET(socket_fd, &write_set);
    FD_ZERO(&except_set);
    FD_SET(socket_fd, &except_set);

    if (timeout != ATX_SOCKET_TIMEOUT_INFINITE) {
        timeout_value.tv_sec = timeout/1000;
        timeout_value.tv_usec = 1000*(timeout-1000*(timeout/1000));
    }

    io_result = select((int)(socket_fd+1), &read_set, &write_set, &except_set, 
                       timeout == ATX_SOCKET_TIMEOUT_INFINITE ? 
                       NULL : &timeout_value);
    if (io_result == ATX_BSD_SOCKET_ERROR) {
        /* select error */
        return ATX_ERROR_SELECT_FAILED;
    } else if (io_result == 0) {
        /* timeout */
        return ATX_ERROR_TIMEOUT;
    }
    if (FD_ISSET(socket_fd, &read_set) || 
        FD_ISSET(socket_fd, &write_set) ||
        FD_ISSET(socket_fd, &except_set)) {
        int       error;
        socklen_t length = sizeof(error);

        /* get error status from socket */
        if (getsockopt(socket_fd, 
                       SOL_SOCKET, 
                       SO_ERROR, 
                       (SocketOption)&error, 
                       &length) == ATX_BSD_SOCKET_ERROR) {
            return ATX_ERROR_CONNECTION_FAILED;
        }
        if (error) {
            if (error == ECONNREFUSED) {
                return ATX_ERROR_CONNECTION_REFUSED;
            } else if (error == ETIMEDOUT) {
                return ATX_ERROR_TIMEOUT;
            } else {
                return ATX_ERROR_CONNECTION_FAILED;
            }
        }
    }
    
    /* put the socket back in blocking mode */
    BsdSocket_SetBlockingMode(self, ATX_TRUE);

    /* get socket info */
    BsdSocket_RefreshInfo(self);

    /* done */
    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|   BsdTcpClientSocket_GetInterface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BsdTcpClientSocket)
    ATX_GET_INTERFACE_ACCEPT(BsdTcpClientSocket, ATX_Socket)
    ATX_GET_INTERFACE_ACCEPT(BsdTcpClientSocket, ATX_Destroyable)
ATX_END_GET_INTERFACE_IMPLEMENTATION(BsdTcpClientSocket)

/*----------------------------------------------------------------------
|       ATX_Socket interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdTcpClientSocket, ATX_Socket)
    BsdSocket_Bind,
    BsdTcpClientSocket_Connect,
    BsdSocket_GetInputStream,
    BsdSocket_GetOutputStream,
    BsdSocket_GetInfo
ATX_END_INTERFACE_MAP(BsdTcpClientSocket, ATX_Socket)

/*----------------------------------------------------------------------
|       ATX_Destroyable interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdTcpClientSocket, ATX_Destroyable)
    BsdSocket_Destroy
ATX_END_INTERFACE_MAP(BsdTcpClientSocket, ATX_Destroyable)

/*----------------------------------------------------------------------
|       forward declarations
+---------------------------------------------------------------------*/
ATX_DECLARE_INTERFACE_MAP(BsdTcpServerSocket, ATX_ServerSocket);

/*----------------------------------------------------------------------
|       ATX_TcpServerSocket_Create
+---------------------------------------------------------------------*/
ATX_Result
ATX_TcpServerSocket_Create(ATX_ServerSocket** object)
{ 
    BsdTcpServerSocket* server;

    /* make sure the TCP/IP stack is initialized */
    ATX_CHECK(BsdSockets_Init());

    /* allocate new object */
    server = (BsdTcpServerSocket*)ATX_AllocateZeroMemory(sizeof(BsdTcpServerSocket));
    if (server == NULL) {
        *object = NULL;
        return ATX_ERROR_OUT_OF_MEMORY;
    }

    /* construct object */
    BsdSocket_Construct(&ATX_BASE(server,BsdSocket), socket(AF_INET, SOCK_STREAM, 0));
    server->max_clients = 0;

    /* set socket options */
    { 
        int option = 1;
        setsockopt(ATX_BASE(server,BsdSocket).socket_ref->fd, 
                SOL_SOCKET, 
                SO_REUSEADDR, 
                (SocketOption)&option, 
                sizeof(option));
    }

    /* setup interfaces */
    ATX_SET_INTERFACE(server, BsdTcpServerSocket, ATX_ServerSocket);
    *object = &ATX_BASE(server, ATX_ServerSocket);

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdTcpServerSocket_Listen
+---------------------------------------------------------------------*/
ATX_METHOD
BsdTcpServerSocket_Listen(ATX_ServerSocket* _self, unsigned int max_clients)
{
    BsdTcpServerSocket* self = ATX_SELF(BsdTcpServerSocket, ATX_ServerSocket);

    /* listen for connections */
    if (listen(ATX_BASE(self, BsdSocket).socket_ref->fd, max_clients) < 0) {
        self->max_clients = 0;
        return ATX_ERROR_LISTEN_FAILED;
    }   
    self->max_clients = max_clients;

    return ATX_SUCCESS;
}

/*----------------------------------------------------------------------
|       BsdTcpServerSocket_WaitForNewClient
+---------------------------------------------------------------------*/
ATX_METHOD
BsdTcpServerSocket_WaitForNewClient(ATX_ServerSocket* _self,
                                    ATX_Socket**      client)
{
    BsdTcpServerSocket* self = ATX_SELF(BsdTcpServerSocket, ATX_ServerSocket);
    struct sockaddr_in  inet_address;
    socklen_t           namelen = sizeof(inet_address);
    SocketFd            socket_fd;
    ATX_Result          result;

    /* check that we are listening for clients */
    if (self->max_clients == 0) {
        BsdTcpServerSocket_Listen(_self, ATX_TCP_SERVER_SOCKET_DEFAULT_LISTEN_COUNT);
    }

    /* wait for incoming connection */
    socket_fd = accept(ATX_BASE(self, BsdSocket).socket_ref->fd, 
                       (struct sockaddr*)&inet_address, 
                       &namelen); 
    if (socket_fd == ATX_BSD_INVALID_SOCKET) {
        client = NULL;
        return ATX_ERROR_ACCEPT_FAILED;
    }

    /* create a new client socket to wrap this file descriptor */
    result = BsdSocket_Create(socket_fd, client);
    if (result != ATX_SUCCESS) return result;
    
    /* done */
    return ATX_SUCCESS;    
}

/*----------------------------------------------------------------------
|   BsdTcpServerSocket_GetInterface
+---------------------------------------------------------------------*/
ATX_BEGIN_GET_INTERFACE_IMPLEMENTATION(BsdTcpServerSocket)
    ATX_GET_INTERFACE_ACCEPT_EX(BsdTcpServerSocket, BsdSocket, ATX_Socket)
    ATX_GET_INTERFACE_ACCEPT_EX(BsdTcpServerSocket, BsdSocket, ATX_Destroyable)
    ATX_GET_INTERFACE_ACCEPT(BsdTcpServerSocket, ATX_ServerSocket)
ATX_END_GET_INTERFACE_IMPLEMENTATION(BsdTcpServerSocket)

/*----------------------------------------------------------------------
|       ATX_Socket interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_GET_INTERFACE_ADAPTER_EX(BsdTcpServerSocket, BsdSocket, ATX_Socket)
ATX_INTERFACE_MAP(BsdTcpServerSocket, ATX_Socket) = {
    BsdTcpServerSocket_ATX_Socket_GetInterface,
    BsdSocket_Bind,
    BsdSocket_Connect,
    BsdSocket_GetInputStream,
    BsdSocket_GetOutputStream,
    BsdSocket_GetInfo
};

/*----------------------------------------------------------------------
|       ATX_ServerSocket interface
+---------------------------------------------------------------------*/
ATX_BEGIN_INTERFACE_MAP(BsdTcpServerSocket, ATX_ServerSocket)
    BsdTcpServerSocket_Listen,
    BsdTcpServerSocket_WaitForNewClient
ATX_END_INTERFACE_MAP(BsdTcpServerSocket, ATX_ServerSocket)

/*----------------------------------------------------------------------
|       ATX_Destroyable interface
+---------------------------------------------------------------------*/
ATX_IMPLEMENT_GET_INTERFACE_ADAPTER_EX(BsdTcpServerSocket, BsdSocket, ATX_Destroyable)
ATX_INTERFACE_MAP(BsdTcpServerSocket, ATX_Destroyable) = {
    BsdTcpServerSocket_ATX_Destroyable_GetInterface,
    BsdSocket_Destroy
};










