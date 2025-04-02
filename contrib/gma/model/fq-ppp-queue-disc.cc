/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
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
 * Authors: Pasquale Imputato <p.imputato@gmail.com>
 *          Stefano Avallone <stefano.avallone@unina.it>
*/

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/queue.h"
#include "fq-ppp-queue-disc.h"

#include "ns3/fifo-queue-disc.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/simulator.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/codel-queue-disc.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("FqPppQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (FqPppFlow);

TypeId FqPppFlow::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::FqPppFlow")
    .SetParent<QueueDiscClass> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<FqPppFlow> ()
  ;
  return tid;
}

FqPppFlow::FqPppFlow ()
  : m_deficit (0),
    m_status (INACTIVE),
    m_index (0)
{
  NS_LOG_FUNCTION (this);
}

FqPppFlow::~FqPppFlow ()
{
  NS_LOG_FUNCTION (this);
}

void
FqPppFlow::SetDeficit (uint32_t deficit)
{
  NS_LOG_FUNCTION (this << deficit);
  m_deficit = deficit;
}

int32_t
FqPppFlow::GetDeficit (void) const
{
  NS_LOG_FUNCTION (this);
  return m_deficit;
}

void
FqPppFlow::IncreaseDeficit (int32_t deficit)
{
  NS_LOG_FUNCTION (this << deficit);
  m_deficit += deficit;
}

void
FqPppFlow::SetStatus (FlowStatus status)
{
  NS_LOG_FUNCTION (this);
  m_status = status;
}

FqPppFlow::FlowStatus
FqPppFlow::GetStatus (void) const
{
  NS_LOG_FUNCTION (this);
  return m_status;
}

void
FqPppFlow::SetIndex (uint32_t index)
{
  NS_LOG_FUNCTION (this);
  m_index = index;
}

uint32_t
FqPppFlow::GetIndex (void) const
{
  return m_index;
}


NS_OBJECT_ENSURE_REGISTERED (FqPppQueueDisc);

TypeId FqPppQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::FqPppQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<FqPppQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The maximum number of packets accepted by this queue disc",
                   QueueSizeValue (QueueSize ("10240p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("Flows",
                   "The number of queues into which the incoming packets are classified",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&FqPppQueueDisc::m_flows),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DropBatchSize",
                   "The maximum number of packets dropped from the fat flow",
                   UintegerValue (64),
                   MakeUintegerAccessor (&FqPppQueueDisc::m_dropBatchSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Perturbation",
                   "The salt used as an additional input to the hash function used to classify packets",
                   UintegerValue (0),
                   MakeUintegerAccessor (&FqPppQueueDisc::m_perturbation),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("EnableSetAssociativeHash",
                   "Enable/Disable Set Associative Hash",
                   BooleanValue (true),
                   MakeBooleanAccessor (&FqPppQueueDisc::m_enableSetAssociativeHash),
                   MakeBooleanChecker ())
    .AddAttribute ("SetWays",
                   "The size of a set of queues (used by set associative hash)",
                   UintegerValue (8),
                   MakeUintegerAccessor (&FqPppQueueDisc::m_setWays),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Type", "Type of queues",
                   EnumValue (PPP_AQM_V2),
                   MakeEnumAccessor (&FqPppQueueDisc::m_type),
                   MakeEnumChecker (FqPppQueueDisc::Fifo, "Fifo",
                                    FqPppQueueDisc::Red, "Red",
                                    FqPppQueueDisc::CoDel, "CoDel",
                                    FqPppQueueDisc::Pie, "Pie",
                                    FqPppQueueDisc::PppPie, "PppPie",
                                    FqPppQueueDisc::PPP, "Per Packet Priority",
                                    FqPppQueueDisc::PPP_DELAY, "Per Packet Priority Delay Version",
                                    FqPppQueueDisc::PPP_AQM, "Per Packet Priority AQM Version",
                                    FqPppQueueDisc::PPP_AQM_V2, "Per Packet Priority AQM Version 2"))
  ;
  return tid;
}

FqPppQueueDisc::FqPppQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
    m_quantum (0)
{
  NS_LOG_FUNCTION (this);
}

FqPppQueueDisc::~FqPppQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
FqPppQueueDisc::SetQuantum (uint32_t quantum)
{
  NS_LOG_FUNCTION (this << quantum);
  m_quantum = quantum;
}

uint32_t
FqPppQueueDisc::GetQuantum (void) const
{
  return m_quantum;
}

uint32_t
FqPppQueueDisc::SetAssociativeHash (uint32_t flowHash)
{
  NS_LOG_FUNCTION (this << flowHash);

  uint32_t h = (flowHash % m_flows);
  uint32_t innerHash = h % m_setWays;
  uint32_t outerHash = h - innerHash;

  for (uint32_t i = outerHash; i < outerHash + m_setWays; i++)
    {
      auto it = m_flowsIndices.find (i);

      if (it == m_flowsIndices.end ()
          || (m_tags.find (i) != m_tags.end () && m_tags[i] == flowHash)
          || StaticCast<FqPppFlow> (GetQueueDiscClass (it->second))->GetStatus () == FqPppFlow::INACTIVE)
        {
          // this queue has not been created yet or is associated with this flow
          // or is inactive, hence we can use it
          m_tags[i] = flowHash;
          return i;
        }
    }

  // all the queues of the set are used. Use the first queue of the set
  m_tags[outerHash] = flowHash;
  return outerHash;
}

bool
FqPppQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  uint32_t flowHash, h;

  if (GetNPacketFilters () == 0)
    {
      flowHash = item->Hash (m_perturbation);
    }
  else
    {
      int32_t ret = Classify (item);

      if (ret != PacketFilter::PF_NO_MATCH)
        {
          flowHash = static_cast<uint32_t> (ret);
        }
      else
        {
          NS_LOG_ERROR ("No filter has been able to classify this packet, drop it.");
          PppTag tag;
          item->GetPacket()->PeekPacketTag (tag);

          std::cout << "DROP PACKET BEFORE ENQUEUE PPP: " << +tag.GetPriority() << "\n";

          DropBeforeEnqueue (item, UNCLASSIFIED_DROP);
          return false;
        }
    }

  if (m_enableSetAssociativeHash)
    {
      h = SetAssociativeHash (flowHash);
    }
  else
    {
      h = flowHash % m_flows;
    }

  Ptr<FqPppFlow> flow;
  if (m_flowsIndices.find (h) == m_flowsIndices.end ())
    {
      NS_LOG_DEBUG ("Creating a new flow queue with index " << h);
      flow = m_flowFactory.Create<FqPppFlow> ();
      Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc> ();

      Ptr<CoDelQueueDisc> codel = qd->GetObject<CoDelQueueDisc> ();
      if (codel)
        {
          codel->SetAttribute ("UseEcn", BooleanValue (false));
          codel->SetAttribute ("CeThreshold", TimeValue (Time::Max ()));
          codel->SetAttribute ("UseL4s", BooleanValue (false));
        }

      qd->Initialize ();
      flow->SetQueueDisc (qd);
      flow->SetIndex (h);
      AddQueueDiscClass (flow);

      m_flowsIndices[h] = GetNQueueDiscClasses () - 1;
    }
  else
    {
      flow = StaticCast<FqPppFlow> (GetQueueDiscClass (m_flowsIndices[h]));
    }

  if (flow->GetStatus () == FqPppFlow::INACTIVE)
    {
      flow->SetStatus (FqPppFlow::NEW_FLOW);
      flow->SetDeficit (m_quantum);
      m_newFlows.push_back (flow);
    }

  flow->GetQueueDisc ()->Enqueue (item);

  NS_LOG_DEBUG ("Packet enqueued into flow " << h << "; flow index " << m_flowsIndices[h]);

  //do not drop the packet in this queue
  /*if (GetCurrentSize () > GetMaxSize ())
    {
      NS_LOG_DEBUG ("Overload; enter FqPppDrop ()");
      //FqPppDrop ();
      flow->GetQueueDisc ()->Dequeue();
      return false; 
    }*/

  return true;
}

/*Ptr<QueueDiscItem>
FqPppQueueDisc::Dequeue (WifiMacHeader & hdr)
{
  //std::cout <<"Origi: "<< hdr.GetAddr1() << " "<< hdr.GetAddr2() << " "<< hdr.GetAddr3() << " "<< hdr.GetAddr4() << "\n";
  //std::cout << " new flow:" << m_newFlows.size() << " old flows:" << m_oldFlows.size() << "\n";
  Ptr<FqPppFlow> flow;
  Ptr<const QueueDiscItem> item;
  std::list<Ptr<FqPppFlow> >::iterator it;
  //find the pkt from the same user...
  for (it = m_oldFlows.begin(); it != m_oldFlows.end(); ++it){
     flow = *it;
     item = flow->GetQueueDisc ()->Peek();
     if (item)
     {
      WifiMacHeader macHeader = DynamicCast<const WifiQueueDiscItem> (item)->GetWifiHeader();
      if(hdr.GetAddr1() == macHeader.GetAddr1() && hdr.GetAddr2() == macHeader.GetAddr2() && hdr.GetAddr3() == macHeader.GetAddr3() && hdr.GetAddr4() == macHeader.GetAddr4())
      //std::cout << "Queue: " << macHeader.GetAddr1() << " "<< macHeader.GetAddr2() << " "<< macHeader.GetAddr3() << " "<< macHeader.GetAddr4() << "\n";
      return flow->GetQueueDisc ()->Dequeue ();
     }
  }

  return DoDequeue();
}*/


Ptr<QueueDiscItem>
FqPppQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<FqPppFlow> flow;
  Ptr<QueueDiscItem> item;

  do
    {
      bool found = false;

      while (!found && !m_newFlows.empty ())
        {
          flow = m_newFlows.front ();

          if (flow->GetDeficit () <= 0)
            {
              NS_LOG_DEBUG ("Increase deficit for new flow index " << flow->GetIndex ());
              flow->IncreaseDeficit (m_quantum);
              flow->SetStatus (FqPppFlow::OLD_FLOW);
              m_oldFlows.push_back (flow);
              m_newFlows.pop_front ();
            }
          else
            {
              NS_LOG_DEBUG ("Found a new flow " << flow->GetIndex () << " with positive deficit");
              found = true;
            }
        }

      while (!found && !m_oldFlows.empty ())
        {
          flow = m_oldFlows.front ();

          if (flow->GetDeficit () <= 0)
            {
              NS_LOG_DEBUG ("Increase deficit for old flow index " << flow->GetIndex ());
              flow->IncreaseDeficit (m_quantum);
              m_oldFlows.push_back (flow);
              m_oldFlows.pop_front ();
            }
          else
            {
              NS_LOG_DEBUG ("Found an old flow " << flow->GetIndex () << " with positive deficit");
              found = true;
            }
        }

      if (!found)
        {
          NS_LOG_DEBUG ("No flow found to dequeue a packet");
          return 0;
        }

      item = flow->GetQueueDisc ()->Dequeue ();

      if (!item)
        {
          NS_LOG_DEBUG ("Could not get a packet from the selected flow queue");
          if (!m_newFlows.empty ())
            {
              flow->SetStatus (FqPppFlow::OLD_FLOW);
              m_oldFlows.push_back (flow);
              m_newFlows.pop_front ();
            }
          else
            {
              flow->SetStatus (FqPppFlow::INACTIVE);
              m_oldFlows.pop_front ();
            }
        }
      else
        {
          NS_LOG_DEBUG ("Dequeued packet " << item->GetPacket ());
        }
    } while (item == nullptr);

  flow->IncreaseDeficit (item->GetSize () * -1);

  return item;
}

bool
FqPppQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("FqPppQueueDisc cannot have classes");
      return false;
    }

  if (GetNInternalQueues () > 0)
    {
      NS_LOG_ERROR ("FqPppQueueDisc cannot have internal queues");
      return false;
    }

  // we are at initialization time. If the user has not set a quantum value,
  // set the quantum to the MTU of the device (if any)
  if (!m_quantum)
    {
      Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface ();
      Ptr<NetDevice> dev;
      // if the NetDeviceQueueInterface object is aggregated to a
      // NetDevice, get the MTU of such NetDevice
      if (ndqi && (dev = ndqi->GetObject<NetDevice> ()))
        {
          m_quantum = dev->GetMtu ();
          NS_LOG_DEBUG ("Setting the quantum to the MTU of the device: " << m_quantum);
        }

      if(!m_quantum)
      {
        m_quantum = 2000;//random value...
      }

      if (!m_quantum)
        {
          NS_LOG_ERROR ("The quantum parameter cannot be null");
          return false;
        }
    }

  if (m_enableSetAssociativeHash && (m_flows % m_setWays != 0))
    {
      NS_LOG_ERROR ("The number of queues must be an integer multiple of the size "
                    "of the set of queues used by set associative hash");
      return false;
    }

  return true;
}

void
FqPppQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);

  m_flowFactory.SetTypeId ("ns3::FqPppFlow");

  if(m_type == CoDel)
  {
    m_queueDiscFactory.SetTypeId ("ns3::CoDelQueueDisc");
    std::cout << "set codel ===============\n";
  }
  else if(m_type == Fifo)
  {
    m_queueDiscFactory.SetTypeId ("ns3::FifoQueueDisc");
    std::cout << "set fifo ===============\n";
  }
  else if(m_type == Red)
  {
    m_queueDiscFactory.SetTypeId ("ns3::RedQueueDisc");
    std::cout << "set red ===============\n";
  }
  else if(m_type == Pie)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PieQueueDisc");
    std::cout << "set Pie ===============\n";
  }
  else if(m_type == PppPie)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PppPieQueueDisc");
    std::cout << "set PppPie ===============\n";
  }
  else if(m_type == PPP)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PppQueueDisc");

        std::cout << "set ppp ===============\n";

  }
    else if(m_type == PPP_DELAY)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PppDelayQueueDisc");
        std::cout << "set ppp delay ===============\n";

  }
    else if(m_type == PPP_AQM)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PppAqmQueueDisc");
        std::cout << "set ppp aqm ===============\n";

  }
    else if(m_type == PPP_AQM_V2)
  {
    m_queueDiscFactory.SetTypeId ("ns3::PppAqmQueueDiscV2");
        std::cout << "set ppp aqm v2 ===============\n";

  }
  else
  {
    NS_FATAL_ERROR("unkonw type:" << m_type);
  }

  m_queueDiscFactory.Set ("MaxSize", QueueSizeValue (GetMaxSize ()));
}

uint32_t
FqPppQueueDisc::FqPppDrop (void)
{
  NS_LOG_FUNCTION (this);

  uint32_t maxBacklog = 0, index = 0;
  Ptr<QueueDisc> qd;

  /* Queue is full! Find the fat flow and drop packet(s) from it */
  for (uint32_t i = 0; i < GetNQueueDiscClasses (); i++)
    {
      qd = GetQueueDiscClass (i)->GetQueueDisc ();
      uint32_t bytes = qd->GetNBytes ();
      if (bytes > maxBacklog)
        {
          maxBacklog = bytes;
          index = i;
        }
    }

  /* Our goal is to drop half of this fat flow backlog */
  uint32_t len = 0, count = 0, threshold = maxBacklog >> 1;
  qd = GetQueueDiscClass (index)->GetQueueDisc ();
  Ptr<QueueDiscItem> item;

  do
    {
      NS_LOG_DEBUG ("Drop packet (overflow); count: " << count << " len: " << len << " threshold: " << threshold);
      item = qd->GetInternalQueue (0)->Dequeue ();

      PppTag tag;
      item->GetPacket()->PeekPacketTag (tag);

      //std::cout << "DROP PACKET PPP: " << +tag.GetPriority() << "\n";

      DropAfterDequeue (item, OVERLIMIT_DROP);
      len += item->GetSize ();
    } while (++count < m_dropBatchSize && len < threshold);

  return index;
}



} // namespace ns3

