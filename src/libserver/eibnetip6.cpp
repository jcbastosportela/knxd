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

#include <string.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include "eibnetip6.h"
#include "config.h"

EIBNet6IPPacket::EIBNet6IPPacket ()
{
  service = 0;
  memset (&src, 0, sizeof (src));
}

EIBNet6IPPacket *
EIBNet6IPPacket::fromPacket (const CArray & c, const struct sockaddr_in6 src)
{
  EIBNet6IPPacket *p;
  if (c.size() < 6)
    return 0;
  if (c[0] != 0x6 || c[1] != 0x10)
    return 0;
  unsigned len = (c[4] << 8) | c[5];
  if (len != c.size())
    return 0;
  p = new EIBNet6IPPacket;
  p->service = (c[2] << 8) | c[3];
  p->data.set (c.data() + 6, len - 6);
  p->src = src;
  return p;
}

CArray
EIBNet6IPPacket::ToPacket ()
  CONST
{
  CArray c;
  c.resize (6 + data.size());
  c[0] = 0x06;
  c[1] = 0x10;
  c[2] = (service >> 8) & 0xff;
  c[3] = (service) & 0xff;
  c[4] = ((data.size() + 6) >> 8) & 0xff;
  c[5] = ((data.size() + 6)) & 0xff;
  c.setpart (data, 6);
  return c;
}

CArray
IPtoEIBNetIP (const struct sockaddr_in6 * a, bool nat)
{
  CArray buf;
  buf.resize (18);
  buf[0] = 0x08;
  buf[1] = 0x01;
  if (nat)
    {
      memset( &buf[2], 0, 18-2 );
    }
  else
    {
      for( uint8_t u8idx = 0; u8idx < 16; u8idx++ )
      {
        buf[2+u8idx] = a->sin6_addr.s6_addr[15-u8idx];
      }
      buf[6] = (ntohs (a->sin6_port) >> 8) & 0xff;
      buf[7] = (ntohs (a->sin6_port) >> 0) & 0xff;
    }
  return buf;
}

bool
EIBnettoIP (const CArray & buf, struct sockaddr_in6 *a,
	    const struct sockaddr_in6 *src, bool & nat)
{
  int port;
  struct in6_addr ip;
  uint8_t u8ipCalc = 0;

  memset (a, 0, sizeof (*a));
  if (buf[0] != 0x8 || buf[1] != 0x1)
    return true;
  for( uint8_t u8idx = 0; u8idx < 16; u8idx++)
  {
    ip.s6_addr[15-u8idx]=buf[2+u8idx];
    u8ipCalc |= buf[2+u8idx];
  }
  port = (buf[6] << 8) | (buf[7]);
#ifdef HAVE_SOCKADDR_IN_LEN
  a->sin6_len = sizeof (*a);
#endif
  a->sin6_family = AF_INET6;
  if (port == 0)
    a->sin6_port = src->sin6_port;
  else
    a->sin6_port = htons (port);
  if (u8ipCalc == 0)
    {
      nat = true;
      a->sin6_addr = src->sin6_addr;
    }
  else
    a->sin6_addr = ip;

  return false;
}

EIBNet6IPSocket::EIBNet6IPSocket (struct sockaddr_in6 bindaddr, bool reuseaddr,
				TracePtr tr, SockMode mode)
{
  int i;
  t = tr;
  TRACEPRINTF (t, 0, "Open");
  multicast = false;
  memset (&maddr, 0, sizeof (maddr));
  memset (&sendaddr, 0, sizeof (sendaddr));
  memset (&recvaddr, 0, sizeof (recvaddr));
  memset (&recvaddr2, 0, sizeof (recvaddr2));
  recvall = 0;

  io_send.set<EIBNet6IPSocket, &EIBNet6IPSocket::io_send_cb>(this);
  io_recv.set<EIBNet6IPSocket, &EIBNet6IPSocket::io_recv_cb>(this);
  on_recv.set<EIBNet6IPSocket, &EIBNet6IPSocket::recv_cb>(this); // dummy
  on_error.set<EIBNet6IPSocket, &EIBNet6IPSocket::error_cb>(this); // dummy
  on_next.set<EIBNet6IPSocket, &EIBNet6IPSocket::next_cb>(this); // dummy

  fd = socket (AF_INET6, SOCK_DGRAM, 0);
  if (fd == -1)
    return;
  set_non_blocking(fd);

  // Set the number of hops: this will affect how many subnetworks it can pass
  // TODO: maybe this must be a parameter
  uint32_t u8Hops = 10;
  if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &u8Hops, sizeof(u8Hops)) < 0)
  {
    ERRORPRINTF (t, E_ERROR | 39, "cannot set number of hops: %s", strerror(errno));
    close(fd);
    fd = -1;
    return;
  }

  if (reuseaddr)
    {
      i = 1;
      if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof (i)) == -1)
	{
          ERRORPRINTF (t, E_ERROR | 45, "cannot reuse address: %s", strerror(errno));
	  close (fd);
	  fd = -1;
	  return;
	}
    }
  if (bind (fd, (struct sockaddr *) &bindaddr, sizeof (bindaddr)) == -1)
    {
      ERRORPRINTF (t, E_ERROR | 38, "cannot bind to address: %s", strerror(errno));
      close (fd);
      fd = -1;
      return;
    }

  // Enable loopback so processes on the same host see each other.
  {
    uint32_t loopch=1;
 
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loopch, sizeof(loopch)) < 0)
    {
      ERRORPRINTF (t, E_ERROR | 39, "cannot turn on multicast loopback: %s", strerror(errno));
      close(fd);
      fd = -1;
      return;
    }
  }

  // don't really care if this fails
  if (mode == S_RD)
    shutdown (fd, SHUT_WR);
  if (mode == S_WR)
    shutdown (fd, SHUT_RD);
  else
    io_recv.start(fd, ev::READ);

  TRACEPRINTF (t, 0, "Opened");
}

EIBNet6IPSocket::~EIBNet6IPSocket ()
{
  TRACEPRINTF (t, 0, "Close");
  stop();
}

void
EIBNet6IPSocket::stop()
{
  if (fd != -1)
    {
      io_recv.stop();
      io_send.stop();
      if (multicast)
	setsockopt (fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &maddr,
		    sizeof (maddr));
      close (fd);
      fd = -1;
    }
}

void
EIBNet6IPSocket::pause()
{
    if (paused)
        return;
    paused = true;
    io_recv.stop();
}

void
EIBNet6IPSocket::unpause()
{
    if (! paused)
        return;
    paused = false;
    io_recv.start(fd, ev::READ);
}

bool
EIBNet6IPSocket::init ()
{
  if (fd < 0)
    return false;
  io_recv.start(fd, ev::READ);
  return true;
}

int
EIBNet6IPSocket::port ()
{
  struct sockaddr_in6 sa;
  socklen_t saLen = sizeof(sa);
  if (getsockname(fd, (struct sockaddr *) &sa, &saLen) < 0)
    return -1;
  if (sa.sin6_family != AF_INET6)
    {
      errno = ENODATA;
      return -1;
    }
  return sa.sin6_port;
}

bool
EIBNet6IPSocket::SetMulticast (struct ipv6_mreq multicastaddr)
{
  if (multicast)
    return false;
  maddr = multicastaddr;
  if (setsockopt (fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &maddr, sizeof (maddr))
      == -1)
    return false;
  multicast = true;
  return true;
}

void
EIBNet6IPSocket::Send (EIBNet6IPPacket p, struct sockaddr_in6 addr)
{
  struct _EIBNet6IP_Send s;
  t->TracePacket (1, "Send", p.data);
  s.data = p;
  s.addr = addr;

  if (send_q.isempty())
    io_send.start(fd, ev::WRITE);
  send_q.put (std::move(s));
}

void
EIBNet6IPSocket::io_send_cb (ev::io &w UNUSED, int revents UNUSED)
{
  if (send_q.isempty ())
    {
      io_send.stop();
      on_next();
      return;
    }
  const struct _EIBNet6IP_Send s = send_q.front ();
  CArray p = s.data.ToPacket ();
  t->TracePacket (0, "Send", p);
  int i = sendto (fd, p.data(), p.size(), 0,
                      (const struct sockaddr *) &s.addr, sizeof (s.addr));
  if (i > 0)
    {
      send_q.get ();
      send_error = 0;
    }
  else
    {
      if (i == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        {
          TRACEPRINTF (t, 0, "Send: %s", strerror(errno));
          if (send_error++ > 5)
            {
              t->TracePacket (0, "EIBnetSocket:drop", p);
              send_q.get ();
              send_error = 0;
              on_error();
            }
        }
    }
}

void
EIBNet6IPSocket::io_recv_cb (ev::io &w UNUSED, int revents UNUSED)
{
  uchar buf[255];
  socklen_t rl;
  sockaddr_in6 r;
  rl = sizeof (r);
  memset (&r, 0, sizeof (r));

  int i = recvfrom (fd, buf, sizeof (buf), 0, (struct sockaddr *) &r, &rl);
  if (i < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    on_error();
  else if (i >= 0 && rl == sizeof (r))
    {
      if (recvall == 1 || !memcmp (&r, &recvaddr, sizeof (r)) ||
          (recvall == 2 && memcmp (&r, &localaddr, sizeof (r))) ||
          (recvall == 3 && !memcmp (&r, &recvaddr2, sizeof (r))))
        {
          t->TracePacket (0, "Recv", i, buf);
          EIBNet6IPPacket *p =
            EIBNet6IPPacket::fromPacket (CArray (buf, i), r);
          if (p)
            on_recv(p);
          else
            t->TracePacket (0, "Parse?", i, buf);
        }
      else
        t->TracePacket (0, "Dropped", i, buf);
    }
}

bool
EIBNet6IPSocket::SetInterface(std::string& iface)
{
  struct sockaddr_in6 sa;
  struct ipv6_mreq addr;

  memset (&sa, 0, sizeof(sa));
  memset (&addr, 0, sizeof(addr));

  if (iface.size() == 0)
    return true;
  addr.ipv6mr_interface = if_nametoindex(iface.c_str());
  return
    setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &addr, sizeof(addr)) >= 0;
}

EIBnet6_ConnectRequest::EIBnet6_ConnectRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  memset (&daddr, 0, sizeof (daddr));
  nat = false;
}

EIBNet6IPPacket EIBnet6_ConnectRequest::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  CArray ca, da;
  ca = IPtoEIBNetIP (&caddr, nat);
  da = IPtoEIBNetIP (&daddr, nat);
  p.service = CONNECTION_REQUEST;
  p.data.resize (ca.size() + da.size() + 1 + CRI.size());
  p.data.setpart (ca, 0);
  p.data.setpart (da, ca.size());
  p.data[ca.size() + da.size()] = CRI.size() + 1;
  p.data.setpart (CRI, ca.size() + da.size() + 1);
  return p;
}

int
parseEIBnet6_ConnectRequest (const EIBNet6IPPacket & p,
			    EIBnet6_ConnectRequest & r)
{
  if (p.service != CONNECTION_REQUEST)
    return 1;
  if (p.data.size() < 18)
    return 1;
  if (EIBnettoIP (CArray (p.data.data(), 8), &r.caddr, &p.src, r.nat))
    return 1;
  if (EIBnettoIP (CArray (p.data.data() + 8, 8), &r.daddr, &p.src, r.nat))
    return 1;
  if (p.data.size() - 16 != p.data[16])
    return 1;
  r.CRI = CArray (p.data.data() + 17, p.data.size() - 17);
  return 0;
}

EIBnet6_ConnectResponse::EIBnet6_ConnectResponse ()
{
  memset (&daddr, 0, sizeof (daddr));
  nat = false;
  channel = 0;
  status = 0;
}

EIBNet6IPPacket EIBnet6_ConnectResponse::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  CArray da = IPtoEIBNetIP (&daddr, nat);
  p.service = CONNECTION_RESPONSE;
  if (status != 0)
    p.data.resize (2);
  else
    p.data.resize (da.size() + CRD.size() + 3);
  p.data[0] = channel;
  p.data[1] = status;
  if (status == 0)
    {
      p.data.setpart (da, 2);
      p.data[da.size() + 2] = CRD.size() + 1;
      p.data.setpart (CRD, da.size() + 3);
    }
  return p;
}

int
parseEIBnet6_ConnectResponse (const EIBNet6IPPacket & p,
			     EIBnet6_ConnectResponse & r)
{
  if (p.service != CONNECTION_RESPONSE)
    return 1;
  if (p.data.size() < 2)
    return 1;
  if (p.data[1] != 0)
    {
      if (p.data.size() != 2)
	return 1;
      r.channel = p.data[0];
      r.status = p.data[1];
      return 0;
    }
  if (p.data.size() < 12)
    return 1;
  if (EIBnettoIP (CArray (p.data.data() + 2, 8), &r.daddr, &p.src, r.nat))
    return 1;
  if (p.data.size() - 10 != p.data[10])
    return 1;
  r.channel = p.data[0];
  r.status = p.data[1];
  r.CRD = CArray (p.data.data() + 11, p.data.size() - 11);
  return 0;
}

EIBnet6_ConnectionStateRequest::EIBnet6_ConnectionStateRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
  channel = 0;
}

EIBNet6IPPacket EIBnet6_ConnectionStateRequest::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  CArray ca = IPtoEIBNetIP (&caddr, nat);
  p.service = CONNECTIONSTATE_REQUEST;
  p.data.resize (ca.size() + 2);
  p.data[0] = channel;
  p.data[1] = 0;
  p.data.setpart (ca, 2);
  return p;
}

int
parseEIBnet6_ConnectionStateRequest (const EIBNet6IPPacket & p,
				    EIBnet6_ConnectionStateRequest & r)
{
  if (p.service != CONNECTIONSTATE_REQUEST)
    return 1;
  if (p.data.size() != 10)
    return 1;
  if (EIBnettoIP (CArray (p.data.data() + 2, 8), &r.caddr, &p.src, r.nat))
    return 1;
  r.channel = p.data[0];
  return 0;
}

EIBnet6_ConnectionStateResponse::EIBnet6_ConnectionStateResponse ()
{
  channel = 0;
  status = 0;
}

EIBNet6IPPacket EIBnet6_ConnectionStateResponse::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = CONNECTIONSTATE_RESPONSE;
  p.data.resize (2);
  p.data[0] = channel;
  p.data[1] = status;
  return p;
}

int
parseEIBnet6_ConnectionStateResponse (const EIBNet6IPPacket & p,
				     EIBnet6_ConnectionStateResponse & r)
{
  if (p.service != CONNECTIONSTATE_RESPONSE)
    return 1;
  if (p.data.size() != 2)
    return 1;
  r.channel = p.data[0];
  r.status = p.data[1];
  return 0;
}

EIBnet6_DisconnectRequest::EIBnet6_DisconnectRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
  channel = 0;
}

EIBNet6IPPacket EIBnet6_DisconnectRequest::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  CArray ca = IPtoEIBNetIP (&caddr, nat);
  p.service = DISCONNECT_REQUEST;
  p.data.resize (ca.size() + 2);
  p.data[0] = channel;
  p.data[1] = 0;
  p.data.setpart (ca, 2);
  return p;
}

int
parseEIBnet6_DisconnectRequest (const EIBNet6IPPacket & p,
			       EIBnet6_DisconnectRequest & r)
{
  if (p.service != DISCONNECT_REQUEST)
    return 1;
  if (p.data.size() != 10)
    return 1;
  if (EIBnettoIP (CArray (p.data.data() + 2, 8), &r.caddr, &p.src, r.nat))
    return 1;
  r.channel = p.data[0];
  return 0;
}

EIBnet6_DisconnectResponse::EIBnet6_DisconnectResponse ()
{
  channel = 0;
  status = 0;
}

EIBNet6IPPacket EIBnet6_DisconnectResponse::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = DISCONNECT_RESPONSE;
  p.data.resize (2);
  p.data[0] = channel;
  p.data[1] = status;
  return p;
}

int
parseEIBnet6_DisconnectResponse (const EIBNet6IPPacket & p,
				EIBnet6_DisconnectResponse & r)
{
  if (p.service != DISCONNECT_RESPONSE)
    return 1;
  if (p.data.size() != 2)
    return 1;
  r.channel = p.data[0];
  r.status = p.data[1];
  return 0;
}

EIBnet6_TunnelRequest::EIBnet6_TunnelRequest ()
{
  channel = 0;
  seqno = 0;
}

EIBNet6IPPacket EIBnet6_TunnelRequest::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = TUNNEL_REQUEST;
  p.data.resize (CEMI.size() + 4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = 0;
  p.data.setpart (CEMI, 4);
  return p;
}

int
parseEIBnet6_TunnelRequest (const EIBNet6IPPacket & p, EIBnet6_TunnelRequest & r)
{
  if (p.service != TUNNEL_REQUEST)
    return 1;
  if (p.data.size() < 6)
    return 1;
  if (p.data[0] != 4)
    return 1;
  r.channel = p.data[1];
  r.seqno = p.data[2];
  r.CEMI.set (p.data.data() + 4, p.data.size() - 4);
  return 0;
}

EIBnet6_TunnelACK::EIBnet6_TunnelACK ()
{
  channel = 0;
  seqno = 0;
  status = 0;
}

EIBNet6IPPacket EIBnet6_TunnelACK::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = TUNNEL_RESPONSE;
  p.data.resize (4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = status;
  return p;
}

int
parseEIBnet6_TunnelACK (const EIBNet6IPPacket & p, EIBnet6_TunnelACK & r)
{
  if (p.service != TUNNEL_RESPONSE)
    return 1;
  if (p.data.size() != 4)
    return 1;
  if (p.data[0] != 4)
    return 1;
  r.channel = p.data[1];
  r.seqno = p.data[2];
  r.status = p.data[3];
  return 0;
}

EIBnet6_ConfigRequest::EIBnet6_ConfigRequest ()
{
  channel = 0;
  seqno = 0;
}

EIBNet6IPPacket EIBnet6_ConfigRequest::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = DEVICE_CONFIGURATION_REQUEST;
  p.data.resize (CEMI.size() + 4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = 0;
  p.data.setpart (CEMI, 4);
  return p;
}

int
parseEIBnet6_ConfigRequest (const EIBNet6IPPacket & p, EIBnet6_ConfigRequest & r)
{
  if (p.service != DEVICE_CONFIGURATION_REQUEST)
    return 1;
  if (p.data.size() < 6)
    return 1;
  if (p.data[0] != 4)
    return 1;
  r.channel = p.data[1];
  r.seqno = p.data[2];
  r.CEMI.set (p.data.data() + 4, p.data.size() - 4);
  return 0;
}

EIBnet6_ConfigACK::EIBnet6_ConfigACK ()
{
  channel = 0;
  seqno = 0;
  status = 0;
}

EIBNet6IPPacket EIBnet6_ConfigACK::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  p.service = DEVICE_CONFIGURATION_ACK;
  p.data.resize (4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = status;
  return p;
}

int
parseEIBnet6_ConfigACK (const EIBNet6IPPacket & p, EIBnet6_ConfigACK & r)
{
  if (p.service != DEVICE_CONFIGURATION_ACK)
    return 1;
  if (p.data.size() != 4)
    return 1;
  if (p.data[0] != 4)
    return 1;
  r.channel = p.data[1];
  r.seqno = p.data[2];
  r.status = p.data[3];
  return 0;
}

EIBnet6_DescriptionRequest::EIBnet6_DescriptionRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
}

EIBNet6IPPacket EIBnet6_DescriptionRequest::ToPacket ()CONST
{
  EIBNet6IPPacket
    p;
  CArray
    ca = IPtoEIBNetIP (&caddr, nat);
  p.service = DESCRIPTION_REQUEST;
  p.data = ca;
  return p;
}

int
parseEIBnet6_DescriptionRequest (const EIBNet6IPPacket & p,
				EIBnet6_DescriptionRequest & r)
{
  if (p.service != DESCRIPTION_REQUEST)
    return 1;
  if (p.data.size() != 8)
    return 1;
  if (EIBnettoIP (p.data, &r.caddr, &p.src, r.nat))
    return 1;
  return 0;
}


EIBnet6_DescriptionResponse::EIBnet6_DescriptionResponse ()
{
  KNXmedium = 0;
  devicestatus = 0;
  individual_addr = 0;
  installid = 0;
  memset (&serial, 0, sizeof (serial));
  memset(multicastaddr.s6_addr, 0, sizeof(multicastaddr.s6_addr));
  memset (&MAC, 0, sizeof (MAC));
  memset (&name, 0, sizeof (name));
}

EIBNet6IPPacket EIBnet6_DescriptionResponse::ToPacket ()CONST
{
  EIBNet6IPPacket
    p;
  p.service = DESCRIPTION_RESPONSE;
  p.data.resize (56 + services.size() * 2);
  p.data[0] = 54;
  p.data[1] = 1;
  p.data[2] = KNXmedium;
  p.data[3] = devicestatus;
  p.data[4] = (individual_addr >> 8) & 0xff;
  p.data[5] = (individual_addr) & 0xff;
  p.data[6] = (installid >> 8) & 0xff;
  p.data[7] = (installid) & 0xff;
  p.data.setpart((const uint8_t *)&serial, 8, 6);
  p.data.setpart((const uint8_t *)&multicastaddr, 14, 4);
  p.data.setpart((const uint8_t *)&MAC, 18, 6);
  p.data.setpart((const uint8_t *)&name, 24, 30);
  p.data[53] = 0;
  p.data[54] = services.size() * 2 + 2;
  p.data[55] = 2;
  for (unsigned int i = 0; i < services.size(); i++)
    {
      p.data[56 + i * 2] = services[i].family;
      p.data[57 + i * 2] = services[i].version;
    }
  p.data.setpart (optional, 56 + services.size() * 2);
  return p;
}

int
parseEIBnet6_DescriptionResponse (const EIBNet6IPPacket & p,
				 EIBnet6_DescriptionResponse & r)
{
  if (p.service != DESCRIPTION_RESPONSE)
    return 1;
  if (p.data.size() < 56)
    return 1;
  if (p.data[0] != 54)
    return 1;
  if (p.data[1] != 1)
    return 1;
  r.KNXmedium = p.data[2];
  r.devicestatus = p.data[3];
  r.individual_addr = (p.data[4] << 8) | p.data[5];
  r.installid = (p.data[6] << 8) | p.data[7];
  memcpy (&r.serial, p.data.data() + 8, 6);
  memcpy (&r.multicastaddr, p.data.data() + 14, 4);
  memcpy (&r.MAC, p.data.data() + 18, 6);
  memcpy (&r.name, p.data.data() + 24, 30);
  r.name[29] = 0;
  if (p.data[55] != 2)
    return 1;
  if (p.data[54] % 2)
    return 1;
  if (p.data[54] + 54U > p.data.size())
    return 1;
  r.services.resize ((p.data[54] / 2) - 1);
  for (int i = 0; i < (p.data[54] / 2) - 1; i++)
    {
      r.services[i].family = p.data[56 + 2 * i];
      r.services[i].version = p.data[57 + 2 * i];
    }
  r.optional.set (p.data.data() + p.data[54] + 54,
		  p.data.size() - p.data[54] - 54);
  return 0;
}

EIBnet6_SearchRequest::EIBnet6_SearchRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
}

EIBNet6IPPacket EIBnet6_SearchRequest::ToPacket ()CONST
{
  EIBNet6IPPacket
    p;
  CArray
    ca = IPtoEIBNetIP (&caddr, nat);
  p.service = SEARCH_REQUEST;
  p.data = ca;
  return p;
}

int
parseEIBnet6_SearchRequest (const EIBNet6IPPacket & p, EIBnet6_SearchRequest & r)
{
  if (p.service != SEARCH_REQUEST)
    return 1;
  if (p.data.size() != 8)
    return 1;
  if (EIBnettoIP (p.data, &r.caddr, &p.src, r.nat))
    return 1;
  return 0;
}


EIBnet6_SearchResponse::EIBnet6_SearchResponse ()
{
  KNXmedium = 0;
  devicestatus = 0;
  individual_addr = 0;
  installid = 0;
  memset (&serial, 0, sizeof (serial));
  memset(multicastaddr.s6_addr, 0, sizeof(multicastaddr.s6_addr));
  memset (&MAC, 0, sizeof (MAC));
  memset (&name, 0, sizeof (name));
}

EIBNet6IPPacket EIBnet6_SearchResponse::ToPacket ()CONST
{
  EIBNet6IPPacket p;
  CArray ca = IPtoEIBNetIP (&caddr, nat);
  p.service = SEARCH_RESPONSE;
  p.data.resize (64 + services.size() * 2);
  p.data.setpart (ca, 0);
  p.data[8] = 54;
  p.data[9] = 1;
  p.data[10] = KNXmedium;
  p.data[11] = devicestatus;
  p.data[12] = (individual_addr >> 8) & 0xff;
  p.data[13] = (individual_addr) & 0xff;
  p.data[14] = (installid >> 8) & 0xff;
  p.data[15] = (installid) & 0xff;
  p.data.setpart((uint8_t *)&serial, 16, 6);
  p.data.setpart((uint8_t *)&multicastaddr, 22, 4);
  p.data.setpart((uint8_t *)&MAC, 26, 6);
  p.data.setpart((uint8_t *)&name, 32, 30);
  p.data[61] = 0;
  p.data[62] = services.size() * 2 + 2;
  p.data[63] = 2;
  for (unsigned int i = 0; i < services.size(); i++)
    {
      p.data[64 + i * 2] = services[i].family;
      p.data[65 + i * 2] = services[i].version;
    }
  return p;
}

int
parseEIBnet6_SearchResponse (const EIBNet6IPPacket & p,
			    EIBnet6_SearchResponse & r)
{
  if (p.service != SEARCH_RESPONSE)
    return 1;
  if (p.data.size() < 64)
    return 1;
  if (EIBnettoIP (CArray (p.data.data() + 0, 8), &r.caddr, &p.src, r.nat))
    return 1;
  if (p.data[8] != 54)
    return 1;
  if (p.data[9] != 1)
    return 1;
  r.KNXmedium = p.data[10];
  r.devicestatus = p.data[11];
  r.individual_addr = (p.data[12] << 8) | p.data[13];
  r.installid = (p.data[14] << 8) | p.data[15];
  memcpy (&r.serial, p.data.data() + 16, 6);
  memcpy (&r.multicastaddr, p.data.data() + 22, 4);
  memcpy (&r.MAC, p.data.data() + 26, 6);
  memcpy (&r.name, p.data.data() + 32, 30);
  r.name[29] = 0;
  if (p.data[63] != 2)
    return 1;
  if (p.data[62] % 2)
    return 1;
  if (p.data[62] + 62U > p.data.size())
    return 1;
  r.services.resize ((p.data[62] / 2) - 1);
  for (int i = 0; i < (p.data[62] / 2) - 1; i++)
    {
      r.services[i].family = p.data[64 + 2 * i];
      r.services[i].version = p.data[65 + 2 * i];
    }
  return 0;
}
