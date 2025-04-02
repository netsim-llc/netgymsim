/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *  Copyright (c) 2007,2008, 2009 INRIA, UDcast
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mohamed Amine Ismail <amine.ismail@sophia.inria.fr>
 *                              <amine.ismail@udcast.com>
 */
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/string.h"
#include "ns3/seq-ts-header.h"
#include "svc-trace-client.h"
#include <cstdlib>
#include <cstdio>
#include <fstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SvcTraceClient");

NS_OBJECT_ENSURE_REGISTERED (SvcTraceClient);

TypeId
SvcTraceClient::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SvcTraceClient")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<SvcTraceClient> ()
    .AddAttribute ("RemoteAddress",
                   "The destination Address of the outbound packets",
                   AddressValue (),
                   MakeAddressAccessor (&SvcTraceClient::m_peerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("RemotePort",
                   "The destination port of the outbound packets",
                   UintegerValue (100),
                   MakeUintegerAccessor (&SvcTraceClient::m_peerPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("MaxPacketSize",
                   "The maximum size of a packet (including the SeqTsHeader, 12 bytes).",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&SvcTraceClient::m_maxPacketSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("TraceFilename",
                   "Name of file to load a trace from. By default, uses a hardcoded trace.",
                   StringValue (""),
                   MakeStringAccessor (&SvcTraceClient::SetTraceFile),
                   MakeStringChecker ())
    .AddAttribute ("TraceLoop",
                   "Loops through the trace file, starting again once it is over.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&SvcTraceClient::SetTraceLoop),
                   MakeBooleanChecker ())
    .AddAttribute ("Interval",
                   "The time to wait between packets", TimeValue (Seconds (0.033333)),
                   MakeTimeAccessor (&SvcTraceClient::m_interval),
                   MakeTimeChecker ())

  ;
  return tid;
}

SvcTraceClient::SvcTraceClient ()
{
  NS_LOG_FUNCTION (this);
  m_sent = 0;
  m_socket = 0;
  m_sendEvent = EventId ();
  m_maxPacketSize = 1400;
}

SvcTraceClient::SvcTraceClient (Ipv4Address ip, uint16_t port,
                                char *traceFile)
{
  NS_LOG_FUNCTION (this);
  m_sent = 0;
  m_socket = 0;
  m_sendEvent = EventId ();
  m_peerAddress = ip;
  m_peerPort = port;
  m_currentEntry = 0;
  m_maxPacketSize = 1400;
  if (traceFile != NULL)
    {
      SetTraceFile (traceFile);
    }
}

SvcTraceClient::~SvcTraceClient ()
{
  NS_LOG_FUNCTION (this);
  m_entries.clear ();
}

void
SvcTraceClient::SetRemote (Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_entries.clear ();
  m_peerAddress = ip;
  m_peerPort = port;
}

void
SvcTraceClient::SetRemote (Address addr)
{
  NS_LOG_FUNCTION (this << addr);
  m_entries.clear ();
  m_peerAddress = addr;
}

void
SvcTraceClient::SetTraceFile (std::string traceFile)
{
  NS_LOG_FUNCTION (this << traceFile);
  if (traceFile == "")
    {
      LoadDefaultTrace ();
    }
  else
    {
      LoadTrace (traceFile);
    }
}

void
SvcTraceClient::SetMaxPacketSize (uint16_t maxPacketSize)
{
  NS_LOG_FUNCTION (this << maxPacketSize);
  m_maxPacketSize = maxPacketSize;
}


uint16_t SvcTraceClient::GetMaxPacketSize (void)
{
  NS_LOG_FUNCTION (this);
  return m_maxPacketSize;
}


void
SvcTraceClient::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
SvcTraceClient::LoadTrace (std::string filename)
{
  NS_LOG_FUNCTION (this << filename);
  uint32_t bitrateKbps = 0;
  uint32_t frame = 0;
  uint32_t layer = 0;
  uint32_t frameSizeBytes = 0;
  uint32_t accumulatedBitrateKbps = 0;
  double ssim = 0;

  TraceEntry entry;
  std::ifstream ifTraceFile;
  ifTraceFile.open (filename.c_str (), std::ifstream::in);
  m_entries.clear ();
  if (!ifTraceFile.good ())
    {
      LoadDefaultTrace ();
    }
  while (ifTraceFile.good ())
    {
      ifTraceFile >> bitrateKbps >> frame >> layer >> frameSizeBytes >> accumulatedBitrateKbps >> ssim;
      //std::cout << bitrateKbps << "\t" << frame << "\t" << layer << "\t" << frameSizeBytes << "\t" << accumulatedBitrateKbps << "\t" << ssim << std::endl;
      
      entry.frameSize = frameSizeBytes;
      entry.frameNum = frame;
      entry.layerNum = layer;
      entry.ssim = ssim;
      m_entries.push_back (entry);
    }
  ifTraceFile.close ();
  m_currentEntry = 0;
}

void
SvcTraceClient::LoadDefaultTrace (void)
{
  NS_FATAL_ERROR ("Configure the correct trace file path!!");
}

void
SvcTraceClient::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  if (m_socket == nullptr)
    {
      //TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
        {
          if (m_socket->Bind () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom (m_peerAddress), m_peerPort));
        }
      else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
        {
          if (m_socket->Bind6 () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (Inet6SocketAddress (Ipv6Address::ConvertFrom (m_peerAddress), m_peerPort));
        }
      else if (InetSocketAddress::IsMatchingType (m_peerAddress) == true)
        {
          if (m_socket->Bind () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (m_peerAddress);
        }
      else if (Inet6SocketAddress::IsMatchingType (m_peerAddress) == true)
        {
          if (m_socket->Bind6 () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (m_peerAddress);
        }
      else
        {
          NS_ASSERT_MSG (false, "Incompatible address type: " << m_peerAddress);
        }
    }
  m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
  m_socket->SetAllowBroadcast (true);
  m_sendEvent = Simulator::Schedule (Seconds (0.0), &SvcTraceClient::Send, this);
}

void
SvcTraceClient::StopApplication ()
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_sendEvent);
}

void
SvcTraceClient::SendPacket (uint32_t size)
{
  //std::cout << Now().GetSeconds() << " Packet size: " << size << std::endl;
  NS_LOG_FUNCTION (this << size);
  Ptr<Packet> p;
  uint32_t packetSize;
  if (size>12)
    {
      packetSize = size - 12; // 12 is the size of the SeqTsHeader
    }
  else
    {
      packetSize = 0;
    }
  p = Create<Packet> (packetSize);
  SeqTsHeader seqTs;
  seqTs.SetSeq (m_sent);
  p->AddHeader (seqTs);

  std::stringstream addressString;
  if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
    {
      addressString << Ipv4Address::ConvertFrom (m_peerAddress);
    }
  else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
    {
      addressString << Ipv6Address::ConvertFrom (m_peerAddress);
    }
  else
    {
      addressString << m_peerAddress;
    }

  if ((m_socket->Send (p)) >= 0)
    {
      ++m_sent;
      NS_LOG_INFO ("Sent " << size << " bytes to "
                           << addressString.str ());
    }
  else
    {
      NS_LOG_INFO ("Error while sending " << size << " bytes to "
                                          << addressString.str ());
    }
}

void
SvcTraceClient::Send (void)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (m_sendEvent.IsExpired ());

  bool cycled = false;
  Ptr<Packet> p;
  struct TraceEntry *entry = &m_entries[m_currentEntry];
  uint32_t txFrameNum = entry->frameNum;
  do
    {
      //std::cout << "Frame:" << entry->frameNum << " Layer:" << entry->layerNum << " Size:" << entry->frameSize << std::endl;
      for (uint32_t i = 0; i < entry->frameSize / m_maxPacketSize; i++)
        {
          SendPacket (m_maxPacketSize);
        }

      uint16_t sizetosend = entry->frameSize % m_maxPacketSize;
      SendPacket (sizetosend);

      m_currentEntry++;
      if (m_currentEntry >= m_entries.size ())
        {
          m_currentEntry = 0;
          cycled = true;
        }
      entry = &m_entries[m_currentEntry];
    }
  while (txFrameNum == entry->frameNum);

  if (!cycled || m_traceLoop)
    {
      m_sendEvent = Simulator::Schedule (m_interval, &SvcTraceClient::Send, this);
    }
}

void
SvcTraceClient::SetTraceLoop (bool traceLoop)
{
  m_traceLoop = traceLoop;
}

} // Namespace ns3
