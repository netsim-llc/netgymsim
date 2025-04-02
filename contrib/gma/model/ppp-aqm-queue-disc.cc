/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "ppp-aqm-queue-disc.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PppAqmQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (PppAqmQueueDisc);

TypeId PppAqmQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PppAqmQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PppAqmQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The max queue size",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
  ;
  return tid;
}

PppAqmQueueDisc::PppAqmQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
  NS_LOG_FUNCTION (this);
  for (uint32_t ind = 0; ind < m_level; ind++)
  {
    m_pktPerPpp.push_back(0);
  }

  Simulator::Schedule(MEASURE_INTERVAL, &PppAqmQueueDisc::MeasurementEvent, this);

}

PppAqmQueueDisc::~PppAqmQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

bool
PppAqmQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  if (GetCurrentSize () + item > GetMaxSize ())
    {
      NS_LOG_LOGIC ("Queue full -- dropping pkt");
      DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
      return false;
    }


  item->SetTimeStamp(Now());

  PppTag tag;
  if(item->GetPacket()->PeekPacketTag(tag))//ppp tag exists, add to the m_pktPerPpp
  {
    m_pktPerPpp.at(tag.GetPriority())++;
  }

  /*for (uint32_t ind = 0; ind < m_level; ind++)
  {
    std::cout << "PPP:" << ind << " #:" << m_pktPerPpp.at(ind) << " | ";
  }
  std::cout << "\n";*/



  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return retval;
}

Ptr<QueueDiscItem>
PppAqmQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  while(true)
  {
    //dequeue a new packet to replace the current one. Old one is dropped
    Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();

    if (!item)
      {
        NS_LOG_LOGIC ("Queue empty");
        return 0;
      }

    PppTag tag;
    if(!item->GetPacket()->PeekPacketTag(tag))//no ppp tag exists, return this tiem
    {
      return item;
    }

    //update packet count per PPP, we do not need P = 0 and P = 1
    m_pktPerPpp.at(tag.GetPriority())--;

    //update owd
    uint32_t owdMs = Now().GetMilliSeconds() - item->GetTimeStamp().GetMilliSeconds();

    if(owdMs > m_maxDelayPpp )
    {
      m_maxDelayPpp = owdMs;
    }

    //update high and low thresh if necssary.

    if(owdMs > m_delayLimit*m_highThreshPer/100 && Now() > m_lowReduceTs + MEASURE_INTERVAL)
    {
      m_lowThreshPer = m_lowThreshPer/2;
      m_lowThreshPer = std::max(m_lowThreshPer, (uint32_t)10 ); //min is 10%
      m_lowReduceTs = Now();
    }

    if(owdMs > m_delayLimit && Now() > m_highReduceTs + MEASURE_INTERVAL)//more than delay bound, and the last reduce time is more than one interval
    {
      m_highThreshPer = m_highThreshPer/2;
      m_highThreshPer = std::max(m_highThreshPer, (uint32_t)10); //min is 10%
      if(m_lowThreshPer > m_highThreshPer)
      {
        m_lowThreshPer = m_highThreshPer;
      }
      m_highReduceTs = Now();
    }


    //if the current queue delay > queue delay
    if(owdMs > m_delayLimit)
    {
      //drop this packet no matter what priority
      continue;
    }
    else if(owdMs > m_delayLimit * m_highThreshPer/100 )//heavy congestion, drop all packets except P == 0
    {
      if(tag.GetPriority() > 0)//priority > 0
      {
        //std::cout << Now().GetSeconds()<< "HIGH DELAY drop ppp" << +tag.GetPriority() << " High Thresh:" << m_highThreshPer << " Low Thresh:" << m_lowThreshPer <<"\n";
        continue;//drop this packet (P > 0) and dequeue a new one.
      }
      else{
      //transmit this packet
      }
    }
    else if(owdMs > m_delayLimit * m_lowThreshPer/100) //light congestion, drop packets according to priority
    {
      uint32_t pppIter = tag.GetPriority() + 1;
      uint32_t sumPkt = 0;
      while(pppIter < m_level)
      {
        sumPkt += m_pktPerPpp.at(pppIter);
        pppIter++;
      }

      if(tag.GetPriority() > 0 && m_pktDrop > sumPkt)//not enough low priority packets in the queue, drop it
      {
        //std::cout << "y>X(i)[drop ppp" << +tag.GetPriority() <<"] ---- pkt to be dropped:" << m_pktDrop << " low ppp pkt in the queue:" << sumPkt << "\n";
        continue;
      }
      else
      {
        m_pktDrop++;//increase drop deficit
        //transmit this packet
      }

    }
    else// low delay
    {
      if(m_pktDrop > 0)
      {
        m_pktDrop--;//reduce one drop deficit
      }
      //transmit this packet
    }

    //transmit this packet
    return item;
  }
}

Ptr<const QueueDiscItem>
PppAqmQueueDisc::DoPeek (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<const QueueDiscItem> item = GetInternalQueue (0)->Peek ();

  if (!item)
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  return item;
}

bool
PppAqmQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("PppAqmQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("PppAqmQueueDisc needs no packet filter");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // add a DropTail queue
      AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >
                          ("MaxSize", QueueSizeValue (GetMaxSize ())));
    }

  if (GetNInternalQueues () != 1)
    {
      NS_LOG_ERROR ("PppAqmQueueDisc needs 1 internal queue");
      return false;
    }

  return true;
}

void
PppAqmQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

void
PppAqmQueueDisc::MeasurementEvent ()
{
  //std::cout << Now().GetSeconds()<< " High Thresh:" << m_highThreshPer << " Low Thresh:" << m_lowThreshPer << " max delay of ppp:"<< m_maxDelayPpp << "\n";

  if(m_maxDelayPpp < m_delayLimit * INITIAL_PER/100)
  {
    m_highThreshPer += 10;
    m_highThreshPer = std::min(m_highThreshPer, INITIAL_PER); //max is initial_PER
  }

  if (m_maxDelayPpp < m_delayLimit*m_highThreshPer*INITIAL_PER/10000)
  {
    m_lowThreshPer += 10;
    m_lowThreshPer = std::min(m_lowThreshPer, m_highThreshPer);
  }

  m_maxDelayPpp = 0;
  Simulator::Schedule(MEASURE_INTERVAL, &PppAqmQueueDisc::MeasurementEvent, this);

}

} // namespace ns3
