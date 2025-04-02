/*
 * Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
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
 * Author: Marco Miozzo <marco.miozzo@cttc.es>
 */

#include <ns3/boolean.h>
#include <ns3/log.h>
#include <ns3/lte-amc.h>
#include <ns3/lte-vendor-specific-parameters.h>
#include <ns3/math.h>
#include <ns3/ns-pf-ff-mac-scheduler.h>
#include <ns3/pointer.h>
#include <ns3/simulator.h>

#include <cfloat>
#include <set>
#include <ns3/enum.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NsPfFfMacScheduler");

/// PF type 0 allocation RBG
static const int PfType0AllocationRbg[4] = {
    10,  // RGB size 1
    26,  // RGB size 2
    63,  // RGB size 3
    110, // RGB size 4
};       // see table 7.1.6.1-1 of 36.213

NS_OBJECT_ENSURE_REGISTERED(NsPfFfMacScheduler);

NsPfFfMacScheduler::NsPfFfMacScheduler()
    : m_cschedSapUser(nullptr),
      m_schedSapUser(nullptr),
      m_timeWindow(99.0),
      m_nextRntiUl(0)
{
    m_amc = CreateObject<LteAmc>();
    m_cschedSapProvider = new MemberCschedSapProvider<NsPfFfMacScheduler>(this);
    m_schedSapProvider = new MemberSchedSapProvider<NsPfFfMacScheduler>(this);
    m_ffrSapProvider = nullptr;
    m_ffrSapUser = new MemberLteFfrSapUser<NsPfFfMacScheduler>(this);
}

NsPfFfMacScheduler::~NsPfFfMacScheduler()
{
    NS_LOG_FUNCTION(this);
}

void
NsPfFfMacScheduler::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_dlHarqProcessesDciBuffer.clear();
    m_dlHarqProcessesTimer.clear();
    m_dlHarqProcessesRlcPduListBuffer.clear();
    m_dlInfoListBuffered.clear();
    m_ulHarqCurrentProcessId.clear();
    m_ulHarqProcessesStatus.clear();
    m_ulHarqProcessesDciBuffer.clear();
    delete m_cschedSapProvider;
    delete m_schedSapProvider;
    delete m_ffrSapUser;
}

TypeId
NsPfFfMacScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NsPfFfMacScheduler")
            .SetParent<FfMacScheduler>()
            .SetGroupName("Lte")
            .AddConstructor<NsPfFfMacScheduler>()
            .AddAttribute("CqiTimerThreshold",
                          "The number of TTIs a CQI is valid (default 1000 - 1 sec.)",
                          UintegerValue(1000),
                          MakeUintegerAccessor(&NsPfFfMacScheduler::m_cqiTimersThreshold),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("HarqEnabled",
                          "Activate/Deactivate the HARQ [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&NsPfFfMacScheduler::m_harqOn),
                          MakeBooleanChecker())
            .AddAttribute("UlGrantMcs",
                          "The MCS of the UL grant, must be [0..15] (default 0)",
                          UintegerValue(0),
                          MakeUintegerAccessor(&NsPfFfMacScheduler::m_ulGrantMcs),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute ("MeasurementInterval",
                        "LTE measurement interval",
                        TimeValue (Seconds(1.0)),
                        MakeTimeAccessor (&NsPfFfMacScheduler::m_measurementInterval),
                        MakeTimeChecker ())
            .AddAttribute ("MeasurementGuardInterval",
                        "Guard time interval between 2 measurements",
                        TimeValue (Seconds(0.0)),
                        MakeTimeAccessor (&NsPfFfMacScheduler::m_measurementGuardInterval),
                        MakeTimeChecker ())
            .AddTraceSource("LteEnbMeasurement",
                        "The LTE Measurement for each cell",
                        MakeTraceSourceAccessor(&NsPfFfMacScheduler::m_LteEnbMeasurement),
                        "ns3::NsPfFfMacScheduler::LteEnbMeasurementTracedCallback")
            .AddTraceSource("LteUeMeasurement",
                        "The LTE Measurement for each user",
                        MakeTraceSourceAccessor(&NsPfFfMacScheduler::m_lteUeMeasurement),
                        "ns3::NsPfFfMacScheduler::LteUeMeasurementTracedCallback");
    return tid;
}

void
NsPfFfMacScheduler::SetFfMacCschedSapUser(FfMacCschedSapUser* s)
{
    m_cschedSapUser = s;
    Simulator::Schedule(m_measurementGuardInterval, &NsPfFfMacScheduler::MeasurementGuardEnd, this);

}

void
NsPfFfMacScheduler::SetFfMacSchedSapUser(FfMacSchedSapUser* s)
{
    m_schedSapUser = s;
}

FfMacCschedSapProvider*
NsPfFfMacScheduler::GetFfMacCschedSapProvider()
{
    return m_cschedSapProvider;
}

FfMacSchedSapProvider*
NsPfFfMacScheduler::GetFfMacSchedSapProvider()
{
    return m_schedSapProvider;
}

void
NsPfFfMacScheduler::SetLteFfrSapProvider(LteFfrSapProvider* s)
{
    m_ffrSapProvider = s;
}

LteFfrSapUser*
NsPfFfMacScheduler::GetLteFfrSapUser()
{
    return m_ffrSapUser;
}

void
NsPfFfMacScheduler::DoCschedCellConfigReq(
    const struct FfMacCschedSapProvider::CschedCellConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    // Read the subset of parameters used
    m_cschedCellConfig = params;
    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);
    FfMacCschedSapUser::CschedUeConfigCnfParameters cnf;
    cnf.m_result = SUCCESS;
    m_cschedSapUser->CschedUeConfigCnf(cnf);
}

void
NsPfFfMacScheduler::DoCschedUeConfigReq(
    const struct FfMacCschedSapProvider::CschedUeConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this << " RNTI " << params.m_rnti << " txMode "
                         << (uint16_t)params.m_transmissionMode);
    std::map<uint16_t, uint8_t>::iterator it = m_uesTxMode.find(params.m_rnti);
    if (it == m_uesTxMode.end())
    {
        m_uesTxMode.insert(std::pair<uint16_t, double>(params.m_rnti, params.m_transmissionMode));
        // generate HARQ buffers
        m_dlHarqCurrentProcessId.insert(std::pair<uint16_t, uint8_t>(params.m_rnti, 0));
        DlHarqProcessesStatus_t dlHarqPrcStatus;
        dlHarqPrcStatus.resize(8, 0);
        m_dlHarqProcessesStatus.insert(
            std::pair<uint16_t, DlHarqProcessesStatus_t>(params.m_rnti, dlHarqPrcStatus));
        DlHarqProcessesTimer_t dlHarqProcessesTimer;
        dlHarqProcessesTimer.resize(8, 0);
        m_dlHarqProcessesTimer.insert(
            std::pair<uint16_t, DlHarqProcessesTimer_t>(params.m_rnti, dlHarqProcessesTimer));
        DlHarqProcessesDciBuffer_t dlHarqdci;
        dlHarqdci.resize(8);
        m_dlHarqProcessesDciBuffer.insert(
            std::pair<uint16_t, DlHarqProcessesDciBuffer_t>(params.m_rnti, dlHarqdci));
        DlHarqRlcPduListBuffer_t dlHarqRlcPdu;
        dlHarqRlcPdu.resize(2);
        dlHarqRlcPdu.at(0).resize(8);
        dlHarqRlcPdu.at(1).resize(8);
        m_dlHarqProcessesRlcPduListBuffer.insert(
            std::pair<uint16_t, DlHarqRlcPduListBuffer_t>(params.m_rnti, dlHarqRlcPdu));
        m_ulHarqCurrentProcessId.insert(std::pair<uint16_t, uint8_t>(params.m_rnti, 0));
        UlHarqProcessesStatus_t ulHarqPrcStatus;
        ulHarqPrcStatus.resize(8, 0);
        m_ulHarqProcessesStatus.insert(
            std::pair<uint16_t, UlHarqProcessesStatus_t>(params.m_rnti, ulHarqPrcStatus));
        UlHarqProcessesDciBuffer_t ulHarqdci;
        ulHarqdci.resize(8);
        m_ulHarqProcessesDciBuffer.insert(
            std::pair<uint16_t, UlHarqProcessesDciBuffer_t>(params.m_rnti, ulHarqdci));
    }
    else
    {
        (*it).second = params.m_transmissionMode;
    }
}

void
NsPfFfMacScheduler::DoCschedLcConfigReq(
    const struct FfMacCschedSapProvider::CschedLcConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this << " New LC, rnti: " << params.m_rnti);

    NS_LOG_FUNCTION("LC configuration. Number of LCs:" << params.m_logicalChannelConfigList.size());

    // m_reconfigureFlat indicates if this is a reconfiguration or new UE is added, table  4.1.5 in
    // LTE MAC scheduler specification
    if (params.m_reconfigureFlag)
    {
        std::vector<struct LogicalChannelConfigListElement_s>::const_iterator lcit;

        for (lcit = params.m_logicalChannelConfigList.begin();
             lcit != params.m_logicalChannelConfigList.end();
             lcit++)
        {
            LteFlowId_t flowid = LteFlowId_t(params.m_rnti, lcit->m_logicalChannelIdentity);

            if (m_ueLogicalChannelsConfigList.find(flowid) == m_ueLogicalChannelsConfigList.end())
            {
                NS_LOG_ERROR("UE logical channels can not be reconfigured because it was not "
                             "configured before.");
            }
            else
            {
                m_ueLogicalChannelsConfigList.find(flowid)->second = *lcit;
            }
        }

    } // else new UE is added
    else
    {
        std::vector<struct LogicalChannelConfigListElement_s>::const_iterator lcit;

        for (lcit = params.m_logicalChannelConfigList.begin();
             lcit != params.m_logicalChannelConfigList.end();
             lcit++)
        {
            LteFlowId_t flowId = LteFlowId_t(params.m_rnti, lcit->m_logicalChannelIdentity);
            m_ueLogicalChannelsConfigList.insert(
                std::pair<LteFlowId_t, LogicalChannelConfigListElement_s>(flowId, *lcit));
        }
    }

    std::map<uint16_t, nsPfsFlowPerf_t>::iterator it;
    for (std::size_t i = 0; i < params.m_logicalChannelConfigList.size(); i++)
    {
        it = m_flowStatsDl.find(params.m_rnti);

        if (it == m_flowStatsDl.end())
        {
            nsPfsFlowPerf_t flowStatsDl;
            flowStatsDl.flowStart = Simulator::Now();
            flowStatsDl.totalBytesTransmitted = 0;
            flowStatsDl.lastTtiBytesTrasmitted = 0;
            flowStatsDl.lastTtiSlicedBytesTrasmitted = 0;
            flowStatsDl.lastAveragedThroughput = 1;
            flowStatsDl.lastAveragedSlicedThroughput = 1;
            m_flowStatsDl.insert(std::pair<uint16_t, nsPfsFlowPerf_t>(params.m_rnti, flowStatsDl));
            nsPfsFlowPerf_t flowStatsUl;
            flowStatsUl.flowStart = Simulator::Now();
            flowStatsUl.totalBytesTransmitted = 0;
            flowStatsUl.lastTtiBytesTrasmitted = 0;
            flowStatsUl.lastTtiSlicedBytesTrasmitted = 0;
            flowStatsUl.lastAveragedThroughput = 1;
            flowStatsUl.lastAveragedSlicedThroughput = 1;
            m_flowStatsUl.insert(std::pair<uint16_t, nsPfsFlowPerf_t>(params.m_rnti, flowStatsUl));
        }
        else
        {
            //std::cout << "OLD QCI :" << +params.m_logicalChannelConfigList.at(i).m_qci << std::endl;
        }

        if (params.m_logicalChannelConfigList.at(i).m_eRabMaximulBitrateDl == UINT32_MAX)//use for network slicing id
        {   
            Ptr<SliceInfo> sliceInfo = Create<SliceInfo> ();
            uint64_t encodedBits = params.m_logicalChannelConfigList.at(i).m_eRabGuaranteedBitrateDl;
            uint32_t sliceId = encodedBits & 0xff;
            sliceInfo->m_dedicatedRbg = (encodedBits >> 8) & 0xff;
            sliceInfo->m_prioritizedRbg = (encodedBits >> 16) & 0xff;
            sliceInfo->m_sharedRbg = (encodedBits >> 24) & 0xff;
            uint32_t imsi = (encodedBits >> 32) & 0xff;
            sliceInfo->m_maxRbg = sliceInfo->m_dedicatedRbg + sliceInfo->m_prioritizedRbg + sliceInfo->m_sharedRbg;
            sliceInfo->m_minRbg = sliceInfo->m_dedicatedRbg + sliceInfo->m_prioritizedRbg;
            m_flowStatsDl[params.m_rnti].sliceId = sliceId;
            m_flowStatsUl[params.m_rnti].sliceId = sliceId;
            m_flowStatsDl[params.m_rnti].imsi = imsi;
            m_flowStatsUl[params.m_rnti].imsi = imsi;

            m_sliceInfoMap[sliceId] = sliceInfo;


            std::cout << "imsi:" << imsi<< " rnti:" <<+params.m_rnti << " lcIdenity:" << +params.m_logicalChannelConfigList.at(i).m_logicalChannelIdentity << " bearerType:" << params.m_logicalChannelConfigList.at(i).m_qosBearerType  
                << " | slicd ID:" << sliceId << " dedicatedRbg:" << m_sliceInfoMap[sliceId]->m_dedicatedRbg << " prioritizedRbg:" << m_sliceInfoMap[sliceId]->m_prioritizedRbg << " sharedRbg:" << m_sliceInfoMap[sliceId]->m_sharedRbg
                << " minRbg:" << m_sliceInfoMap[sliceId]->m_minRbg << " maxRbg:" << m_sliceInfoMap[sliceId]->m_maxRbg << std::endl;
        }

    }

    int totalScheduledRbg = 0;
    auto iter = m_sliceInfoMap.begin();
    while(iter != m_sliceInfoMap.end())
    {
        totalScheduledRbg += iter->second->m_minRbg;            
        iter++;
    }

    int rbgSize = GetRbgSize(m_cschedCellConfig.m_dlBandwidth);
    int rbgNum = m_cschedCellConfig.m_dlBandwidth / rbgSize;
    if(totalScheduledRbg > rbgNum)
    {
        NS_FATAL_ERROR("the total schedule RBGs from config.json file is greater than available RBGs (" << rbgNum << ")");
    }

    /*for (int i = 0; i < 25; i++)
    {
        RbgSliceInfo rbgSlice = GetRbgSliceInfo (i);
        std::cout << "rbg:" << i << " type:" << rbgSlice.first << " slice ID:" << rbgSlice.second << std::endl;
    }*/

}

void
NsPfFfMacScheduler::DoCschedLcReleaseReq(
    const struct FfMacCschedSapProvider::CschedLcReleaseReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    std::vector<uint8_t>::const_iterator it;

    for (it = params.m_logicalChannelIdentity.begin(); it != params.m_logicalChannelIdentity.end();
         it++)
    {
        LteFlowId_t flowId = LteFlowId_t(params.m_rnti, *it);

        // find the logical channel with the same Logical Channel Identity in the current list,
        // release it
        if (m_ueLogicalChannelsConfigList.find(flowId) != m_ueLogicalChannelsConfigList.end())
        {
            m_ueLogicalChannelsConfigList.erase(flowId);
        }
        else
        {
            NS_FATAL_ERROR("Logical channels cannot be released because it can not be found in the "
                           "list of active LCs");
        }
    }

    for (std::size_t i = 0; i < params.m_logicalChannelIdentity.size(); i++)
    {
        std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it =
            m_rlcBufferReq.begin();
        std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator temp;
        while (it != m_rlcBufferReq.end())
        {
            if (((*it).first.m_rnti == params.m_rnti) &&
                ((*it).first.m_lcId == params.m_logicalChannelIdentity.at(i)))
            {
                temp = it;
                it++;
                m_rlcBufferReq.erase(temp);
            }
            else
            {
                it++;
            }
        }
    }
}

void
NsPfFfMacScheduler::DoCschedUeReleaseReq(
    const struct FfMacCschedSapProvider::CschedUeReleaseReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    for (int i = 0; i < MAX_LC_LIST; i++)
    {
        LteFlowId_t flowId = LteFlowId_t(params.m_rnti, i);
        // find the logical channel with the same Logical Channel Identity in the current list,
        // release it
        if (m_ueLogicalChannelsConfigList.find(flowId) != m_ueLogicalChannelsConfigList.end())
        {
            m_ueLogicalChannelsConfigList.erase(flowId);
        }
    }

    m_uesTxMode.erase(params.m_rnti);
    m_dlHarqCurrentProcessId.erase(params.m_rnti);
    m_dlHarqProcessesStatus.erase(params.m_rnti);
    m_dlHarqProcessesTimer.erase(params.m_rnti);
    m_dlHarqProcessesDciBuffer.erase(params.m_rnti);
    m_dlHarqProcessesRlcPduListBuffer.erase(params.m_rnti);
    m_ulHarqCurrentProcessId.erase(params.m_rnti);
    m_ulHarqProcessesStatus.erase(params.m_rnti);
    m_ulHarqProcessesDciBuffer.erase(params.m_rnti);
    m_flowStatsDl.erase(params.m_rnti);
    m_flowStatsUl.erase(params.m_rnti);
    m_ceBsrRxed.erase(params.m_rnti);
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it =
        m_rlcBufferReq.begin();
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator temp;
    while (it != m_rlcBufferReq.end())
    {
        if ((*it).first.m_rnti == params.m_rnti)
        {
            temp = it;
            it++;
            m_rlcBufferReq.erase(temp);
        }
        else
        {
            it++;
        }
    }
    if (m_nextRntiUl == params.m_rnti)
    {
        m_nextRntiUl = 0;
    }
}

void
NsPfFfMacScheduler::DoSchedDlRlcBufferReq(
    const struct FfMacSchedSapProvider::SchedDlRlcBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this << params.m_rnti << (uint32_t)params.m_logicalChannelIdentity);
    // API generated by RLC for updating RLC parameters on a LC (tx and retx queues)

    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it;

    LteFlowId_t flow(params.m_rnti, params.m_logicalChannelIdentity);

    it = m_rlcBufferReq.find(flow);

    if (it == m_rlcBufferReq.end())
    {
        m_rlcBufferReq.insert(
            std::pair<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>(flow,
                                                                                         params));
    }
    else
    {
        (*it).second = params;
    }
}

void
NsPfFfMacScheduler::DoSchedDlPagingBufferReq(
    const struct FfMacSchedSapProvider::SchedDlPagingBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("method not implemented");
}

void
NsPfFfMacScheduler::DoSchedDlMacBufferReq(
    const struct FfMacSchedSapProvider::SchedDlMacBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("method not implemented");
}

int
NsPfFfMacScheduler::GetRbgSize(int dlbandwidth)
{
    for (int i = 0; i < 4; i++)
    {
        if (dlbandwidth < PfType0AllocationRbg[i])
        {
            return (i + 1);
        }
    }

    return (-1);
}

unsigned int
NsPfFfMacScheduler::LcActivePerFlow(uint16_t rnti)
{
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it;
    unsigned int lcActive = 0;
    for (it = m_rlcBufferReq.begin(); it != m_rlcBufferReq.end(); it++)
    {
        if (((*it).first.m_rnti == rnti) && (((*it).second.m_rlcTransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcRetransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcStatusPduSize > 0)))
        {
            lcActive++;
        }
        if ((*it).first.m_rnti > rnti)
        {
            break;
        }
    }
    return (lcActive);
}

void
NsPfFfMacScheduler::RefreshSlicePriority()
{
    m_sliceBearerTypeMap.clear();
    //std::cout<< "Refresh Slice Priority" << std::endl;

    for (auto it = m_rlcBufferReq.begin(); it != m_rlcBufferReq.end(); it++)
    {

        uint64_t buffReqBytes = (*it).second.m_rlcTransmissionQueueSize + (*it).second.m_rlcRetransmissionQueueSize + (*it).second.m_rlcStatusPduSize;
        if (buffReqBytes > 0)
        {
            //buffer not empty...
            if ( m_scheduledBytesPerLc[(*it).first] < buffReqBytes )
            {
                //still have data to send for this lc
                if(m_flowStatsDl.find((*it).first.m_rnti) == m_flowStatsDl.end())
                {
                    NS_FATAL_ERROR("cannot find this rinti:" <<(*it).first.m_rnti << " in the m_flowStatsDl");
                }
                    //uint64_t imsi = m_flowStatsDl[(*it).first.m_rnti].imsi;

                    //std::cout << "imsi:" << imsi << "rnti:"<< (*it).first.m_rnti << " lc:" << (*it).first.m_lcId <<" scheduled:" << m_scheduledBytesPerLc[(*it).first]
                    //<< " txqueue: "<< (*it).second.m_rlcTransmissionQueueSize 
                    //<< " retxqueue :"<< (*it).second.m_rlcRetransmissionQueueSize 
                    //<< " status :"<< (*it).second.m_rlcStatusPduSize
                    //<< std::endl;


                auto iterFlow = m_flowStatsDl.find((*it).first.m_rnti);
                if(iterFlow != m_flowStatsDl.end())
                {

                    //std::cout << "imsi:" << imsi << "rnti:"<< (*it).first.m_rnti << " lc:" << (*it).first.m_lcId 
                    //<< " add type:" << m_ueLogicalChannelsConfigList[(*it).first].m_qosBearerType << std::endl;
                    //add the bearer type to the map
                    auto key = std::make_pair((*iterFlow).second.sliceId, m_ueLogicalChannelsConfigList[(*it).first].m_qosBearerType);
                    m_sliceBearerTypeMap[key] = true;
                }

            }
            else
            {  
                //std::cout << "Done | rnti:"<< (*it).first.m_rnti << " lc:" << (*it).first.m_lcId << " buffer:" <<(*it).second.m_rlcTransmissionQueueSize + (*it).second.m_rlcRetransmissionQueueSize + (*it).second.m_rlcStatusPduSize 
                //    <<" scheduled:" << m_scheduledBytesPerLc[(*it).first] << std::endl;
                //already scheduled, not data for transmission....
            }
         }
    }

    /*auto iter = m_sliceBearerTypeMap.begin();
    while (iter != m_sliceBearerTypeMap.end())
    {
        std::cout << "slice ID:" << (*iter).first.first << " type:" << (*iter).first.second << " value:" << (*iter).second << std::endl;
        iter++;
    }*/

}

unsigned int
NsPfFfMacScheduler::PriorityAwareLcActivePerFlow(uint16_t rnti)
{
    if(m_flowStatsDl.find(rnti) == m_flowStatsDl.end())
    {
        NS_FATAL_ERROR("cannot find this rinti:" <<rnti<< " in the m_flowStatsDl");
    }
    //uint64_t imsi = m_flowStatsDl[rnti].imsi;

    //if this rnti has GBR bearer, return LcActivePerFlow
    //else if this rnti only has NGBR bearer
    //  if this slice only has NGBR bearer, return LcActivePerFlow
    //  else this slice has GBR bearer, return 0. We will not schule NGBR users if the same slice has GBR users.
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it;
    unsigned int lcActive = 0;
    bool gbrFlag = false;
    for (it = m_rlcBufferReq.begin(); it != m_rlcBufferReq.end(); it++)
    {
        if (((*it).first.m_rnti == rnti) && (((*it).second.m_rlcTransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcRetransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcStatusPduSize > 0)))
        {
            lcActive++;

            LteFlowId_t flowId = LteFlowId_t(rnti, (*it).first.m_lcId);
            // find the logical channel with the same Logical Channel Identity in the current list,
            if (m_ueLogicalChannelsConfigList.find(flowId) != m_ueLogicalChannelsConfigList.end())
            {
                if (m_ueLogicalChannelsConfigList[flowId].m_qosBearerType == m_ueLogicalChannelsConfigList[flowId].QBT_GBR)
                {
                    //this is a gbr bearer
                    gbrFlag = true;

                    //std::cout << " GBR | imsi:"<< imsi << "rnti:" << rnti << " lcid:"<<(*it).first.m_lcId
                    //    << " type:" << m_ueLogicalChannelsConfigList[flowId].m_qosBearerType
                    //    <<std::endl;

                }
                else
                {

                    //std::cout << "NGBR | imsi:"<< imsi << "rnti:"<< rnti << " lcid:"<<(*it).first.m_lcId
                    //    << " type:" << m_ueLogicalChannelsConfigList[flowId].m_qosBearerType
                    //   <<std::endl;

                }
            }
        }
        if ((*it).first.m_rnti > rnti)
        {
            break;
        }
    }
    if (gbrFlag)
    {
        //this user has GBR traffic
        return lcActive;
    }
    else
    {
        //check if there is GBR traffic for other users in the same slice...
        auto iterFlow = m_flowStatsDl.find(rnti);
        if(iterFlow != m_flowStatsDl.end())
        {
            auto gbr_key = std::make_pair((*iterFlow).second.sliceId, LogicalChannelConfigListElement_s::QBT_GBR);
            if(m_sliceBearerTypeMap.find(gbr_key) != m_sliceBearerTypeMap.end())
            {
                //find a GBR user in the same slice, cannot schedule this user, return 0 lc
                //std::cout << "imsi:" <<imsi << "find GBR in slice:" << (*iterFlow).second.sliceId << std::endl;
                return 0;
            }
            else
            {
                //std::cout << "imsi:" <<imsi << "find NBGR in this slice:" << (*iterFlow).second.sliceId << std::endl;
            }
        }

     
    }
    return (lcActive);
}

bool
NsPfFfMacScheduler::HarqProcessAvailability(uint16_t rnti)
{
    NS_LOG_FUNCTION(this << rnti);

    std::map<uint16_t, uint8_t>::iterator it = m_dlHarqCurrentProcessId.find(rnti);
    if (it == m_dlHarqCurrentProcessId.end())
    {
        NS_FATAL_ERROR("No Process Id found for this RNTI " << rnti);
    }
    std::map<uint16_t, DlHarqProcessesStatus_t>::iterator itStat =
        m_dlHarqProcessesStatus.find(rnti);
    if (itStat == m_dlHarqProcessesStatus.end())
    {
        NS_FATAL_ERROR("No Process Id Statusfound for this RNTI " << rnti);
    }
    uint8_t i = (*it).second;
    do
    {
        i = (i + 1) % HARQ_PROC_NUM;
    } while (((*itStat).second.at(i) != 0) && (i != (*it).second));
    if ((*itStat).second.at(i) == 0)
    {
        return (true);
    }
    else
    {
        return (false); // return a not valid harq proc id
    }
}

uint8_t
NsPfFfMacScheduler::UpdateHarqProcessId(uint16_t rnti)
{
    NS_LOG_FUNCTION(this << rnti);

    if (m_harqOn == false)
    {
        return (0);
    }

    std::map<uint16_t, uint8_t>::iterator it = m_dlHarqCurrentProcessId.find(rnti);
    if (it == m_dlHarqCurrentProcessId.end())
    {
        NS_FATAL_ERROR("No Process Id found for this RNTI " << rnti);
    }
    std::map<uint16_t, DlHarqProcessesStatus_t>::iterator itStat =
        m_dlHarqProcessesStatus.find(rnti);
    if (itStat == m_dlHarqProcessesStatus.end())
    {
        NS_FATAL_ERROR("No Process Id Statusfound for this RNTI " << rnti);
    }
    uint8_t i = (*it).second;
    do
    {
        i = (i + 1) % HARQ_PROC_NUM;
    } while (((*itStat).second.at(i) != 0) && (i != (*it).second));
    if ((*itStat).second.at(i) == 0)
    {
        (*it).second = i;
        (*itStat).second.at(i) = 1;
    }
    else
    {
        NS_FATAL_ERROR("No HARQ process available for RNTI "
                       << rnti << " check before update with HarqProcessAvailability");
    }

    return ((*it).second);
}

void
NsPfFfMacScheduler::RefreshHarqProcesses()
{
    NS_LOG_FUNCTION(this);

    std::map<uint16_t, DlHarqProcessesTimer_t>::iterator itTimers;
    for (itTimers = m_dlHarqProcessesTimer.begin(); itTimers != m_dlHarqProcessesTimer.end();
         itTimers++)
    {
        for (uint16_t i = 0; i < HARQ_PROC_NUM; i++)
        {
            if ((*itTimers).second.at(i) == HARQ_DL_TIMEOUT)
            {
                // reset HARQ process

                NS_LOG_DEBUG(this << " Reset HARQ proc " << i << " for RNTI " << (*itTimers).first);
                std::map<uint16_t, DlHarqProcessesStatus_t>::iterator itStat =
                    m_dlHarqProcessesStatus.find((*itTimers).first);
                if (itStat == m_dlHarqProcessesStatus.end())
                {
                    NS_FATAL_ERROR("No Process Id Status found for this RNTI "
                                   << (*itTimers).first);
                }
                (*itStat).second.at(i) = 0;
                (*itTimers).second.at(i) = 0;
            }
            else
            {
                (*itTimers).second.at(i)++;
            }
        }
    }
}

void
NsPfFfMacScheduler::DoSchedDlTriggerReq(
    const struct FfMacSchedSapProvider::SchedDlTriggerReqParameters& params)
{
    NS_LOG_FUNCTION(this << " Frame no. " << (params.m_sfnSf >> 4) << " subframe no. "
                         << (0xF & params.m_sfnSf));
    // API generated by RLC for triggering the scheduling of a DL subframe

    // evaluate the relative channel quality indicator for each UE per each RBG
    // (since we are using allocation type 0 the small unit of allocation is RBG)
    // Resource allocation type 0 (see sec 7.1.6.1 of 36.213)

    RefreshDlCqiMaps();

    int rbgSize = GetRbgSize(m_cschedCellConfig.m_dlBandwidth);
    int rbgNum = m_cschedCellConfig.m_dlBandwidth / rbgSize;
    std::map<uint16_t, std::vector<uint16_t>> allocationMap; // RBs map per RNTI
    std::map<uint16_t, std::vector<uint16_t>> sliceAllocationMap; // slice RBs map per RNTI
    std::vector<bool> rbgMap;                                // global RBGs map
    uint16_t rbgAllocatedNum = 0;
    std::set<uint16_t> rntiAllocated;
    rbgMap.resize(m_cschedCellConfig.m_dlBandwidth / rbgSize, false);

    rbgMap = m_ffrSapProvider->GetAvailableDlRbg();
    for (std::vector<bool>::iterator it = rbgMap.begin(); it != rbgMap.end(); it++)
    {
        if ((*it) == true)
        {
            rbgAllocatedNum++;
        }
    }

    FfMacSchedSapUser::SchedDlConfigIndParameters ret;

    //   update UL HARQ proc id
    std::map<uint16_t, uint8_t>::iterator itProcId;
    for (itProcId = m_ulHarqCurrentProcessId.begin(); itProcId != m_ulHarqCurrentProcessId.end();
         itProcId++)
    {
        (*itProcId).second = ((*itProcId).second + 1) % HARQ_PROC_NUM;
    }

    // RACH Allocation
    std::vector<bool> ulRbMap;
    ulRbMap.resize(m_cschedCellConfig.m_ulBandwidth, false);
    ulRbMap = m_ffrSapProvider->GetAvailableUlRbg();
    uint8_t maxContinuousUlBandwidth = 0;
    uint8_t tmpMinBandwidth = 0;
    uint16_t ffrRbStartOffset = 0;
    uint16_t tmpFfrRbStartOffset = 0;
    uint16_t index = 0;

    for (std::vector<bool>::iterator it = ulRbMap.begin(); it != ulRbMap.end(); it++)
    {
        if ((*it) == true)
        {
            if (tmpMinBandwidth > maxContinuousUlBandwidth)
            {
                maxContinuousUlBandwidth = tmpMinBandwidth;
                ffrRbStartOffset = tmpFfrRbStartOffset;
            }
            tmpMinBandwidth = 0;
        }
        else
        {
            if (tmpMinBandwidth == 0)
            {
                tmpFfrRbStartOffset = index;
            }
            tmpMinBandwidth++;
        }
        index++;
    }

    if (tmpMinBandwidth > maxContinuousUlBandwidth)
    {
        maxContinuousUlBandwidth = tmpMinBandwidth;
        ffrRbStartOffset = tmpFfrRbStartOffset;
    }

    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);
    uint16_t rbStart = 0;
    rbStart = ffrRbStartOffset;
    std::vector<struct RachListElement_s>::iterator itRach;
    for (itRach = m_rachList.begin(); itRach != m_rachList.end(); itRach++)
    {
        NS_ASSERT_MSG(m_amc->GetUlTbSizeFromMcs(m_ulGrantMcs, m_cschedCellConfig.m_ulBandwidth) >
                          (*itRach).m_estimatedSize,
                      " Default UL Grant MCS does not allow to send RACH messages");
        BuildRarListElement_s newRar;
        newRar.m_rnti = (*itRach).m_rnti;
        // DL-RACH Allocation
        // Ideal: no needs of configuring m_dci
        // UL-RACH Allocation
        newRar.m_grant.m_rnti = newRar.m_rnti;
        newRar.m_grant.m_mcs = m_ulGrantMcs;
        uint16_t rbLen = 1;
        uint16_t tbSizeBits = 0;
        // find lowest TB size that fits UL grant estimated size
        while ((tbSizeBits < (*itRach).m_estimatedSize) &&
               (rbStart + rbLen < (ffrRbStartOffset + maxContinuousUlBandwidth)))
        {
            rbLen++;
            tbSizeBits = m_amc->GetUlTbSizeFromMcs(m_ulGrantMcs, rbLen);
        }
        if (tbSizeBits < (*itRach).m_estimatedSize)
        {
            // no more allocation space: finish allocation
            break;
        }
        newRar.m_grant.m_rbStart = rbStart;
        newRar.m_grant.m_rbLen = rbLen;
        newRar.m_grant.m_tbSize = tbSizeBits / 8;
        newRar.m_grant.m_hopping = false;
        newRar.m_grant.m_tpc = 0;
        newRar.m_grant.m_cqiRequest = false;
        newRar.m_grant.m_ulDelay = false;
        NS_LOG_INFO(this << " UL grant allocated to RNTI " << (*itRach).m_rnti << " rbStart "
                         << rbStart << " rbLen " << rbLen << " MCS " << m_ulGrantMcs << " tbSize "
                         << newRar.m_grant.m_tbSize);
        for (uint16_t i = rbStart; i < rbStart + rbLen; i++)
        {
            m_rachAllocationMap.at(i) = (*itRach).m_rnti;
        }

        if (m_harqOn == true)
        {
            // generate UL-DCI for HARQ retransmissions
            UlDciListElement_s uldci;
            uldci.m_rnti = newRar.m_rnti;
            uldci.m_rbLen = rbLen;
            uldci.m_rbStart = rbStart;
            uldci.m_mcs = m_ulGrantMcs;
            uldci.m_tbSize = tbSizeBits / 8;
            uldci.m_ndi = 1;
            uldci.m_cceIndex = 0;
            uldci.m_aggrLevel = 1;
            uldci.m_ueTxAntennaSelection = 3; // antenna selection OFF
            uldci.m_hopping = false;
            uldci.m_n2Dmrs = 0;
            uldci.m_tpc = 0;            // no power control
            uldci.m_cqiRequest = false; // only period CQI at this stage
            uldci.m_ulIndex = 0;        // TDD parameter
            uldci.m_dai = 1;            // TDD parameter
            uldci.m_freqHopping = 0;
            uldci.m_pdcchPowerOffset = 0; // not used

            uint8_t harqId = 0;
            std::map<uint16_t, uint8_t>::iterator itProcId;
            itProcId = m_ulHarqCurrentProcessId.find(uldci.m_rnti);
            if (itProcId == m_ulHarqCurrentProcessId.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << uldci.m_rnti);
            }
            harqId = (*itProcId).second;
            std::map<uint16_t, UlHarqProcessesDciBuffer_t>::iterator itDci =
                m_ulHarqProcessesDciBuffer.find(uldci.m_rnti);
            if (itDci == m_ulHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in UL DCI HARQ buffer for RNTI "
                               << uldci.m_rnti);
            }
            (*itDci).second.at(harqId) = uldci;
        }

        rbStart = rbStart + rbLen;
        ret.m_buildRarList.push_back(newRar);
    }
    m_rachList.clear();

    // Process DL HARQ feedback
    RefreshHarqProcesses();
    // retrieve past HARQ retx buffered
    if (!m_dlInfoListBuffered.empty())
    {
        if (!params.m_dlInfoList.empty())
        {
            NS_LOG_INFO(this << " Received DL-HARQ feedback");
            m_dlInfoListBuffered.insert(m_dlInfoListBuffered.end(),
                                        params.m_dlInfoList.begin(),
                                        params.m_dlInfoList.end());
        }
    }
    else
    {
        if (!params.m_dlInfoList.empty())
        {
            m_dlInfoListBuffered = params.m_dlInfoList;
        }
    }
    if (m_harqOn == false)
    {
        // Ignore HARQ feedback
        m_dlInfoListBuffered.clear();
    }
    std::vector<struct DlInfoListElement_s> dlInfoListUntxed;
    for (std::size_t i = 0; i < m_dlInfoListBuffered.size(); i++)
    {
        std::set<uint16_t>::iterator itRnti = rntiAllocated.find(m_dlInfoListBuffered.at(i).m_rnti);
        if (itRnti != rntiAllocated.end())
        {
            // RNTI already allocated for retx
            continue;
        }
        auto nLayers = m_dlInfoListBuffered.at(i).m_harqStatus.size();
        std::vector<bool> retx;
        NS_LOG_INFO(this << " Processing DLHARQ feedback");
        if (nLayers == 1)
        {
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(0) ==
                           DlInfoListElement_s::NACK);
            retx.push_back(false);
        }
        else
        {
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(0) ==
                           DlInfoListElement_s::NACK);
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(1) ==
                           DlInfoListElement_s::NACK);
        }
        if (retx.at(0) || retx.at(1))
        {
            // retrieve HARQ process information
            uint16_t rnti = m_dlInfoListBuffered.at(i).m_rnti;
            uint8_t harqId = m_dlInfoListBuffered.at(i).m_harqProcessId;
            NS_LOG_INFO(this << " HARQ retx RNTI " << rnti << " harqId " << (uint16_t)harqId);
            std::map<uint16_t, DlHarqProcessesDciBuffer_t>::iterator itHarq =
                m_dlHarqProcessesDciBuffer.find(rnti);
            if (itHarq == m_dlHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << rnti);
            }

            DlDciListElement_s dci = (*itHarq).second.at(harqId);
            int rv = 0;
            if (dci.m_rv.size() == 1)
            {
                rv = dci.m_rv.at(0);
            }
            else
            {
                rv = (dci.m_rv.at(0) > dci.m_rv.at(1) ? dci.m_rv.at(0) : dci.m_rv.at(1));
            }

            if (rv == 3)
            {
                // maximum number of retx reached -> drop process
                NS_LOG_INFO("Maximum number of retransmissions reached -> drop process");
                std::map<uint16_t, DlHarqProcessesStatus_t>::iterator it =
                    m_dlHarqProcessesStatus.find(rnti);
                if (it == m_dlHarqProcessesStatus.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) "
                                 << m_dlInfoListBuffered.at(i).m_rnti);
                }
                (*it).second.at(harqId) = 0;
                std::map<uint16_t, DlHarqRlcPduListBuffer_t>::iterator itRlcPdu =
                    m_dlHarqProcessesRlcPduListBuffer.find(rnti);
                if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
                {
                    NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                                   << m_dlInfoListBuffered.at(i).m_rnti);
                }
                for (std::size_t k = 0; k < (*itRlcPdu).second.size(); k++)
                {
                    (*itRlcPdu).second.at(k).at(harqId).clear();
                }
                continue;
            }
            // check the feasibility of retransmitting on the same RBGs
            // translate the DCI to Spectrum framework
            std::vector<int> dciRbg;
            uint32_t mask = 0x1;
            NS_LOG_INFO("Original RBGs " << dci.m_rbBitmap << " rnti " << dci.m_rnti);
            for (int j = 0; j < 32; j++)
            {
                if (((dci.m_rbBitmap & mask) >> j) == 1)
                {
                    dciRbg.push_back(j);
                    NS_LOG_INFO("\t" << j);
                }
                mask = (mask << 1);
            }
            bool free = true;
            for (std::size_t j = 0; j < dciRbg.size(); j++)
            {
                if (rbgMap.at(dciRbg.at(j)) == true)
                {
                    free = false;
                    break;
                }
            }
            if (free)
            {
                // use the same RBGs for the retx
                // reserve RBGs
                for (std::size_t j = 0; j < dciRbg.size(); j++)
                {
                    rbgMap.at(dciRbg.at(j)) = true;
                    NS_LOG_INFO("RBG " << dciRbg.at(j) << " assigned");
                    rbgAllocatedNum++;
                    if(m_flowStatsDl.find(rnti) == m_flowStatsDl.end())
                    {
                        NS_FATAL_ERROR("cannot find this rinti:" <<rnti << " in the m_flowStatsDl");
                    }
                    uint64_t imsi = m_flowStatsDl[rnti].imsi;
                    std::map<uint64_t, int>::iterator itRb = m_usedHarqRbsMap.find(imsi);
                    if(itRb != m_usedHarqRbsMap.end())
                    {
                        m_usedHarqRbsMap[imsi] += rbgSize;
                    }
                    else
                    {
                        m_usedHarqRbsMap[imsi] = rbgSize;
                    }
                }

                NS_LOG_INFO(this << " Send retx in the same RBGs");
            }
            else
            {
                // find RBGs for sending HARQ retx
                uint8_t j = 0;
                uint8_t rbgId = (dciRbg.at(dciRbg.size() - 1) + 1) % rbgNum;
                uint8_t startRbg = dciRbg.at(dciRbg.size() - 1);
                std::vector<bool> rbgMapCopy = rbgMap;
                while ((j < dciRbg.size()) && (startRbg != rbgId))
                {
                    if (rbgMapCopy.at(rbgId) == false)
                    {
                        rbgMapCopy.at(rbgId) = true;
                        dciRbg.at(j) = rbgId;
                        j++;
                    }
                    rbgId = (rbgId + 1) % rbgNum;
                }
                if (j == dciRbg.size())
                {
                    // find new RBGs -> update DCI map
                    uint32_t rbgMask = 0;
                    for (std::size_t k = 0; k < dciRbg.size(); k++)
                    {
                        rbgMask = rbgMask + (0x1 << dciRbg.at(k));
                        rbgAllocatedNum++;
                        if(m_flowStatsDl.find(rnti) == m_flowStatsDl.end())
                        {
                            NS_FATAL_ERROR("cannot find this rinti:" <<rnti << " in the m_flowStatsDl");
                        }
                        uint64_t imsi = m_flowStatsDl[rnti].imsi;
                        std::map<uint64_t, int>::iterator itRb = m_usedHarqRbsMap.find(imsi);
                        if(itRb != m_usedHarqRbsMap.end())
                        {
                            m_usedHarqRbsMap[imsi] += rbgSize;
                        }
                        else
                        {
                            m_usedHarqRbsMap[imsi] = rbgSize;
                        }
                    }
                    dci.m_rbBitmap = rbgMask;
                    rbgMap = rbgMapCopy;
                    NS_LOG_INFO(this << " Move retx in RBGs " << dciRbg.size());
                }
                else
                {
                    // HARQ retx cannot be performed on this TTI -> store it
                    dlInfoListUntxed.push_back(m_dlInfoListBuffered.at(i));
                    NS_LOG_INFO(this << " No resource for this retx -> buffer it");
                }
            }
            // retrieve RLC PDU list for retx TBsize and update DCI
            BuildDataListElement_s newEl;
            std::map<uint16_t, DlHarqRlcPduListBuffer_t>::iterator itRlcPdu =
                m_dlHarqProcessesRlcPduListBuffer.find(rnti);
            if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI " << rnti);
            }
            for (std::size_t j = 0; j < nLayers; j++)
            {
                if (retx.at(j))
                {
                    if (j >= dci.m_ndi.size())
                    {
                        // for avoiding errors in MIMO transient phases
                        dci.m_ndi.push_back(0);
                        dci.m_rv.push_back(0);
                        dci.m_mcs.push_back(0);
                        dci.m_tbsSize.push_back(0);
                        NS_LOG_INFO(this << " layer " << (uint16_t)j
                                         << " no txed (MIMO transition)");
                    }
                    else
                    {
                        dci.m_ndi.at(j) = 0;
                        dci.m_rv.at(j)++;
                        (*itHarq).second.at(harqId).m_rv.at(j)++;
                        NS_LOG_INFO(this << " layer " << (uint16_t)j << " RV "
                                         << (uint16_t)dci.m_rv.at(j));
                    }
                }
                else
                {
                    // empty TB of layer j
                    dci.m_ndi.at(j) = 0;
                    dci.m_rv.at(j) = 0;
                    dci.m_mcs.at(j) = 0;
                    dci.m_tbsSize.at(j) = 0;
                    NS_LOG_INFO(this << " layer " << (uint16_t)j << " no retx");
                }
            }
            for (std::size_t k = 0; k < (*itRlcPdu).second.at(0).at(dci.m_harqProcess).size(); k++)
            {
                std::vector<struct RlcPduListElement_s> rlcPduListPerLc;
                for (std::size_t j = 0; j < nLayers; j++)
                {
                    if (retx.at(j))
                    {
                        if (j < dci.m_ndi.size())
                        {
                            NS_LOG_INFO(" layer " << (uint16_t)j << " tb size "
                                                  << dci.m_tbsSize.at(j));
                            rlcPduListPerLc.push_back(
                                (*itRlcPdu).second.at(j).at(dci.m_harqProcess).at(k));
                        }
                    }
                    else
                    { // if no retx needed on layer j, push an RlcPduListElement_s object with
                      // m_size=0 to keep the size of rlcPduListPerLc vector = 2 in case of MIMO
                        NS_LOG_INFO(" layer " << (uint16_t)j << " tb size " << dci.m_tbsSize.at(j));
                        RlcPduListElement_s emptyElement;
                        emptyElement.m_logicalChannelIdentity = (*itRlcPdu)
                                                                    .second.at(j)
                                                                    .at(dci.m_harqProcess)
                                                                    .at(k)
                                                                    .m_logicalChannelIdentity;
                        emptyElement.m_size = 0;
                        rlcPduListPerLc.push_back(emptyElement);
                    }
                }

                if (!rlcPduListPerLc.empty())
                {
                    newEl.m_rlcPduList.push_back(rlcPduListPerLc);
                }
            }
            newEl.m_rnti = rnti;
            newEl.m_dci = dci;
            (*itHarq).second.at(harqId).m_rv = dci.m_rv;
            // refresh timer
            std::map<uint16_t, DlHarqProcessesTimer_t>::iterator itHarqTimer =
                m_dlHarqProcessesTimer.find(rnti);
            if (itHarqTimer == m_dlHarqProcessesTimer.end())
            {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)rnti);
            }
            (*itHarqTimer).second.at(harqId) = 0;
            ret.m_buildDataList.push_back(newEl);
            rntiAllocated.insert(rnti);
        }
        else
        {
            // update HARQ process status
            NS_LOG_INFO(this << " HARQ received ACK for UE " << m_dlInfoListBuffered.at(i).m_rnti);
            std::map<uint16_t, DlHarqProcessesStatus_t>::iterator it =
                m_dlHarqProcessesStatus.find(m_dlInfoListBuffered.at(i).m_rnti);
            if (it == m_dlHarqProcessesStatus.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE "
                               << m_dlInfoListBuffered.at(i).m_rnti);
            }
            (*it).second.at(m_dlInfoListBuffered.at(i).m_harqProcessId) = 0;
            std::map<uint16_t, DlHarqRlcPduListBuffer_t>::iterator itRlcPdu =
                m_dlHarqProcessesRlcPduListBuffer.find(m_dlInfoListBuffered.at(i).m_rnti);
            if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                               << m_dlInfoListBuffered.at(i).m_rnti);
            }
            for (std::size_t k = 0; k < (*itRlcPdu).second.size(); k++)
            {
                (*itRlcPdu).second.at(k).at(m_dlInfoListBuffered.at(i).m_harqProcessId).clear();
            }
        }
    }
    m_dlInfoListBuffered.clear();
    m_dlInfoListBuffered = dlInfoListUntxed;

    if (rbgAllocatedNum == rbgNum)
    {
        // all the RBGs are already allocated -> exit
        if (!ret.m_buildDataList.empty() || !ret.m_buildRarList.empty())
        {
            m_schedSapUser->SchedDlConfigInd(ret);
        }
        return;
    }
    
    m_scheduledBytesPerLc.clear();
    std::map<uint32_t, uint32_t> sliceRbgUsageMap;
    for (int run = 0; run < 2; run++)
    {
    //the first run will schedule the dedicated and prioritized RBs
    //the second run will schedule the unsed RBs (including unused prioritized RBs)
    for (int i = 0; i < rbgNum; i++)
    {
        NS_LOG_INFO(this << " ALLOCATION for RBG " << i << " of " << rbgNum);
        if (rbgMap.at(i) == false)
        {
            std::map<uint16_t, nsPfsFlowPerf_t>::iterator it;
            std::map<uint16_t, nsPfsFlowPerf_t>::iterator itMax = m_flowStatsDl.end();
            double rcqiMax = 0.0;
            double achievableRateMax = 0.0;
            LteFlowId_t flowIdMax;

            for (it = m_flowStatsDl.begin(); it != m_flowStatsDl.end(); it++)
            {
                //check the slice ID here...
                //std::cout << "rb:" << i <<"rnti:" << (*it).first << " slice ID:"<< (*it).second.sliceId << std::endl;
                RbgSliceInfo info = GetRbgSliceInfo (rbgNum-1-i); //we check RB index in reverse order since HARQ may borrows RBs from the begining.
                uint64_t maxRbgs = UINT64_MAX;
                if (m_sliceInfoMap.find((*it).second.sliceId) != m_sliceInfoMap.end())
                {
                    maxRbgs = m_sliceInfoMap[(*it).second.sliceId]->m_maxRbg;
                }
                uint64_t usedRbgs = 0;
                if (sliceRbgUsageMap.find((*it).second.sliceId) != sliceRbgUsageMap.end())
                {
                    usedRbgs = sliceRbgUsageMap[(*it).second.sliceId];
                    //std::cout << "run "<< run <<" rnti:" << (*it).first << " slice ID:"<< (*it).second.sliceId 
                    //    << " rbg usage:" << usedRbgs << " rbg max:" <<  maxRbgs
                    //    << " rbg:" << i << " is scheduled for slice:" << info.second << " as " << info.first << std::endl;
                }

                if (run == 0)
                {
                    //first run schedules the dedicated and prioritized RGBs
                    
                    //if dedicated or prioritized rbg, the info.second returns the slice id. othwer wise, it returns UINT32_MAX
                    if (info.second != (*it).second.sliceId)//this RBG not schedule for this slice
                    {
                        continue;
                    }

                }
                else if (run == 1)
                {
                    //the second run treat the un-used prioritized RGBs as shared RGBs. we will schedule all shared RBs

                    if(info.first == DEDICATED_RBG) //this is a dedicated RGB, cannot schedule it in the second run
                    {
                        continue;
                    }
                    else
                    {
                        //check if the slice usage is under tha max usage ratio...
                        if (usedRbgs >= maxRbgs)//rbg usage is equal or greater than the max RBGs, cannot schedule it in this run
                        {
                            continue;
                        }

                    }

                }
                else
                {
                    NS_FATAL_ERROR("only need 2 runs to schedule all RBGs");
                }
                //std::cout << "scheduled imsi " << (*it).second.imsi << std::endl;
                if ((m_ffrSapProvider->IsDlRbgAvailableForUe(i, (*it).first)) == false)
                {
                    continue;
                }

                std::set<uint16_t>::iterator itRnti = rntiAllocated.find((*it).first);
                if ((itRnti != rntiAllocated.end()) || (!HarqProcessAvailability((*it).first)))
                {
                    // UE already allocated for HARQ or without HARQ process available -> drop it
                    if (itRnti != rntiAllocated.end())
                    {
                        NS_LOG_DEBUG(this << " RNTI discarded for HARQ tx"
                                          << (uint16_t)(*it).first);
                    }
                    if (!HarqProcessAvailability((*it).first))
                    {
                        NS_LOG_DEBUG(this << " RNTI discarded for HARQ id"
                                          << (uint16_t)(*it).first);
                    }
                    continue;
                }
                std::map<uint16_t, SbMeasResult_s>::iterator itCqi;
                itCqi = m_a30CqiRxed.find((*it).first);
                std::map<uint16_t, uint8_t>::iterator itTxMode;
                itTxMode = m_uesTxMode.find((*it).first);
                if (itTxMode == m_uesTxMode.end())
                {
                    NS_FATAL_ERROR("No Transmission Mode info on user " << (*it).first);
                }
                auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);
                std::vector<uint8_t> sbCqi;
                if (itCqi == m_a30CqiRxed.end())
                {
                    for (uint8_t k = 0; k < nLayer; k++)
                    {
                        sbCqi.push_back(1); // start with lowest value
                    }
                }
                else
                {
                    sbCqi = (*itCqi).second.m_higherLayerSelected.at(i).m_sbCqi;
                }
                uint8_t cqi1 = sbCqi.at(0);
                uint8_t cqi2 = 0;
                if (sbCqi.size() > 1)
                {
                    cqi2 = sbCqi.at(1);
                }

                if ((cqi1 > 0) ||
                    (cqi2 > 0)) // CQI == 0 means "out of range" (see table 7.2.3-1 of 36.213)
                {
                    RefreshSlicePriority();
                    if (PriorityAwareLcActivePerFlow((*it).first) > 0)
                    {

                        std::map <LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator itBufReq;
                        for (itBufReq = m_rlcBufferReq.begin (); itBufReq != m_rlcBufferReq.end (); itBufReq++)//User RLC buffer request exits
                        {
                            if((*itBufReq).first.m_rnti != (*it).first)
                            {
                                continue;
                            }   

                            uint32_t scheduledBytes = 0;//bytes already scheduled for this user of this lc
                            if(m_scheduledBytesPerLc.find((*itBufReq).first) != m_scheduledBytesPerLc.end())
                            {
                                scheduledBytes = m_scheduledBytesPerLc[(*itBufReq).first];
                            }

                            uint64_t buffReqBytes = (*itBufReq).second.m_rlcTransmissionQueueSize + (*itBufReq).second.m_rlcRetransmissionQueueSize + (*itBufReq).second.m_rlcStatusPduSize;

                            //the following if check if the scheduled bytes is smaller than the buffer size. Else if scheduled bytes is larger, skip this user
                            if (((*itBufReq).first.m_rnti == (*it).first)
                                && (buffReqBytes > scheduledBytes))
                            {
                                // this UE has data to transmit
                                double achievableRate = 0.0;
                                uint8_t mcs = 0;
                                for (uint8_t k = 0; k < nLayer; k++)
                                {
                                    if (sbCqi.size() > k)
                                    {
                                        mcs = m_amc->GetMcsFromCqi(sbCqi.at(k));
                                    }
                                    else
                                    {
                                        // no info on this subband -> worst MCS
                                        mcs = 0;
                                    }
                                    achievableRate += ((m_amc->GetDlTbSizeFromMcs(mcs, rbgSize) / 8) /
                                                    0.001); // = TB size / TTI
                                }

                                double rcqi = 0;
                                if (info.second == (*it).second.sliceId)//this RBG is schedule for this slice.
                                {
                                    //since we always schedule the reserve RBG first (run 0), the m_scheduledThroughput equals the scheduled throughput for this slice...
                                    /*if(m_scheduledBytesPerLc.find((*itBufReq).first) == m_scheduledBytesPerLc.end())//no scheduled throughput for this user
                                    {
                                        rcqi = achievableRate / (*it).second.lastAveragedSlicedThroughput;
                                    }
                                    else
                                    {
                                        //rcqi now also consider the throughput scheduled for this slice in this interval.
                                        rcqi = achievableRate / ((*it).second.lastAveragedSlicedThroughput + m_scheduledBytesPerLc[(*itBufReq).first]*1000);
                                    }*/
                                    //we can continue using the old formula since we will not schedule users with empty tx buffer.
                                    rcqi = achievableRate / (*it).second.lastAveragedSlicedThroughput; //rcqi = achievablerate / SlicedThroughput
                                }
                                else//shared RBG, including the unused priotized RBGs. Use the default algorithm.
                                {
                                    rcqi = achievableRate / std::max(1.0, ((*it).second.lastAveragedThroughput - (*it).second.lastAveragedSlicedThroughput)); //rcqi = achievablerate / SharedThroughput
                                }

                                NS_LOG_INFO(this << " RNTI " << (*it).first << " MCS " << (uint32_t)mcs
                                                << " achievableRate " << achievableRate << " avgThr "
                                                << (*it).second.lastAveragedThroughput << " RCQI "
                                                << rcqi);

                                if (rcqi > rcqiMax)
                                {
                                    rcqiMax = rcqi;
                                    itMax = it;
                                    achievableRateMax = achievableRate;
                                    flowIdMax = (*itBufReq).first;
                                }
                            }
                        }
                    }
                } // end if cqi
            }     // end for m_rlcBufferReq

            if (itMax == m_flowStatsDl.end())
            {
                // no UE available for this RB
                NS_LOG_INFO(this << " any UE found");
            }
            else
            {
                rbgMap.at(i) = true;
                std::map<uint16_t, std::vector<uint16_t>>::iterator itMap;
                itMap = allocationMap.find((*itMax).first);
                if (itMap == allocationMap.end())
                {
                    // insert new element
                    std::vector<uint16_t> tempMap;
                    tempMap.push_back(i);
                    allocationMap.insert(
                        std::pair<uint16_t, std::vector<uint16_t>>((*itMax).first, tempMap));
                }
                else
                {
                    (*itMap).second.push_back(i);
                }

                //insert to slice allocation map
                if (run == 0) //run 0 is for sliced RBs
                {
                    auto sliceItMap = sliceAllocationMap.find((*itMax).first);
                    if (sliceItMap == sliceAllocationMap.end())
                    {
                        // insert new element
                        std::vector<uint16_t> tempMap;
                        tempMap.push_back(i);
                        sliceAllocationMap.insert(
                            std::pair<uint16_t, std::vector<uint16_t>>((*itMax).first, tempMap));
                    }
                    else
                    {
                        (*sliceItMap).second.push_back(i);
                    }
                }

                NS_LOG_INFO(this << " UE assigned " << (*itMax).first);
                //std::cout << this << " UE IMSI: " << (*itMax).second.imsi << " to RB:" << i << " in run:" << run << std::endl;

                if(m_scheduledBytesPerLc.find(flowIdMax) == m_scheduledBytesPerLc.end())
                {
                    m_scheduledBytesPerLc[flowIdMax] = 0;
                }
                
                m_scheduledBytesPerLc[flowIdMax] += achievableRateMax/1000; //increase scheduled bytes after assign a UE

                //refresh after the schedule throughput updates...
                RefreshSlicePriority();

                //update slice rbg usage
                auto iter = sliceRbgUsageMap.find((*itMax).second.sliceId);
                if (iter == sliceRbgUsageMap.end())
                {
                    sliceRbgUsageMap[(*itMax).second.sliceId] = 0;
                    iter = sliceRbgUsageMap.find((*itMax).second.sliceId);
                }

                if(m_flowStatsDl.find((*itMax).first) == m_flowStatsDl.end())
                {
                    NS_FATAL_ERROR("cannot find this rinti:" <<(*itMax).first << " in the m_flowStatsDl");
                }
                uint64_t imsi = m_flowStatsDl[(*itMax).first].imsi;
                std::map<uint64_t, int>::iterator itRb = m_usedDataRbsMap.find(imsi);
                if(itRb != m_usedDataRbsMap.end())
                {
                    m_usedDataRbsMap[imsi] += rbgSize;
                }
                else
                {
                    m_usedDataRbsMap[imsi] = rbgSize;
                }

                iter->second += 1;
                //std::cout <<"[ADD] slice: " << iter->first <<" rbg usage:" << iter->second << std::endl;

            }
        } // end for RBG free
    }     // end for RBGs
    } //end for 2 runs.
    m_totalRbs += rbgSize*rbgNum;


    /*auto iter1 = sliceRbgUsageMap.begin();
    while (iter1 != sliceRbgUsageMap.end())
    {
        std::cout << " slice ID:" << (*iter1).first << " RBG:" << (*iter1).second << std::endl;
        iter1++;
    }*/

    /*auto iter2 = m_usedHarqRbsMap.begin();
    while (iter2 != m_usedHarqRbsMap.end())
    {
        std::cout << " IMSI:" << (*iter2).first << " RBG:" << (*iter2).second << std::endl;
        iter2++;
    }*/
    //m_usedHarqRbsMap.clear();

    /*auto iter3 = m_usedDataRbsMap.begin();
    while (iter3 != m_usedDataRbsMap.end())
    {
        std::cout << " IMSI:" << (*iter3).first << " RBG:" << (*iter3).second << std::endl;
        iter3++;
    }*/
    //m_usedDataRbsMap.clear();

    // reset TTI stats of users
    std::map<uint16_t, nsPfsFlowPerf_t>::iterator itStats;
    for (itStats = m_flowStatsDl.begin(); itStats != m_flowStatsDl.end(); itStats++)
    {
        (*itStats).second.lastTtiBytesTrasmitted = 0;
        (*itStats).second.lastTtiSlicedBytesTrasmitted = 0;
    }

    // generate the transmission opportunities by grouping the RBGs of the same RNTI and
    // creating the correspondent DCIs
    std::map<uint16_t, std::vector<uint16_t>>::iterator itMap = allocationMap.begin();
    while (itMap != allocationMap.end())
    {
        // create new BuildDataListElement_s for this LC
        BuildDataListElement_s newEl;
        newEl.m_rnti = (*itMap).first;
        // create the DlDciListElement_s
        DlDciListElement_s newDci;
        newDci.m_rnti = (*itMap).first;
        newDci.m_harqProcess = UpdateHarqProcessId((*itMap).first);

        //TODO: check the exact bytes for each lc, do not equal share!!!
        uint16_t lcActives = PriorityAwareLcActivePerFlow((*itMap).first);
        NS_LOG_INFO(this << "Allocate user " << newEl.m_rnti << " rbg " << lcActives);
        if (lcActives == 0)
        {
            // Set to max value, to avoid divide by 0 below
            lcActives = (uint16_t)65535; // UINT16_MAX;
        }
        uint16_t RgbPerRnti = (*itMap).second.size();
        std::map<uint16_t, SbMeasResult_s>::iterator itCqi;
        itCqi = m_a30CqiRxed.find((*itMap).first);
        std::map<uint16_t, uint8_t>::iterator itTxMode;
        itTxMode = m_uesTxMode.find((*itMap).first);
        if (itTxMode == m_uesTxMode.end())
        {
            NS_FATAL_ERROR("No Transmission Mode info on user " << (*itMap).first);
        }
        auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);
        std::vector<uint8_t> worstCqi(2, 15);
        if (itCqi != m_a30CqiRxed.end())
        {
            for (std::size_t k = 0; k < (*itMap).second.size(); k++)
            {
                if ((*itCqi).second.m_higherLayerSelected.size() > (*itMap).second.at(k))
                {
                    NS_LOG_INFO(this << " RBG " << (*itMap).second.at(k) << " CQI "
                                     << (uint16_t)((*itCqi)
                                                       .second.m_higherLayerSelected
                                                       .at((*itMap).second.at(k))
                                                       .m_sbCqi.at(0)));
                    for (uint8_t j = 0; j < nLayer; j++)
                    {
                        if ((*itCqi)
                                .second.m_higherLayerSelected.at((*itMap).second.at(k))
                                .m_sbCqi.size() > j)
                        {
                            if (((*itCqi)
                                     .second.m_higherLayerSelected.at((*itMap).second.at(k))
                                     .m_sbCqi.at(j)) < worstCqi.at(j))
                            {
                                worstCqi.at(j) =
                                    ((*itCqi)
                                         .second.m_higherLayerSelected.at((*itMap).second.at(k))
                                         .m_sbCqi.at(j));
                            }
                        }
                        else
                        {
                            // no CQI for this layer of this suband -> worst one
                            worstCqi.at(j) = 1;
                        }
                    }
                }
                else
                {
                    for (uint8_t j = 0; j < nLayer; j++)
                    {
                        worstCqi.at(j) = 1; // try with lowest MCS in RBG with no info on channel
                    }
                }
            }
        }
        else
        {
            for (uint8_t j = 0; j < nLayer; j++)
            {
                worstCqi.at(j) = 1; // try with lowest MCS in RBG with no info on channel
            }
        }
        for (uint8_t j = 0; j < nLayer; j++)
        {
            NS_LOG_INFO(this << " Layer " << (uint16_t)j << " CQI selected "
                             << (uint16_t)worstCqi.at(j));
        }
        uint32_t bytesTxed = 0;
        uint32_t slicedBytesTxed = 0;
        for (uint8_t j = 0; j < nLayer; j++)
        {
            newDci.m_mcs.push_back(m_amc->GetMcsFromCqi(worstCqi.at(j)));
            int tbSize = (m_amc->GetDlTbSizeFromMcs(newDci.m_mcs.at(j), RgbPerRnti * rbgSize) /
                          8); // (size of TB in bytes according to table 7.1.7.2.1-1 of 36.213)
            newDci.m_tbsSize.push_back(tbSize);
            NS_LOG_INFO(this << " Layer " << (uint16_t)j << " MCS selected"
                             << m_amc->GetMcsFromCqi(worstCqi.at(j)));
            bytesTxed += tbSize;
            
            auto sliceIter = sliceAllocationMap.find((*itMap).first);//check if sliced RB exits
            if (sliceIter != sliceAllocationMap.end())
            {
                ///sliced rbg usage should be smaller or equal to min rbg (dedicated + prioritized).
                uint16_t sliceRgbPerRnti = (*sliceIter).second.size();
                if (m_sliceInfoMap.find(m_flowStatsDl[(*itMap).first].sliceId) != m_sliceInfoMap.end())
                {
                    sliceRgbPerRnti =  std::min(RgbPerRnti, (uint16_t)m_sliceInfoMap[m_flowStatsDl[(*itMap).first].sliceId]->m_minRbg);
                }
                int sliceTbSize = (m_amc->GetDlTbSizeFromMcs(newDci.m_mcs.at(j), sliceRgbPerRnti * rbgSize) /
                            8); // (size of TB in bytes according to table 7.1.7.2.1-1 of 36.213)
                slicedBytesTxed += sliceTbSize;
            }

        }

        newDci.m_resAlloc = 0; // only allocation type 0 at this stage
        newDci.m_rbBitmap = 0; // TBD (32 bit bitmap see 7.1.6 of 36.213)
        uint32_t rbgMask = 0;
        for (std::size_t k = 0; k < (*itMap).second.size(); k++)
        {
            rbgMask = rbgMask + (0x1 << (*itMap).second.at(k));
            NS_LOG_INFO(this << " Allocated RBG " << (*itMap).second.at(k));
        }
        newDci.m_rbBitmap = rbgMask; // (32 bit bitmap see 7.1.6 of 36.213)

        // create the rlc PDUs -> equally divide resources among actives LCs
        std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator
            itBufReq;
        for (itBufReq = m_rlcBufferReq.begin(); itBufReq != m_rlcBufferReq.end(); itBufReq++)
        {
            if (((*itBufReq).first.m_rnti == (*itMap).first) &&
                (((*itBufReq).second.m_rlcTransmissionQueueSize > 0) ||
                 ((*itBufReq).second.m_rlcRetransmissionQueueSize > 0) ||
                 ((*itBufReq).second.m_rlcStatusPduSize > 0)))
            {
                std::vector<struct RlcPduListElement_s> newRlcPduLe;
                for (uint8_t j = 0; j < nLayer; j++)
                {
                    RlcPduListElement_s newRlcEl;
                    newRlcEl.m_logicalChannelIdentity = (*itBufReq).first.m_lcId;
                    newRlcEl.m_size = newDci.m_tbsSize.at(j) / lcActives;
                    NS_LOG_INFO(this << " LCID " << (uint32_t)newRlcEl.m_logicalChannelIdentity
                                     << " size " << newRlcEl.m_size << " layer " << (uint16_t)j);
                    newRlcPduLe.push_back(newRlcEl);
                    UpdateDlRlcBufferInfo(newDci.m_rnti,
                                          newRlcEl.m_logicalChannelIdentity,
                                          newRlcEl.m_size);
                    if (m_harqOn == true)
                    {
                        // store RLC PDU list for HARQ
                        std::map<uint16_t, DlHarqRlcPduListBuffer_t>::iterator itRlcPdu =
                            m_dlHarqProcessesRlcPduListBuffer.find((*itMap).first);
                        if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
                        {
                            NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                                           << (*itMap).first);
                        }
                        (*itRlcPdu).second.at(j).at(newDci.m_harqProcess).push_back(newRlcEl);
                    }
                }
                newEl.m_rlcPduList.push_back(newRlcPduLe);
            }
            if ((*itBufReq).first.m_rnti > (*itMap).first)
            {
                break;
            }
        }
        for (uint8_t j = 0; j < nLayer; j++)
        {
            newDci.m_ndi.push_back(1);
            newDci.m_rv.push_back(0);
        }

        newDci.m_tpc = m_ffrSapProvider->GetTpc((*itMap).first);

        newEl.m_dci = newDci;

        if (m_harqOn == true)
        {
            // store DCI for HARQ
            std::map<uint16_t, DlHarqProcessesDciBuffer_t>::iterator itDci =
                m_dlHarqProcessesDciBuffer.find(newEl.m_rnti);
            if (itDci == m_dlHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in DCI HARQ buffer for RNTI "
                               << newEl.m_rnti);
            }
            (*itDci).second.at(newDci.m_harqProcess) = newDci;
            // refresh timer
            std::map<uint16_t, DlHarqProcessesTimer_t>::iterator itHarqTimer =
                m_dlHarqProcessesTimer.find(newEl.m_rnti);
            if (itHarqTimer == m_dlHarqProcessesTimer.end())
            {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)newEl.m_rnti);
            }
            (*itHarqTimer).second.at(newDci.m_harqProcess) = 0;
        }

        // ...more parameters -> ignored in this version

        ret.m_buildDataList.push_back(newEl);
        // update UE stats
        std::map<uint16_t, nsPfsFlowPerf_t>::iterator it;
        it = m_flowStatsDl.find((*itMap).first);
        if (it != m_flowStatsDl.end())
        {
            (*it).second.lastTtiBytesTrasmitted = bytesTxed;
            (*it).second.lastTtiSlicedBytesTrasmitted = slicedBytesTxed;
            NS_LOG_INFO(this << " UE total bytes txed " << (*it).second.lastTtiBytesTrasmitted);
        }
        else
        {
            NS_FATAL_ERROR(this << " No Stats for this allocated UE");
        }

        itMap++;
    }                               // end while allocation
    ret.m_nrOfPdcchOfdmSymbols = 1; /// \todo check correct value according the DCIs txed

    // update UEs stats
    NS_LOG_INFO(this << " Update UEs statistics");
    for (itStats = m_flowStatsDl.begin(); itStats != m_flowStatsDl.end(); itStats++)
    {
        (*itStats).second.totalBytesTransmitted += (*itStats).second.lastTtiBytesTrasmitted;
        // update average throughput (see eq. 12.3 of Sec 12.3.1.2 of LTE – The UMTS Long Term
        // Evolution, Ed Wiley)
        (*itStats).second.lastAveragedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiBytesTrasmitted / 0.001));
        (*itStats).second.lastAveragedSlicedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedSlicedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiSlicedBytesTrasmitted / 0.001));
        NS_LOG_INFO(this << " UE total bytes " << (*itStats).second.totalBytesTransmitted);
        NS_LOG_INFO(this << " UE average throughput " << (*itStats).second.lastAveragedThroughput);
        (*itStats).second.lastTtiBytesTrasmitted = 0;
        (*itStats).second.lastTtiSlicedBytesTrasmitted = 0;

    }

    m_schedSapUser->SchedDlConfigInd(ret);
}

void
NsPfFfMacScheduler::DoSchedDlRachInfoReq(
    const struct FfMacSchedSapProvider::SchedDlRachInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    m_rachList = params.m_rachList;
}

void
NsPfFfMacScheduler::DoSchedDlCqiInfoReq(
    const struct FfMacSchedSapProvider::SchedDlCqiInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    m_ffrSapProvider->ReportDlCqiInfo(params);

    for (unsigned int i = 0; i < params.m_cqiList.size(); i++)
    {
        if (params.m_cqiList.at(i).m_cqiType == CqiListElement_s::P10)
        {
            NS_LOG_LOGIC("wideband CQI " << (uint32_t)params.m_cqiList.at(i).m_wbCqi.at(0)
                                         << " reported");
            std::map<uint16_t, uint8_t>::iterator it;
            uint16_t rnti = params.m_cqiList.at(i).m_rnti;
            it = m_p10CqiRxed.find(rnti);
            if (it == m_p10CqiRxed.end())
            {
                // create the new entry
                m_p10CqiRxed.insert(std::pair<uint16_t, uint8_t>(
                    rnti,
                    params.m_cqiList.at(i).m_wbCqi.at(0))); // only codeword 0 at this stage (SISO)
                // generate correspondent timer
                m_p10CqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
            }
            else
            {
                // update the CQI value and refresh correspondent timer
                (*it).second = params.m_cqiList.at(i).m_wbCqi.at(0);
                // update correspondent timer
                std::map<uint16_t, uint32_t>::iterator itTimers;
                itTimers = m_p10CqiTimers.find(rnti);
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        else if (params.m_cqiList.at(i).m_cqiType == CqiListElement_s::A30)
        {
            // subband CQI reporting high layer configured
            std::map<uint16_t, SbMeasResult_s>::iterator it;
            uint16_t rnti = params.m_cqiList.at(i).m_rnti;
            it = m_a30CqiRxed.find(rnti);
            if (it == m_a30CqiRxed.end())
            {
                // create the new entry
                m_a30CqiRxed.insert(
                    std::pair<uint16_t, SbMeasResult_s>(rnti,
                                                        params.m_cqiList.at(i).m_sbMeasResult));
                m_a30CqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
            }
            else
            {
                // update the CQI value and refresh correspondent timer
                (*it).second = params.m_cqiList.at(i).m_sbMeasResult;
                std::map<uint16_t, uint32_t>::iterator itTimers;
                itTimers = m_a30CqiTimers.find(rnti);
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        else
        {
            NS_LOG_ERROR(this << " CQI type unknown");
        }
    }
}

double
NsPfFfMacScheduler::EstimateUlSinr(uint16_t rnti, uint16_t rb)
{
    std::map<uint16_t, std::vector<double>>::iterator itCqi = m_ueCqi.find(rnti);
    if (itCqi == m_ueCqi.end())
    {
        // no cqi info about this UE
        return (NO_SINR);
    }
    else
    {
        // take the average SINR value among the available
        double sinrSum = 0;
        unsigned int sinrNum = 0;
        for (uint32_t i = 0; i < m_cschedCellConfig.m_ulBandwidth; i++)
        {
            double sinr = (*itCqi).second.at(i);
            if (sinr != NO_SINR)
            {
                sinrSum += sinr;
                sinrNum++;
            }
        }
        double estimatedSinr = (sinrNum > 0) ? (sinrSum / sinrNum) : DBL_MAX;
        // store the value
        (*itCqi).second.at(rb) = estimatedSinr;
        return (estimatedSinr);
    }
}

void
NsPfFfMacScheduler::DoSchedUlTriggerReq(
    const struct FfMacSchedSapProvider::SchedUlTriggerReqParameters& params)
{
    NS_LOG_FUNCTION(this << " UL - Frame no. " << (params.m_sfnSf >> 4) << " subframe no. "
                         << (0xF & params.m_sfnSf) << " size " << params.m_ulInfoList.size());

    RefreshUlCqiMaps();
    m_ffrSapProvider->ReportUlCqiInfo(m_ueCqi);

    // Generate RBs map
    FfMacSchedSapUser::SchedUlConfigIndParameters ret;
    std::vector<bool> rbMap;
    uint16_t rbAllocatedNum = 0;
    std::set<uint16_t> rntiAllocated;
    std::vector<uint16_t> rbgAllocationMap;
    // update with RACH allocation map
    rbgAllocationMap = m_rachAllocationMap;
    // rbgAllocationMap.resize (m_cschedCellConfig.m_ulBandwidth, 0);
    m_rachAllocationMap.clear();
    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);

    rbMap.resize(m_cschedCellConfig.m_ulBandwidth, false);
    rbMap = m_ffrSapProvider->GetAvailableUlRbg();

    for (std::vector<bool>::iterator it = rbMap.begin(); it != rbMap.end(); it++)
    {
        if ((*it) == true)
        {
            rbAllocatedNum++;
        }
    }

    uint8_t minContinuousUlBandwidth = m_ffrSapProvider->GetMinContinuousUlBandwidth();
    uint8_t ffrUlBandwidth = m_cschedCellConfig.m_ulBandwidth - rbAllocatedNum;

    // remove RACH allocation
    for (uint16_t i = 0; i < m_cschedCellConfig.m_ulBandwidth; i++)
    {
        if (rbgAllocationMap.at(i) != 0)
        {
            rbMap.at(i) = true;
            NS_LOG_DEBUG(this << " Allocated for RACH " << i);
        }
    }

    if (m_harqOn == true)
    {
        //   Process UL HARQ feedback

        for (std::size_t i = 0; i < params.m_ulInfoList.size(); i++)
        {
            if (params.m_ulInfoList.at(i).m_receptionStatus == UlInfoListElement_s::NotOk)
            {
                // retx correspondent block: retrieve the UL-DCI
                uint16_t rnti = params.m_ulInfoList.at(i).m_rnti;
                std::map<uint16_t, uint8_t>::iterator itProcId =
                    m_ulHarqCurrentProcessId.find(rnti);
                if (itProcId == m_ulHarqCurrentProcessId.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                }
                uint8_t harqId = (uint8_t)((*itProcId).second - HARQ_PERIOD) % HARQ_PROC_NUM;
                NS_LOG_INFO(this << " UL-HARQ retx RNTI " << rnti << " harqId " << (uint16_t)harqId
                                 << " i " << i << " size " << params.m_ulInfoList.size());
                std::map<uint16_t, UlHarqProcessesDciBuffer_t>::iterator itHarq =
                    m_ulHarqProcessesDciBuffer.find(rnti);
                if (itHarq == m_ulHarqProcessesDciBuffer.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                    continue;
                }
                UlDciListElement_s dci = (*itHarq).second.at(harqId);
                std::map<uint16_t, UlHarqProcessesStatus_t>::iterator itStat =
                    m_ulHarqProcessesStatus.find(rnti);
                if (itStat == m_ulHarqProcessesStatus.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                }
                if ((*itStat).second.at(harqId) >= 3)
                {
                    NS_LOG_INFO("Max number of retransmissions reached (UL)-> drop process");
                    continue;
                }
                bool free = true;
                for (int j = dci.m_rbStart; j < dci.m_rbStart + dci.m_rbLen; j++)
                {
                    if (rbMap.at(j) == true)
                    {
                        free = false;
                        NS_LOG_INFO(this << " BUSY " << j);
                    }
                }
                if (free)
                {
                    // retx on the same RBs
                    for (int j = dci.m_rbStart; j < dci.m_rbStart + dci.m_rbLen; j++)
                    {
                        rbMap.at(j) = true;
                        rbgAllocationMap.at(j) = dci.m_rnti;
                        NS_LOG_INFO("\tRB " << j);
                        rbAllocatedNum++;
                    }
                    NS_LOG_INFO(this << " Send retx in the same RBs " << (uint16_t)dci.m_rbStart
                                     << " to " << dci.m_rbStart + dci.m_rbLen << " RV "
                                     << (*itStat).second.at(harqId) + 1);
                }
                else
                {
                    NS_LOG_INFO("Cannot allocate retx due to RACH allocations for UE " << rnti);
                    continue;
                }
                dci.m_ndi = 0;
                // Update HARQ buffers with new HarqId
                (*itStat).second.at((*itProcId).second) = (*itStat).second.at(harqId) + 1;
                (*itStat).second.at(harqId) = 0;
                (*itHarq).second.at((*itProcId).second) = dci;
                ret.m_dciList.push_back(dci);
                rntiAllocated.insert(dci.m_rnti);
            }
            else
            {
                NS_LOG_INFO(this << " HARQ-ACK feedback from RNTI "
                                 << params.m_ulInfoList.at(i).m_rnti);
            }
        }
    }

    std::map<uint16_t, uint32_t>::iterator it;
    int nflows = 0;

    for (it = m_ceBsrRxed.begin(); it != m_ceBsrRxed.end(); it++)
    {
        std::set<uint16_t>::iterator itRnti = rntiAllocated.find((*it).first);
        // select UEs with queues not empty and not yet allocated for HARQ
        if (((*it).second > 0) && (itRnti == rntiAllocated.end()))
        {
            nflows++;
        }
    }

    if (nflows == 0)
    {
        if (!ret.m_dciList.empty())
        {
            m_allocationMaps.insert(
                std::pair<uint16_t, std::vector<uint16_t>>(params.m_sfnSf, rbgAllocationMap));
            m_schedSapUser->SchedUlConfigInd(ret);
        }

        return; // no flows to be scheduled
    }

    // Divide the remaining resources equally among the active users starting from the subsequent
    // one served last scheduling trigger
    uint16_t tempRbPerFlow = (ffrUlBandwidth) / (nflows + rntiAllocated.size());
    uint16_t rbPerFlow =
        (minContinuousUlBandwidth < tempRbPerFlow) ? minContinuousUlBandwidth : tempRbPerFlow;

    if (rbPerFlow < 3)
    {
        rbPerFlow = 3; // at least 3 rbg per flow (till available resource) to ensure TxOpportunity
                       // >= 7 bytes
    }

    int rbAllocated = 0;

    std::map<uint16_t, nsPfsFlowPerf_t>::iterator itStats;
    if (m_nextRntiUl != 0)
    {
        for (it = m_ceBsrRxed.begin(); it != m_ceBsrRxed.end(); it++)
        {
            if ((*it).first == m_nextRntiUl)
            {
                break;
            }
        }
        if (it == m_ceBsrRxed.end())
        {
            NS_LOG_ERROR(this << " no user found");
        }
    }
    else
    {
        it = m_ceBsrRxed.begin();
        m_nextRntiUl = (*it).first;
    }
    do
    {
        std::set<uint16_t>::iterator itRnti = rntiAllocated.find((*it).first);
        if ((itRnti != rntiAllocated.end()) || ((*it).second == 0))
        {
            // UE already allocated for UL-HARQ -> skip it
            NS_LOG_DEBUG(this << " UE already allocated in HARQ -> discarded, RNTI "
                              << (*it).first);
            it++;
            if (it == m_ceBsrRxed.end())
            {
                // restart from the first
                it = m_ceBsrRxed.begin();
            }
            continue;
        }
        if (rbAllocated + rbPerFlow - 1 > m_cschedCellConfig.m_ulBandwidth)
        {
            // limit to physical resources last resource assignment
            rbPerFlow = m_cschedCellConfig.m_ulBandwidth - rbAllocated;
            // at least 3 rbg per flow to ensure TxOpportunity >= 7 bytes
            if (rbPerFlow < 3)
            {
                // terminate allocation
                rbPerFlow = 0;
            }
        }

        rbAllocated = 0;
        UlDciListElement_s uldci;
        uldci.m_rnti = (*it).first;
        uldci.m_rbLen = rbPerFlow;
        bool allocated = false;

        while ((!allocated) && ((rbAllocated + rbPerFlow - m_cschedCellConfig.m_ulBandwidth) < 1) &&
               (rbPerFlow != 0))
        {
            // check availability
            bool free = true;
            for (int j = rbAllocated; j < rbAllocated + rbPerFlow; j++)
            {
                if (rbMap.at(j) == true)
                {
                    free = false;
                    break;
                }
                if ((m_ffrSapProvider->IsUlRbgAvailableForUe(j, (*it).first)) == false)
                {
                    free = false;
                    break;
                }
            }
            if (free)
            {
                NS_LOG_INFO(this << "RNTI: " << (*it).first << " RB Allocated " << rbAllocated
                                 << " rbPerFlow " << rbPerFlow << " flows " << nflows);
                uldci.m_rbStart = rbAllocated;

                for (int j = rbAllocated; j < rbAllocated + rbPerFlow; j++)
                {
                    rbMap.at(j) = true;
                    // store info on allocation for managing ul-cqi interpretation
                    rbgAllocationMap.at(j) = (*it).first;
                }
                rbAllocated += rbPerFlow;
                allocated = true;
                break;
            }
            rbAllocated++;
            if (rbAllocated + rbPerFlow - 1 > m_cschedCellConfig.m_ulBandwidth)
            {
                // limit to physical resources last resource assignment
                rbPerFlow = m_cschedCellConfig.m_ulBandwidth - rbAllocated;
                // at least 3 rbg per flow to ensure TxOpportunity >= 7 bytes
                if (rbPerFlow < 3)
                {
                    // terminate allocation
                    rbPerFlow = 0;
                }
            }
        }
        if (!allocated)
        {
            // unable to allocate new resource: finish scheduling
            m_nextRntiUl = (*it).first;
            //          if (ret.m_dciList.size () > 0)
            //            {
            //              m_schedSapUser->SchedUlConfigInd (ret);
            //            }
            //          m_allocationMaps.insert (std::pair <uint16_t, std::vector <uint16_t> >
            //          (params.m_sfnSf, rbgAllocationMap)); return;
            break;
        }

        std::map<uint16_t, std::vector<double>>::iterator itCqi = m_ueCqi.find((*it).first);
        int cqi = 0;
        if (itCqi == m_ueCqi.end())
        {
            // no cqi info about this UE
            uldci.m_mcs = 0; // MCS 0 -> UL-AMC TBD
        }
        else
        {
            // take the lowest CQI value (worst RB)
            NS_ABORT_MSG_IF((*itCqi).second.empty(),
                            "CQI of RNTI = " << (*it).first << " has expired");
            double minSinr = (*itCqi).second.at(uldci.m_rbStart);
            if (minSinr == NO_SINR)
            {
                minSinr = EstimateUlSinr((*it).first, uldci.m_rbStart);
            }
            for (uint16_t i = uldci.m_rbStart; i < uldci.m_rbStart + uldci.m_rbLen; i++)
            {
                double sinr = (*itCqi).second.at(i);
                if (sinr == NO_SINR)
                {
                    sinr = EstimateUlSinr((*it).first, i);
                }
                if (sinr < minSinr)
                {
                    minSinr = sinr;
                }
            }

            // translate SINR -> cqi: WILD ACK: same as DL
            double s = log2(1 + (std::pow(10, minSinr / 10) / ((-std::log(5.0 * 0.00005)) / 1.5)));
            cqi = m_amc->GetCqiFromSpectralEfficiency(s);
            if (cqi == 0)
            {
                it++;
                if (it == m_ceBsrRxed.end())
                {
                    // restart from the first
                    it = m_ceBsrRxed.begin();
                }
                NS_LOG_DEBUG(this << " UE discarded for CQI = 0, RNTI " << uldci.m_rnti);
                // remove UE from allocation map
                for (uint16_t i = uldci.m_rbStart; i < uldci.m_rbStart + uldci.m_rbLen; i++)
                {
                    rbgAllocationMap.at(i) = 0;
                }
                continue; // CQI == 0 means "out of range" (see table 7.2.3-1 of 36.213)
            }
            uldci.m_mcs = m_amc->GetMcsFromCqi(cqi);
        }

        uldci.m_tbSize = (m_amc->GetUlTbSizeFromMcs(uldci.m_mcs, rbPerFlow) / 8);
        UpdateUlRlcBufferInfo(uldci.m_rnti, uldci.m_tbSize);
        uldci.m_ndi = 1;
        uldci.m_cceIndex = 0;
        uldci.m_aggrLevel = 1;
        uldci.m_ueTxAntennaSelection = 3; // antenna selection OFF
        uldci.m_hopping = false;
        uldci.m_n2Dmrs = 0;
        uldci.m_tpc = 0;            // no power control
        uldci.m_cqiRequest = false; // only period CQI at this stage
        uldci.m_ulIndex = 0;        // TDD parameter
        uldci.m_dai = 1;            // TDD parameter
        uldci.m_freqHopping = 0;
        uldci.m_pdcchPowerOffset = 0; // not used
        ret.m_dciList.push_back(uldci);
        // store DCI for HARQ_PERIOD
        uint8_t harqId = 0;
        if (m_harqOn == true)
        {
            std::map<uint16_t, uint8_t>::iterator itProcId;
            itProcId = m_ulHarqCurrentProcessId.find(uldci.m_rnti);
            if (itProcId == m_ulHarqCurrentProcessId.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << uldci.m_rnti);
            }
            harqId = (*itProcId).second;
            std::map<uint16_t, UlHarqProcessesDciBuffer_t>::iterator itDci =
                m_ulHarqProcessesDciBuffer.find(uldci.m_rnti);
            if (itDci == m_ulHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in UL DCI HARQ buffer for RNTI "
                               << uldci.m_rnti);
            }
            (*itDci).second.at(harqId) = uldci;
            // Update HARQ process status (RV 0)
            std::map<uint16_t, UlHarqProcessesStatus_t>::iterator itStat =
                m_ulHarqProcessesStatus.find(uldci.m_rnti);
            if (itStat == m_ulHarqProcessesStatus.end())
            {
                NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) "
                             << uldci.m_rnti);
            }
            (*itStat).second.at(harqId) = 0;
        }

        NS_LOG_INFO(this << " UE Allocation RNTI " << (*it).first << " startPRB "
                         << (uint32_t)uldci.m_rbStart << " nPRB " << (uint32_t)uldci.m_rbLen
                         << " CQI " << cqi << " MCS " << (uint32_t)uldci.m_mcs << " TBsize "
                         << uldci.m_tbSize << " RbAlloc " << rbAllocated << " harqId "
                         << (uint16_t)harqId);

        // update TTI  UE stats
        itStats = m_flowStatsUl.find((*it).first);
        if (itStats != m_flowStatsUl.end())
        {
            (*itStats).second.lastTtiBytesTrasmitted = uldci.m_tbSize;
            (*itStats).second.lastTtiSlicedBytesTrasmitted = uldci.m_tbSize; //update after implement uplink...
        }
        else
        {
            NS_LOG_DEBUG(this << " No Stats for this allocated UE");
        }

        it++;
        if (it == m_ceBsrRxed.end())
        {
            // restart from the first
            it = m_ceBsrRxed.begin();
        }
        if ((rbAllocated == m_cschedCellConfig.m_ulBandwidth) || (rbPerFlow == 0))
        {
            // Stop allocation: no more PRBs
            m_nextRntiUl = (*it).first;
            break;
        }
    } while (((*it).first != m_nextRntiUl) && (rbPerFlow != 0));

    // Update global UE stats
    // update UEs stats
    for (itStats = m_flowStatsUl.begin(); itStats != m_flowStatsUl.end(); itStats++)
    {
        (*itStats).second.totalBytesTransmitted += (*itStats).second.lastTtiBytesTrasmitted;
        // update average throughput (see eq. 12.3 of Sec 12.3.1.2 of LTE – The UMTS Long Term
        // Evolution, Ed Wiley)
        (*itStats).second.lastAveragedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiBytesTrasmitted / 0.001));
        (*itStats).second.lastAveragedSlicedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedSlicedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiSlicedBytesTrasmitted / 0.001));
        NS_LOG_INFO(this << " UE total bytes " << (*itStats).second.totalBytesTransmitted);
        NS_LOG_INFO(this << " UE average throughput " << (*itStats).second.lastAveragedThroughput);
        (*itStats).second.lastTtiBytesTrasmitted = 0;
        (*itStats).second.lastTtiSlicedBytesTrasmitted = 0;
    }
    m_allocationMaps.insert(
        std::pair<uint16_t, std::vector<uint16_t>>(params.m_sfnSf, rbgAllocationMap));
    m_schedSapUser->SchedUlConfigInd(ret);
}

void
NsPfFfMacScheduler::DoSchedUlNoiseInterferenceReq(
    const struct FfMacSchedSapProvider::SchedUlNoiseInterferenceReqParameters& params)
{
    NS_LOG_FUNCTION(this);
}

void
NsPfFfMacScheduler::DoSchedUlSrInfoReq(
    const struct FfMacSchedSapProvider::SchedUlSrInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);
}

void
NsPfFfMacScheduler::DoSchedUlMacCtrlInfoReq(
    const struct FfMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    std::map<uint16_t, uint32_t>::iterator it;

    for (unsigned int i = 0; i < params.m_macCeList.size(); i++)
    {
        if (params.m_macCeList.at(i).m_macCeType == MacCeListElement_s::BSR)
        {
            // buffer status report
            // note that this scheduler does not differentiate the
            // allocation according to which LCGs have more/less bytes
            // to send.
            // Hence the BSR of different LCGs are just summed up to get
            // a total queue size that is used for allocation purposes.

            uint32_t buffer = 0;
            for (uint8_t lcg = 0; lcg < 4; ++lcg)
            {
                uint8_t bsrId = params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(lcg);
                buffer += BufferSizeLevelBsr::BsrId2BufferSize(bsrId);
            }

            uint16_t rnti = params.m_macCeList.at(i).m_rnti;
            NS_LOG_LOGIC(this << "RNTI=" << rnti << " buffer=" << buffer);
            it = m_ceBsrRxed.find(rnti);
            if (it == m_ceBsrRxed.end())
            {
                // create the new entry
                m_ceBsrRxed.insert(std::pair<uint16_t, uint32_t>(rnti, buffer));
            }
            else
            {
                // update the buffer size value
                (*it).second = buffer;
            }
        }
    }
}

void
NsPfFfMacScheduler::DoSchedUlCqiInfoReq(
    const struct FfMacSchedSapProvider::SchedUlCqiInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    m_ffrSapProvider->ReportUlCqiInfo(params);

    // retrieve the allocation for this subframe
    switch (m_ulCqiFilter)
    {
    case FfMacScheduler::SRS_UL_CQI: {
        // filter all the CQIs that are not SRS based
        if (params.m_ulCqi.m_type != UlCqi_s::SRS)
        {
            return;
        }
    }
    break;
    case FfMacScheduler::PUSCH_UL_CQI: {
        // filter all the CQIs that are not SRS based
        if (params.m_ulCqi.m_type != UlCqi_s::PUSCH)
        {
            return;
        }
    }
    break;

    default:
        NS_FATAL_ERROR("Unknown UL CQI type");
    }

    switch (params.m_ulCqi.m_type)
    {
    case UlCqi_s::PUSCH: {
        std::map<uint16_t, std::vector<uint16_t>>::iterator itMap;
        std::map<uint16_t, std::vector<double>>::iterator itCqi;
        NS_LOG_DEBUG(this << " Collect PUSCH CQIs of Frame no. " << (params.m_sfnSf >> 4)
                          << " subframe no. " << (0xF & params.m_sfnSf));
        itMap = m_allocationMaps.find(params.m_sfnSf);
        if (itMap == m_allocationMaps.end())
        {
            return;
        }
        for (uint32_t i = 0; i < (*itMap).second.size(); i++)
        {
            // convert from fixed point notation Sxxxxxxxxxxx.xxx to double
            double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(i));
            itCqi = m_ueCqi.find((*itMap).second.at(i));
            if (itCqi == m_ueCqi.end())
            {
                // create a new entry
                std::vector<double> newCqi;
                for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
                {
                    if (i == j)
                    {
                        newCqi.push_back(sinr);
                    }
                    else
                    {
                        // initialize with NO_SINR value.
                        newCqi.push_back(NO_SINR);
                    }
                }
                m_ueCqi.insert(
                    std::pair<uint16_t, std::vector<double>>((*itMap).second.at(i), newCqi));
                // generate correspondent timer
                m_ueCqiTimers.insert(
                    std::pair<uint16_t, uint32_t>((*itMap).second.at(i), m_cqiTimersThreshold));
            }
            else
            {
                // update the value
                (*itCqi).second.at(i) = sinr;
                NS_LOG_DEBUG(this << " RNTI " << (*itMap).second.at(i) << " RB " << i << " SINR "
                                  << sinr);
                // update correspondent timer
                std::map<uint16_t, uint32_t>::iterator itTimers;
                itTimers = m_ueCqiTimers.find((*itMap).second.at(i));
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        // remove obsolete info on allocation
        m_allocationMaps.erase(itMap);
    }
    break;
    case UlCqi_s::SRS: {
        NS_LOG_DEBUG(this << " Collect SRS CQIs of Frame no. " << (params.m_sfnSf >> 4)
                          << " subframe no. " << (0xF & params.m_sfnSf));
        // get the RNTI from vendor specific parameters
        uint16_t rnti = 0;
        NS_ASSERT(!params.m_vendorSpecificList.empty());
        for (std::size_t i = 0; i < params.m_vendorSpecificList.size(); i++)
        {
            if (params.m_vendorSpecificList.at(i).m_type == SRS_CQI_RNTI_VSP)
            {
                Ptr<SrsCqiRntiVsp> vsp =
                    DynamicCast<SrsCqiRntiVsp>(params.m_vendorSpecificList.at(i).m_value);
                rnti = vsp->GetRnti();
            }
        }
        std::map<uint16_t, std::vector<double>>::iterator itCqi;
        itCqi = m_ueCqi.find(rnti);
        if (itCqi == m_ueCqi.end())
        {
            // create a new entry
            std::vector<double> newCqi;
            for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
            {
                double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(j));
                newCqi.push_back(sinr);
                NS_LOG_INFO(this << " RNTI " << rnti << " new SRS-CQI for RB  " << j << " value "
                                 << sinr);
            }
            m_ueCqi.insert(std::pair<uint16_t, std::vector<double>>(rnti, newCqi));
            // generate correspondent timer
            m_ueCqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
        }
        else
        {
            // update the values
            for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
            {
                double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(j));
                (*itCqi).second.at(j) = sinr;
                NS_LOG_INFO(this << " RNTI " << rnti << " update SRS-CQI for RB  " << j << " value "
                                 << sinr);
            }
            // update correspondent timer
            std::map<uint16_t, uint32_t>::iterator itTimers;
            itTimers = m_ueCqiTimers.find(rnti);
            (*itTimers).second = m_cqiTimersThreshold;
        }
    }
    break;
    case UlCqi_s::PUCCH_1:
    case UlCqi_s::PUCCH_2:
    case UlCqi_s::PRACH: {
        NS_FATAL_ERROR("NsPfFfMacScheduler supports only PUSCH and SRS UL-CQIs");
    }
    break;
    default:
        NS_FATAL_ERROR("Unknown type of UL-CQI");
    }
}

void
NsPfFfMacScheduler::RefreshDlCqiMaps()
{
    // refresh DL CQI P01 Map
    std::map<uint16_t, uint32_t>::iterator itP10 = m_p10CqiTimers.begin();
    while (itP10 != m_p10CqiTimers.end())
    {
        NS_LOG_INFO(this << " P10-CQI for user " << (*itP10).first << " is "
                         << (uint32_t)(*itP10).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itP10).second == 0)
        {
            // delete correspondent entries
            std::map<uint16_t, uint8_t>::iterator itMap = m_p10CqiRxed.find((*itP10).first);
            NS_ASSERT_MSG(itMap != m_p10CqiRxed.end(),
                          " Does not find CQI report for user " << (*itP10).first);
            NS_LOG_INFO(this << " P10-CQI expired for user " << (*itP10).first);
            m_p10CqiRxed.erase(itMap);
            std::map<uint16_t, uint32_t>::iterator temp = itP10;
            itP10++;
            m_p10CqiTimers.erase(temp);
        }
        else
        {
            (*itP10).second--;
            itP10++;
        }
    }

    // refresh DL CQI A30 Map
    std::map<uint16_t, uint32_t>::iterator itA30 = m_a30CqiTimers.begin();
    while (itA30 != m_a30CqiTimers.end())
    {
        NS_LOG_INFO(this << " A30-CQI for user " << (*itA30).first << " is "
                         << (uint32_t)(*itA30).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itA30).second == 0)
        {
            // delete correspondent entries
            std::map<uint16_t, SbMeasResult_s>::iterator itMap = m_a30CqiRxed.find((*itA30).first);
            NS_ASSERT_MSG(itMap != m_a30CqiRxed.end(),
                          " Does not find CQI report for user " << (*itA30).first);
            NS_LOG_INFO(this << " A30-CQI expired for user " << (*itA30).first);
            m_a30CqiRxed.erase(itMap);
            std::map<uint16_t, uint32_t>::iterator temp = itA30;
            itA30++;
            m_a30CqiTimers.erase(temp);
        }
        else
        {
            (*itA30).second--;
            itA30++;
        }
    }
}

void
NsPfFfMacScheduler::RefreshUlCqiMaps()
{
    // refresh UL CQI  Map
    std::map<uint16_t, uint32_t>::iterator itUl = m_ueCqiTimers.begin();
    while (itUl != m_ueCqiTimers.end())
    {
        NS_LOG_INFO(this << " UL-CQI for user " << (*itUl).first << " is "
                         << (uint32_t)(*itUl).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itUl).second == 0)
        {
            // delete correspondent entries
            std::map<uint16_t, std::vector<double>>::iterator itMap = m_ueCqi.find((*itUl).first);
            NS_ASSERT_MSG(itMap != m_ueCqi.end(),
                          " Does not find CQI report for user " << (*itUl).first);
            NS_LOG_INFO(this << " UL-CQI exired for user " << (*itUl).first);
            (*itMap).second.clear();
            m_ueCqi.erase(itMap);
            std::map<uint16_t, uint32_t>::iterator temp = itUl;
            itUl++;
            m_ueCqiTimers.erase(temp);
        }
        else
        {
            (*itUl).second--;
            itUl++;
        }
    }
}

void
NsPfFfMacScheduler::UpdateDlRlcBufferInfo(uint16_t rnti, uint8_t lcid, uint16_t size)
{
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it;
    LteFlowId_t flow(rnti, lcid);
    it = m_rlcBufferReq.find(flow);
    if (it != m_rlcBufferReq.end())
    {
        NS_LOG_INFO(this << " UE " << rnti << " LC " << (uint16_t)lcid << " txqueue "
                         << (*it).second.m_rlcTransmissionQueueSize << " retxqueue "
                         << (*it).second.m_rlcRetransmissionQueueSize << " status "
                         << (*it).second.m_rlcStatusPduSize << " decrease " << size);
        // Update queues: RLC tx order Status, ReTx, Tx
        // Update status queue
        if (((*it).second.m_rlcStatusPduSize > 0) && (size >= (*it).second.m_rlcStatusPduSize))
        {
            (*it).second.m_rlcStatusPduSize = 0;
        }
        else if (((*it).second.m_rlcRetransmissionQueueSize > 0) &&
                 (size >= (*it).second.m_rlcRetransmissionQueueSize))
        {
            (*it).second.m_rlcRetransmissionQueueSize = 0;
        }
        else if ((*it).second.m_rlcTransmissionQueueSize > 0)
        {
            uint32_t rlcOverhead;
            if (lcid == 1)
            {
                // for SRB1 (using RLC AM) it's better to
                // overestimate RLC overhead rather than
                // underestimate it and risk unneeded
                // segmentation which increases delay
                rlcOverhead = 4;
            }
            else
            {
                // minimum RLC overhead due to header
                rlcOverhead = 2;
            }
            // update transmission queue
            if ((*it).second.m_rlcTransmissionQueueSize <= size - rlcOverhead)
            {
                (*it).second.m_rlcTransmissionQueueSize = 0;
            }
            else
            {
                (*it).second.m_rlcTransmissionQueueSize -= size - rlcOverhead;
            }
        }
    }
    else
    {
        NS_LOG_ERROR(this << " Does not find DL RLC Buffer Report of UE " << rnti);
    }
}

void
NsPfFfMacScheduler::UpdateUlRlcBufferInfo(uint16_t rnti, uint16_t size)
{
    size = size - 2; // remove the minimum RLC overhead
    std::map<uint16_t, uint32_t>::iterator it = m_ceBsrRxed.find(rnti);
    if (it != m_ceBsrRxed.end())
    {
        NS_LOG_INFO(this << " UE " << rnti << " size " << size << " BSR " << (*it).second);
        if ((*it).second >= size)
        {
            (*it).second -= size;
        }
        else
        {
            (*it).second = 0;
        }
    }
    else
    {
        NS_LOG_ERROR(this << " Does not find BSR report info of UE " << rnti);
    }
}

void
NsPfFfMacScheduler::TransmissionModeConfigurationUpdate(uint16_t rnti, uint8_t txMode)
{
    NS_LOG_FUNCTION(this << " RNTI " << rnti << " txMode " << (uint16_t)txMode);
    FfMacCschedSapUser::CschedUeConfigUpdateIndParameters params;
    params.m_rnti = rnti;
    params.m_transmissionMode = txMode;
    m_cschedSapUser->CschedUeConfigUpdateInd(params);
}

NsPfFfMacScheduler::RbgSliceInfo
NsPfFfMacScheduler::GetRbgSliceInfo (int rbgId)
{
    uint32_t sliceId = UINT32_MAX; //max means no slice ID

    /*
    * The scheduler will allocate dedicated RBGs for all slices first, 
    * then allocate prioritized RBGs for all slices, 
    * and the leftover RBs are shared RBGs. As shown in the following:
    * 
    * +----------+------------------ total, e.g., 25 (RBGs)
    * |All Slices|   Shared RBGs     
    * +----------+------------------
    * |Slice N   |
    * +----------+
    * |...       |   Prioritized RBGs
    * +----------+
    * |Slice 0   |
    * +----------+------------------
    * |Slice N   |
    * +----------+
    * |...       |   Dedicated RBGs
    * +----------+
    * |Slice 0   |
    * +----------+------------------ 0 (RBs)
    */

    //check the dedicated rbg
    int total_dedicatedRbg = 0;
    auto iter = m_sliceInfoMap.begin();
    while(iter != m_sliceInfoMap.end())
    {
        //std::cout << " dedicated size" << iter->second->m_dedicatedRbg << std::endl;
        int start_dedicatedRbg = total_dedicatedRbg; //starting rbg id for this slice
        total_dedicatedRbg += iter->second->m_dedicatedRbg;
        if (start_dedicatedRbg <= rbgId && rbgId < total_dedicatedRbg)
        {
            sliceId = iter->first;//find the rbg range for this slice
        }

        iter++;
    }

    if (rbgId < total_dedicatedRbg)
    {
        //this is a dedicated resouce block group
        return std::make_pair(DEDICATED_RBG, sliceId);
    }

    //continue check prioritized rbg
    int total_prioritizedRbg = 0;
    iter = m_sliceInfoMap.begin();
    while(iter != m_sliceInfoMap.end())
    {
        //std::cout << " prioritized size" << iter->second->m_prioritizedRbg << std::endl;
        int start_prioritizedRbg = total_dedicatedRbg + total_prioritizedRbg; //starting rbg id for this slice
        total_prioritizedRbg += iter->second->m_prioritizedRbg;
        if (start_prioritizedRbg <= rbgId && rbgId < total_dedicatedRbg + total_prioritizedRbg)
        {
            sliceId = iter->first;//find the rbg range for this slice
        }

        iter++;
    }

    if (rbgId < total_dedicatedRbg + total_prioritizedRbg)
    {
        //this is a prioritized resouce block group
        return std::make_pair(PRIORITIZED_RBG, sliceId);
    }

    //else are shared RBgs...
    return std::make_pair(SHARED_RBG, sliceId);
}

void
NsPfFfMacScheduler::MeasurementEvent ()
{
    //first measurement (at 0 second) will not have any flow, and will be not be reported...
    std::map<uint32_t, std::tuple<int, double, double>> sliceToMeasureMap; //key is sliceId and value is <userPerSlice, sumRatePerSlice, SumRbUsagePerSlice>

    for (auto it = m_flowStatsDl.begin (); it != m_flowStatsDl.end (); it++)
    {
        uint32_t imsi = (*it).second.imsi;
        if (imsi == UINT32_MAX)
        {
            //no user info yet. Do not report measurement.
            continue;
        }
        //link capacity measurement
        std::map<uint16_t, SbMeasResult_s>::iterator itCqi;
        itCqi = m_a30CqiRxed.find((*it).first);
        std::map<uint16_t, uint8_t>::iterator itTxMode;
        itTxMode = m_uesTxMode.find((*it).first);
        if (itTxMode == m_uesTxMode.end())
        {
            NS_FATAL_ERROR("No Transmission Mode info on user " << (*it).first);
        }
        auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);

        std::vector<uint8_t> worstSbCqi;

        if (itCqi == m_a30CqiRxed.end())
        {
            for (uint8_t k = 0; k < nLayer; k++)
            {
                worstSbCqi.push_back(1); // start with lowest value
            }
        }
        else
        {
            int rbgSize = GetRbgSize(m_cschedCellConfig.m_dlBandwidth);
            int rbgNum = m_cschedCellConfig.m_dlBandwidth / rbgSize;

            for (int i = 0; i < rbgNum; i++)
            {
                if (worstSbCqi.size() == 0)
                {
                   worstSbCqi = (*itCqi).second.m_higherLayerSelected.at(i).m_sbCqi; //initilize sbcqi
                }
                else
                {
                    for (uint8_t k = 0; k < nLayer; k++)
                    {
                        worstSbCqi.at(k) = std::min(worstSbCqi.at(k), (*itCqi).second.m_higherLayerSelected.at(i).m_sbCqi.at(k)); //update to worst sbcqi
                    }

                }

            }
        }

        double achievableRate = 0.0;
        uint8_t mcs = 0;
        for (uint8_t k = 0; k < nLayer; k++)
        {
            if (worstSbCqi.size() > k)
            {
                mcs = m_amc->GetMcsFromCqi(worstSbCqi.at(k));
            }
            else
            {
                // no info on this subband -> worst MCS
                mcs = 0;
            }
            achievableRate += ((m_amc->GetDlTbSizeFromMcs(mcs, m_cschedCellConfig.m_dlBandwidth) / 8) /
                            0.001); // = TB size / TTI
        }
        
        double rateOfAllRbs = -1;
        if(achievableRate > 0 )//report new measurement
        {
            rateOfAllRbs = std::round(achievableRate/125e3);
            //std::cout << Now().GetSeconds () << " UE:"<< imsi << " rate :" << rateOfAllRbs  << " kbps " << " rb:" << m_cschedCellConfig.m_dlBandwidth << std::endl;
            //Time nowTime = Now();
        }
        else
        {
            //Time nowTime = Now();
        }

   
        //slice ID measurement.
        //std::cout << Now().GetSeconds () << " UE:"<< imsi << " slice ID:" << (*it).second.sliceId << std::endl;

        //rb usage measurement.
        int usedHarqRb = 0;
        int usedDataRb = 0;
        if (m_usedHarqRbsMap.find(imsi) != m_usedHarqRbsMap.end())
        {
            usedHarqRb = m_usedHarqRbsMap[imsi];
        }
        if (m_usedDataRbsMap.find(imsi) != m_usedDataRbsMap.end())
        {
            usedDataRb = m_usedDataRbsMap[imsi];
        }
        double usagePercent = 0;
        if(usedHarqRb + usedDataRb > 0)
        {
            usagePercent = (double)(usedHarqRb + usedDataRb)*100/m_totalRbs;
        }
        //std::cout << Now().GetSeconds () << " UE: "<< imsi << " harq rb: " << usedHarqRb << " data rb: " << usedDataRb << " total rb: "<<  m_totalRbs << " usage(%): " << usagePercent << std::endl;
        uint32_t sliceId = (*it).second.sliceId;
        m_lteUeMeasurement(sliceId, rateOfAllRbs, usagePercent, imsi, true);
        if (sliceToMeasureMap.find(sliceId) == sliceToMeasureMap.end())
        {
            sliceToMeasureMap[sliceId] = std::make_tuple(0, 0, 0);
        }

        auto userCount = std::get<0>(sliceToMeasureMap[sliceId]) + 1;
        auto sumRate = std::get<1>(sliceToMeasureMap[sliceId]) + rateOfAllRbs;
        auto sumUsage = std::get<2>(sliceToMeasureMap[sliceId]) + usagePercent;
        sliceToMeasureMap[sliceId] = std::make_tuple(userCount, sumRate, sumUsage);
        
    }

    std::vector<int> sliceId;
    std::vector<double> listRate;
    std::vector<double> listUsage;

    auto iter = sliceToMeasureMap.begin();
    while (iter!= sliceToMeasureMap.end())
    {
        //std::cout << "slice:" <<iter->first << " rate:" <<iter->second.first  << " usage:" << iter->second.second << std::endl;
        sliceId.push_back(iter->first);
        if (std::get<0>(iter->second) == 0)
        {
            listRate.push_back(0);

        }
        else{
            listRate.push_back(std::get<1>(iter->second)/std::get<0>(iter->second));//average of max_rate
        }
        listUsage.push_back(std::get<2>(iter->second));
        iter++;
    }
    m_LteEnbMeasurement(sliceId, listRate, listUsage, true);

    //Uplink TODO...
    /*for (auto it = m_flowStatsUl.begin (); it != m_flowStatsUl.end (); it++)
    {
      // m_RANMacMeasurement->m_rate.at(i) = std::round((*it).second.lastAveragedThroughput/1000);
        uint32_t imsi = (*it).second.imsi;
        if((*it).second.achievableRate > 0 )//report new measurement
        {
          //ratePerRbg is the rate per RB group...
          //int rbgSize = GetRbgSize (m_cschedCellConfig.m_ulBandwidth);
          double rateOfAllRbs = std::round((*it).second.achievableRate/125e3);
          //std::cout << Now().GetSeconds () << " UE:"<< i << " kbps " << " rbsize:" << m_cschedCellConfig.m_ulBandwidth << " rbgSize:" << rbgSize  << " rate of all RBs:" << rateOfAllRbs << std::endl;
          //Time nowTime = Now();
          m_rateMeasurement(rateOfAllRbs, imsi, false);
          (*it).second.achievableRate = 0; //reset to zero after this measurement.
        }
        else
        {
          //Time nowTime = Now();
          m_rateMeasurement(-1, imsi, false);
        }

    }*/

    m_usedHarqRbsMap.clear();
    m_usedDataRbsMap.clear();
    m_totalRbs = 0;
    Simulator::Schedule(m_measurementGuardInterval, &NsPfFfMacScheduler::MeasurementGuardEnd, this);
}

void
NsPfFfMacScheduler::MeasurementGuardEnd ()
{
    Simulator::Schedule(m_measurementInterval, &NsPfFfMacScheduler::MeasurementEvent, this);
}

void
NsPfFfMacScheduler::UpdateSliceInfo(const json& action, char type)
{
    if (action == nullptr)
	{
		return;
	}
    Time pullTime = Now();
    int totalScheduledRbg = 0;
    auto iter = m_sliceInfoMap.begin();
    while(iter != m_sliceInfoMap.end())
    {   
        if (!action["slice"].is_array())
        {
            NS_FATAL_ERROR("action must be a list of slices!");
        }
        if (!action["value"].is_array())
        {
            NS_FATAL_ERROR("action must be a list of slices!");
        }

        if (action["value"].size() > 0) //with action
        {
            if (action["slice"].size() != action["value"].size())
            {
                NS_FATAL_ERROR("the size of slice id and value must be the same!");
            }
            for (uint32_t i = 0; i < action["slice"].size(); i++)
            {
                if (iter->first == action["slice"][i])
                {
                    //found action for this slice.
                    if (type == 'd')
                    {
                        iter->second->m_dedicatedRbg = action["value"][i];
                    }
                    else if (type == 'p')
                    {
                        iter->second->m_prioritizedRbg = action["value"][i];
                    }
                    else if (type == 's')
                    {
                        iter->second->m_sharedRbg = action["value"][i];
                    }
                    else
                    {
                        NS_FATAL_ERROR("Only surpports d, p or s type.");
                    }
                    break;
                }
            }

            iter->second->m_maxRbg = iter->second->m_dedicatedRbg + iter->second->m_prioritizedRbg + iter->second->m_sharedRbg;
            iter->second->m_minRbg = iter->second->m_dedicatedRbg + iter->second->m_prioritizedRbg;

            totalScheduledRbg += iter->second->m_minRbg;
            std::cout << "Update slice ID:" << iter->first << " dedicatedRbg:" << iter->second->m_dedicatedRbg << " prioritizedRbg:" << iter->second->m_prioritizedRbg << " sharedRbg:" << iter->second->m_sharedRbg 
            << " minRbg:" << iter->second->m_minRbg << " maxRbg:" << iter->second->m_maxRbg << std::endl;
        }
        
        iter++;
    }

    int rbgSize = GetRbgSize(m_cschedCellConfig.m_dlBandwidth);
    int rbgNum = m_cschedCellConfig.m_dlBandwidth / rbgSize;
    if(totalScheduledRbg > rbgNum)
    {
        NS_FATAL_ERROR("the total schedule RBGs (" << totalScheduledRbg << ") is greater than available RBGs (" << rbgNum << ")");
    }
}

void
NsPfFfMacScheduler::ReceiveDrbAllocation(const json& action)
{
    std::cout << "Rx DRB action "<< action << std::endl;
    UpdateSliceInfo(action, 'd');
}

void
NsPfFfMacScheduler::ReceivePrbAllocation(const json& action)
{
   std::cout << "Rx PRB action "<< action << std::endl;
   UpdateSliceInfo(action, 'p');
}

void
NsPfFfMacScheduler::ReceiveSrbAllocation(const json& action)
{
   std::cout << "Rx SRB action "<< action << std::endl;
   UpdateSliceInfo(action, 's');
}

} // namespace ns3
