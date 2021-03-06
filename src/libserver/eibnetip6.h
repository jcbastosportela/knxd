/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
/*
    date modified: 13 feb 2018
    author: João Carlos Bastos Portela <jcbastosportela@gmail.com>
*/

#ifndef EIBNETIP_H
#define EIBNETIP_H

#include <netinet/in.h>
#include <ev++.h>
#include "common.h"
#include "iobuf.h" // for nonblocking
#include "lpdu.h"
#include "ipsupport.h"

// all values are from 03_08_01 5.* unless otherwise specified

typedef enum {
  SEARCH_REQUEST = 0x0201,
  SEARCH_RESPONSE = 0x0202,
  DESCRIPTION_REQUEST = 0x0203,
  DESCRIPTION_RESPONSE = 0x0204,

  CONNECTION_REQUEST = 0x0205,
  CONNECTION_RESPONSE = 0x0206,
  CONNECTIONSTATE_REQUEST = 0x0207,
  CONNECTIONSTATE_RESPONSE = 0x0208,
  DISCONNECT_REQUEST = 0x0209,
  DISCONNECT_RESPONSE = 0x020A,

  DEVICE_CONFIGURATION_REQUEST = 0x0310,
  DEVICE_CONFIGURATION_ACK = 0x0311,

  TUNNEL_REQUEST = 0x0420,
  TUNNEL_RESPONSE = 0x0421,

  ROUTING_INDICATION = 0x0530,
  ROUTING_LOST_MESSAGE = 0x0531,
} ServiceType;

typedef enum 
{
  DEVICE_MGMT_CONNECTION = 0x03,
  TUNNEL_CONNECTION = 0x04,
  REMLOG_CONNECTION = 0x06,
  REMCONF_CONNECTION = 0x07,
  OBJSRV_CONNECTION = 0x08,
} ConnectionType;

typedef enum 
{
  E_NO_ERROR = 0x00,
  E_HOST_PROTOCOL_TYPE = 0x01,
  E_VERSION_NOT_SUPPORTED = 0x02,
  E_SEQUENCE_NUMBER = 0x04,
  E_CONNECTION_ID = 0x21,
  E_CONNECTION_TYPE = 0x22,
  E_CONNECTION_OPTION = 0x23,
  E_NO_MORE_CONNECTIONS = 0x24,
  E_NO_MORE_UNIQUE_CONNECTIONS = 0x25, // 03.08.04, 2.2.2
  E_DATA_CONNECTION = 0x26,
  E_KNX_CONNECTION = 0x27,
  E_TUNNELING_LAYER = 0x29,
} ErrorCode;

typedef enum {
    DEVICE_INFO = 0x01,
    SUPP_SVC_FAMILIES = 0x02,
} DIBcode;

typedef enum {
    M_TP1 = 0x02,
    M_PL110 = 0x04,
    M_RF = 0x10,
    M_IP = 0x20,
} MediumCode;

// timeouts
#define CONNECT_REQUEST_TIMEOUT 10
#define CONNECTIONSTATE_REQUEST_TIMEOUT 10
#define DEVICE_CONFIGURATION_REQUEST_TIMEOUT 10
#define TUNNELING_REQUEST_TIMEOUT 1
#define CONNECTION_ALIVE_TIME 120

typedef enum {
  S_RDWR, S_RD, S_WR,
} SockMode;

/** represents a EIBnet/IP packet */
class EIBNet6IPPacket
{

public:
  /** service code*/
  int service;
  /** payload */
  CArray data;
  /** source address */
  struct sockaddr_in6 src;

  EIBNet6IPPacket ();
  /** create from character array */
  static EIBNet6IPPacket *fromPacket (const CArray & c,
				     const struct sockaddr_in6 src);
  /** convert to character array */
  CArray ToPacket () const;
  virtual ~EIBNet6IPPacket ()
  {
  }
};

class EIBnet6_ConnectRequest
{
public:
  EIBnet6_ConnectRequest ();
  struct sockaddr_in6 caddr;
  struct sockaddr_in6 daddr;
  CArray CRI;
  bool nat;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConnectRequest (const EIBNet6IPPacket & p,
				EIBnet6_ConnectRequest & r);

class EIBnet6_ConnectResponse
{
public:
  EIBnet6_ConnectResponse ();
  uchar channel;
  uchar status;
  struct sockaddr_in6 daddr;
  bool nat;
  CArray CRD;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConnectResponse (const EIBNet6IPPacket & p,
				 EIBnet6_ConnectResponse & r);

class EIBnet6_ConnectionStateRequest
{
public:
  EIBnet6_ConnectionStateRequest ();
  uchar channel;
  uchar status;
  struct sockaddr_in6 caddr;
  bool nat;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConnectionStateRequest (const EIBNet6IPPacket & p,
					EIBnet6_ConnectionStateRequest & r);

class EIBnet6_ConnectionStateResponse
{
public:
  EIBnet6_ConnectionStateResponse ();
  uchar channel;
  uchar status;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConnectionStateResponse (const EIBNet6IPPacket & p,
					 EIBnet6_ConnectionStateResponse & r);

class EIBnet6_DisconnectRequest
{
public:
  EIBnet6_DisconnectRequest ();
  struct sockaddr_in6 caddr;
  uchar channel;
  bool nat;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_DisconnectRequest (const EIBNet6IPPacket & p,
				   EIBnet6_DisconnectRequest & r);

class EIBnet6_DisconnectResponse
{
public:
  EIBnet6_DisconnectResponse ();
  uchar channel;
  uchar status;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_DisconnectResponse (const EIBNet6IPPacket & p,
				    EIBnet6_DisconnectResponse & r);

class EIBnet6_TunnelRequest
{
public:
  EIBnet6_TunnelRequest ();
  uchar channel;
  uchar seqno;
  CArray CEMI;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_TunnelRequest (const EIBNet6IPPacket & p,
			       EIBnet6_TunnelRequest & r);

class EIBnet6_TunnelACK
{
public:
  EIBnet6_TunnelACK ();
  uchar channel;
  uchar seqno;
  uchar status;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_TunnelACK (const EIBNet6IPPacket & p, EIBnet6_TunnelACK & r);

class EIBnet6_ConfigRequest
{
public:
  EIBnet6_ConfigRequest ();
  uchar channel;
  uchar seqno;
  CArray CEMI;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConfigRequest (const EIBNet6IPPacket & p,
			       EIBnet6_ConfigRequest & r);

class EIBnet6_ConfigACK
{
public:
  EIBnet6_ConfigACK ();
  uchar channel;
  uchar seqno;
  uchar status;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_ConfigACK (const EIBNet6IPPacket & p, EIBnet6_ConfigACK & r);

typedef struct
{
  uchar family;
  uchar version;
} DIB_service_Entry;

class EIBnet6_DescriptionRequest
{
public:
  EIBnet6_DescriptionRequest ();
  struct sockaddr_in6 caddr;
  bool nat;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_DescriptionRequest (const EIBNet6IPPacket & p,
				    EIBnet6_DescriptionRequest & r);

class EIBnet6_DescriptionResponse
{
public:
  EIBnet6_DescriptionResponse ();
  uchar KNXmedium;
  uchar devicestatus;
  eibaddr_t individual_addr;
  uint16_t installid;
  serialnumber_t serial;
  Array < DIB_service_Entry > services;
  struct in6_addr multicastaddr;
  uchar MAC[6];
  uchar name[30];
  CArray optional;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_DescriptionResponse (const EIBNet6IPPacket & p,
				     EIBnet6_DescriptionResponse & r);

class EIBnet6_SearchRequest
{
public:
  EIBnet6_SearchRequest ();
  struct sockaddr_in6 caddr;
  bool nat;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_SearchRequest (const EIBNet6IPPacket & p,
			       EIBnet6_SearchRequest & r);

class EIBnet6_SearchResponse
{
public:
  EIBnet6_SearchResponse ();
  uchar KNXmedium;
  uchar devicestatus;
  eibaddr_t individual_addr;
  uint16_t installid;
  serialnumber_t serial;
  Array < DIB_service_Entry > services;
  struct in6_addr multicastaddr;
  uchar MAC[6];
  uchar name[30];
  struct sockaddr_in6 caddr;
  bool nat = false;
  EIBNet6IPPacket ToPacket () const;
};

int parseEIBnet6_SearchResponse (const EIBNet6IPPacket & p,
				EIBnet6_SearchResponse & r);



typedef void (*eibpacket_cb_t)(void *data, EIBNet6IPPacket *p);

class EIBPacketCallback {
    eibpacket_cb_t cb_code = 0;
    void *cb_data = 0;

    void set_ (const void *data, eibpacket_cb_t cb)
    {
      this->cb_data = (void *)data;
      this->cb_code = cb;
    }

public:
    // function callback
    template<void (*function)(EIBNet6IPPacket *p)>
    void set (void *data = 0) throw ()
    {
      set_ (data, function_thunk<function>);
    }

    template<void (*function)(EIBNet6IPPacket *p)>
    static void function_thunk (void *arg, EIBNet6IPPacket *p)
    {
      function(p);
    }

    // method callback
    template<class K, void (K::*method)(EIBNet6IPPacket *p)>
    void set (K *object)
    {
      set_ (object, method_thunk<K, method>);
    }

    template<class K, void (K::*method)(EIBNet6IPPacket *p)>
    static void method_thunk (void *arg, EIBNet6IPPacket *p)
    {
      (static_cast<K *>(arg)->*method) (p);
    }

    void operator()(EIBNet6IPPacket *p) {
        (*cb_code)(cb_data, p);
    }
};


/** represents a EIBnet/IP packet to send*/
struct _EIBNet6IP_Send
{
  /** packat */
  EIBNet6IPPacket data;
  /** destination address */
  struct sockaddr_in6 addr;
};

/** EIBnet/IP socket */
class EIBNet6IPSocket
{
  /** debug output */
  TracePtr t;
  /** input */
  ev::io io_recv; void io_recv_cb (ev::io &w, int revents);
  /** output */
  ev::io io_send; void io_send_cb (ev::io &w, int revents);
  unsigned int send_error;

public:
  EIBPacketCallback on_recv;
  InfoCallback on_error;
  InfoCallback on_next;
private:
  void recv_cb(EIBNet6IPPacket *p)
    {
      t->TracePacket (0, "Drop", p->data);
      delete p;
    }
  void error_cb() { stop(); }
  void next_cb() { }

  /** output queue */
  Queue < struct _EIBNet6IP_Send > send_q;
  void send_q_drop();

  /** multicast address */
  struct ipv6_mreq maddr;
  /** file descriptor */
  int fd;
  /** multicast in use? */
  bool multicast;

public:
  EIBNet6IPSocket (struct sockaddr_in6 bindaddr, bool reuseaddr, TracePtr tr,
                  SockMode mode = S_RDWR);
  virtual ~EIBNet6IPSocket ();
  bool init ();
  void stop();

  /** enables multicast */
  bool SetMulticast (struct ipv6_mreq multicastaddr);
  /** sends a packet */
  void Send (EIBNet6IPPacket p, struct sockaddr_in6 addr);
  void Send (EIBNet6IPPacket p) { Send (p, sendaddr); }

  /** get the port this socket is bound to (network byte order) */
  int port ();

  bool SetInterface(std::string& iface);

  /** default send address */
  struct sockaddr_in6 sendaddr;

  /** address to accept packets from, if recvall is 0 */
  struct sockaddr_in6 recvaddr;
  /** address to also accept packets from, if 'recvall' is 3 */
  struct sockaddr_in6 recvaddr2;
  /** address to NOT accept packets from, if 'recvall' is 2 */
  struct sockaddr_in6 localaddr;

  void pause();
  void unpause();
  bool paused;

  /** flag whether to accept (almost) all packets */
  uchar recvall;
};

#endif
