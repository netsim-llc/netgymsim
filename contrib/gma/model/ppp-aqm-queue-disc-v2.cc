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
#include "ppp-aqm-queue-disc-v2.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/simulator.h"
#include <iostream>
#include <fstream>
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PppAqmQueueDiscV2");

NS_OBJECT_ENSURE_REGISTERED (PppAqmQueueDiscV2);

TypeId PppAqmQueueDiscV2::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PppAqmQueueDiscV2")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PppAqmQueueDiscV2> ()
    .AddAttribute ("MaxSize",
                   "The max queue size",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
  ;
  return tid;
}

PppAqmQueueDiscV2::PppAqmQueueDiscV2 ()
  : QueueDisc (QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
  NS_LOG_FUNCTION (this);
  for (uint32_t ind = 0; ind < m_level; ind++)
  {
    m_bytesPerPpp.push_back(0);
  }

  //comment this line to disable dynamic high low threshold
  Simulator::Schedule(MEASURE_INTERVAL, &PppAqmQueueDiscV2::MeasurementEvent, this);
}

PppAqmQueueDiscV2::~PppAqmQueueDiscV2 ()
{
  NS_LOG_FUNCTION (this);
}

uint32_t
PppAqmQueueDiscV2::GetDropDeficit(uint32_t owdMs)
{
  uint32_t dropDeficit = 0;
  uint32_t queueSize = GetInternalQueue (0)->GetNBytes ();
  //if (queueSize > 1)//if queue size is small, no need to drop
      {
        //dropDeficit = std::round(std::max((double)0, (double)queueSize - 0.2 * m_delayLimit * m_outPktPerMs));
        //dropDeficit = std::round(std::max((double)0, (double)queueSize - m_delayLimit * (m_inPktPerMs - m_outPktPerMs)));
        //double ratio = (((double)owdMs/m_delayLimit-m_lineAlpha));
        //if(ratio > 0)
        //{
        // dropDeficit = std::round(ratio*queueSize);
        //}


        //dropDeficit = std::round( (double)queueSize*owdMs/m_delayLimit);
        dropDeficit = std::round( std::pow((double)owdMs/m_delayLimit, 2)*queueSize);
        //dropDeficit = queueSize*std::sqrt((double)owdMs/m_delayLimit);
        //dropDeficit = queueSize*owdMs*owdMs*owdMs*owdMs/((m_delayLimit-7)*(m_delayLimit-7)*(m_delayLimit-7)*(m_delayLimit-7));

        //dropDeficit = std::max((double)0, (double)queueSize - ((double)m_delayLimit - (double)owdMs)*m_outPktPerMs);
        //std::cout << Now().GetSeconds() << "queue :" << queueSize << " deficit :" << dropDeficit << " m_inPktPerMs:" << m_inPktPerMs << " m_outPktPerMs:" << m_outPktPerMs << "\n";

        //sigmoid
        /*if(owdMs < m_delayLimit)
        {
          double x = (double)owdMs/m_delayLimit;
          double x_tran = x*10-5;
          double z = 1/(1 + std::exp(-x_tran));
          //double z = 1/(1 + std::exp(-x_tran))*1.1-0.05;
          dropDeficit = std::round(z*queueSize);
        }
        else
        {
          dropDeficit = queueSize;
        }*/
      }
      return dropDeficit;
}

bool
PppAqmQueueDiscV2::DoEnqueue (Ptr<QueueDiscItem> item)
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
  if(item->GetPacket()->PeekPacketTag(tag))//ppp tag exists, add to the m_bytesPerPpp
  {
    //let us increase drop deficit even at enqueue!!!

    uint32_t owdMs = UINT32_MAX;

    if(owdMs == UINT32_MAX)//no measurement, we use the queuing delay of the first packet in the queue...
    {
      Ptr<const QueueDiscItem> firstItem = GetInternalQueue (0)->Peek ();

      if (!firstItem)
      {
          NS_LOG_LOGIC ("Queue empty");
      }
      else
      {
        //update owd
        owdMs = Now().GetMilliSeconds() - firstItem->GetTimeStamp().GetMilliSeconds();

      }

    }

    if(owdMs != UINT32_MAX)//we have measurement..
    {

      //uint32_t dropDeficit = GetDropDeficit(owdMs);
      
      uint32_t dropDeficit = 0;

      uint32_t pppIter = tag.GetPriority() + 1;
      uint32_t lowPriorityBytes = 0;
      while(pppIter < m_level)
      {
        lowPriorityBytes += m_bytesPerPpp.at(pppIter);
        pppIter++;
      }

      if(tag.GetPriority() > 0 && dropDeficit > lowPriorityBytes + item->GetPacket()->GetSize())//not enough low priority packets in the queue, drop it
      {
        std::cout << "LOW DELAY drop owd:" << owdMs << " ppp" << +tag.GetPriority() <<" ---- bytes to be dropped:" << dropDeficit << " low ppp bytes in the queue:" << lowPriorityBytes <<"\n";
        //drop this packet (P > 0).
        m_earlyLossSum++;
        NS_LOG_LOGIC ("AQM -- dropping pkt");
        DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
        return false;
      }
      else
      {
        //enqueue this packet
      }

    }

    m_bytesPerPpp.at(tag.GetPriority()) += item->GetPacket()->GetSize();
  }

  /*for (uint32_t ind = 0; ind < m_level; ind++)
  {
    std::cout << "PPP:" << ind << " #:" << m_bytesPerPpp.at(ind) << " | ";
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
PppAqmQueueDiscV2::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  while(true)
  {
    //dequeue a new packet to replace the current one. Old one is dropped
    Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();

    if (!item)
      {
        //m_queueMin = 0;
        NS_LOG_LOGIC ("Queue empty");
        return 0;
      }

    PppTag tag;
    if(!item->GetPacket()->PeekPacketTag(tag))//no ppp tag exists, return this tiem
    {
      return item;
    }

    //update packet count per PPP, we do not need P = 0 and P = 1
    m_bytesPerPpp.at(tag.GetPriority()) -= item->GetPacket()->GetSize();


    //update owd
    uint32_t owdMs = Now().GetMilliSeconds() - item->GetTimeStamp().GetMilliSeconds();

    m_numMeasure++;
    m_queueSum += GetInternalQueue (0) ->GetNBytes ();
    //if(GetCurrentSize().GetValue() < m_queueMin)
    //{
    //  m_queueMin = GetCurrentSize().GetValue();
    //}
    m_owdSum += owdMs;



    //if the current queue delay > queue delay
    if(owdMs > m_delayLimit && m_dropHighestPriority)
    {
      std::cout << "MAX delay limit drop\n";
      //drop this packet no matter what priority
      m_lossSum++;
      continue;
    }
    else
    {
 
      //uint32_t dropDeficit = GetDropDeficit(owdMs);
      if(m_meanDelay == UINT32_MAX)//initial value
      {
        m_meanDelay = owdMs;
      }
      else
      {
        m_meanDelay = m_ewmaLambda*owdMs + (1-m_ewmaLambda)*m_meanDelay;
      }
      uint32_t dropDeficit = GetDropDeficit(m_meanDelay);

      uint32_t pppIter = tag.GetPriority()+1;
      uint32_t lowPriorityBytes = 0;
      while(pppIter < m_level)
      {
        lowPriorityBytes += m_bytesPerPpp.at(pppIter);
        pppIter++;
      }

      if(tag.GetPriority() > 0 && dropDeficit > lowPriorityBytes + item->GetPacket()->GetSize())//not enough low priority packets in the queue, drop it
      {
        std::cout << "LOW DELAY drop owd:" << owdMs << " ppp" << +tag.GetPriority() <<" ---- bytes to be dropped:" << dropDeficit << " low ppp bytes in the queue:" << lowPriorityBytes <<"\n";
        m_earlyLossSum++;
        continue;
      }

    }

    //m_owdLast = owdMs;
    //if(owdMs > m_owdMax)
    //{
    //  m_owdMax = owdMs;
    //}
    //if (owdMs < m_owdMin)
    //{
    //  m_owdMin = owdMs;
    //}

    //transmit this packet
    if(owdMs > 0.5*m_delayLimit)
    {
      m_highDelaySum++;
    }
    m_txSum++;
    return item;
  }
}

Ptr<const QueueDiscItem>
PppAqmQueueDiscV2::DoPeek (void)
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
PppAqmQueueDiscV2::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("PppAqmQueueDiscV2 cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("PppAqmQueueDiscV2 needs no packet filter");
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
      NS_LOG_ERROR ("PppAqmQueueDiscV2 needs 1 internal queue");
      return false;
    }

  return true;
}

void
PppAqmQueueDiscV2::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

void
PppAqmQueueDiscV2::MeasurementEvent ()
{
  //std::cout << Now().GetSeconds()<< " High Thresh:" << m_highThreshPer << " Low Thresh:" << m_lowThreshPer << " max delay of ppp:"<< m_maxDelayPpp << "\n";

    //I will store a txt file for measurement...

      //log file;
    std::ostringstream fileName;
    fileName <<"test-trace.txt";
    std::ofstream myfile;
    myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);

    if(m_numMeasure > 0 && (double)m_queueSum/m_numMeasure > 0)
    {
      myfile << Now().GetSeconds() << "\t" 
      << m_numMeasure << "\t" 
      << m_txSum << "\t " 
      << m_lossSum << "\t" 
      << (double)m_queueSum/m_numMeasure << "\t"
      << (double)m_owdSum/m_numMeasure<< "\n";
    }
    myfile.close();

    
    std::ostringstream fileName1;
    fileName1 <<"alpha-trace"<< this <<".txt";
    std::ofstream myfile1;
    myfile1.open (fileName1.str ().c_str (), std::ios::out | std::ios::app);
    myfile1 << Now().GetSeconds() << "\t" << m_lossSum << "\t" << (double)m_queueSum/m_numMeasure << "\t" << m_lineAlpha << "\n";
    myfile1.close();


  //TODO: inspired by ARED, we can use average queueing delay to adapt the line alpha.

  if(m_enableAdaptiveLine)
  {
    if(m_earlyLossSum != 0 || m_lossSum+m_highDelaySum != 0)
    {

      if(m_lossSum+m_highDelaySum != 0)
      {
        //if(m_earlyLossSum != 0)
        //{
        //  //both not zero.. update m_lineAlpha
        //  double offset = (double) m_lossSum/(m_earlyLossSum + m_lossSum);
        //  std::cout << "Alpha decrease by offset :" << offset << "\n";
        //  m_lineAlpha = std::max(0.0, m_lineAlpha-offset);
        //}
        //else
        {
          //reduce a small number 
          m_lineAlpha = std::max(0.0, m_lineAlpha*0.8);
          //m_lineAlpha = std::max(0.0, m_lineAlpha-0.1);
          std::cout << "Alpha decrease by fix scale: x0.8\n";
        }
      }
      else //m_lossSum == 0
      {
        if(m_earlyLossSum != 0)
        {
          //uint32_t owdPara = (double)m_owdSum/m_numMeasure; //average
          //uint32_t owdPara = m_owdLast;
          //uint32_t owdPara = m_owdMin;
          //uint32_t owdPara = m_owdMax;
          //if(owdPara < 0.1*m_delayLimit)
          //if(m_queueMin <= 2)
          {
            //we need to increase m_lineAlpha
            m_lineAlpha = std::min(m_maxLineAlpha, m_lineAlpha+0.1);
            std::cout << "Alpha increase by fix value: 0.1\n";
          }
        }
        else
        {
          //no loss, do nothing..
        }

      }

      std::cout << Now().GetSeconds() << "Early Loss:" << m_earlyLossSum << " Loss:" << m_lossSum
      << " Ave Owd:" << (double)m_owdSum/m_numMeasure 
      //<< " Last Owd:" << m_owdLast 
      //<< " Min Owd:" << m_owdMin
      //<< " Max Owd:" << m_owdMax
      << " alpha:" << m_lineAlpha<< "\n";
    }
  }
  
  m_numMeasure = 0;
  m_queueSum = 0;
  m_owdSum = 0;
  //m_owdLast = 0;
  //m_owdMin = UINT64_MAX;
  //m_owdMax = 0;
  m_lossSum = 0;
  m_txSum = 0;
  m_earlyLossSum = 0;
  m_highDelaySum = 0;
  Simulator::Schedule(MEASURE_INTERVAL, &PppAqmQueueDiscV2::MeasurementEvent, this);

}


} // namespace ns3
