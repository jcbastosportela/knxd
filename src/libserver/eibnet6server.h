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

#ifndef EIBNET6_SERVER_H
#define EIBNET6_SERVER_H

#include <ev++.h>
#include "callbacks.h"
#include "eibnetip6.h"
#include "link.h"
#include "server.h"
#include "lpdu.h"


class EIBnet6Server;
typedef std::shared_ptr<EIBnet6Server> EIBnet6ServerPtr;

typedef enum {
	CT_NONE = 0,
	CT_STANDARD,
	CT_BUSMONITOR,
	CT_CONFIG,
} ConnType;


/** Driver for tunnels */
class ConnState_ipv6: public SubDriver, public L_Busmonitor_CallBack
{
public:
  ConnState_ipv6 (LinkConnectClientPtr c, eibaddr_t addr);
  virtual ~ConnState_ipv6 ();
  bool setup();
  // void start();
  void stop();

  EIBnet6Server *parent;

  eibaddr_t addr;
  uchar channel;
  uchar sno;
  uchar rno;
  int retries;
  ConnType type = CT_NONE;
  int no;
  bool nat;

  ev::timer timeout; void timeout_cb(ev::timer &w, int revents);
  ev::timer sendtimeout; void sendtimeout_cb(ev::timer &w, int revents);
  ev::async send_trigger; void send_trigger_cb(ev::async &w, int revents);
  bool do_send_next = false;
  Queue < CArray > out;
  void reset_timer();

  struct sockaddr_in6 daddr;
  struct sockaddr_in6 caddr;

  // handle various packets from the connection
  void tunnel_request(EIBnet6_TunnelRequest &r1, EIBNetIPSocket *isock);
  void tunnel_response(EIBnet6_TunnelACK &r1);
  void config_request(EIBnet6_ConfigRequest &r1, EIBNetIPSocket *isock);
  void config_response (EIBnet6_ConfigACK &r1);

  void send_L_Data (LDataPtr l);
  void send_L_Busmonitor (LBusmonPtr l);
};
typedef std::shared_ptr<ConnState_ipv6> ConnState_ipv6Ptr;


/** Driver for routing */
class EIBnet6Driver : public SubDriver
{
  EIBNetIPSocket *sock; // receive only

  void recv_cb(EIBNetIPPacket *p);
  EIBPacketCallback on_recv;
  void error_cb();

public:
  EIBnet6Driver (LinkConnectClientPtr c, std::string& multicastaddr, int port, std::string& intf);
  virtual ~EIBnet6Driver ();
  struct sockaddr_in6 maddr;

  bool setup();
  // void start();
  // void stop();

  void Send (EIBNetIPPacket p, struct sockaddr_in6 addr);

  void send_L_Data (LDataPtr l);
};

typedef std::shared_ptr<EIBnet6Driver> EIBnet6DriverPtr;

SERVER(EIBnet6Server,ets_router6)
{
  friend class ConnState_ipv6_ipv6;
  friend class EIBnet6Driver;

  EIBnet6DriverPtr mcast;   // used for multicast receiving
  EIBNetIPSocket *sock;  // used for normal dialog

  int sock_mac;          // used to query the list of interfaces
  int Port;              // copy of sock->port()

  /** config */
  bool tunnel;
  bool route;
  bool discover;
  bool single_port;
  std::string multicastaddr;
  uint16_t port;
  std::string interface;
  std::string servername;
  IniSectionPtr router_cfg;
  IniSectionPtr tunnel_cfg;

  Array < ConnState_ipv6Ptr > connections;
  Queue < ConnState_ipv6Ptr > drop_q;

  int addClient (ConnType type, const EIBnet6_ConnectRequest & r1,
                 eibaddr_t addr = 0);
  void addNAT (const LDataPtr &&l);

  void recv_cb(EIBNetIPPacket *p);
  void error_cb();

  void stop_();
public:
  EIBnet6Server (BaseRouter& r, IniSectionPtr& s);
  virtual ~EIBnet6Server ();
  bool setup ();
  void start();
  void stop();

  void handle_packet (EIBNetIPPacket *p1, EIBNetIPSocket *isock);

  void drop_connection (ConnState_ipv6Ptr s);
  ev::async drop_trigger; void drop_trigger_cb(ev::async &w, int revents);

  inline void Send (EIBNetIPPacket p) {
    Send (p, mcast->maddr);
  }
  inline void Send (EIBNetIPPacket p, struct sockaddr_in addr) {
    if (sock)
      sock->Send (p, addr);
  }

  bool checkAddress(eibaddr_t addr UNUSED) { return route; }
  bool checkGroupAddress(eibaddr_t addr UNUSED) { return route; }
};
typedef std::shared_ptr<EIBnet6Server> EIBnet6ServerPtr;

#endif
