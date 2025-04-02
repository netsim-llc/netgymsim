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
#include "ppp-delay-queue-disc.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PppDelayQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (PppDelayQueueDisc);

TypeId PppDelayQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PppDelayQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PppDelayQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The max queue size",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
  ;
  return tid;
}

PppDelayQueueDisc::PppDelayQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
  NS_LOG_FUNCTION (this);
}

PppDelayQueueDisc::~PppDelayQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

bool
PppDelayQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  if (GetCurrentSize () + item > GetMaxSize ())
    {
      NS_LOG_LOGIC ("Queue full -- dropping pkt");
      DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
      return false;
    }


  item->SetTimeStamp(Now());

  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return retval;
}

Ptr<QueueDiscItem>
PppDelayQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

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

  /*if(Now().GetMilliSeconds() - tsTag.GetTimeStamp() > 20)
  {
    std::cout << Now().GetSeconds() << " dequeue pkt PPP:" << +tag.GetPriority() << " enequeue TS:" 
    << +tsTag.GetTimeStamp() << " Now:" << Now().GetMilliSeconds() 
    << " queue delay: " << Now().GetMilliSeconds() - tsTag.GetTimeStamp() << "\n";
  }*/

  //drop dequeud packet add dequeue a new one, if the current queue delay > weighted queue delay
  while(Now().GetMilliSeconds() - item->GetTimeStamp().GetMilliSeconds() > GetWeightedDelayLimit(tag.GetPriority(), m_level))
  {
    //drop this packet...
    item = GetInternalQueue (0)->Dequeue ();
    if (!item)
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

    if(!item->GetPacket()->PeekPacketTag(tag))
    {
      return item;//no ppp tag, break loop and return this item
    }
  }

  return item;
}

Ptr<const QueueDiscItem>
PppDelayQueueDisc::DoPeek (void)
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
PppDelayQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("PppDelayQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("PppDelayQueueDisc needs no packet filter");
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
      NS_LOG_ERROR ("PppDelayQueueDisc needs 1 internal queue");
      return false;
    }

  return true;
}

void
PppDelayQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

uint32_t 
PppDelayQueueDisc::GetWeightedDelayLimit (uint8_t ppp, uint8_t L)
{
  //we use a simple linear function;
  NS_ASSERT_MSG(ppp<=L, "per packet priority (" << +ppp<< ") cannot be greater than L(" << +L<< ")!!!");
  return m_delayLimit * (L-ppp)/L;
}


} // namespace ns3
