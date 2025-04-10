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

#ifndef FQ_PPP_QUEUE_DISC
#define FQ_PPP_QUEUE_DISC

#include "ns3/queue-disc.h"
#include "ns3/object-factory.h"
#include <list>
#include <map>
#include "ppp-queue-disc.h"
#include "ns3/wifi-mac-header.h"

namespace ns3 {

/**
 * \ingroup traffic-control
 *
 * \brief A flow queue used by the FqPpp queue disc
 */

class FqPppFlow : public QueueDiscClass {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief FqPppFlow constructor
   */
  FqPppFlow ();

  virtual ~FqPppFlow ();

  /**
   * \enum FlowStatus
   * \brief Used to determine the status of this flow queue
   */
  enum FlowStatus
    {
      INACTIVE,
      NEW_FLOW,
      OLD_FLOW
    };

  /**
   * \brief Set the deficit for this flow
   * \param deficit the deficit for this flow
   */
  void SetDeficit (uint32_t deficit);
  /**
   * \brief Get the deficit for this flow
   * \return the deficit for this flow
   */
  int32_t GetDeficit (void) const;
  /**
   * \brief Increase the deficit for this flow
   * \param deficit the amount by which the deficit is to be increased
   */
  void IncreaseDeficit (int32_t deficit);
  /**
   * \brief Set the status for this flow
   * \param status the status for this flow
   */
  void SetStatus (FlowStatus status);
  /**
   * \brief Get the status of this flow
   * \return the status of this flow
   */
  FlowStatus GetStatus (void) const;
  /**
   * \brief Set the index for this flow
   * \param index the index for this flow
   */
  void SetIndex (uint32_t index);
  /**
   * \brief Get the index of this flow
   * \return the index of this flow
   */
  uint32_t GetIndex (void) const;

private:
  int32_t m_deficit;    //!< the deficit for this flow
  FlowStatus m_status;  //!< the status of this flow
  uint32_t m_index;     //!< the index for this flow
};


/**
 * \ingroup traffic-control
 *
 * \brief A FqPpp packet queue disc
 */

class FqPppQueueDisc : public QueueDisc {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief FqPppQueueDisc constructor
   */
  FqPppQueueDisc ();

  virtual ~FqPppQueueDisc ();

   /**
    * \brief Set the quantum value.
    *
    * \param quantum The number of bytes each queue gets to dequeue on each round of the scheduling algorithm
    */
   void SetQuantum (uint32_t quantum);

   /**
    * \brief Get the quantum value.
    *
    * \returns The number of bytes each queue gets to dequeue on each round of the scheduling algorithm
    */
   uint32_t GetQuantum (void) const;

  // Reasons for dropping packets
  static constexpr const char* UNCLASSIFIED_DROP = "Unclassified drop";  //!< No packet filter able to classify packet
  static constexpr const char* OVERLIMIT_DROP = "Overlimit drop";        //!< Overlimit dropped packets

  //Ptr<QueueDiscItem> Dequeue (WifiMacHeader & hdr);
  enum QueueType
  {
    CoDel = 0,
    PPP = 1,
    PPP_DELAY = 2,
    PPP_AQM = 3,
    PPP_AQM_V2 = 4,
    Red = 5,
    Fifo = 6,
    Pie = 7,
    PppPie = 8
  };
private:
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);

  /**
   * \brief Drop a packet from the head of the queue with the largest current byte count
   * \return the index of the queue with the largest current byte count
   */
  uint32_t FqPppDrop (void);

  /**
   * Compute the index of the queue for the flow having the given flowHash,
   * according to the set associative hash approach.
   *
   * \param flowHash the hash of the flow 5-tuple
   * \return the index of the queue for the given flow
   */
  uint32_t SetAssociativeHash (uint32_t flowHash);

  uint32_t m_quantum;        //!< Deficit assigned to flows at each round
  uint32_t m_flows;          //!< Number of flow queues
  uint32_t m_setWays;        //!< size of a set of queues (used by set associative hash)
  uint32_t m_dropBatchSize;  //!< Max number of packets dropped from the fat flow
  uint32_t m_perturbation;   //!< hash perturbation value
  bool m_enableSetAssociativeHash; //!< whether to enable set associative hash

  std::list<Ptr<FqPppFlow> > m_newFlows;    //!< The list of new flows
  std::list<Ptr<FqPppFlow> > m_oldFlows;    //!< The list of old flows

  std::map<uint32_t, uint32_t> m_flowsIndices;    //!< Map with the index of class for each flow
  std::map<uint32_t, uint32_t> m_tags;            //!< Tags used by set associative hash

  ObjectFactory m_flowFactory;         //!< Factory to create a new flow
  ObjectFactory m_queueDiscFactory;    //!< Factory to create a new queue

  QueueType m_type;
};

} // namespace ns3

#endif /* FQ_PPP_QUEUE_DISC */

