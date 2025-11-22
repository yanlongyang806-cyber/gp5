
/* WinXnet.h -- definitions to be used with the XNet protocol on Windows.
 *
 */


#ifndef _WINXNET_H
#define _WINXNET_H

#include <sal.h>


/*
 * Ensure structures are packed consistently.
 */

#ifndef _WIN64
#include <pshpack4.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

SOCKET
WSAAPI
XSocketCreate(
    __in int af,
    __in int type,
    __in int protocol
    );
int
WSAAPI
XSocketClose(
    __in SOCKET s
    );
int
WSAAPI
XSocketShutdown(
    __in SOCKET s,
    __in int how
    );
int
WSAAPI
XSocketIOCTLSocket(
    __in SOCKET s,
    __in long cmd,
    __inout u_long FAR * argp
    );
int
WSAAPI
XSocketSetSockOpt(
    __in SOCKET s,
    __in int level,
    __in int optname,
    __in_bcount_opt(optlen) const char FAR * optval,
    __in int optlen
    );
int
WSAAPI
XSocketGetSockOpt(
    __in SOCKET s,
    __in int level,
    __in int optname,
    __out_ecount_part(*optlen, *optlen) char FAR * optval,
    __inout int FAR * optlen
    );
int
WSAAPI
XSocketGetSockName(
    __in SOCKET s,
    __out_bcount(namelen) struct sockaddr FAR * name,
    __inout int FAR * namelen
    );
int
WSAAPI
XSocketGetPeerName(
    __in SOCKET s,
    __out_bcount(namelen) struct sockaddr FAR * name,
    __inout int FAR * namelen
    );
int
WSAAPI
XSocketBind(
    __in SOCKET s,
    __in_bcount(namelen) const struct sockaddr FAR * name,
    __in int namelen
    );
int
WSAAPI
XSocketConnect(
    __in SOCKET s,
    __in_bcount(namelen) const struct sockaddr FAR * name,
    __in int namelen
    );
int
WSAAPI
XSocketListen(
    __in SOCKET s,
    __in int backlog
    );
SOCKET
WSAAPI
XSocketAccept(
    __in SOCKET s,
    __out_bcount(addrlen) struct sockaddr FAR * addr,
    __inout int FAR * addrlen
    );
int
WSAAPI
XSocketSelect(
    __in int nfds,
    __inout_opt fd_set FAR * readfds,
    __inout_opt fd_set FAR * writefds,
    __inout_opt fd_set FAR * exceptfds,
    __in_opt const struct timeval FAR * timeout
    );
BOOL
WSAAPI
XWSAGetOverlappedResult(
    __in SOCKET s,
    __in LPWSAOVERLAPPED lpOverlapped,
    __out LPDWORD lpcbTransfer,
    __in BOOL fWait,
    __out LPDWORD lpdwFlags
    );
INT
WSAAPI
XWSACancelOverlappedIO(
	__in SOCKET s
	);
int
WSAAPI
XSocketRecv(
    __in SOCKET s,
    __out_ecount(len) char FAR * buf,
    __in int len,
    __in int flags
    );
int
WSAAPI
XWSARecv(
    __in SOCKET s,
    __inout_ecount(dwBufferCount) LPWSABUF lpBuffers,
    __in DWORD dwBufferCount,
    __out LPDWORD lpNumberOfBytesRecvd,
    __inout LPDWORD lpFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );
int
WSAAPI
XSocketRecvFrom(
    __in SOCKET s,
    __out_ecount(len) char FAR * buf,
    __in int len,
    __in int flags,
    __out_bcount_opt(fromlen) struct sockaddr FAR * from,
    __inout_opt int FAR * fromlen
    );
int
WSAAPI
XWSARecvFrom(
    __in SOCKET s,
    __inout_ecount(dwBufferCount) LPWSABUF lpBuffers,
    __in DWORD dwBufferCount,
    __out LPDWORD lpNumberOfBytesRecvd,
    __inout LPDWORD lpFlags,
    __out_bcount_opt(lpFromlen) struct sockaddr FAR * lpFrom,
    __inout_opt LPINT lpFromlen,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );
int
WSAAPI
XSocketSend(
    __in SOCKET s,
    __in_bcount(len) const char FAR * buf,
    __in int len,
    __in int flags
    );
int
WSAAPI
XWSASend(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUF lpBuffers,
    __in DWORD dwBufferCount,
    __out LPDWORD lpNumberOfBytesSent,
    __in DWORD dwFlags,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );
int
WSAAPI
XSocketSendTo(
    __in SOCKET s,
    __in_bcount(len) const char FAR * buf,
    __in int len,
    __in int flags,
    __in_bcount_opt(tolen) const struct sockaddr FAR * to,
    __in int tolen
    );
int
WSAAPI
XWSASendTo(
    __in SOCKET s,
    __in_ecount(dwBufferCount) LPWSABUF lpBuffers,
    __in DWORD dwBufferCount,
    __out LPDWORD lpNumberOfBytesSent,
    __in DWORD dwFlags,
    __in_bcount_opt(iTolen) const struct sockaddr FAR * lpTo,
    __in int iTolen,
    __in_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );
int
WSAAPI
XWSAGetLastError(
    void
    );
void
WSAAPI
XWSASetLastError(
    __in int iError
    );
int
WSAAPI
__XWSAFDIsSet(
    __in SOCKET s,
    __in fd_set FAR * set
    );
int
WSAAPI
XWSAEventSelect(
    __in SOCKET s,
    __in WSAEVENT hEventObject,
    __in long lNetworkEvents
    );
int
WSAAPI
XWSACleanup(
    void
    );
int
WSAAPI
XWSAStartup(
    __in WORD wVersionRequested,
    __out LPWSADATA lpWSAData
    );
BOOL
WSAAPI
XWSACloseEvent(
    __in WSAEVENT hEvent
    );
WSAEVENT
WSAAPI
XWSACreateEvent(
    void
    );
BOOL
WSAAPI
XWSAResetEvent(
    __in WSAEVENT hEvent
    );
BOOL
WSAAPI
XWSASetEvent(
    __in WSAEVENT hEvent
    );
DWORD
WSAAPI
XWSAWaitForMultipleEvents(
    __in DWORD cEvents,
    __in_ecount(cEvents) const WSAEVENT FAR * lphEvents,
    __in BOOL fWaitAll,
    __in DWORD dwTimeout,
    __in BOOL fAlertable
    );
u_long
WSAAPI
XSocketHTONL(
    __in u_long hostlong
    );
u_short
WSAAPI
XSocketHTONS(
    __in u_short hostshort
    );
unsigned long
WSAAPI
XSocketInet_Addr(
    __in const char FAR * cp
    );
u_long
WSAAPI
XSocketNTOHL(
    __in u_long netlong
    );
u_short
WSAAPI
XSocketNTOHS(
    __in u_short netshort
    );



// Xbox Secure Network Library ------------------------------------------------

#include <pshpack1.h>

//
// XNetStartup is called to load the Xbox Secure Network Library.  It takes an
// optional pointer to a parameter structure.  To initialize the library with
// the default set of parameters, simply pass NULL for this argument.  To
// initialize the library with a different set of parameters, place an
// XNetStartupParams on the stack, zero it out, set the cfgSizeOfStruct to
// sizeof(XNetStartupParams), set any of the parameters you want to configure
// (leaving the remaining ones zeroed), and pass a pointer to this structure to
// XNetStartup.
//

//
// This flag instructs XNet to pre-allocate memory for the maximum number of
// datagram (UDP and VDP) sockets during the 'XNetStartup' call and store the
// objects in an internal pool.  Otherwise, sockets are allocated on demand (by
// the 'socket' function).  In either case, SOCK_DGRAM sockets are returned to
// the internal pool once closed.  The memory will remain allocated until
// XNetCleanup is called.
//
#define XNET_STARTUP_ALLOCATE_MAX_DGRAM_SOCKETS     0x02

//
// This flag instructs XNet to pre-allocate memory for the maximum number of
// stream (TCP) sockets during the 'XNetStartup' call and store the objects in
// an internal pool.  Otherwise, sockets are allocated on demand (by the
// 'socket', 'listen', and 'accept' functions).  Note that 'listen' will still
// attempt to pre-allocate the specified maximum backlog number of sockets even
// without this flag set.  The 'accept' function will always return a socket
// retrieved from the pool, though it will also attempt to allocate a
// replacement if the cfgSockMaxStreamSockets limit and memory permit.
// In all cases, SOCK_STREAM sockets are returned to the internal pool once
// closed. The memory will remain allocated until XNetCleanup is called.
//
#define XNET_STARTUP_ALLOCATE_MAX_STREAM_SOCKETS    0x04


typedef struct {

    //
    // Must be set to sizeof(XNetStartupParams).  There is no default.
    //
    BYTE        cfgSizeOfStruct;

    //
    // One or more of the XNET_STARTUP_xxx flags OR'd together.
    //
    // The default is 0 (no flags specified).
    BYTE        cfgFlags;

    //
    // The maximum number of SOCK_DGRAM (UDP or VDP) sockets that can be
    // opened at once.
    //
    // The default is 4 sockets.
    //
    BYTE        cfgSockMaxDgramSockets;

    //
    // The maximum number of SOCK_STREAM (TCP) sockets that can be opened at
    // once, including those sockets created as a result of incoming connection
    // requests.  Remember that a TCP socket may not be closed immediately
    // after 'closesocket' is called, depending on the linger options in place
    // (by default a TCP socket will linger).
    //
    // The default is 32 sockets.
    //
    BYTE        cfgSockMaxStreamSockets;

    //
    // The default receive buffer size for a socket, in units of K (1024 bytes).
    //
    // The default is 16 units (16K).
    //
    BYTE        cfgSockDefaultRecvBufsizeInK;

    //
    // The default send buffer size for a socket, in units of K (1024 bytes).
    //
    // The default is 16 units (16K).
    //
    BYTE        cfgSockDefaultSendBufsizeInK;

    //
    // The maximum number of XNKID / XNKEY pairs that can be registered at the
    // same time by calling XNetRegisterKey.
    //
    // The default is 8 key pair registrations.
    //
    BYTE        cfgKeyRegMax;

    //
    // The maximum number of security associations that can be registered at
    // the same time.  Security associations are created for each unique
    // XNADDR / XNKID pair passed to XNetXnAddrToInAddr.  Security associations
    // are also implicitly created for each secure host that establishes an
    // incoming connection with this host on a given registered XNKID.  Note
    // that there will only be one security association between a pair of hosts
    // on a given XNKID no matter how many sockets are actively communicating
    // on that secure connection.
    //
    // The default is 32 security associations.
    //
    BYTE        cfgSecRegMax;

    //
    // The maximum amount of QoS data, in units of DWORD (4 bytes), that can be
    // supplied to a call to XNetQosListen or returned in the result set of a
    // call to XNetQosLookup.
    //
    // The default is 64 (256 bytes).
    //
    BYTE        cfgQosDataLimitDiv4;

    //
    // The amount of time to wait for a response after sending a QoS packet
    // before sending it again (or giving up).  This should be set to the same
    // value on clients (XNetQosLookup callers) and servers (XNetQosListen
    // callers).
    //
    // The default is 2 seconds.
    //
    BYTE        cfgQosProbeTimeoutInSeconds;

    //
    // The maximum number of times to retry a given QoS packet when no response
    // is received.  This should be set to the same value on clients
    // (XNetQosLookup callers) and servers (XNetQosListen callers).
    //
    // The default is 3 retries.
    //
    BYTE        cfgQosProbeRetries;

    //
    // The maximum number of simultaneous QoS lookup responses that a QoS
    // listener supports.  Note that the bandwidth throttling parameter passed
    // to XNetQosListen may impact the number of responses queued, and thus
    // affects how quickly this limit is reached.
    //
    // The default is 8 responses.
    //
    BYTE        cfgQosSrvMaxSimultaneousResponses;

    //
    // The maximum amount of time for QoS listeners to wait for the second
    // packet in a packet pair.
    //
    // The default is 2 seconds.
    //
    BYTE        cfgQosPairWaitTimeInSeconds;

} XNetStartupParams;

typedef struct {
    IN_ADDR     ina;                            // IP address (zero if not static/DHCP)
    IN_ADDR     inaOnline;                      // Online IP address (zero if not online)
    WORD        wPortOnline;                    // Online port
    BYTE        abEnet[6];                      // Ethernet MAC address
    BYTE        abOnline[20];                   // Online identification
} XNADDR;

typedef struct {
    BYTE        ab[8];                          // xbox to xbox key identifier
} XNKID;

#define XNET_XNKID_MASK                 0xF0    // Mask of flag bits in first byte of XNKID
#define XNET_XNKID_SYSTEM_LINK          0x00    // Peer to peer system link session
#define XNET_XNKID_SYSTEM_LINK_XPLAT    0x40    // Peer to peer system link session for cross-platform
#define XNET_XNKID_ONLINE_PEER          0x80    // Peer to peer online session
#define XNET_XNKID_ONLINE_SERVER        0xC0    // Client to server online session

#define XNetXnKidIsSystemLinkXbox(pxnkid)       (((pxnkid)->ab[0] & 0xE0) == XNET_XNKID_SYSTEM_LINK)
#define XNetXnKidIsSystemLinkXPlat(pxnkid)      (((pxnkid)->ab[0] & 0xE0) == XNET_XNKID_SYSTEM_LINK_XPLAT)
#define XNetXnKidIsSystemLink(pxnkid)           (XNetXnKidIsSystemLinkXbox(pxnkid) || XNetXnKidIsSystemLinkXPlat(pxnkid))
#define XNetXnKidIsOnlinePeer(pxnkid)           (((pxnkid)->ab[0] & 0xE0) == XNET_XNKID_ONLINE_PEER)
#define XNetXnKidIsOnlineServer(pxnkid)         (((pxnkid)->ab[0] & 0xE0) == XNET_XNKID_ONLINE_SERVER)


typedef struct {
    BYTE        ab[16];                         // xbox to xbox key exchange key
} XNKEY;

typedef struct {
    INT         iStatus;                        // WSAEINPROGRESS if pending; 0 if success; error if failed
    UINT        cina;                           // Count of IP addresses for the given host
    IN_ADDR     aina[8];                        // Vector of IP addresses for the given host
} XNDNS;

#define XNET_XNQOSINFO_COMPLETE         0x01    // Qos has finished processing this entry
#define XNET_XNQOSINFO_TARGET_CONTACTED 0x02    // Target host was successfully contacted
#define XNET_XNQOSINFO_TARGET_DISABLED  0x04    // Target host has disabled its Qos listener
#define XNET_XNQOSINFO_DATA_RECEIVED    0x08    // Target host supplied Qos data
#define XNET_XNQOSINFO_PARTIAL_COMPLETE 0x10    // Qos has unfinished estimates for this entry

typedef struct {
    BYTE        bFlags;                         // See XNET_XNQOSINFO_*
    BYTE        bReserved;                      // Reserved
    WORD        cProbesXmit;                    // Count of Qos probes transmitted
    WORD        cProbesRecv;                    // Count of Qos probes successfully received
    WORD        cbData;                         // Size of Qos data supplied by target (may be zero)
    BYTE *      pbData;                         // Qos data supplied by target (may be NULL)
    WORD        wRttMinInMsecs;                 // Minimum round-trip time in milliseconds
    WORD        wRttMedInMsecs;                 // Median round-trip time in milliseconds
    DWORD       dwUpBitsPerSec;                 // Upstream bandwidth in bits per second
    DWORD       dwDnBitsPerSec;                 // Downstream bandwidth in bits per second
} XNQOSINFO;

typedef struct {
    UINT        cxnqos;                         // Count of items in axnqosinfo[] array
    UINT        cxnqosPending;                  // Count of items still pending
    XNQOSINFO   axnqosinfo[1];                  // Vector of Qos results
} XNQOS;

typedef struct {
    DWORD       dwSizeOfStruct;                 // Structure size, must be set prior to calling XNetQosGetListenStats
    DWORD       dwNumDataRequestsReceived;      // Number of client data request probes received
    DWORD       dwNumProbesReceived;            // Number of client probe requests received
    DWORD       dwNumSlotsFullDiscards;         // Number of client requests discarded because all slots are full
    DWORD       dwNumDataRepliesSent;           // Number of data replies sent
    DWORD       dwNumDataReplyBytesSent;        // Number of data reply bytes sent
    DWORD       dwNumProbeRepliesSent;          // Number of probe replies sent
} XNQOSLISTENSTATS;


#include <poppack.h>

INT   WSAAPI XNetStartup(__in_opt const XNetStartupParams * pxnsp);
INT   WSAAPI XNetCleanup();

INT   WSAAPI XNetRandom(__out_ecount_opt(cb) BYTE * pb, __in UINT cb);

INT   WSAAPI XNetCreateKey(__out XNKID * pxnkid, __out XNKEY * pxnkey);
INT   WSAAPI XNetRegisterKey(__in const XNKID * pxnkid, __in const XNKEY * pxnkey);
INT   WSAAPI XNetUnregisterKey(__in const XNKID * pxnkid);
INT   WSAAPI XNetReplaceKey(__in const XNKID * pxnkidUnregister, __in_opt const XNKID * pxnkidReplace);

INT   WSAAPI XNetXnAddrToInAddr(__in const XNADDR * pxna, __in const XNKID * pxnkid, __out IN_ADDR * pina);
INT   WSAAPI XNetServerToInAddr(__in const IN_ADDR ina, __in DWORD dwServiceId, __out IN_ADDR * pina);
INT   WSAAPI XNetInAddrToXnAddr(__in const IN_ADDR ina, __out_opt XNADDR * pxna, __out_opt XNKID * pxnkid);
INT   WSAAPI XNetInAddrToServer(__in const IN_ADDR ina, __out_opt IN_ADDR *pina);
INT   WSAAPI XNetInAddrToString(__in const IN_ADDR ina, __out_ecount(cbBuf) char * pchBuf, __in INT cchBuf);
INT   WSAAPI XNetUnregisterInAddr(__in const IN_ADDR ina);
INT   WSAAPI XNetXnAddrToMachineId(__in const XNADDR * pxnaddr, __out ULONGLONG * pqwMachineId);

#define XNET_XNADDR_PLATFORM_XBOX1          0x00000000 // Platform type is original Xbox
#define XNET_XNADDR_PLATFORM_XBOX360        0x00000001 // Platform type is Xbox 360
#define XNET_XNADDR_PLATFORM_WINPC          0x00000002 // Platform type is Windows PC

INT   WSAAPI XNetGetXnAddrPlatform(__in const XNADDR * pxnaddr, __out DWORD * pdwPlatform);

#define XNET_CONNECT_STATUS_IDLE            0x00000000 // Connection not started; use XNetConnect or send packet
#define XNET_CONNECT_STATUS_PENDING         0x00000001 // Connecting in progress; not complete yet
#define XNET_CONNECT_STATUS_CONNECTED       0x00000002 // Connection is established
#define XNET_CONNECT_STATUS_LOST            0x00000003 // Connection was lost

INT   WSAAPI XNetConnect(__in const IN_ADDR ina);
DWORD WSAAPI XNetGetConnectStatus(__in const IN_ADDR ina);


INT   WSAAPI XNetDnsLookup(__in const char * pszHost, __in_opt WSAEVENT hEvent, __out XNDNS ** ppxndns);
INT   WSAAPI XNetDnsRelease(__in XNDNS * pxndns);


#define XNET_QOS_LISTEN_ENABLE              0x00000001 // Responds to queries on the given XNKID
#define XNET_QOS_LISTEN_DISABLE             0x00000002 // Rejects queries on the given XNKID
#define XNET_QOS_LISTEN_SET_DATA            0x00000004 // Sets the block of data to send back to queriers
#define XNET_QOS_LISTEN_SET_BITSPERSEC      0x00000008 // Sets max bandwidth that query reponses may consume
#define XNET_QOS_LISTEN_RELEASE             0x00000010 // Stops listening on given XNKID and releases memory

#define XNET_QOS_LOOKUP_RESERVED            0x00000000 // No flags defined yet for XNetQosLookup

#define XNET_QOS_SERVICE_LOOKUP_RESERVED    0x00000000 // No flags defined yet for XNetQosServiceLookup

INT   WSAAPI XNetQosListen(__in const XNKID * pxnkid, __in_ecount_opt(cb) const BYTE * pb, __in UINT cb, __in DWORD dwBitsPerSec, __in DWORD dwFlags);
INT   WSAAPI XNetQosLookup(__in UINT cxna, __in_ecount_opt(cxna) const XNADDR * apxna[], __in_ecount_opt(cxna) const XNKID * apxnkid[], __in_ecount_opt(cxna) const XNKEY * apxnkey[], __in UINT cina, __in_ecount_opt(cina) const IN_ADDR aina[], __in_ecount_opt(cina) const DWORD adwServiceId[], __in UINT cProbes, __in DWORD dwBitsPerSec, __in DWORD dwFlags, __in_opt WSAEVENT hEvent, __out XNQOS ** ppxnqos);
INT   WSAAPI XNetQosServiceLookup(__in DWORD dwFlags, __in_opt WSAEVENT hEvent, __out XNQOS ** ppxnqos);
INT   WSAAPI XNetQosRelease(__in XNQOS * pxnqos);
INT   WSAAPI XNetQosGetListenStats(__in const XNKID * pxnkid, __inout XNQOSLISTENSTATS * pQosListenStats);


#define XNET_GET_XNADDR_PENDING                 0x00000000 // Address acquisition is not yet complete
#define XNET_GET_XNADDR_NONE                    0x00000001 // XNet is uninitialized or no debugger found
#define XNET_GET_XNADDR_ETHERNET                0x00000002 // Host has ethernet address (no IP address)
#define XNET_GET_XNADDR_STATIC                  0x00000004 // Host has statically assigned IP address
#define XNET_GET_XNADDR_DHCP                    0x00000008 // Host has DHCP assigned IP address
#define XNET_GET_XNADDR_PPPOE                   0x00000010 // Host has PPPoE assigned IP address
#define XNET_GET_XNADDR_GATEWAY                 0x00000020 // Host has one or more gateways configured
#define XNET_GET_XNADDR_DNS                     0x00000040 // Host has one or more DNS servers configured
#define XNET_GET_XNADDR_ONLINE                  0x00000080 // Host is currently connected to online service
#define XNET_GET_XNADDR_TROUBLESHOOT            0x00008000 // Network configuration requires troubleshooting
#define XNET_GET_XNADDR_TROUBLESHOOT_SYSTEMLINK 0x00010000 // The system link port requires troubleshooting

DWORD WSAAPI XNetGetTitleXnAddr(__out XNADDR * pxna);

#define XNET_ETHERNET_LINK_ACTIVE           0x00000001 // Ethernet cable is connected and active
#define XNET_ETHERNET_LINK_100MBPS          0x00000002 // Ethernet link is set to 100 Mbps
#define XNET_ETHERNET_LINK_10MBPS           0x00000004 // Ethernet link is set to 10 Mbps
#define XNET_ETHERNET_LINK_FULL_DUPLEX      0x00000008 // Ethernet link is in full duplex mode
#define XNET_ETHERNET_LINK_HALF_DUPLEX      0x00000010 // Ethernet link is in half duplex mode
#define XNET_ETHERNET_LINK_WIRELESS         0x00000020 // Ethernet link is wireless (802.11 based)


DWORD WSAAPI XNetGetEthernetLinkStatus();


#define XNET_BROADCAST_VERSION_OLDER        0x00000001 // Got broadcast packet(s) from incompatible older version of title
#define XNET_BROADCAST_VERSION_NEWER        0x00000002 // Got broadcast packet(s) from incompatible newer version of title

DWORD WSAAPI XNetGetBroadcastVersionStatus(__in BOOL fReset);


//
// Value = XNetStartupParams
// Get   = Returns the XNetStartupParams values that were used at
//         initialization time.
// Set   = Not allowed.
//
#define XNET_OPTID_STARTUP_PARAMS                   1

//
// Value = ULONGLONG
// Get   = Returns total number of bytes sent by the NIC hardware since system
//         boot, including sizes of all protocol headers.
// Set   = Not allowed.
//
#define XNET_OPTID_NIC_XMIT_BYTES                   2

//
// Value = DWORD
// Get   = Returns total number of frames sent by the NIC hardware since system
//         boot.
// Set   = Not allowed.
//
#define XNET_OPTID_NIC_XMIT_FRAMES                  3

//
// Value = ULONGLONG
// Get   = Returns total number of bytes received by the NIC hardware since
//         system boot, including sizes of all protocol headers.
// Set   = Not allowed.
//
#define XNET_OPTID_NIC_RECV_BYTES                   4

//
// Value = DWORD
// Get   = Returns total number of frames received by the NIC hardware since
//         system boot.
// Set   = Not allowed.
//
#define XNET_OPTID_NIC_RECV_FRAMES                  5

//
// Value = ULONGLONG
// Get   = Returns the number of bytes sent by the caller since XNetStartup/
//         WSAStartup, including sizes of all protocol headers.
// Set   = Not allowed.
//
#define XNET_OPTID_CALLER_XMIT_BYTES                6

//
// Value = DWORD
// Get   = Returns total number of frames sent by the caller since XNetStartup/
//         WSAStartup.
// Set   = Not allowed.
//
#define XNET_OPTID_CALLER_XMIT_FRAMES               7

//
// Value = ULONGLONG
// Get   = Returns total number of bytes received by the caller since
//         XNetStartup/WSAStartup, including sizes of all protocol headers.
// Set   = Not allowed.
//
#define XNET_OPTID_CALLER_RECV_BYTES                8

//
// Value = DWORD
// Get   = Returns total number of frames received by the caller since
//         XNetStartup/WSAStartup.
// Set   = Not allowed.
//
#define XNET_OPTID_CALLER_RECV_FRAMES               9


INT    WSAAPI XNetGetOpt(__in DWORD dwOptId, __out_ecount_part_opt(*pdwValueSize, *pdwValueSize) BYTE * pbValue, __inout DWORD * pdwValueSize);
INT    WSAAPI XNetSetOpt(__in DWORD dwOptId, __in_ecount(dwValueSize) const BYTE * pbValue, __in DWORD dwValueSize);



//
// The following protocol can be passed to the socket() API to create a datagram socket that
// transports encrypted data and unencrypted voice in the same packet.
//

#define IPPROTO_VDP                         254

INT   WSAAPI XNetGetSystemLinkPort(__out WORD * pwSystemLinkPort);
INT   WSAAPI XNetSetSystemLinkPort(__in WORD wSystemLinkPort);


#ifdef __cplusplus
}
#endif

#ifndef _WIN64
#include <poppack.h>
#endif



#endif  /* _WINSOCK2API_ */

