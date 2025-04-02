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

#ifndef PPP_AQM_QUEUE_DISC_H
#define PPP_AQM_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/ppp-tag.h"

namespace ns3 {
/**
 * \ingroup traffic-control
 *
 * Simple queue disc implementing the PPP (First-In First-Out) policy.
 *
 */
static const Time MEASURE_INTERVAL = MilliSeconds(100);
static const uint32_t INITIAL_PER = 80;
class PppAqmQueueDisc : public QueueDisc {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief PppAqmQueueDisc constructor
   *
   * Creates a queue with a depth of 1000 packets by default
   */
  PppAqmQueueDisc ();

  virtual ~PppAqmQueueDisc();

  // Reasons for dropping packets
  static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded

private:
  void MeasurementEvent();

  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void);
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);
  static const uint8_t m_level = 10; //the level of priority, [0, 1, 2, 3 ,4]
  static const uint32_t m_delayLimit = 35; //uint ms
  uint32_t m_highThreshPer = INITIAL_PER; //this is percentige.
  uint32_t m_lowThreshPer = INITIAL_PER; //tshi is percentige.
  std::vector<uint32_t> m_pktPerPpp;
  uint32_t m_pktDrop = 0;

  uint32_t m_maxDelayPpp = 0;
  Time m_highReduceTs = Seconds(0);
  Time m_lowReduceTs = Seconds(0);


};

} // namespace ns3

#endif /* PPP_AQM_QUEUE_DISC_H */
