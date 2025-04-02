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

#ifndef PPP_AQM_QUEUE_DISC_V2_H
#define PPP_AQM_QUEUE_DISC_V2_H

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

class PppAqmQueueDiscV2 : public QueueDisc {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief PppAqmQueueDiscV2 constructor
   *
   * Creates a queue with a depth of 1000 packets by default
   */
  PppAqmQueueDiscV2 ();

  virtual ~PppAqmQueueDiscV2();

  // Reasons for dropping packets
  static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded

private:
  void MeasurementEvent();

  uint32_t GetDropDeficit(uint32_t owd);

  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void);
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);
  static const uint8_t m_level = 16; //the level of priority, [0, 1, 2, 3 ,4]
  uint32_t m_delayLimit = 100; //uint ms
  std::vector<uint32_t> m_bytesPerPpp;  //a vector stores the the number of bytes per priority

  uint64_t m_numMeasure = 0;
  uint64_t m_txSum = 0;
  uint64_t m_queueSum = 0;
  uint64_t m_owdSum = 0;
  //uint64_t m_owdLast = 0;
  //uint64_t m_owdMin = UINT64_MAX;
  //uint64_t m_owdMax = 0;
  uint64_t m_lossSum = 0;
  uint64_t m_earlyLossSum = 0; //loss due to adaptive algorithm
  uint64_t m_highDelaySum = 0; //delivered packet with high delay

  bool m_enableAdaptiveLine = false;
  const double m_maxLineAlpha = 0.9;
  double m_lineAlpha = 0.5; //range (0, m_maxLineAlpha)
  bool m_dropHighestPriority = true;
  double m_ewmaLambda = 1;
  uint32_t m_meanDelay = UINT32_MAX;

};

} // namespace ns3

#endif /* PPP_AQM_QUEUE_DISC_V2_H */
