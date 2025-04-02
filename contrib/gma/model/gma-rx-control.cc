/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "gma-rx-control.h"
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("GmaRxControl");

NS_OBJECT_ENSURE_REGISTERED (GmaRxControl);

GmaRxControl::GmaRxControl ()
{
  NS_LOG_FUNCTION (this);
  m_updateThreshold = 0.03;
}


TypeId
GmaRxControl::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::GmaRxControl")
    .SetParent<Object> ()
    .SetGroupName("Gma")
    .AddAttribute ("SplittingAlgorithm", 
    			"Receiver side traffic splitting algorithm.",
               EnumValue (GmaRxControl::Delay),
               MakeEnumAccessor (&GmaRxControl::m_algorithm),
               MakeEnumChecker (GmaRxControl::Delay, "optimize average delay",
			   					GmaRxControl::gma, "same as Delay, optimize average delay",
								GmaRxControl::gma2, "split case optimize delay violation, steer case select min owd link",
              					GmaRxControl::CongDelay, "after primary link congests, optimize average delay",
								GmaRxControl::QosSteer, "qos steer mode",
              					GmaRxControl::DefaultLink, "default link"))
    .AddAttribute ("SplittingBurst",
               "The splitting burst size for traffic spliting algorithm, support 1(steer mode), 4, 8, 16, 32, 64, 128",
               UintegerValue (1),
               MakeUintegerAccessor (&GmaRxControl::m_splittingBurst),
               MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("DelayThresh",
               "traffic split only if the max link delay > min link dleay + DelayThresh [ms]",
			   DoubleValue (5),
               MakeDoubleAccessor (&GmaRxControl::m_delayThresh),
               MakeDoubleChecker<double> ())
    .AddAttribute ("EnableAdaptiveStep",
	           "If true, enable adaptive steps",
	           BooleanValue (true),
	           MakeBooleanAccessor (&GmaRxControl::m_enableAdaptiveStep),
	           MakeBooleanChecker ())
    .AddAttribute ("EnableStableAlgorithm",
	           "If true, enable stable version of the algorithm",
	           BooleanValue (true),
	           MakeBooleanAccessor (&GmaRxControl::m_enableStableAlgorithm),
	           MakeBooleanChecker ())
    .AddAttribute ("EnableLossAlgorithm",
	           "If true, if two links have the same delay range, favor the low loss one",
	           BooleanValue (true),
	           MakeBooleanAccessor (&GmaRxControl::m_enableLossAlgorithm),
	           MakeBooleanChecker ())
	.AddAttribute ("QosDelayViolationTarget",
               "the max acceptable ratio of high delay packet for Qos Flow",
			   DoubleValue (0.01),
               MakeDoubleAccessor (&GmaRxControl::m_qosDelayViolationTarget),
               MakeDoubleChecker<double> ())
	.AddAttribute ("QosLossTarget",
			"the max acceptable ratio of packet loss for qos flow",
			DoubleValue (1.0),
			MakeDoubleAccessor (&GmaRxControl::m_qosLossTarget),
			MakeDoubleChecker<double> ())
    .AddAttribute ("EnableQosFlowPrioritization",
	           "If true, Enable Qos Flow Prioritization",
	           BooleanValue (false),
	           MakeBooleanAccessor (&GmaRxControl::m_enableQosFlowPrioritization),
	           MakeBooleanChecker ())
  ;
  return tid;
}


GmaRxControl::~GmaRxControl ()
{
	NS_LOG_FUNCTION (this);
}

void
GmaRxControl::SetLinkState(Ptr<LinkState> linkstate)
{
	m_linkState = linkstate;
}

void
GmaRxControl::SetSplittingBurst(uint8_t splittingBurst)
{
	NS_ASSERT_MSG(splittingBurst == 1 || splittingBurst == 4 || splittingBurst == 8 || splittingBurst == 16 || splittingBurst == 32 || splittingBurst == 128,
			"Now only support size 1, 4, 8 16, 32, 64 and 128.");
	if (splittingBurst == 0)
	{
		//enable adaptive splitting burst.
		m_splittingBurst = GetMinSplittingBurst();
		m_adaptiveSplittingBurst = true;
	}
	else
	{
		m_splittingBurst = splittingBurst;
		m_adaptiveSplittingBurst = false;
	}
}

uint8_t
GmaRxControl::GetSplittingBurst(void)
{
	return m_splittingBurst;
}

void
GmaRxControl::SetDelayThresh(double delay)
{
	NS_ASSERT_MSG(delay >= 0, "delay thresh must be greater than 0");
	m_delayThresh = delay;
}

void
GmaRxControl::SetAlgorithm (std::string algorithm)
{
	if (algorithm == "Delay")
	{
		m_algorithm = GmaRxControl::Delay;
	}
	else if (algorithm == "gma")
	{
		m_algorithm = GmaRxControl::gma;
	}
	else if (algorithm == "gma2")
	{
		m_algorithm = GmaRxControl::gma2;
	}
	else if (algorithm == "CongDelay")
	{
		m_algorithm = GmaRxControl::CongDelay;
	} 
	else if (algorithm == "DefaultLink")
	{
		m_algorithm = GmaRxControl::DefaultLink;
	}
	else if (algorithm == "RlSplit")
	{
		m_algorithm = GmaRxControl::RlSplit;
	} 
	else if (algorithm == "QosSteer")
	{
		m_algorithm = GmaRxControl::QosSteer;
	} 
	else
	{
		NS_FATAL_ERROR ("algorithm not supported");
	}
}

Ptr<SplittingDecision>
GmaRxControl::GetTrafficSplittingDecision (Ptr<RxMeasurement> measurement)
{
	if(measurement->m_links == 0)
	{
		NS_FATAL_ERROR("Link cannot be 0!");
	}

	NS_ASSERT_MSG(measurement->m_links == measurement->m_cidList.size(), "size does not match!");
	NS_ASSERT_MSG(measurement->m_links == measurement->m_delayThisInterval.size(), "size does not match!");
	NS_ASSERT_MSG(measurement->m_links == measurement->m_delayList.size(), "size does not match!");
	NS_ASSERT_MSG(measurement->m_links == measurement->m_minOwdLongTerm.size(), "size does not match!");
	NS_ASSERT_MSG(measurement->m_links == measurement->m_lossRateList.size(), "size does not match!");
	//oscialltion happens because the optimal ratio may not be acheivable due to the finite number of splitting burst size.
	//In this case, the split ratio may bounce between 2 or few values.
	Ptr<SplittingDecision> params;

	if(m_algorithm == GmaRxAlgorithm::QosSteer)
	{
		if(m_splittingBurst == 1)//QosSteer m_splittingBurst size equals 1
		{
			//qos algorithms.
			params = QosSteerAlgorithm (measurement);
		}
		else
		{
			NS_FATAL_ERROR("must use the qos steer mode!!");
		}
	}
	else if(m_algorithm == GmaRxControl::Delay)
	{
		params = DelayAlgorithm (measurement);
	}
	else if(m_algorithm == GmaRxControl::gma)
	{
		params = DelayAlgorithm (measurement);
	}
	else if(m_algorithm == GmaRxControl::gma2)
	{
		if(m_splittingBurst == 1)//steer mode
		{
			m_enableLossAlgorithm = false;
			params = DelayAlgorithm (measurement, true); //still use old algorithm for steer mode, but change measurement from average to min delay.
		}
		else //split mode
		{
			if((int)m_splittingBurst < GetMinSplittingBurst() || (int)m_splittingBurst > GetMaxSplittingBurst())
			{
				NS_FATAL_ERROR("for gma2.0 algorithm, must set the splitting burst size to be the range [min, max].");
			}
			params = DelayViolationAlgorithm (measurement);
		}
	}
	else if(m_algorithm == GmaRxControl::CongDelay)
	{
		params = CongDelayAlgorithm (measurement);
	}
	else if(m_algorithm == GmaRxControl::DefaultLink)
	{
		//std::cout << "no update \n";
		Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
		decision->m_update = false;
		return decision;
	}
	else if(m_algorithm == GmaRxControl::RlSplit)
	{
		//std::cout << "no update \n";
		Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
		decision->m_update = false;
		return decision;
	}
	else
	{
		NS_FATAL_ERROR("Unknown link selection method");
	}


	return params;
}

Ptr<SplittingDecision>
GmaRxControl::GetTrafficSplittingDecision (Ptr<RlAction> action)
{
	if(action->m_links == 0)
	{
		NS_FATAL_ERROR("Link cannot be 0!");
	}

	NS_ASSERT_MSG(action->m_links == action->m_cidList.size(), "size does not match!");
	NS_ASSERT_MSG(action->m_links == action->m_ratio.size(), "size does not match!");
	//oscialltion happens because the optimal ratio may not be acheivable due to the finite number of splitting burst size.
	//In this case, the split ratio may bounce between 2 or few values.
	bool update = false;
	std::vector<uint8_t> splittingIndexList;
	for (uint8_t ind = 0; ind < action->m_links; ind++)
	{
		uint8_t cid = action->m_cidList.at(ind);
		uint8_t ratio = action->m_ratio.at(ind);
		if (m_lastSplittingIndexList.size() == 0)
		{
			update = true;
			std::vector<uint8_t> cidList = m_linkState->GetCidList();

			for (uint8_t i = 0; i < cidList.size(); i++)
			{
				if(cidList.at(i) == cid)
				{
					splittingIndexList.push_back(ratio);
				}
			}

		}
		else if (m_lastSplittingIndexList.at(m_linkState->m_cidToIndexMap[cid]) != ratio)
		{
			update = true;
			m_lastSplittingIndexList.at(m_linkState->m_cidToIndexMap[cid]) =  ratio;
		}
	}

	if(m_lastSplittingIndexList.size() > 0)
	{
		splittingIndexList = m_lastSplittingIndexList;
	}
	//check if the sum equals splitting burst size;
	uint8_t sumRatio = 0;
	for (uint8_t ind = 0; ind < action->m_links; ind++)
	{
		sumRatio += splittingIndexList.at (ind);
	}
	//For ML algorithm, we can use any splitting burst size.
	//NS_ASSERT_MSG(sumRatio == m_splittingBurst, "the summation must equals the splitting burst size");
	if(sumRatio != m_splittingBurst)
	{
		m_splittingBurst = sumRatio;
		NS_ASSERT_MSG(m_splittingBurst == 1 || m_splittingBurst == 4 || m_splittingBurst == 8 || m_splittingBurst == 16 || m_splittingBurst == 32 || m_splittingBurst == 64 || m_splittingBurst == 128,
			"Now only support size 1, 4, 8 16, 32, 64 and 128.");
	}

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
	decision->m_splitIndexList = splittingIndexList;
	decision->m_update = update;
	//std::cout << " update:" << decision->m_update << " decision:" << +decision->m_splitIndexList.at(0) << " " <<+decision->m_splitIndexList.at(1) << " splittingBurst size:" << +m_splittingBurst<< std::endl;
	return decision;

}

Ptr<SplittingDecision>
GmaRxControl::CongDelayAlgorithm (Ptr<RxMeasurement> measurement)
{
	//start from all traffic goes to the first link
	if(m_lastSplittingIndexList.size() == 0)
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
			{
				m_lastSplittingIndexList.push_back(m_splittingBurst);
			}
			else
			{
				m_lastSplittingIndexList.push_back(0);
			}
			m_decreaseCounter.push_back(1);//all link start counter from 1
			if(m_enableStableAlgorithm)
			{
				m_lastOwd.push_back(measurement->m_delayList.at(ind));//initial last owd to the same as the first one
			}
		}
	}
	
	//std::cout << "RX APP ";
	if(m_lastSplittingIndexList.at(m_linkState->m_cidToIndexMap[m_linkState->GetDefaultLinkCid()]) == m_splittingBurst)
	{
		//std::cout << " Wi-Fi only loss:" << measurement->m_lossRateList.at(0);
		//all traffic goes to primary link, detect congestion
		if (measurement->m_lossRateList.at(0) > CONGESTION_LOSS_THRESHOLD)//detects congestion as packet loss
		{
			//use delay algorithm if primary link is congested.
			//std::cout <<" -> delay algorithm. \n";
			return DelayAlgorithm (measurement);
		}
		else
		{
			//not congested, stay in primary link
			//std::cout << "no update \n";
			Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
			decision->m_update = false;
			return decision;
		}
	}
	else
	{
		//already start splitting, keep using delay algorithm
		//std::cout <<"more than one link is used -> delay algorithm. \n";
		return DelayAlgorithm (measurement);
	}



}

Ptr<SplittingDecision>
GmaRxControl::DelayAlgorithm (Ptr<RxMeasurement> measurement, bool useMinOwd)
{
	if (useMinOwd)
	{
		//overwrite the delay measurement with owd.
		measurement->m_delayList = measurement->m_minOwdLongTerm;
		
	}
	//start from all traffic goes to the first link
	if(m_lastSplittingIndexList.size() == 0)
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
			{
				m_lastSplittingIndexList.push_back(m_splittingBurst);
			}
			else
			{
				m_lastSplittingIndexList.push_back(0);
			}
			m_decreaseCounter.push_back(1);//all link start counter from 1
			if(m_enableStableAlgorithm)
			{
				m_lastOwd.push_back(measurement->m_delayList.at(ind));//initial last owd to the same as the first one
			}
		}
	}

	//we have steps in the delay algorithm:
	//(1) if max owd - min owd > Thresh, move traffic from max owd link to min owd link;
	//(2) all links have same delay, if max loss - min loss > loss thresh, move traffic from max loss link to min loss link;
	//(3) [For steer mode: all links have same delay and same loss, if Wi-Fi RSSI is high, move traffic to wifi].
	//find the max and min delay and index of all links.
	double minDelay = 0;
	double maxDelay = 0;
	uint8_t minIndex = 0; 
	uint8_t maxIndex = 0;
	bool initialDelay = false;
	//initial min and max link cid to any link with traffic.

	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		if(m_lastSplittingIndexList.at(ind) != 0)
		{
			minDelay = measurement->m_delayList.at(ind);
			minIndex = ind;
			maxDelay = measurement->m_delayList.at(ind);
			maxIndex = ind;
			initialDelay = true;
			break;
		}
	}
	NS_ASSERT_MSG(initialDelay == true, "cannot initialize the min and max delay index");

	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
		{
			if(minDelay > measurement->m_delayList.at(ind))
			{
				//find a link with lower delay. this can be idle link (no traffic)
				minDelay = measurement->m_delayList.at(ind);
				minIndex = ind;
			}

			if(maxDelay < measurement->m_delayList.at(ind) && m_lastSplittingIndexList.at(ind) != 0)
			{
				//find a link with active traffic and with higher delay,
				maxDelay = measurement->m_delayList.at(ind);
				maxIndex = ind;
			}

		}
	}

	//change ratio only if |max delay - min delay| > m_delayThresh, in order to make this algorithm converge

	//std::cout << Now().GetSeconds() << " minDelay: " << minDelay << " last minDelay: " << m_lastOwd.at(minIndex) << " minIndex: " << +minIndex << " minCid: " << +measurement->m_cidList.at(minIndex) << std::endl;
	//std::cout << Now().GetSeconds() << " maxDelay: " << maxDelay << " last maxDelay: " << m_lastOwd.at(maxIndex) << " maxIndex: " << +maxIndex << " maxCid: " << +measurement->m_cidList.at(maxIndex) << std::endl;

	bool update = false;
	if( maxDelay - minDelay > m_delayThresh)
	{
		if (m_enableStableAlgorithm == false || (m_enableStableAlgorithm == true && m_lastOwd.at(maxIndex) <= measurement->m_delayList.at(maxIndex) ))
		{
			//if statble algorithm not enabled, 
			//or stable algorithm enabled and this measurement delay is not decreasing
			if(m_lastSplittingIndexList.at(maxIndex) > 0)
			{
				if(m_enableAdaptiveStep)
				{
					for (uint8_t ind = 0; ind < m_decreaseCounter.size(); ind++)
					{
						if(ind == maxIndex)
						{
							//increase the number of decrease counter
							m_decreaseCounter.at(maxIndex)++;
						}
						else
						{
							//for other links, reset to 1.
							m_decreaseCounter.at(ind) = 1;
						}
					}
					//increase step if the maxIndex link continue decreases multiple times
					int step = std::max((int)m_decreaseCounter.at(maxIndex) - STEP_THRESH, 1);

					if (step >= m_lastSplittingIndexList.at(maxIndex))
					{
						//make sure the m_lastSplittingIndexList.at(maxIndex)-step not equal to zero!
						step = m_lastSplittingIndexList.at(maxIndex);
					}

					m_lastSplittingIndexList.at(maxIndex) -= step;
					m_lastSplittingIndexList.at(minIndex) += step;
				}
				else
				{
					m_lastSplittingIndexList.at(maxIndex)--;
					m_lastSplittingIndexList.at(minIndex)++;
				}
				update = true;
			}
		}
	}
	else//all links have same delay, reset all decrease counter to 1.
	{
		for (uint8_t ind = 0; ind < m_decreaseCounter.size(); ind++)
		{
			m_decreaseCounter.at(ind) = 1; //reset to 1.
		}
	}

	if(m_enableStableAlgorithm)
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			m_lastOwd.at(ind) = measurement->m_delayList.at(ind);
		}

	}

	if(update == false && (maxDelay - minDelay <= m_delayThresh) && m_enableLossAlgorithm)//the delay difference of all links are small.
	{
		//find the max and min Loss and index of all links.
		double minLoss = 0;
		double maxLoss= 0;
		uint8_t minLossInd = 0;
		uint8_t maxLossInd = 0;

		bool initialLoss = false;

		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
			{
				if(initialLoss == false)//min max loss not initialized yet
				{
					minLoss = measurement->m_lossRateList.at(ind);
					minLossInd = ind;
					maxLoss = measurement->m_lossRateList.at(ind);
					maxLossInd = ind;
					initialLoss = true;
				}
				else
				{
					if(minLoss > measurement->m_lossRateList.at(ind))
					{
						minLoss = measurement->m_lossRateList.at(ind);
						minLossInd = ind;
					}

					if(maxLoss < measurement->m_lossRateList.at(ind) && m_lastSplittingIndexList.at(ind) != 0)
					{
						maxLoss = measurement->m_lossRateList.at(ind);
						maxLossInd = ind;
					}
				}
			}
		}

		if(maxLoss > minLoss * LOSS_ALGORITHM_BOUND)
		{
			if(m_lastSplittingIndexList.at(maxLossInd) > 0)
			{
				//no need to use adaptive algorithm here
				m_lastSplittingIndexList.at(maxLossInd)--;
				m_lastSplittingIndexList.at(minLossInd)++;
				update = true;
			}
		}
		else
		{
			//For steer mode, if same delay and same loss, we check if the default link is up...
			//make sure the min and max is not the same link! If minIndex and maxIndex is the same link, we stay on this link.
			if(m_splittingBurst == 1 && minIndex != maxIndex && m_linkState->IsLinkUp(m_linkState->GetDefaultLinkCid()))
			{
				//steer mode, we move all traffic over wifi...
				NS_ASSERT_MSG(m_linkState->m_cidToIndexMap.find(m_linkState->GetDefaultLinkCid()) != m_linkState->m_cidToIndexMap.end(), "cannot find this cid in the map");
				if(m_lastSplittingIndexList.at(m_linkState->m_cidToIndexMap[m_linkState->GetDefaultLinkCid()]) == m_splittingBurst)
				{
					//do nothing...traffic is over Default link already
				}
				else
				{
					for (uint8_t ind = 0; ind < measurement->m_links; ind++)
					{
						if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
						{
							m_lastSplittingIndexList.at(ind) = m_splittingBurst;
							update = true;
						}
						else
						{
							m_lastSplittingIndexList.at(ind) = 0;
						}
					}
					NS_ASSERT_MSG(update, "Default CID should be in the cid list!!!");
				}
			}

		}
		//std::cout << " >>> max loss:" << maxLoss << " index:" << +maxLossInd << " min loss:" <<minLoss << " index:" << +minLossInd << "\n";
	}

	if(update)
	{
		/*std::cout << "RX APP ======= ";
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			std::cout << "[link:" << +ind << " delay:"<<measurement->m_delayList.at (ind)
			<< " loss:" << measurement->m_lossRateList.at (ind)<<"] ";
		}
		std::cout << "\n";
		std::cout << " >>>>>> max delay:" << maxDelay << " index:" << +maxIndex << " min delay:" <<minDelay << " index:" << +minIndex << "\n";
		std::cout << " >>>>>> ";
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			std::cout << "[link:" << +ind << " ratio:"<<+m_lastSplittingIndexList.at (ind)<< "] ";
		}
		std::cout << "\n";*/
	}

	
	//check if the sum equals splitting burst size;
	uint8_t sumRatio = 0;
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		sumRatio += m_lastSplittingIndexList.at (ind);
	}

	NS_ASSERT_MSG(sumRatio == m_splittingBurst, "the summation must equals the splitting burst size");

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
	decision->m_splitIndexList = m_lastSplittingIndexList;
	decision->m_update = update;
	return decision;

}

Ptr<SplittingDecision>
GmaRxControl::LinkDownTsu (uint8_t cid)
{
	bool update = false;

	if(m_algorithm == GmaRxControl::DefaultLink)
	{
		//do nothing
	}
	else
	{

		if(m_lastSplittingIndexList.size() == 0)
		{
			if(cid != m_linkState->GetDefaultLinkCid())
			{
				//if no TSU sent yet, send one just in case...
				update = true;

				std::vector<uint8_t> splittingIndexList;
				std::vector<uint8_t> cidList = m_linkState->GetCidList();

				for (uint8_t ind = 0; ind < cidList.size(); ind++)
				{
					if(cidList.at(ind) == m_linkState->GetDefaultLinkCid())
					{
						splittingIndexList.push_back(m_splittingBurst);
					}
					else
					{
						splittingIndexList.push_back(0);
					}
				}

				/*std::cout << "RX APP link (CID):" << +cid << " failed\n";

				std::cout << "RX APP";
				for (uint8_t ind = 0; ind < splittingIndexList.size(); ind++)
				{
					std::cout << "[link:" << +ind << " ratio:"<<+splittingIndexList.at (ind)<< "] ";
				}
				std::cout << "\n";*/

				Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
				decision->m_splitIndexList = splittingIndexList;
				decision->m_update = update;
				return decision;

			}
			//else link down cid is the same as the new default link, no need to send tsu

		}
		else
		{
			NS_ASSERT_MSG(m_linkState->m_cidToIndexMap.find(cid) != m_linkState->m_cidToIndexMap.end(), "cannot find this cid in the map");

			uint8_t failedLink = m_linkState->m_cidToIndexMap[cid];//get the index of the failed link

			if(m_lastSplittingIndexList.at(failedLink) == 0)
			{
				//this link already have zero traffic splitting, do nothing
				update = false;
			}
			else
			{
				bool allLinkDown = true;
				std::vector<uint8_t> cidList = m_linkState->GetCidList();

				for (uint16_t ind = 0; ind < m_lastSplittingIndexList.size(); ind++ )
				{
					if(m_linkState->IsLinkUp(cidList.at(ind)))
					{
						allLinkDown = false;
						break;
					}
				}

				if(allLinkDown)
				{
					//all link down, do nothing..
					update = false;
				}
				else
				{

					update = true;

					uint8_t splittingIndex = m_lastSplittingIndexList.at(failedLink);
					m_lastSplittingIndexList.at(failedLink) = 0;//set its split index = 0
					uint8_t iterInd = 0;
					while(splittingIndex != 0)//distribute its split index to all links that are ok (not failed)
					{
						if(m_linkState->IsLinkUp(cidList.at(iterInd)))
						{
							m_lastSplittingIndexList.at(iterInd)++;
							splittingIndex--;
						}
						iterInd = (iterInd + 1) % m_lastSplittingIndexList.size();
					}
					/*std::cout << "RX APP link:" << +failedLink << " failed\n";

					std::cout << "RX APP";
					for (uint8_t ind = 0; ind < m_lastSplittingIndexList.size(); ind++)
					{
						std::cout << "[link:" << +ind << " ratio:"<<+m_lastSplittingIndexList.at (ind)<< "] ";
					}
					std::cout << "\n";*/
					//check if the sum equals splitting burst size;
					uint8_t sumRatio = 0;
					for (uint8_t ind = 0; ind < m_lastSplittingIndexList.size(); ind++)
					{
						sumRatio += m_lastSplittingIndexList.at (ind);
					}

					NS_ASSERT_MSG(sumRatio == m_splittingBurst, "the summation must equals the splitting burst size");

				}

			}
		}
	}

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
	decision->m_splitIndexList = m_lastSplittingIndexList;
	decision->m_update = update;
	return decision;

}

Ptr<SplittingDecision>
GmaRxControl::QosSteerAlgorithm (Ptr<RxMeasurement> measurement)
{
	NS_ASSERT_MSG(m_splittingBurst == 1, "this algorithm is for steering mode only");
	//NS_ASSERT_MSG (measurement->m_links==2, "qos steer should only measure 2 links, 1 primary and 1 backup");
	//NS_ASSERT_MSG (measurement->m_cidList.size()==2, "qos steer should only measure 2 links, 1 primary and 1 backup");
	//start from all traffic goes to the first link
	//the measurtement may record more than 2 links now, since the backup link can be changed by the ml action.

	if(m_lastSplittingIndexList.size() == 0)
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
			{
				m_lastSplittingIndexList.push_back(m_splittingBurst);
			}
			else
			{
				m_lastSplittingIndexList.push_back(0);
			}
		}
	}

	//implement qos based traffic steering here!!!
	bool update = false;

	//we need to be careful that the traffic may be measured from the primary link, the backup link and other (the previous backup link).
	uint16_t trafficOverBackupLinks = 0;
	uint16_t trafficOverPrimaryLink = 0;
	uint16_t trafficOverOtherLinks = 0; //traffic over the previous backuplink.

	uint8_t primaryLinkIndex = m_linkState->m_cidToIndexMap[m_linkState->GetDefaultLinkCid()];
	uint8_t backupCid = m_linkState->GetBackupLinkCid();
	uint8_t backupLinkIndex = m_linkState->m_cidToIndexMap[backupCid];
	std::vector<uint8_t> otherCidList;
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		if(ind == primaryLinkIndex)
		{
			trafficOverPrimaryLink += m_lastSplittingIndexList.at(ind);
		}
		else if (ind == backupLinkIndex)
		{
			trafficOverBackupLinks += m_lastSplittingIndexList.at(ind);
		}
		else
		{
			trafficOverOtherLinks += m_lastSplittingIndexList.at(ind);
			otherCidList.push_back(measurement->m_cidList.at(ind));
		}
	}
	//std::cout<< " primary cid:" << +m_linkState->GetDefaultLinkCid() << " primary ind:" << +primaryLinkIndex
	//	<< " backup cid:" << +backupCid << std::endl;
	//trafficOverBackupLinks == 0 -> traffic over the primary link
	//trafficOverPrimaryLink == 0 -> traffic over the backup link
	//trafficOverBackupLinks !=0 && trafficOverPrimaryLink != 0 -> traffic over primary and backup links. (testing mode)

	if(trafficOverBackupLinks + trafficOverOtherLinks == 0) //no traffic over backup or other link, received measurement for QoS testing...
	{
		std::vector<uint8_t> qosOkList;
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(ind != primaryLinkIndex && m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//not the primary link and this link is okey
			{
				if(measurement->m_highDelayRatioList.at(ind) >= 0 && measurement->m_highDelayRatioList.at(ind) < m_qosDelayViolationTarget 
					&& measurement->m_lossRateList.at(ind) >= 0 && measurement->m_lossRateList.at(ind) < m_qosLossTarget)
				{
					//pass the check of qos requirement
					qosOkList.push_back(ind);//add this link into qos OK list
				}
			}
		}

		//std::cout  << "links:" << +measurement->m_links << " Traffic over primary link ("<<+m_linkState->GetDefaultLinkCid()<<"):" << trafficOverPrimaryLink
		//<< " Traffic over Backup link("<<+backupCid<<"):" << trafficOverBackupLinks 
		//<< " m_lastTestingTime:" << m_linkState->m_cidToLastTestFailTime[backupCid].GetSeconds()
		//<< " m_lastTestingTime+m_linkState->MIN_QOS_TESTING_INTERVAL:" << (m_linkState->m_cidToLastTestFailTime[backupCid]+m_linkState->MIN_QOS_TESTING_INTERVAL).GetSeconds()
		//<< " now:" << Now().GetSeconds();

		//std::cout << " qosOkList:";
		//for (uint32_t i = 0; i < qosOkList.size(); i++)
		//{
		//	std::cout << +qosOkList.at(i) << "";
		//}
		//std::cout << std::endl;

		if(qosOkList.empty())//qos fail or no traffic
		{
			//qos check failed, no switch
			if(measurement->m_highDelayRatioList.at(backupLinkIndex) >= 0 && measurement->m_lossRateList.at(backupLinkIndex) >= 0)//measurement available.
			{
				m_linkState->m_cidToLastTestFailTime[backupCid] = Now();//record the failed time
				m_linkState->m_cidToValidTestExpireTime.erase(backupCid);
				
			}
			else//idle case
			{
				m_linkState->m_cidToLastTestFailTime.erase(backupCid);//remove this cid, client can re start testing right away (after receiving a packet)
			}

			for (uint32_t i = 0; i < otherCidList.size(); i++)
			{
				if(measurement->m_highDelayRatioList.at(m_linkState->m_cidToIndexMap[otherCidList.at(i)]) >= 0 && measurement->m_lossRateList.at(m_linkState->m_cidToIndexMap[otherCidList.at(i)]) >= 0)//measurement available, and qos fail...
				{
					m_linkState->m_cidToLastTestFailTime[otherCidList.at(i)] = Now();//record the failed time
					m_linkState->m_cidToValidTestExpireTime.erase(otherCidList.at(i));
					
				}
				else//idle case
				{
					m_linkState->m_cidToLastTestFailTime.erase(otherCidList.at(i));//remove this cid, client can re start testing right away (after receiving a packet)
				}
			}
		}
		else if(qosOkList.size() == 1)//qos pass for 1 link, switch to this link
		{
			
			//traffic over both primary and backup links, in testing mode -> switch to backup link that meet qos requirement
			for (uint8_t ind = 0; ind < measurement->m_links; ind++)
			{
				if(ind == qosOkList.at(0))
				{
					m_lastSplittingIndexList.at(ind) = m_splittingBurst;
				}
				else
				{
					m_lastSplittingIndexList.at(ind) = 0;
				}
			}
			update = true;
			m_linkState->m_cidToLastTestFailTime.erase(measurement->m_cidList.at(qosOkList.at(0)));//remove this cid, client can re start testing right away (after receiving a packet) if it fails again
			m_linkState->m_cidToValidTestExpireTime[measurement->m_cidList.at(qosOkList.at(0))] = Now() + m_linkState->MAX_QOS_VALID_INTERVAL; // we assume the qos of this cid link is valid for m_linkState->MAX_QOS_VALID_INTERVAL.

		}
		else //multiple links meet the requirement... did not implement this case yet
		{
			NS_FATAL_ERROR("did not implement testing over multiple backup links yet...");
		}
	}
	else //traffic over backup link, check qos requirement.
	{
		std::vector<uint8_t> qosOkList;
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(ind != primaryLinkIndex && m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//not the primary link and this link is okey
			{
				if(measurement->m_highDelayRatioList.at(ind) >= 0 && measurement->m_highDelayRatioList.at(ind) < m_qosDelayViolationTarget 
					&& measurement->m_lossRateList.at(ind) >= 0 && measurement->m_lossRateList.at(ind) < m_qosLossTarget)
				{
					//pass the check of qos requirement
					qosOkList.push_back(ind);
				}
			}
		}
		/*std::cout << Now().GetSeconds() << " QOS Monitoring Measurement End. qosOkList:";
		for (uint32_t i = 0; i < qosOkList.size(); i++)
		{
			std::cout << +qosOkList.at(i) << "";
		}
		std::cout << std::endl;*/

		if(qosOkList.empty())//may be qos fail or no traffic
		{
			//qos check failed, fall back to LTE...
			for (uint8_t ind = 0; ind < measurement->m_links; ind++)
			{
				if(ind == primaryLinkIndex)
				{
					m_lastSplittingIndexList.at(ind) = m_splittingBurst;
				}
				else
				{
					m_lastSplittingIndexList.at(ind) = 0;
				}
			}
			update = true;

			if(measurement->m_highDelayRatioList.at(backupLinkIndex) >= 0 && measurement->m_lossRateList.at(backupLinkIndex) >= 0)//measurement available, and qos fail...
			{
				m_linkState->m_cidToLastTestFailTime[backupCid] = Now();//record the failed time
				m_linkState->m_cidToValidTestExpireTime.erase(backupCid);
			}
			else//idle case
			{
				m_linkState->m_cidToLastTestFailTime.erase(backupCid);//remove this cid, client can re start testing right away (after receiving a packet)
				//m_linkState->m_cidToValidTestExpireTime[backupCid] = Now() + m_linkState->MAX_QOS_VALID_INTERVAL; // we assume the qos of this cid link is valid for m_linkState->MAX_QOS_VALID_INTERVAL.
			}

			for (uint32_t i = 0; i < otherCidList.size(); i++)
			{
				if(measurement->m_highDelayRatioList.at(m_linkState->m_cidToIndexMap[otherCidList.at(i)]) >= 0 && measurement->m_lossRateList.at(m_linkState->m_cidToIndexMap[otherCidList.at(i)]) >= 0)//measurement available, and qos fail...
				{
					m_linkState->m_cidToLastTestFailTime[otherCidList.at(i)] = Now();//record the failed time
					m_linkState->m_cidToValidTestExpireTime.erase(otherCidList.at(i));
				}
				else//idle case
				{
					m_linkState->m_cidToLastTestFailTime.erase(otherCidList.at(i));//remove this cid, client can re start testing right away (after receiving a packet)
				}
			}
		}
		else if(qosOkList.size() == 1)//qos pass for 1 link, continue sending on this link...
		{
			//check if the passed link is the current link...
			for (uint8_t ind = 0; ind < measurement->m_links; ind++)
			{
				if(ind == qosOkList.at(0))
				{
					if (m_lastSplittingIndexList.at(ind) == m_splittingBurst)
					{
						//traffic is over this link, no action
						m_linkState->m_cidToLastTestFailTime.erase(measurement->m_cidList.at(qosOkList.at(0)));//remove this cid, client can re start testing right away (after receiving a packet) if it fails again
					}
					else
					{
						//NS_FATAL_ERROR("did not implement QOS testing over multiple links yet..");
						//backup link changed, qos test also passed!
						for (uint8_t itemp = 0; itemp < measurement->m_links; itemp++)
						{
							if(itemp == ind)
							{
								m_lastSplittingIndexList.at(itemp) = m_splittingBurst;
							}
							else
							{
								m_lastSplittingIndexList.at(itemp) = 0;
							}
						}
						m_linkState->m_cidToLastTestFailTime.erase(measurement->m_cidList.at(qosOkList.at(0)));//remove this cid, client can re start testing right away (after receiving a packet) if it fails again
						update = true;
					}
				}
			}
			m_linkState->m_cidToValidTestExpireTime[measurement->m_cidList.at(qosOkList.at(0))] = Now() + m_linkState->MAX_QOS_VALID_INTERVAL; // we assume the qos of this cid link is valid for m_linkState->MAX_QOS_VALID_INTERVAL.

		}
		else //multiple links meet the requirement... did not implement this case yet
		{
			NS_FATAL_ERROR("did not implement testing over multiple backup links yet...");
		}

	}

	/*std::cout << "RX APP ======= ";
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		std::cout << "[link:" << +ind << " cid:" << +measurement->m_cidList.at(ind) <<" delay:"<<measurement->m_delayList.at (ind)
		<< " highdelay:" << measurement->m_highDelayRatioList.at (ind)
		<< " loss:" << measurement->m_lossRateList.at (ind)<<"] ";
	}
	std::cout << "\n";

	std::cout << "RX APP >>>>>>> ratio";
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		std::cout << " ["<<+m_lastSplittingIndexList.at (ind)<< "]";
	}
	if(update)
	{
		std::cout << " -> TSU ";
	}
	std::cout << "\n";*/

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
	decision->m_splitIndexList = m_lastSplittingIndexList;
	decision->m_update = update;
	return decision;
}

bool
GmaRxControl::QosSteerEnabled ()
{
	if(m_algorithm == GmaRxAlgorithm::QosSteer)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool
GmaRxControl::QosFlowPrioritizationEnabled ()
{
	return m_enableQosFlowPrioritization;
}

bool
GmaRxControl::QoSTestingRequestCheck (uint8_t cid)
{
	NS_ASSERT_MSG(m_algorithm == GmaRxAlgorithm::QosSteer, "call this function only in QoS enabled mode");
	//std::cout << Now().GetSeconds() <<" " << this << " received ack, test qos over link: " << +cid << " default cid:" << +m_linkState->GetDefaultLinkCid() <<  std::endl;
	//implement qos based traffic steering here!!!

	uint16_t trafficOverBackupLinks = 0;
	uint16_t trafficOverPrimaryLink = 0;
	uint16_t trafficOverOtherLinks = 0;

	if(m_lastSplittingIndexList.size() == 0)
	{
		//no traffic splitting yet, traffic over primary link
		trafficOverPrimaryLink = 1;
	}
	else
	{
		uint8_t primaryLinkIndex = m_linkState->m_cidToIndexMap[m_linkState->GetDefaultLinkCid()];
		uint8_t backupCid = m_linkState->GetBackupLinkCid();
		uint8_t backupLinkIndex = m_linkState->m_cidToIndexMap[backupCid];

		for (uint8_t ind = 0; ind < m_lastSplittingIndexList.size(); ind++)
		{
			if(ind == primaryLinkIndex)
			{
				trafficOverPrimaryLink += m_lastSplittingIndexList.at(ind);
			}
			else if (ind == backupLinkIndex)
			{
				trafficOverBackupLinks += m_lastSplittingIndexList.at(ind);
			}
			else
			{
				trafficOverOtherLinks += m_lastSplittingIndexList.at(ind);
			}
		}
	}
	/*std::cout << "Traffic over primary link:" << trafficOverPrimaryLink
	<< " Traffic over Backup link:" << trafficOverBackupLinks 
	<< " m_lastTestingTime:" << m_lastTestingTime.GetSeconds()
	<< " m_lastTestingTime+m_linkState->MIN_QOS_TESTING_INTERVAL:" << (m_lastTestingTime+m_linkState->MIN_QOS_TESTING_INTERVAL).GetSeconds()
	<< " now:" << Now().GetSeconds() 
	<< std::endl;*/

	if(trafficOverBackupLinks + trafficOverOtherLinks == 0) // traffic over the primary link
	{
		if(m_linkState->m_cidToLastTestFailTime.empty())
		{
			//we can start a new qos test
			m_linkState->m_cidToLastTestFailTime[cid] = Now();//mark the testing time
			return true;//start qos testing for this cid
		}
		else if (m_linkState->m_cidToLastTestFailTime.size() == 1)//one link tried qos testing
		{
			std::map<uint8_t, Time>::iterator iter = m_linkState->m_cidToLastTestFailTime.find(cid);
			if(iter == m_linkState->m_cidToLastTestFailTime.end())
			{
				//NS_FATAL_ERROR("cannot find this cid");
				// qos testing over a different backup link. The backup link cid have been changed!
				m_linkState->m_cidToLastTestFailTime[cid] = Now();//mark the testing time
				return true;
			}
			if(Now() >= iter->second + m_linkState->MIN_QOS_TESTING_INTERVAL)//current time passed the min testing interval
			{
				iter->second = Now();
				return true;
			}
		}
		else
		{
			//auto iter = m_linkState->m_cidToLastTestFailTime.begin();
			//while (iter != m_linkState->m_cidToLastTestFailTime.end())
			//{
			//	std::cout << " cid:" << +iter->first << " fail time:" << iter->second << std::endl;
			//	iter++;
			//}
			NS_FATAL_ERROR("we only implemented one link case");
		}
		
	}
	return false;

}

bool
GmaRxControl::QoSValidCheck (uint8_t cid)
{
	NS_ASSERT_MSG(m_algorithm == GmaRxAlgorithm::QosSteer, "call this function only in QoS enabled mode");
	//std::cout << Now().GetSeconds() <<" " << this << " received ack, test qos over link: " << +cid << " default cid:" << +m_linkState->GetDefaultLinkCid() <<  std::endl;
	//implement qos based traffic steering here!!!
	auto iter = m_linkState->m_cidToValidTestExpireTime.find(cid);
	if(iter == m_linkState->m_cidToValidTestExpireTime.end())
	{
		//not valid
		return false;
	}
	else
	{
		if(Now() > iter->second)//current time is greater than the valid time 
		{
			//not valid
			return false;
		}
	}
	return true;

}

Ptr<SplittingDecision>
GmaRxControl::GenerateTrafficSplittingDecision (uint8_t cid, bool reverse)
{
	bool update = true;
	//NS_ASSERT_MSG(m_linkState->m_cidToIndexMap.find(cid) != m_linkState->m_cidToIndexMap.end(), "cannot be end of this map cid:" << +cid);

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();

	std::vector<uint8_t> cidList = m_linkState->GetCidList();

	for (uint8_t i = 0; i < cidList.size(); i++)
	{
		if(cidList.at(i) == cid)
		{
			decision->m_splitIndexList.push_back(m_splittingBurst);
		}
		else
		{
			decision->m_splitIndexList.push_back(0);
		}
	}

	if(!reverse)
	{
		m_lastSplittingIndexList = decision->m_splitIndexList;
	}
	//for reverse TSU, do not update m_lastSplittingIndexList.

	std::cout << "RX APP Generate TSU for cid:" << +cid << " ---- ";
	for (uint8_t ind = 0; ind < decision->m_splitIndexList.size(); ind++)
	{
		std::cout << +decision->m_splitIndexList.at(ind) << "";
	}
	std::cout << std::endl;
	decision->m_update = update;
	decision->m_reverse = reverse;
	return decision;

}

int
GmaRxControl::GetMinSplittingBurst()
{
	return 8;
}

int
GmaRxControl::GetMaxSplittingBurst()
{
	return 128;
}

int
GmaRxControl::GetMeasurementBurstRequirement()
{
	if (m_splittingBurstScalar*m_splittingBurst == 0)
	{
		NS_FATAL_ERROR("the splitting burst requiremment cannot be zero!!!");
	}
	return m_splittingBurstScalar*m_splittingBurst;
}


uint32_t 
GmaRxControl::GetQueueingDelayTargetMs()
{
	return 	m_queueingDelayTargetMs;
}

void
GmaRxControl::SetQosTarget(double delayTarget, double lossTarge)
{
  m_qosDelayViolationTarget = delayTarget;
  m_qosLossTarget = lossTarge;
}

Ptr<SplittingDecision>
GmaRxControl::DelayViolationAlgorithm (Ptr<RxMeasurement> measurement)
{	

	NS_ASSERT_MSG(measurement->m_links == measurement->m_delayViolationPktNumList.size(), "size does not match!");
	NS_ASSERT_MSG(measurement->m_links == measurement->m_totalPktNumList.size(), "size does not match!");

	/**
	 * 
	 * Implement the GMA2.0 Traffic Splitting Algorithm from "AF9456 An Enhanced Dynamic Multi-Access Traffic Splitting Method for Next-Gen Edge Network"
	 * 
	 * Congestion Measurement Requirement:
	 * 1. measurement time should be within range [10ms, 1000ms]
	 * 2. larger than 1 RTT
	 * 3. larger than 2 packet splitting burst 128x2=256. (link failure has to meet this condition!)
	 * 
	 * First define the following parameters:
	 * k(i): num of delay violation data packets for link i.
	 * n(i): num of data packets for link i.
	 * s(i): splitting ratio for link i.
	 * b(i): bandwidth estimation for link i, packets/second.
	 * 
	 * This algorithm includes Link failure detection and Enhanced Multi-Path Traffic Splitting Algorithm
	 * 
	 * 
	 * [Optional]Sender Side Drain Phase: added a min owd measurement in TSU to info sender to drain the outstanding packets in the queue.
	 * 1. Receiver side: The TSU will feedback a per link queueing delay estimate.
	 * 	If a link's queuing delay is smaller than a threashold, e.g., m_queueingDelayTargetMs = 10, feedback 0 as queueing delay.
	 * 	If no measurement for a link, send back 255.
	 * 2. Transmitter side: after receives the delayed TSU, it sends back TSA (with drain phase delay), switch to the new splitting ratio and enter the drain pahse.
	 * 	During the drain phase, if the link with queueing delay greater than 0, stop sending packets to this link for the duration of the queueing delay estimate.
	 * 	After all links ared drained, it resume to normal splitting to all links.
	 * 3. Receiver side: after receives the TSA and wait for the packet with start sequence arrives, the receiver wait for the drain phase delay before starting the measurement again.
	 *   I.e., the receiver will not take measurements when the transmitter is in the drain phase.
	 * 
	 * 
	*/
	NS_ASSERT_MSG (m_congestionScaler < 1, "The m_congestionScaler cannot be 1");
	bool update = false;

	//start from all traffic goes to the default link
	if(m_lastSplittingIndexList.size() == 0)
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
			{
				m_lastSplittingIndexList.push_back(m_splittingBurst);
			}
			else
			{
				m_lastSplittingIndexList.push_back(0);
			}
		}
	}

	if(m_lastRatio.size() == 0)//not initialized yet.
	{
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			m_lastRatio.push_back((double)m_lastSplittingIndexList.at(ind)/m_splittingBurst);
		}
	}

	uint64_t sumPkt = 0; //sum(n(i))

	/**Link Failure Detection**/
	//The following implements link failure detection
	uint8_t activeLink = 0; //the number of link with s(i) > 0
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		if (m_lastSplittingIndexList.at(ind) > 0)
		{
			//link is active for data.
			activeLink++;
		}
		if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
		{
			sumPkt += measurement->m_totalPktNumList.at(ind);
		}
	}

	if (activeLink == 0)
	{
		NS_FATAL_ERROR("active link must be > 0");
	}
	else if (activeLink == 1)
	{
		//Single-Link Operation (steer traffic to a single link):
		//If no packet is received in the last interval, i.e. i.e. sum(n(i)) = 0 and Q = 1 (where Q indicates active traffic for this flow), the link failure is detected, 
		//and redirect the flow to another available link. Furthermore, probe or control messages are transmitted over the link to update its status.
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if (m_lastSplittingIndexList.at(ind) > 0)
			{
				//find the only link is active for data.
				if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
				{
					if (m_lastSplittingIndexList.at(ind) > 0 && measurement->m_totalPktNumList.at(ind) == 0 && m_splittingBurst == GetMaxSplittingBurst())
					{
						//s(i) > 0 AND n(i) = 0
						//we also make sure it is split mode (m_splittingBurst > 1)

						//for single link, we also need to flow to be active.
						if (m_flowActive)
						{
							//If all conditions are met, mark it as link down.... We will send a TSU to probe the link status. and mark it as up if acked.
							std::cout << "RX APP No Data Link Down | ______________";
							for (uint8_t ind = 0; ind < measurement->m_links; ind++)
							{
								std::cout << "[link:" << +ind << " splitting :"<< +m_lastSplittingIndexList.at (ind)
								<< " total:" << +measurement->m_totalPktNumList.at (ind)<<"] ";
							}
							
							std::cout << " sum: " << sumPkt << " splittingBurst: " << +m_splittingBurst << "\n";
							//let find a better way to compute no data link down... Maybe after the reordering.
							m_linkState->NoDataDown(measurement->m_cidList.at(ind));
							update = true; //send tsu
							m_flowActive = false;
						}
					}
				}
				//else already down, do nothing.
				break;//we only have one link is active
			}

		}
	}
	else
	{
		//Multi-Link Operation (split traffic over multiple links):
		//if the total number of received packets sum(n(i)) in the last interval exceeds T2:
		//		If any link has s(i) > 0 and n(i) = 0, a data channel link failure is identified, and s(i) is set to 0, effectively discontinuing the use of the link for data transmission.
		//		Additionally, probe or control messages are sent immediately over the link to check its connection status.
		//else:
		//		there is not enough data packet for link failure detection.
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
			{
				if (m_lastSplittingIndexList.at(ind) > 0 && measurement->m_totalPktNumList.at(ind) == 0 && m_splittingBurst == GetMaxSplittingBurst() && (int)sumPkt >= GetMeasurementBurstRequirement())
				{
					//s(i) > 0 AND n(i) = 0
					//we also make sure it is split mode (m_splittingBurst > 1), and the total received pkt numbner is greater than the min pkt requirement for measurement
					//If all conditions are met, mark it as link down.... We will send a TSU to probe the link status. and mark it as up if acked.
					std::cout << "RX APP No Data Link Down | ______________";
					for (uint8_t ind = 0; ind < measurement->m_links; ind++)
					{
						std::cout << "[link:" << +ind << " splitting :"<< +m_lastSplittingIndexList.at (ind)
						<< " total:" << +measurement->m_totalPktNumList.at (ind)<<"] ";
					}
					
					std::cout << " sum: " << sumPkt << " splittingBurst: " << +m_splittingBurst << "\n";
					//let find a better way to compute no data link down... Maybe after the reordering.
					m_linkState->NoDataDown(measurement->m_cidList.at(ind));
					update = true; //send tsu
				}
			}
		}
	}
	//link failure detection done.

	/*
	std::cout << "Measurement Interval: " << measurement->m_measureIntervalDurationS << " s \n";
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		std::cout << "[link: " << +ind 
		<< ", s(i):"<<+m_lastSplittingIndexList.at (ind) 
		<< ", k(i):"<<measurement->m_delayViolationPktNumList.at (ind)
		<< ", n(i):" << measurement->m_totalPktNumList.at (ind)
		<< ", status:" << m_linkState->IsLinkUp(measurement->m_cidList.at(ind))
		<< "]\n";
	}*/

	/**GMA2.0 Enhanced Traffic Splitting Algorithm**/
	//compute the congestion measurement values after updating the link status.
	uint32_t noneCongestedLink = 0; //check the number of links without congestion k(i) == 0.
	sumPkt = 0; //sum(n(i))
	uint64_t sumCongested = 0; //sum((k(i))
	uint64_t sumPktNoCongestion = 0; //sum(n(i)) under the condition k(i) == 0

	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		double bw = 0;
		if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
		{
			if (m_lastSplittingIndexList.at(ind) > 0 && measurement->m_totalPktNumList.at(ind) > 0)
			{
				//at least one link s(i) > 0 and n(i) > 0
				m_flowActive = true;
			}

			if (measurement->m_delayViolationPktNumList.at(ind) == 0)
			{
				//find one link without congestion
				noneCongestedLink++;
			}
			sumCongested += measurement->m_delayViolationPktNumList.at(ind);
			sumPkt += measurement->m_totalPktNumList.at(ind);
			if (measurement->m_delayViolationPktNumList.at(ind) == 0)
			{
				sumPktNoCongestion += measurement->m_totalPktNumList.at(ind);
			}
			bw = (double)measurement->m_totalPktNumList.at(ind)/measurement->m_measureIntervalDurationS;
		}

		//compute bw estimate b(i) here
		//if link is down, bw = 0
		auto iterBw = m_cidToBwHistMap.find(measurement->m_cidList.at(ind));
		auto iterKi = m_cidToKiHistMap.find(measurement->m_cidList.at(ind));
		if (iterBw == m_cidToBwHistMap.end())
		{
			m_cidToBwHistMap[measurement->m_cidList.at(ind)]=std::vector<double>{bw};
			m_cidToKiHistMap[measurement->m_cidList.at(ind)]=std::vector<uint32_t>{measurement->m_delayViolationPktNumList.at(ind)};

		}
		else
		{
			iterBw->second.push_back(bw);
			iterKi->second.push_back(measurement->m_delayViolationPktNumList.at(ind));
		}
	}

	// only keep the last m_bwHistSize bw estimate, e.g., only use the the last 10 measurements to find the max bandwidth.
	auto iterBw = m_cidToBwHistMap.begin();
	auto iterKi = m_cidToKiHistMap.begin();
	while (iterBw != m_cidToBwHistMap.end())
	{

		while(iterBw->second.size() > m_bwHistSize)
		{
			//remove old measurement-> only keep m_bwHistSize, e.g., 10 bw measurements.
			iterBw->second.erase(iterBw->second.begin());
			iterKi->second.erase(iterKi->second.begin());
		}
		//std::cout << "cid: " << +iterBw->first << " bw: [";
		//for (uint32_t i = 0; i < iterBw->second.size(); i++)
		//{
		//	std::cout << iterBw->second.at(i) << " ";
		//}
		//std::cout << "]" << std::endl;
		iterBw++;
		iterKi++;
	}

	//auto iterMaxBw = m_cidToMaxBwMap.begin();
	//std::cout << "allLinkBwAvailable: " << allLinkBwAvailable << " | sumMaxBwKequalZero: " << sumMaxBwKequalZero;
	//while (iterMaxBw != m_cidToMaxBwMap.end())
	//{
	//	std::cout << "[cid: " << +iterMaxBw->first << " max bw: " << iterMaxBw->second << "]" ;
	//	iterMaxBw++;
	//}
	//std::cout << std::endl;

	//start GMA2.0 splitting algorithm
	std::vector<double> newRatio;

	if (sumPkt == 0)
	{
		//Case #1 (No Traffic): If no links receive any packets, i.e., n(i) = 0 for all links, we will stop splitting and steer the flow to the default link.
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			newRatio.push_back(0.0);
			//If n(i) = 0 for all links //no traffic
			//set s(x) = 1.0 for the default link, and s(j) = 0 for all other links.
			if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
			{
				if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
				{
					newRatio.at(ind) = 1.0;//should only be 1 link.
				}
				else
				{
					NS_FATAL_ERROR("default link should always be up!");
				}
			}
		}
	}
	else
	{
		//data measurement is active.
		if (sumCongested == 0) // no congestion for all links
		{

			//Case #2 (No Congestion): If no links experience congestion, i.e., k(i) = 0 for all links,

			//we will steer traffic from a none-default none-zero traffic link j to the default link.
			uint64_t nDefN = 0; //non-default link with none-zero traffic.
			uint8_t nDefIndex = 0;
			uint8_t defIndex = 0;
			uint8_t linkWithData = 0; //num of links n(i) > 0
			for (uint8_t ind = 0; ind < measurement->m_links; ind++)
			{
				if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
				{
					if (measurement->m_totalPktNumList.at(ind) > 0)
					{
						linkWithData++;
					}

					if(measurement->m_cidList.at(ind) != m_linkState->GetDefaultLinkCid())
					{
						//none default link
						if (measurement->m_totalPktNumList.at(ind) > 0)
						{
							//none default and none zero traffic.
							nDefN = measurement->m_totalPktNumList.at(ind);
							nDefIndex = ind;
						} 
					}
					else
					{
						//default link
						defIndex =  ind;
					}
				}
			}

			if (linkWithData == 1)  //move all traffic to the default link if only one link is active without congestion
			{
				//If only one link has data, i.e., n(x) > 0, set s(x) = 1.0 and s(j) = 0 for other links.
				//x: to indicate the default link.
				//j: indicates non default links.
				for (uint8_t ind = 0; ind < measurement->m_links; ind++)
				{
					newRatio.push_back(0.0);
					if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
					{
						if(measurement->m_cidList.at(ind) == m_linkState->GetDefaultLinkCid())
						{
							newRatio.at(ind) = 1.0;//should only be 1 link.
						}
					}
				}

			}
			else
			{
				//More than one link has data
				//	x: indicates the none-default none zero traffic link that need to reallocate.
				//	j: indicate the default link.
				//  k: other none-default link does not need to reallocate.
				//For link x:
				//	Set s(x) = (n(x) - R)/sum(n(i)), where R = min(n(x), p*sum(n(i))) and p = 0.1 //if n(x) is larger than R, it takes a few iterations to move all traffic from link x to link j.
				//For link j:
				//	s(j) = (n(j) + R)/sum(n(i))
				//For link k:
				// s(k) = (n(k))/sum(n(i))

				m_relocateScaler = ((double)minSplitAdjustmentStep)/((double)m_splittingBurst);				
				//move traffic to the default link
				uint64_t relocatePktNum = std::min(nDefN, (uint64_t)(m_relocateScaler*(double)sumPkt)); //compute R.  

				if (relocatePktNum  > 0)
				{
					//std::cout << " relocate data: " << relocatePktNum << " from nDefIndex: " << +nDefIndex << " to defIndex: " << +defIndex <<std::endl;
					for (uint8_t ind = 0; ind < measurement->m_links; ind++)
					{
						newRatio.push_back(0.0);
						if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
						{
							if (measurement->m_totalPktNumList.at(ind) < measurement->m_delayViolationPktNumList.at(ind) || sumPkt == 0)
							{
								NS_FATAL_ERROR("traffic splitting algorithm condition not meet!");
							}


							if (ind == nDefIndex)//find none-default none-zero link that need reallocate
							{
								//s(x) = (n(x) - R)/sum(n(i))
								newRatio.at(ind) = (double)(nDefN-relocatePktNum)/sumPkt;
							}
							else if (ind == defIndex) //find the default link
							{
								// /s(j) = (n(j) + R)/sum(n(i))	
								newRatio.at(ind)= (double)(measurement->m_totalPktNumList.at(ind) + relocatePktNum)/sumPkt;
							}
							else //other none-default links keeps s(k) = n(k)/sum(n(i))
							{
								//s(k) = (n(k))/sum(n(i))
								newRatio.at(ind)= (double)(measurement->m_totalPktNumList.at(ind))/sumPkt;
							}
						}
					}
				}
			}
		}
		else //we detect links with congestion
		{
			//case #3 and case #4 is implemented here.

			if (noneCongestedLink > 0)//we also detect links without congestion.
			{
				//Case #3 (Medium Congestion): When only a subset of links experience congestion, k(i) > 0 for some links, 
				//we will reallocate a portion of data traffic from a congested link to non-congested links.

				//check if all operational link has bandwidth estimate.
				bool allLinkBwAvailable = true; //for simplicity, here we check all links, not only the non-congested links.
				std::map< uint8_t, double> m_cidToMaxBwMap; // the key is the cid of the link and the value is the bandwith estimate.
				double sumMaxBwKequalZero = 0.0;
				for (uint8_t ind = 0; ind < measurement->m_links; ind++)
				{
					if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
					{
						auto iterBw = m_cidToBwHistMap.find(measurement->m_cidList.at(ind));
						auto iterKi = m_cidToKiHistMap.find(measurement->m_cidList.at(ind));
						if (iterBw != m_cidToBwHistMap.end())
						{
							auto histBwVector = iterBw->second;
							double maxBw = *max_element (histBwVector.begin(), histBwVector.end());

							auto histKiVector = iterKi->second;
							double maxKi = *max_element (histKiVector.begin(), histKiVector.end());

							NS_ASSERT_MSG(histBwVector.size() == histKiVector.size(), "The size of k(i) and b(i) hist must be the same!");

							m_cidToMaxBwMap[measurement->m_cidList.at(ind)] = maxBw;
							if (measurement->m_delayViolationPktNumList.at(ind) == 0)
							{
								sumMaxBwKequalZero += maxBw;
							}

							if (maxKi == 0 || maxBw < 1.0)
							{
								//never experienced congestion or bw smaller than 1 packet / second, consider no bandwidth measurement->
								allLinkBwAvailable = false;
							}
								
						}
						else
						{
							//find at least one link without bw history.
							allLinkBwAvailable = false;
						}
						//else no bw history for this link.
					}
				}

				for (uint8_t ind = 0; ind < measurement->m_links; ind++)
				{
					newRatio.push_back(0.0);
					if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
					{
						if (measurement->m_totalPktNumList.at(ind) < measurement->m_delayViolationPktNumList.at(ind) || sumPkt == 0)
						{
							NS_FATAL_ERROR("traffic splitting algorithm condition not meet!");
						}

						double newScaler = m_congestionScaler; //initial to the default value, e.g., 0.3
						if (m_adaptiveCongestionScaler && allLinkBwAvailable)
						{
							//Enhancement #1:
							//In the case of medium congestion (case #3), the objective is to transfer congested packets from links with k(x) > 0 to links with k(j) = 0 without causing congestion to link j. 
							//This means that the total amount of reallocated traffic, given by sum(k(x))*a, must not exceed the available resources in non-congested links, given by sum(b(j))*D - sum(n(j))), 
							//where D is the interval duration for this congestion measurement-> Instead of using a constant a, e.g., 0.3, 
							//we can adaptively learn its value using the following two steps assuming b(j) > 0 for all links:

							//step 1: a = (sum(b(j))*D - sum(n(j)))/sum(k(x)). 
							newScaler = (sumMaxBwKequalZero*measurement->m_measureIntervalDurationS-sumPktNoCongestion)/sumCongested;
							if (newScaler < -1e-9)//ignore the the rounding error.
							{
								NS_FATAL_ERROR("newScaler cannot be smaller than zero!!!");
							}
							//step 2: a = max(a_min, min(a, a_max)), where a_min = 0.1 and a_max = 0.5. //this step limits the range of ‘a’ to be [0.1, 0.5].
							newScaler = std::max(CONGESTION_SCALER_MIN, newScaler);
							newScaler = std::min(CONGESTION_SCALER_MAX, newScaler);

						}

						if (measurement->m_delayViolationPktNumList.at(ind) > 0) // k(x) > 0
						{
							//For link x, where x indicates the link with k(x) > 0:
							//set s(x) = (n(x) - k(x) * a))/sum(n(i)), where a is configurable control parameter, e.g. 0.3.
							newRatio.at(ind) = (double)(measurement->m_totalPktNumList.at(ind) - measurement->m_delayViolationPktNumList.at(ind) * newScaler) / sumPkt;
						}
						else // k(j) == 0
						{
							//For link j, where j indicates the link with k(j) = 0:
							if (m_enableBwEstimate && allLinkBwAvailable)//check if bandwith estimate is enabled, and whether all links has bw estimate.
							{
								//Option #1: set s(j) = (sum(n(j)) + sum(k(x) * a))*b(j)/sum(b(j))/sum(n(i)) if b(j) > 0 for all links,
								NS_ASSERT_MSG(sumMaxBwKequalZero > 0, "the sumMaxBwKequalZero must be greater than zero");
								newRatio.at(ind) = (double)(sumPktNoCongestion + sumCongested*newScaler)*m_cidToMaxBwMap[measurement->m_cidList.at(ind)]/(sumMaxBwKequalZero*sumPkt);
							}
							else
							{
								//Option #2: set s(j) = (n(j) + sum(k(x)) * a/L)/sum(n(i)) if b(j) = 0 for any link, where L is the number of the links with k(j) = 0.
								//newRatio.at(ind) = (double)(sumPktNoCongestion + sumCongested*newScaler)/(noneCongestedLink*sumPkt);
								newRatio.at(ind) = (double)(measurement->m_totalPktNumList.at(ind) + sumCongested*newScaler/noneCongestedLink)/sumPkt;
							}

						}
					}
					else
					{
						//link down ratio = 0.0;
					}
				}
			}
			else
			{
				//Case #4 (Heavy Congestion): If all links are congested, i.e. k(i) > 0 for all links, the algorithm redistributes traffic among all links in proportion to n(i).
				for (uint8_t ind = 0; ind < measurement->m_links; ind++)
				{
					newRatio.push_back(0.0);
					if(m_linkState->IsLinkUp(measurement->m_cidList.at(ind)))//link is okey
					{
						double newScaler = 0.0;
						if (measurement->m_totalPktNumList.at(ind) < measurement->m_delayViolationPktNumList.at(ind) || sumPkt-sumCongested*newScaler == 0)
						{
							NS_FATAL_ERROR("traffic splitting algorithm condition not meet!");
						}
						//we assume the newScaler < 1.
						//set s(i) = n(i)/sum(n(i))
						newRatio.at(ind) = (double)(measurement->m_totalPktNumList.at(ind) - measurement->m_delayViolationPktNumList.at(ind) * newScaler) / (sumPkt-sumCongested*newScaler);
					}
					else
					{
						//link down ratio = 0.0;
					}
				}

			}
		}

	}

	if (newRatio.size() > 0) //if the algorithm does not need update, the new ratio size will be zero.
	{
		//std::cout << " splitting ratio: ";
		//for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		//{
		//	std::cout << newRatio.at(ind) << " ";
		//}
		//std::cout << "\n";
		//we will use the last link to take care of the rounding error.

		if (m_adaptiveSplittingBurst)
		{
			//compute a new splitting burst
			
			/*m_splittingBurst = GetMaxSplittingBurst();//start from the max one
			while (m_splittingBurst > GetMinSplittingBurst() && GetMeasurementBurstRequirement() > (int)(measurement->m_splittingBurstRequirementEst)) //while the splitting burst is greater than min_bust and requirement is greater than the estimate.
			{
				//double every time
				m_splittingBurst = m_splittingBurst/2;
			}*/

			int estBurstRequirement = (int)floor(log2((double)(sumPkt*measurement->m_measureIntervalThreshS)
				/(double)(m_splittingBurstScalar*measurement->m_measureIntervalDurationS))); 

			m_splittingBurst = (int)pow(2, std::min(std::max(3, (int)(estBurstRequirement)), 7));
			//std::cout << " requirement est: " << +measurement->m_splittingBurstRequirementEst << " new splitting_burst: " << +m_splittingBurst << std::endl;
		}

		//we use to m_roundingScaler (must be negative), e.g., -0.3. to decrease the ratio more aggressively. Define m(i) as splitting burst for link i and sum(m(i)) equals m_splittingBurst.
		//if s(j) is decreased, m(j) = round(s(j)*m_splittingBurst + m_roundingScaler) //reduce splitting burst more while rounding for link j if s(j) decreasing.
		//else s(k) is equal or increased, m(k) = round((m_splittingBurst - sum(m(j)))*s(k)/sum(s(k))) //distribute the remaining burst to other links.
		std::vector < uint8_t > burstPerLink;
		double newRatioSum = 0;
		int burstSum = 0;
		double decreaseBurstSum = 0; //sum(m(j))
		double equalOrIncreaseRatioSum = 0; //sum(s(k))

		//compute burst for decreased ratio
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			burstPerLink.push_back(0); //place holder, will overwrite it later.
			if (newRatio.at(ind) < m_lastRatio.at(ind))
			{
				//s(i) reduced.
				//m(j) = round(s(j)*m_splittingBurst + m_roundingScaler) 
				double mj = std::round(newRatio.at(ind)*m_splittingBurst+m_roundingScaler);
				burstPerLink.at(ind) = mj;
				decreaseBurstSum += mj; //sum(m(j))
			}
			else
			{
				//s(i) increased.
				//burstPerLin will be computed in the next loop.
				equalOrIncreaseRatioSum += newRatio.at(ind);
			}
		}

		NS_ASSERT_MSG(equalOrIncreaseRatioSum, "equalOrIncreaseRatioSum cannot be zero!");

		//compute burst for equal or increased ratio.
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if (newRatio.at(ind) < m_lastRatio.at(ind))
			{
				//s(i) reduced.
				//already computed in the previous loop.
			}
			else
			{
				//s(i) increased.
				//m(k) = round((m_splittingBurst - sum(m(j)))*s(k)/sum(s(k)))
				double mk = std::round( (m_splittingBurst-decreaseBurstSum)*newRatio.at(ind)/equalOrIncreaseRatioSum);
				burstPerLink.at(ind) = mk;
			}
			burstSum += burstPerLink.at(ind);
			newRatioSum += newRatio.at(ind);
		}
		
		if (newRatioSum > 1.001 || newRatioSum < 0.999)
		{
			NS_FATAL_ERROR("The new ratio is not equal 1!!");
		}

		NS_ASSERT_MSG(burstSum != 0, "the sum of burst per link cannot be empty");

		//std::cout << " burst 0: " << +burstPerLink.at(0) << " burst 1: " << +burstPerLink.at(1) << " burst 2: " << +burstPerLink.at(2) << std::endl;

		//take care of rounding error.

		//if total burst size is smaller than the required splitting burst size, increase 1 burst for the highest traffic link?
		//the reason for this it the lowest link may be zero, and it may be intentional, e.g., one link is never used.
		while(burstSum < m_splittingBurst)
		{
			//std::cout << "sum smaller than splitting burst size!!!" << std::endl;
			auto index = std::distance(burstPerLink.begin(),std::max_element(burstPerLink.begin(), burstPerLink.end()));
			burstPerLink.at(index) += 1;
			burstSum += 1;
		}

		//if total burst size is smaller than the required splitting burst size, decrease 1 burst for the highest traffic link.
		while(burstSum > m_splittingBurst)
		{
			//std::cout << "sum greater than splitting burst size!!! burstSum: " << burstSum << std::endl;
			auto index = std::distance(burstPerLink.begin(),std::max_element(burstPerLink.begin(), burstPerLink.end()));
			burstPerLink.at(index) -= 1;
			burstSum -= 1;
		}
		
		//std::cout << "Fix Rounding Error | total: " << +m_splittingBurst << " burst 0: " << +burstPerLink.at(0) << " burst 1: " << +burstPerLink.at(1) << " burst 2: " << +burstPerLink.at(2) << std::endl;
			
		//check if there is any update...
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			if (m_lastSplittingIndexList.at(ind) != burstPerLink.at(ind))
			{
				update = true;
				m_lastSplittingIndexList= burstPerLink;
				m_lastRatio = newRatio;
				break;
			}
		}
		/*disable stable algorithm for now*/
	}
	//end of gma2.0 splitting algorithm.

	/*[Optional] Drain high latency queue*/
	//[Optional]Sender Side Drain Phase: added a min owd measurement in TSU to info sender to drain the outstanding packets in the queue.
	// this code implement receivers side queuing delay measurements.
	uint8_t highQueueingDelayCounter = 0;
	uint8_t activeDatalink = 0;
	std::vector<uint8_t> queueingDelayList;
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		queueingDelayList.push_back(UINT8_MAX);//no measurement.
		if (m_lastSplittingIndexList.at(ind) > 0)
		{
			//link is active for data.
			activeDatalink++;
			if(measurement->m_delayThisInterval.at(ind))//delay is measured in this interval
			{
				uint32_t delay = 0;
				if (measurement->m_delayList.at(ind) > measurement->m_minOwdLongTerm.at(ind))
				{
					delay = measurement->m_delayList.at(ind) - measurement->m_minOwdLongTerm.at(ind);
				}
				if (delay < m_queueingDelayTargetMs)
				{
					//queuing delay is smaller than the targert, e.g., 10 ms, we assume zeroe queuing delay.
					delay = 0;
				}
				else
				{
					//detect one link with outstanding queueing delay
					highQueueingDelayCounter++;
				}
				uint32_t maxValue = UINT8_MAX - 1; //range for queueing delay is 0 ~ 254
				//uint32_t maxValue = 20; //range for queueing delay is 0 ~ 254
				queueingDelayList.at(ind) = (std::min(maxValue, delay)); //measurement available
			}
		}
	}

	if (highQueueingDelayCounter > 0 && highQueueingDelayCounter < activeDatalink)
	{
		//report tsu only if there is at least one link with outstanding queueing delay;
		//however, if all active links (s(i) > 0) have outstanding queueing delay, do not report. since all links are bad...
		//update = true;
	}
	else
	{
		//either no link with high delay, or all links with high delay. do not report queuing delay in this case.
	}
	//end of drain high latency queue.


	if(update)
	{
		std::cout << "RX APP ======= interval: " << measurement->m_measureIntervalDurationS << " s \n";
		for (uint8_t ind = 0; ind < measurement->m_links; ind++)
		{
			std::cout << "[link: " << +ind << ", split ratio:"<<+m_lastSplittingIndexList.at (ind) 
			<< ", queueing delay:"<<+queueingDelayList.at (ind) 
			<< ", violation:"<<measurement->m_delayViolationPktNumList.at (ind)
			<< ", total:" << measurement->m_totalPktNumList.at (ind)<<"] ";
			std::cout << "\n";

		}
	}

	
	//check if the sum equals splitting burst size;
	uint8_t sumRatio = 0;
	for (uint8_t ind = 0; ind < measurement->m_links; ind++)
	{
		sumRatio += m_lastSplittingIndexList.at (ind);
	}

	NS_ASSERT_MSG(sumRatio == m_splittingBurst, "the summation must equals the splitting burst size");

	Ptr<SplittingDecision> decision = Create<SplittingDecision> ();
	decision->m_splitIndexList = m_lastSplittingIndexList;
	if (highQueueingDelayCounter > 0 && highQueueingDelayCounter < activeDatalink)
	{
		//report delay only there exits link with high queueing delay, and the number of high delay link is smaller than all link.
		decision->m_queueingDelay = queueingDelayList;
	}
	decision->m_update = update;
	return decision;

}

}
