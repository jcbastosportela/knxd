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
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <algorithm>
#include "config.h"
#include "ipsupport.h"
#ifdef HAVE_LINUX_NETLINK
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif
#ifdef HAVE_WINDOWS_IPHELPER
#define Array XArray
#include <windows.h>
#include <iphlpapi.h>
#undef Array
#endif
#if HAVE_BSD_SOURCEINFO
#include <net/if.h>
#include <net/route.h>
#endif
/* added by portela */
//#include <ifaddrs.h>
//#include <netdb.h>

bool
GetHostIP6 (TracePtr t, struct sockaddr_in6 *sock, const std::string& name)
{
  int sockfd;  
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_in *h;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // use AF_INET6 to force IPv6
  hints.ai_socktype = SOCK_STREAM;

  if ( (rv = getaddrinfo( name.c_str() , "0" , &hints , &servinfo)) != 0) 
  {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return false;
  }

  // loop through all the results and connect to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) 
  {
    switch( p->ai_family )
    {
      case AF_UNSPEC:
        ERRORPRINTF (t, E_ERROR | 50, "Resolving %s failed: Family unspecified\n", name);
        break;
      
      case AF_INET:
        ERRORPRINTF (t, E_ERROR | 50, "Resolving %s Success: IPv4\n", name);

        break;

      case AF_INET6:
        ERRORPRINTF (t, E_ERROR | 50, "Resolving %s Success: IPv6\n", name);
        memcpy( sock, (p->ai_addr), sizeof(struct sockaddr_in6) );
        break;
    }
  }
    
  freeaddrinfo(servinfo); // all done with this structure
//	std::string name;
//	struct hostent *h;
//	struct ifaddrs *myaddrs, *ifa;
//	void *in_addr;
//	char buf[64];
/*
	if (name.size() == 0)
	{
		return false;
	}
	memset (sock, 0, sizeof (*sock));
	fprintf(stderr, "name : %s\n", name.c_str());

	h = gethostbyname2 (name.c_str(), AF_INET6);
	if (!h)
  {
    if (t)
    {
      ERRORPRINTF (t, E_ERROR | 50, "Resolving %s failed: %s", name, hstrerror(h_errno));
    }
    return false;
  }
#ifdef HAVE_SOCKADDR_IN_LEN
	sock->sin6_len = sizeof (*sock);
#endif
	sock->sin6_family = h->h_addrtype;
	//sock->sin6_addr = h->h_addr_list[0]));
*/
	/*
	if(getifaddrs(&myaddrs) != 0)
	{
		perror("getifaddrs");
		return false;
	}

	for( ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next )
	{
		if( ifa->ifa_addr == NULL )
		{
			continue;
		}
		if( !(ifa->ifa_flags & IFF_UP )
		{
			continue;
		}

		switch( ifa->ifa_addr->sa_familly )
		{
		}
	}
	*/	
	return true;
}

bool
GetHostIP (TracePtr t, struct sockaddr_in *sock, const std::string& name)
{
	struct hostent *h;

	if (name.size() == 0)
	{
		return false;
	}
	memset (sock, 0, sizeof (*sock));

	h = gethostbyname (name.c_str());
	if (!h)
    {
    	if (t)
    	{
    		ERRORPRINTF (t, E_ERROR | 50, "Resolving %s failed: %s", name, hstrerror(h_errno));
    	}
    	return false;
    }
#ifdef HAVE_SOCKADDR_IN_LEN
	sock->sin_len = sizeof (*sock);
#endif
	sock->sin_family = h->h_addrtype;
	sock->sin_addr.s_addr = (*((unsigned long *) h->h_addr_list[0]));

	return true;
}

#ifdef HAVE_LINUX_NETLINK
typedef struct
{
  struct nlmsghdr n;
  struct rtmsg r;
  char data[1000];
} r_req;

bool
GetSourceAddress (TracePtr t UNUSED, const struct sockaddr_in *dest, struct sockaddr_in *src)
{
  int s;
  int l;
  r_req req;
  struct rtattr *a;
  memset (&req, 0, sizeof (req));
  memset (src, 0, sizeof (*src));
  s = socket (PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
  if (s == -1)
    return 0;
  req.n.nlmsg_len =
    NLMSG_SPACE (sizeof (req.r)) +
    RTA_LENGTH (sizeof (dest->sin_addr.s_addr));
  req.n.nlmsg_flags = NLM_F_REQUEST;
  req.n.nlmsg_type = RTM_GETROUTE;
  req.r.rtm_family = AF_INET;
  req.r.rtm_dst_len = 32;
  a = (rtattr *) ((char *) &req + NLMSG_SPACE (sizeof (req.r)));
  a->rta_type = RTA_DST;
  a->rta_len = RTA_LENGTH (sizeof (dest->sin_addr.s_addr));
  memcpy (RTA_DATA (a), &dest->sin_addr.s_addr,
	  sizeof (dest->sin_addr.s_addr));
  if (write (s, &req, req.n.nlmsg_len) < 0)
    goto err_out;
  if (read (s, &req, sizeof (req)) < 0)
    goto err_out;
  close (s);
  if (req.n.nlmsg_type == NLMSG_ERROR)
    return 0;
  l = ((struct nlmsghdr *) &req)->nlmsg_len;
  while (RTA_OK (a, l))
    {
      if (a->rta_type == RTA_PREFSRC
	  && RTA_PAYLOAD (a) == sizeof (src->sin_addr.s_addr))
	{
	  src->sin_family = AF_INET;
	  memcpy (&src->sin_addr.s_addr, RTA_DATA (a), RTA_PAYLOAD (a));
	  return 1;
	}
      a = RTA_NEXT (a, l);
    }
  return 0;
err_out:
  close (s);
  return 0;
}

bool
GetSourceAddress6 (TracePtr t UNUSED, const struct sockaddr_in6 *dest, struct sockaddr_in6 *src)
{
  int s;
  int l;
  r_req req;
  struct rtattr *a;
  memset (&req, 0, sizeof (req));
  memset (src, 0, sizeof (*src));
  s = socket (PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
  if (s == -1)
    return 0;
  req.n.nlmsg_len =
    NLMSG_SPACE (sizeof (req.r)) +
    RTA_LENGTH (sizeof (dest->sin6_addr));
  req.n.nlmsg_flags = NLM_F_REQUEST;
  req.n.nlmsg_type = RTM_GETROUTE;
  req.r.rtm_family = AF_INET6;
  req.r.rtm_dst_len = 32;
  a = (rtattr *) ((char *) &req + NLMSG_SPACE (sizeof (req.r)));
  a->rta_type = RTA_DST;
  a->rta_len = RTA_LENGTH (sizeof (dest->sin6_addr));
  memcpy (RTA_DATA (a), &dest->sin6_addr,
	  sizeof (dest->sin6_addr));
  if (write (s, &req, req.n.nlmsg_len) < 0)
    goto err_out;
  if (read (s, &req, sizeof (req)) < 0)
    goto err_out;
  close (s);
  if (req.n.nlmsg_type == NLMSG_ERROR)
    return 0;
  l = ((struct nlmsghdr *) &req)->nlmsg_len;
  while (RTA_OK (a, l))
    {
      if (a->rta_type == RTA_PREFSRC
	  && RTA_PAYLOAD (a) == sizeof (src->sin6_addr))
	{
	  src->sin6_family = AF_INET6;
	  memcpy (&src->sin6_addr, RTA_DATA (a), RTA_PAYLOAD (a));
	  return 1;
	}
      a = RTA_NEXT (a, l);
    }
  return 0;
err_out:
  close (s);
  return 0;
}
#endif

#ifdef HAVE_WINDOWS_IPHELPER
bool
GetSourceAddress (TracePtr t, const struct sockaddr_in *dest, struct sockaddr_in *src)
{
  DWORD d = 0;
  PMIB_IPADDRTABLE tab;
  DWORD s = 0;

  memset (src, 0, sizeof (*src));
  if (GetBestInterface (dest->sin_addr.s_addr, &d) != NO_ERROR)
    return 0;

  tab = (MIB_IPADDRTABLE *) malloc (sizeof (MIB_IPADDRTABLE));
  if (!tab)
    return 0;
  if (GetIpAddrTable (tab, &s, 0) == ERROR_INSUFFICIENT_BUFFER)
    {
      tab = (MIB_IPADDRTABLE *) realloc (tab, s);
      if (!tab)
	return 0;
    }
  if (GetIpAddrTable (tab, &s, 0) != NO_ERROR)
    {
      if (tab)
	free (tab);
      return 0;
    }
  for (int i = 0; i < tab->dwNumEntries; i++)
    if (tab->table[i].dwIndex == d)
      {
	src->sin_family = AF_INET;
	src->sin_addr.s_addr = tab->table[i].dwAddr;
	free (tab);
	return 1;
      }
  free (tab);
  return 0;
}
#endif

#if HAVE_BSD_SOURCEINFO
typedef struct
{
  struct rt_msghdr hdr;
  char data[1000];
} r_req;

#ifndef HAVE_SA_SIZE
static int
SA_SIZE (struct sockaddr *sa)
{
  int align = sizeof (int);
  int len = (sa->sa_len ? sa->sa_len : align);
  if (len & (align - 1))
    {
      len += align - (len & (align - 1));
    }
  return len;
}
#endif

bool
GetSourceAddress (TracePtr t, const struct sockaddr_in *dest, struct sockaddr_in *src)
{
  int s;
  r_req req;
  char *cp = req.data;
  memset (&req, 0, sizeof (req));
  memset (src, 0, sizeof (*src));
  s = socket (PF_ROUTE, SOCK_RAW, 0);
  if (s == -1)
    return 0;
  req.hdr.rtm_msglen = sizeof (req) + sizeof (*dest);
  req.hdr.rtm_version = RTM_VERSION;
  req.hdr.rtm_flags = RTF_UP;
  req.hdr.rtm_type = RTM_GET;
  req.hdr.rtm_addrs = RTA_DST | RTA_IFP;
  memcpy (cp, dest, sizeof (*dest));
  if (write (s, (char *) &req, req.hdr.rtm_msglen) < 0)
    goto err_out;
  if (read (s, (char *) &req, sizeof (req)) < 0)
    goto err_out;
  close (s);
  int i;
  cp = (char *) (&req.hdr + 1);
  for (i = 1; i; i <<= 1)
    if (i & req.hdr.rtm_addrs)
      {
	struct sockaddr *sa = (struct sockaddr *) cp;
	if (i == RTA_IFA)
	  {
	    src->sin_len = sizeof (*src);
	    src->sin_family = AF_INET;
	    src->sin_addr.s_addr =
	      ((struct sockaddr_in *) sa)->sin_addr.s_addr;
	    return 1;
	  }
	cp += SA_SIZE (sa);
      }
  return 0;
err_out:
  close (s);
  return 0;
}
#endif

bool
compareIPAddress (const struct sockaddr_in & a, const struct sockaddr_in & b)
{
  if (a.sin_family != AF_INET)
    return false;
  if (b.sin_family != AF_INET)
    return false;
  if (a.sin_port != b.sin_port)
    return false;
  if (a.sin_addr.s_addr != b.sin_addr.s_addr)
    return false;
  return true;
}

