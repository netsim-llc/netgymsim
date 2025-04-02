/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "qos-measurement-manager.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QosMeasurementManager");

NS_OBJECT_ENSURE_REGISTERED (QosMeasurementManager);

//meausrement manager

QosMeasurementManager::QosMeasurementManager ()
{
	NS_LOG_FUNCTION (this);
}

TypeId
QosMeasurementManager::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::QosMeasurementManager")
    .SetParent<Object> ()
    .SetGroupName("Gma")
	.AddConstructor<QosMeasurementManager> ()
  ;
  return tid;
}

QosMeasurementManager::~QosMeasurementManager ()
{
	NS_LOG_FUNCTION (this);
}

void QosMeasurementManager::MeasureIntervalStartCheck(Time t, uint32_t sn, uint32_t expectedSn)
{
  //QoS measurement will be triggered by a different function
	/*m_lastIntervalStartSn = sn;
	if(SnDiff(sn, m_measureStartSn) > 0 && m_measureIntervalStarted == false)
	{
		MeasureIntervalStart(t);
	}*/
}

void
QosMeasurementManager::UpdateOwdFromProbe(uint32_t owdMs, uint8_t cid)
{
  //do not update QoS measurement. QoS only measure from data.
}

void
QosMeasurementManager::UpdateOwdFromAck(uint32_t owdMs, uint8_t cid)
{
  //do not update QoS measurement. QoS only measure from data.
  for (uint8_t index = 0; index < m_deviceList.size(); index++)
	{
		if (m_deviceList.at(index)->GetCid() == cid)
		{
			m_deviceList.at(index)->UpdateLastAckOwd(owdMs);//for computing the "normalized" OWD
			break;
		}
	}
}

void
QosMeasurementManager::DataMeasurementSample(uint32_t owdMs, uint8_t lsn, uint8_t cid)
{
	for (uint8_t index = 0; index < m_deviceList.size(); index++)
	{
		if (m_deviceList.at(index)->GetCid() == cid)
		{
			m_deviceList.at(index)->UpdateLastPacketOwd(owdMs, true);
			m_deviceList.at(index)->UpdateLsn(lsn);
			break;
		}
	}
}

void
QosMeasurementManager::MeasureIntervalEndCheck(Time t)
{
  for (uint8_t index = 0; index < m_deviceList.size(); index++)
  {
    uint8_t cid = m_deviceList.at(index)->GetCid();
    auto it = m_cidToQosMeasurementMap.find(cid);
    if(it != m_cidToQosMeasurementMap.end())
    {
      //find measurement for this cid
      if(it->second.IsRunning())
      {
        //measurement is running.
        auto itPkt = m_cidToLastPacketNMap.find(cid);
        if(itPkt != m_cidToLastPacketNMap.end())
        {
          //std::cout << Now().GetSeconds() << "Find measurement Running for CID:" << +cid << std::endl;

          //this map will be empty for the first measurement (QoS Testing or After Idle)
          //For QoS monitoring, this map will not be empty.
          
          //we estimate current interval packetN to be PKT_NUM_WEIGHT * packtN in previous interval.
          double estPktN = m_rxControl->PKT_NUM_WEIGHT*itPkt->second; //scale down the pkt N to be more conservative.
          //if the packet N is this interval is greater than estPktN, we use the current measured packet N.
          estPktN = std::max(estPktN, (double)m_deviceList.at(index)->m_numOfOwdPacketsPerInterval);

          NS_ASSERT_MSG(estPktN != 0, "the estimated pkt number cannot be 0!");

          double highOwdRate = 1.0*m_deviceList.at(index)->m_numOfHighOwdPacketsPerInterval/estPktN;

          double lossRate = 0.0;

          if (m_deviceList.at(index)->m_numOfMissingPacketsPerCycle > m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle)
          {
             lossRate = 1.0*(m_deviceList.at(index)->m_numOfMissingPacketsPerCycle - m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle)
                  /(m_deviceList.at(index)->m_numOfMissingPacketsPerCycle + m_deviceList.at(index)->m_numOfInOrderPacketsPerCycle);
         
          }
          if(highOwdRate > m_rxControl->m_qosDelayViolationTarget || lossRate > m_rxControl->m_qosLossTarget)
          {
              std::cout << Now().GetSeconds() << "[EARLY VIOLATION DETECTED] pktN :" << itPkt->second 
              << " delay violation target:" << m_rxControl->m_qosDelayViolationTarget 
              << " loss target:" << m_rxControl->m_qosLossTarget
              << std::endl;

              std::cout << " delay violation rate:" << highOwdRate<< " = delay violation packet:"<< m_deviceList.at(index)->m_numOfHighOwdPacketsPerInterval 
              << "/ estPktN:" << estPktN
              <<std::endl;
              std::cout << "loss rate:" << lossRate<< " = (missing :"<< m_deviceList.at(index)->m_numOfMissingPacketsPerCycle << " - abnormal :" <<  m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle
              << ")/ (estPktN :" << estPktN << ")" << std::endl;

              Ptr<RxMeasurement> rxMeasurement = Create <RxMeasurement> ();
              rxMeasurement->m_links = m_deviceList.size();
              rxMeasurement->m_measureIntervalThreshS = 0.1*m_qosDuration;
              rxMeasurement->m_measureIntervalDurationS = 0.1*m_qosDuration; //qos duration unit is 100ms
              for (uint8_t index = 0; index < m_deviceList.size(); index++)
              {
                rxMeasurement->m_cidList.push_back(m_deviceList.at(index)->GetCid());

                if(m_deviceList.at(index)->GetCid() == cid)
                {
                  NS_ASSERT_MSG(m_deviceList.at(index)->m_numOfOwdPacketsPerInterval > 0, 
                      "did not receive any pkt, cannot terminate this measurement!");

                  double aveOwd = m_deviceList.at(index)->m_sumOwdPerInterval/m_deviceList.at(index)->m_numOfOwdPacketsPerInterval;
                  //std::cout << " owd sum:" << m_deviceList.at(index)->m_sumOwdPerInterval << " packet sum:" << m_deviceList.at(index)->m_numOfOwdPacketsPerInterval
                  //  << " aveOwd:" << aveOwd
                  //  <<std::endl;
                  rxMeasurement->m_delayList.push_back(aveOwd);


                  rxMeasurement->m_highDelayRatioList.push_back(highOwdRate);
                  rxMeasurement->m_lossRateList.push_back(lossRate);
                  m_cidToLastPacketNMap.erase(cid);
                }
                else//results for primary link, this measurement will not be used by the rx algorithm.
                {
                  rxMeasurement->m_delayList.push_back(-1);
                  rxMeasurement->m_highDelayRatioList.push_back(-1);
                  rxMeasurement->m_lossRateList.push_back(-1);
                }
              }

            Ptr<SplittingDecision> decision = m_rxControl->GetTrafficSplittingDecision(rxMeasurement);
            if(decision->m_update)
            {
              //send TSU message
              m_sendTsuCallback(decision);
            }
            else
            {
              NS_FATAL_ERROR("must return an update == true!");
            }
            it->second.Cancel();//stop this measurement!
          }
        }
      }
    }
  }
}

bool
QosMeasurementManager::QosMeasurementStart (uint8_t cid, uint8_t duration)
{
  //TODO: terminate the QOS measurement if violation is detected (early termination)...
  //e.g., we set some QOS requirement at the beginning of the measurement. packet loss ratio, high delay packet ratio etc.

  auto it = m_cidToQosMeasurementMap.find(cid);
  if (it!=m_cidToQosMeasurementMap.end() && it->second.IsRunning())
  {
    //measurement already running, no action. this happens when qos request is retransmitted...
    //NS_FATAL_ERROR("cannot run this measurement since the previous measurement is running");
    return false;
  }
  else
  {
    //m_qosDUration is updated from the qos testing.
    //for qos monitoring (after TSU/TSA), we reuse the save duration from the testing.
    if(duration != 0)//duration for QoS testing duration will not be zero, duration for QoS monitoring will be 0
    {
      m_qosDuration = duration;//save the test duration for monitoring
      m_cidToLastPacketNMap.erase(cid);//this is testing, clear the map to disable early detection.
    }

    for (uint8_t index = 0; index < m_deviceList.size(); index++)
    {
      if(m_deviceList.at(index)->m_cid == cid)
      {
        m_deviceList.at(index)->m_sumOwdPerInterval = 0;
        m_deviceList.at(index)->m_numOfOwdPacketsPerInterval = 0;
        m_deviceList.at(index)->m_numOfHighOwdPacketsPerInterval = 0;
        m_deviceList.at(index)->InitialMeasurementCycle();
        break;
      }
    }
  }
  NS_ASSERT_MSG(m_qosDuration != 0, "did not receive the qos request yet!!! durationS:" <<+duration);
  m_cidToQosMeasurementMap[cid] = Simulator::Schedule(MilliSeconds(m_qosDuration*100), &QosMeasurementManager::QosMeasurementStop, this, cid, duration);
  return true;

}

void
QosMeasurementManager::QosMeasurementStop (uint8_t cid, uint8_t duration)
{
  Ptr<RxMeasurement> rxMeasurement = Create <RxMeasurement> ();
  rxMeasurement->m_links = m_deviceList.size();
  rxMeasurement->m_measureIntervalThreshS = 0.1*m_qosDuration;
  rxMeasurement->m_measureIntervalDurationS = 0.1*m_qosDuration;  //qos duration unit is 100ms
  //std::cout << Now().GetSeconds() <<" QOS measurement interval " << +m_measureIntervalIndex << " for cid:" << +cid << " End." << std::endl;
  for (uint8_t index = 0; index < m_deviceList.size(); index++)
  {
    rxMeasurement->m_cidList.push_back(m_deviceList.at(index)->GetCid());

    if(m_deviceList.at(index)->GetCid() == cid)
    {
      if(m_deviceList.at(index)->m_numOfOwdPacketsPerInterval > 0)//received packets from this device(lte/wifi)
      {
        double aveOwd = m_deviceList.at(index)->m_sumOwdPerInterval/m_deviceList.at(index)->m_numOfOwdPacketsPerInterval;
        //std::cout << " owd sum:" << m_deviceList.at(index)->m_sumOwdPerInterval << " packet sum:" << m_deviceList.at(index)->m_numOfOwdPacketsPerInterval
        //  << " aveOwd:" << aveOwd
        //  <<std::endl;
        rxMeasurement->m_delayList.push_back(aveOwd);

        double highOwdRate = 1.0*m_deviceList.at(index)->m_numOfHighOwdPacketsPerInterval/m_deviceList.at(index)->m_numOfOwdPacketsPerInterval;

        //std::cout << " high owd rate:" << highOwdRate<< " = high owd packet:"<< m_deviceList.at(index)->m_numOfHighOwdPacketsPerInterval 
        //<< "/ packet sum:" << m_deviceList.at(index)->m_numOfOwdPacketsPerInterval
        //  <<std::endl;

        rxMeasurement->m_highDelayRatioList.push_back(highOwdRate);
        double lossRate = 0;
        if (m_deviceList.at(index)->m_numOfMissingPacketsPerCycle > m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle)
        {
          lossRate = 1.0*(m_deviceList.at(index)->m_numOfMissingPacketsPerCycle - m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle)
                /(m_deviceList.at(index)->m_numOfMissingPacketsPerCycle + m_deviceList.at(index)->m_numOfInOrderPacketsPerCycle);
        }

        //std::cout << "loss rate:" << lossRate<< " = (missing :"<< m_deviceList.at(index)->m_numOfMissingPacketsPerCycle << " - abnormal :" <<  m_deviceList.at(index)->m_numOfAbnormalPacketsPerCycle
        //<< ")/ (missing :" << m_deviceList.at(index)->m_numOfMissingPacketsPerCycle << " + inorder:" << m_deviceList.at(index)->m_numOfInOrderPacketsPerCycle << ")" << std::endl;
        rxMeasurement->m_lossRateList.push_back(lossRate);
        m_cidToLastPacketNMap[cid] = m_deviceList.at(index)->m_numOfOwdPacketsPerInterval;
      }
      else
      {
        //no measurement we mark idle as -1...
        rxMeasurement->m_delayList.push_back(-1);
        rxMeasurement->m_highDelayRatioList.push_back(-1);
        rxMeasurement->m_lossRateList.push_back(-1);
        m_cidToLastPacketNMap.erase(cid);//no measurement, the next measurement will disable early detection
      }      
    }
    else//results for primary link, this measurement will not be used by the rx algorithm.
    {
      rxMeasurement->m_delayList.push_back(-1);
      rxMeasurement->m_highDelayRatioList.push_back(-1);
      rxMeasurement->m_lossRateList.push_back(-1);
    }
    
  }

  Ptr<SplittingDecision> decision = m_rxControl->GetTrafficSplittingDecision(rxMeasurement);
  //let us start a new cycle.
  if(decision->m_update)
  {
    //send TSU message
    m_sendTsuCallback(decision);
  }

  if(duration == 0 && decision->m_update == false)//qos monitoring and traffic over backup link
  {
    //restart qos monitoring
    bool success = QosMeasurementStart(cid, 0);
    NS_ASSERT_MSG(success, "Fail: Measurement is already running!!");
  }

}

void
QosMeasurementManager::QuitQosMeasurement(uint8_t cid)
{
  auto it = m_cidToQosMeasurementMap.find(cid);
  if (it!=m_cidToQosMeasurementMap.end() && it->second.IsRunning())
  {
    it->second.Cancel();
  }
}

}


