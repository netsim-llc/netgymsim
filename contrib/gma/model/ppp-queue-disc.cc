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
#include "ppp-queue-disc.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PppQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (PppQueueDisc);

TypeId PppQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PppQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PppQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The max queue size",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
  ;
  return tid;
}

PppQueueDisc::PppQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
  NS_LOG_FUNCTION (this);
}

PppQueueDisc::~PppQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

bool
PppQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  Ptr<Packet> dummyP = item->GetPacket();
  PppTag tag;
  dummyP->PeekPacketTag(tag);

  //std::cout << "Enqueue PPP:" << +tag.GetPriority() << " weighted size:" << GetWeightedQueueSize(tag.GetPriority(), m_level) <<"\n";

  //if (GetCurrentSize () + item > GetMaxSize ())
  //drop packet if current queue size > weighted queue size.
  if (GetCurrentSize () + item > GetWeightedQueueSize(tag.GetPriority(), m_level))
    {
      NS_LOG_LOGIC ("Queue full -- dropping pkt");
      DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
      return false;
    }

  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return retval;
}

Ptr<QueueDiscItem>
PppQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();

  if (!item)
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  PppTag tag;
  item->GetPacket()->PeekPacketTag(tag);
  //drop dequeud packet add dequeue a new one, if the current queue size > weighted queue size
  while(GetCurrentSize () + item > GetWeightedQueueSize(tag.GetPriority(), m_level))
  {
    //drop this packet...
    item = GetInternalQueue (0)->Dequeue ();
    if (!item)
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }
    item->GetPacket()->PeekPacketTag(tag);
  }

  return item;
}

Ptr<const QueueDiscItem>
PppQueueDisc::DoPeek (void)
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
PppQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("PppQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("PppQueueDisc needs no packet filter");
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
      NS_LOG_ERROR ("PppQueueDisc needs 1 internal queue");
      return false;
    }

  return true;
}

void
PppQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

QueueSize 
PppQueueDisc::GetWeightedQueueSize (uint8_t ppp, uint8_t L)
{
  //we use a simple linear function;
  QueueSizeUnit unit = GetMaxSize().GetUnit();

  NS_ASSERT_MSG(ppp<=L, "per packet priority cannot be greater than L!!!");
  uint32_t weightedSize = GetMaxSize().GetValue() * (L-ppp)/L;
  return QueueSize(unit, weightedSize);

}


} // namespace ns3
