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
#include "eibnetip.h"
#include "config.h"

EIBNetIPPacket::EIBNetIPPacket ()
{
  service = 0;
  memset (&src, 0, sizeof (src));
}

EIBNetIPPacket *
EIBNetIPPacket::fromPacket (const CArray & c, const struct sockaddr_in src)
{
  EIBNetIPPacket *p;
  if (c.size() < 6)
    return 0;
  if (c[0] != 0x6 || c[1] != 0x10)
    return 0;
  unsigned len = (c[4] << 8) | c[5];
  if (len != c.size())
    return 0;
  p = new EIBNetIPPacket;
  p->service = (c[2] << 8) | c[3];
  p->data.set (c.data() + 6, len - 6);
  p->src = src;
  return p;
}

EIBNetIPPacket *
EIBNetIPPacket::fromPacket (const CArray & c, const struct sockaddr_in6 src)
{
  EIBNetIPPacket *p;
  if (c.size() < 6)
    return 0;
  if (c[0] != 0x6 || c[1] != 0x10)
    return 0;
  unsigned len = (c[4] << 8) | c[5];
  if (len != c.size())
    return 0;
  p = new EIBNetIPPacket;
  p->service = (c[2] << 8) | c[3];
  p->data.set (c.data() + 6, len - 6);
  p->src6 = src;
  return p;
}

CArray
EIBNetIPPacket::ToPacket ()
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
IPtoEIBNetIP (const struct sockaddr_in * a, bool nat)
{
  CArray buf;
  buf.resize (8);
  buf[0] = 0x08;
  buf[1] = 0x01;
  if (nat)
    {
      buf[2] = 0;
      buf[3] = 0;
      buf[4] = 0;
      buf[5] = 0;
      buf[6] = 0;
      buf[7] = 0;
    }
  else
    {
      buf[2] = (ntohl (a->sin_addr.s_addr) >> 24) & 0xff;
      buf[3] = (ntohl (a->sin_addr.s_addr) >> 16) & 0xff;
      buf[4] = (ntohl (a->sin_addr.s_addr) >> 8) & 0xff;
      buf[5] = (ntohl (a->sin_addr.s_addr) >> 0) & 0xff;
      buf[6] = (ntohs (a->sin_port) >> 8) & 0xff;
      buf[7] = (ntohs (a->sin_port) >> 0) & 0xff;
    }
  return buf;
}

bool
EIBnettoIP (const CArray & buf, struct sockaddr_in *a,
	    const struct sockaddr_in *src, bool & nat)
{
  int ip, port;
  memset (a, 0, sizeof (*a));
  if (buf[0] != 0x8 || buf[1] != 0x1)
    return true;
  ip = (buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | (buf[5]);
  port = (buf[6] << 8) | (buf[7]);
#ifdef HAVE_SOCKADDR_IN_LEN
  a->sin_len = sizeof (*a);
#endif
  a->sin_family = AF_INET;
  if (port == 0)
    a->sin_port = src->sin_port;
  else
    a->sin_port = htons (port);
  if (ip == 0)
    {
      nat = true;
      a->sin_addr.s_addr = src->sin_addr.s_addr;
    }
  else
    a->sin_addr.s_addr = htonl (ip);

  return false;
}

EIBNetIPSocket::EIBNetIPSocket (struct sockaddr_in bindaddr, bool reuseaddr,
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

  io_send.set<EIBNetIPSocket, &EIBNetIPSocket::io_send_cb>(this);
  io_recv.set<EIBNetIPSocket, &EIBNetIPSocket::io_recv_cb>(this);
  on_recv.set<EIBNetIPSocket, &EIBNetIPSocket::recv_cb>(this); // dummy
  on_error.set<EIBNetIPSocket, &EIBNetIPSocket::error_cb>(this); // dummy
  on_next.set<EIBNetIPSocket, &EIBNetIPSocket::next_cb>(this); // dummy

  fd = socket (AF_INET, SOCK_DGRAM, 0);
  if (fd == -1)
    return;
  set_non_blocking(fd);

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
    char loopch=1;
 
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                   (char *)&loopch, sizeof(loopch)) < 0) {
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

EIBNetIPSocket::EIBNetIPSocket (struct sockaddr_in6 bindaddr, bool reuseaddr,
				TracePtr tr, SockMode mode)
{
  int i;
  t = tr;
  TRACEPRINTF (t, 0, "Open");
  multicast = false;
  memset (&maddr, 0, sizeof (maddr));
  memset (&maddr6, 0, sizeof (maddr6));
  memset (&sendaddr, 0, sizeof (sendaddr));
  memset (&recvaddr, 0, sizeof (recvaddr));
  memset (&recvaddr2, 0, sizeof (recvaddr2));
  recvall = 0;

  io_send.set<EIBNetIPSocket, &EIBNetIPSocket::io_send_cb>(this);
  io_recv.set<EIBNetIPSocket, &EIBNetIPSocket::io_recv_cb>(this);
  on_recv.set<EIBNetIPSocket, &EIBNetIPSocket::recv_cb>(this); // dummy
  on_error.set<EIBNetIPSocket, &EIBNetIPSocket::error_cb>(this); // dummy
  on_next.set<EIBNetIPSocket, &EIBNetIPSocket::next_cb>(this); // dummy

  fd = socket (AF_INET6, SOCK_DGRAM, 0);
  if (fd == -1)
    return;
  //set_non_blocking(fd);

  // Set the number of hops
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

EIBNetIPSocket::~EIBNetIPSocket ()
{
  TRACEPRINTF (t, 0, "Close");
  stop();
}

void
EIBNetIPSocket::stop()
{
  if (fd != -1)
    {
      io_recv.stop();
      io_send.stop();
      if (multicast)
	setsockopt (fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &maddr,
		    sizeof (maddr));
      close (fd);
      fd = -1;
    }
}

void
EIBNetIPSocket::pause()
{
    if (paused)
        return;
    paused = true;
    io_recv.stop();
}

void
EIBNetIPSocket::unpause()
{
    if (! paused)
        return;
    paused = false;
    io_recv.start(fd, ev::READ);
}

bool
EIBNetIPSocket::init ()
{
  if (fd < 0)
    return false;
  io_recv.start(fd, ev::READ);
  return true;
}

int
EIBNetIPSocket::port ()
{
  struct sockaddr_in sa;
  socklen_t saLen = sizeof(sa);
  if (getsockname(fd, (struct sockaddr *) &sa, &saLen) < 0)
    return -1;
  if (sa.sin_family != AF_INET)
    {
      errno = ENODATA;
      return -1;
    }
  return sa.sin_port;
}

int
EIBNetIPSocket::port6 ()
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
EIBNetIPSocket::SetMulticast (struct ip_mreq multicastaddr)
{
  if (multicast)
    return false;
  maddr = multicastaddr;
  if (setsockopt (fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &maddr, sizeof (maddr))
      == -1)
    return false;
  multicast = true;
  return true;
}

bool
EIBNetIPSocket::SetMulticast (struct ipv6_mreq multicastaddr)
{
  if (multicast)
  {
    return false;
  }
  maddr6 = multicastaddr;
  if (setsockopt (fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &maddr6, sizeof (maddr6)) == -1)
  {
    return false;
  }
  multicast = true;
  return true;
}

void
EIBNetIPSocket::Send (EIBNetIPPacket p, struct sockaddr_in addr)
{
  struct _EIBNetIP_Send s;
  t->TracePacket (1, "Send", p.data);
  s.data = p;
  s.addr = addr;

  if (send_q.isempty())
    io_send.start(fd, ev::WRITE);
  send_q.put (std::move(s));
}

void
EIBNetIPSocket::Send (EIBNetIPPacket p, struct sockaddr_in6 addr)
{
  struct _EIBNetIP_Send s;
  t->TracePacket (1, "Send", p.data);
  s.data = p;
  s.addr6 = addr;

  if (send_q.isempty())
    io_send.start(fd, ev::WRITE);
  send_q.put (std::move(s));
}
void
EIBNetIPSocket::io_send_cb (ev::io &w UNUSED, int revents UNUSED)
{
  if (send_q.isempty ())
    {
      io_send.stop();
      on_next();
      return;
    }
  const struct _EIBNetIP_Send s = send_q.front ();
  CArray p = s.data.ToPacket ();
  t->TracePacket (0, "Send", p);
  int i = 0;
  int k = 0;
  //if( s.addr.sin_addr != 0 ) /* if IPv4 available */
  //{
    i = sendto (fd, p.data(), p.size(), 0, (const struct sockaddr *) &s.addr, sizeof (s.addr));
  //}
  //if( s.addr6.sin6_addr != 0 ) /* if IPv6 available */
  //{
    k = sendto (fd, p.data(), p.size(), 0, (const struct sockaddr *) &s.addr6, sizeof (s.addr6));
  //}

  if ( (i > 0) || (k>0) )
    {
      send_q.get ();
      send_error = 0;
    }
  else
    {
      if ( ( (i == 0) && (k == 0) ) || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
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
EIBNetIPSocket::io_recv_cb (ev::io &w UNUSED, int revents UNUSED)
{
  uchar buf[255];
  socklen_t rl;
  sockaddr_in r;
  rl = sizeof (r);
  memset (&r, 0, sizeof (r));

  int i = recvfrom (fd, buf, sizeof (buf), 0, (struct sockaddr *) &r, &rl);
  if (i < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    on_error();
  else if (i >= 0 && rl == sizeof (r))
    {
      if (recvall == 1 || !memcmp (&r, &recvaddr, sizeof (r)) ||
          (recvall == 2 && memcmp (&r, &localaddr, sizeof (r))) ||
          (recvall == 2 && memcmp (&r, &localaddr_ip6, sizeof (r))) ||
          (recvall == 3 && !memcmp (&r, &recvaddr2, sizeof (r))))
        {
          t->TracePacket (0, "Recv", i, buf);
          EIBNetIPPacket *p =
            EIBNetIPPacket::fromPacket (CArray (buf, i), r);
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
EIBNetIPSocket::SetInterface(std::string& iface)
{
  struct sockaddr_in sa;
  struct ip_mreqn addr;

  memset (&sa, 0, sizeof(sa));
  memset (&addr, 0, sizeof(addr));

  if (iface.size() == 0)
    return true;
  addr.imr_ifindex = if_nametoindex(iface.c_str());
  return
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) >= 0;
}

EIBnet_ConnectRequest::EIBnet_ConnectRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  memset (&daddr, 0, sizeof (daddr));
  nat = false;
}

EIBNetIPPacket EIBnet_ConnectRequest::ToPacket ()CONST
{
  EIBNetIPPacket p;
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
parseEIBnet_ConnectRequest (const EIBNetIPPacket & p,
			    EIBnet_ConnectRequest & r)
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

EIBnet_ConnectResponse::EIBnet_ConnectResponse ()
{
  memset (&daddr, 0, sizeof (daddr));
  nat = false;
  channel = 0;
  status = 0;
}

EIBNetIPPacket EIBnet_ConnectResponse::ToPacket ()CONST
{
  EIBNetIPPacket p;
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
parseEIBnet_ConnectResponse (const EIBNetIPPacket & p,
			     EIBnet_ConnectResponse & r)
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

EIBnet_ConnectionStateRequest::EIBnet_ConnectionStateRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
  channel = 0;
}

EIBNetIPPacket EIBnet_ConnectionStateRequest::ToPacket ()CONST
{
  EIBNetIPPacket p;
  CArray ca = IPtoEIBNetIP (&caddr, nat);
  p.service = CONNECTIONSTATE_REQUEST;
  p.data.resize (ca.size() + 2);
  p.data[0] = channel;
  p.data[1] = 0;
  p.data.setpart (ca, 2);
  return p;
}

int
parseEIBnet_ConnectionStateRequest (const EIBNetIPPacket & p,
				    EIBnet_ConnectionStateRequest & r)
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

EIBnet_ConnectionStateResponse::EIBnet_ConnectionStateResponse ()
{
  channel = 0;
  status = 0;
}

EIBNetIPPacket EIBnet_ConnectionStateResponse::ToPacket ()CONST
{
  EIBNetIPPacket p;
  p.service = CONNECTIONSTATE_RESPONSE;
  p.data.resize (2);
  p.data[0] = channel;
  p.data[1] = status;
  return p;
}

int
parseEIBnet_ConnectionStateResponse (const EIBNetIPPacket & p,
				     EIBnet_ConnectionStateResponse & r)
{
  if (p.service != CONNECTIONSTATE_RESPONSE)
    return 1;
  if (p.data.size() != 2)
    return 1;
  r.channel = p.data[0];
  r.status = p.data[1];
  return 0;
}

EIBnet_DisconnectRequest::EIBnet_DisconnectRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
  channel = 0;
}

EIBNetIPPacket EIBnet_DisconnectRequest::ToPacket ()CONST
{
  EIBNetIPPacket p;
  CArray ca = IPtoEIBNetIP (&caddr, nat);
  p.service = DISCONNECT_REQUEST;
  p.data.resize (ca.size() + 2);
  p.data[0] = channel;
  p.data[1] = 0;
  p.data.setpart (ca, 2);
  return p;
}

int
parseEIBnet_DisconnectRequest (const EIBNetIPPacket & p,
			       EIBnet_DisconnectRequest & r)
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

EIBnet_DisconnectResponse::EIBnet_DisconnectResponse ()
{
  channel = 0;
  status = 0;
}

EIBNetIPPacket EIBnet_DisconnectResponse::ToPacket ()CONST
{
  EIBNetIPPacket p;
  p.service = DISCONNECT_RESPONSE;
  p.data.resize (2);
  p.data[0] = channel;
  p.data[1] = status;
  return p;
}

int
parseEIBnet_DisconnectResponse (const EIBNetIPPacket & p,
				EIBnet_DisconnectResponse & r)
{
  if (p.service != DISCONNECT_RESPONSE)
    return 1;
  if (p.data.size() != 2)
    return 1;
  r.channel = p.data[0];
  r.status = p.data[1];
  return 0;
}

EIBnet_TunnelRequest::EIBnet_TunnelRequest ()
{
  channel = 0;
  seqno = 0;
}

EIBNetIPPacket EIBnet_TunnelRequest::ToPacket ()CONST
{
  EIBNetIPPacket p;
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
parseEIBnet_TunnelRequest (const EIBNetIPPacket & p, EIBnet_TunnelRequest & r)
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

EIBnet_TunnelACK::EIBnet_TunnelACK ()
{
  channel = 0;
  seqno = 0;
  status = 0;
}

EIBNetIPPacket EIBnet_TunnelACK::ToPacket ()CONST
{
  EIBNetIPPacket p;
  p.service = TUNNEL_RESPONSE;
  p.data.resize (4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = status;
  return p;
}

int
parseEIBnet_TunnelACK (const EIBNetIPPacket & p, EIBnet_TunnelACK & r)
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

EIBnet_ConfigRequest::EIBnet_ConfigRequest ()
{
  channel = 0;
  seqno = 0;
}

EIBNetIPPacket EIBnet_ConfigRequest::ToPacket ()CONST
{
  EIBNetIPPacket p;
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
parseEIBnet_ConfigRequest (const EIBNetIPPacket & p, EIBnet_ConfigRequest & r)
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

EIBnet_ConfigACK::EIBnet_ConfigACK ()
{
  channel = 0;
  seqno = 0;
  status = 0;
}

EIBNetIPPacket EIBnet_ConfigACK::ToPacket ()CONST
{
  EIBNetIPPacket p;
  p.service = DEVICE_CONFIGURATION_ACK;
  p.data.resize (4);
  p.data[0] = 4;
  p.data[1] = channel;
  p.data[2] = seqno;
  p.data[3] = status;
  return p;
}

int
parseEIBnet_ConfigACK (const EIBNetIPPacket & p, EIBnet_ConfigACK & r)
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

EIBnet_DescriptionRequest::EIBnet_DescriptionRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
}

EIBNetIPPacket EIBnet_DescriptionRequest::ToPacket ()CONST
{
  EIBNetIPPacket
    p;
  CArray
    ca = IPtoEIBNetIP (&caddr, nat);
  p.service = DESCRIPTION_REQUEST;
  p.data = ca;
  return p;
}

int
parseEIBnet_DescriptionRequest (const EIBNetIPPacket & p,
				EIBnet_DescriptionRequest & r)
{
  if (p.service != DESCRIPTION_REQUEST)
    return 1;
  if (p.data.size() != 8)
    return 1;
  if (EIBnettoIP (p.data, &r.caddr, &p.src, r.nat))
    return 1;
  return 0;
}


EIBnet_DescriptionResponse::EIBnet_DescriptionResponse ()
{
  KNXmedium = 0;
  devicestatus = 0;
  individual_addr = 0;
  installid = 0;
  memset (&serial, 0, sizeof (serial));
  multicastaddr.s_addr = 0;
  memset (&MAC, 0, sizeof (MAC));
  memset (&name, 0, sizeof (name));
}

EIBNetIPPacket EIBnet_DescriptionResponse::ToPacket ()CONST
{
  EIBNetIPPacket
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
parseEIBnet_DescriptionResponse (const EIBNetIPPacket & p,
				 EIBnet_DescriptionResponse & r)
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

EIBnet_SearchRequest::EIBnet_SearchRequest ()
{
  memset (&caddr, 0, sizeof (caddr));
  nat = false;
}

EIBNetIPPacket EIBnet_SearchRequest::ToPacket ()CONST
{
  EIBNetIPPacket
    p;
  CArray
    ca = IPtoEIBNetIP (&caddr, nat);
  p.service = SEARCH_REQUEST;
  p.data = ca;
  return p;
}

int
parseEIBnet_SearchRequest (const EIBNetIPPacket & p, EIBnet_SearchRequest & r)
{
  if (p.service != SEARCH_REQUEST)
    return 1;
  if (p.data.size() != 8)
    return 1;
  if (EIBnettoIP (p.data, &r.caddr, &p.src, r.nat))
    return 1;
  return 0;
}


EIBnet_SearchResponse::EIBnet_SearchResponse ()
{
  KNXmedium = 0;
  devicestatus = 0;
  individual_addr = 0;
  installid = 0;
  memset (&serial, 0, sizeof (serial));
  multicastaddr.s_addr = 0;
  memset (&MAC, 0, sizeof (MAC));
  memset (&name, 0, sizeof (name));
}

EIBNetIPPacket EIBnet_SearchResponse::ToPacket ()CONST
{
  EIBNetIPPacket p;
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
parseEIBnet_SearchResponse (const EIBNetIPPacket & p,
			    EIBnet_SearchResponse & r)
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
