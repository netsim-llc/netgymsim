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

#ifndef NS_PF_FF_MAC_SCHEDULER_H
#define NS_PF_FF_MAC_SCHEDULER_H

#include <ns3/ff-mac-csched-sap.h>
#include <ns3/ff-mac-sched-sap.h>
#include <ns3/ff-mac-scheduler.h>
#include <ns3/lte-amc.h>
#include <ns3/lte-common.h>
#include <ns3/lte-ffr-sap.h>
#include <ns3/nstime.h>
#include "ns3/traced-value.h"
#include <ns3/gma-data-processor.h>

#include <map>
#include <vector>

// value for SINR outside the range defined by FF-API, used to indicate that there
// is no CQI for this element
#define NO_SINR -5000

#define HARQ_PROC_NUM 8
#define HARQ_DL_TIMEOUT 11

namespace ns3
{

typedef std::vector<uint8_t> DlHarqProcessesStatus_t;
typedef std::vector<uint8_t> DlHarqProcessesTimer_t;
typedef std::vector<DlDciListElement_s> DlHarqProcessesDciBuffer_t;
typedef std::vector<std::vector<struct RlcPduListElement_s>>
    RlcPduList_t;                                           // vector of the LCs and layers per UE
typedef std::vector<RlcPduList_t> DlHarqRlcPduListBuffer_t; // vector of the 8 HARQ processes per UE

typedef std::vector<UlDciListElement_s> UlHarqProcessesDciBuffer_t;
typedef std::vector<uint8_t> UlHarqProcessesStatus_t;


/// nsPfsFlowPerf_t structure
struct nsPfsFlowPerf_t
{
    Time flowStart;                      ///< flow start time
    unsigned long totalBytesTransmitted; ///< total bytes transmitted
    unsigned int lastTtiBytesTrasmitted; ///< last total bytes transmitted
    unsigned int lastTtiSlicedBytesTrasmitted; ///< last sliced bytes transmitted
    double lastAveragedThroughput;       ///< last averaged throughput
    double lastAveragedSlicedThroughput;       ///< last averaged sliced throughput. Shared throughput = lastAveragedThroughput - lastAveragedSlicedThroughput
    uint32_t sliceId = UINT32_MAX;       ///< slice id
    uint32_t imsi = UINT32_MAX;       ///< imsi
};

struct SliceInfo : public SimpleRefCount<SliceInfo>
{
uint64_t m_numUsers = 0;
uint64_t m_dedicatedRbg = 0;
uint64_t m_prioritizedRbg = 0;
uint64_t m_sharedRbg = 0;
uint64_t m_minRbg = 0; //=m_dedicatedRbg + m_prioritizedRbg
uint64_t m_maxRbg = 0; //=m_dedicatedRbg + m_prioritizedRbg+m_sharedRbg
};

/**
 * \ingroup ff-api
 * \brief Implements the SCHED SAP and CSCHED SAP for a Proportional Fair scheduler
 *
 * This class implements the interface defined by the FfMacScheduler abstract class
 */

class NsPfFfMacScheduler : public FfMacScheduler
{
  public:
    /**
     * \brief Constructor
     *
     * Creates the MAC Scheduler interface implementation
     */
    NsPfFfMacScheduler();

    /**
     * Destructor
     */
    ~NsPfFfMacScheduler() override;

    // inherited from Object
    void DoDispose() override;
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    // inherited from FfMacScheduler
    void SetFfMacCschedSapUser(FfMacCschedSapUser* s) override;
    void SetFfMacSchedSapUser(FfMacSchedSapUser* s) override;
    FfMacCschedSapProvider* GetFfMacCschedSapProvider() override;
    FfMacSchedSapProvider* GetFfMacSchedSapProvider() override;

    // FFR SAPs
    void SetLteFfrSapProvider(LteFfrSapProvider* s) override;
    LteFfrSapUser* GetLteFfrSapUser() override;

    /// allow MemberCschedSapProvider<NsPfFfMacScheduler> class friend access
    friend class MemberCschedSapProvider<NsPfFfMacScheduler>;
    /// allow MemberSchedSapProvider<NsPfFfMacScheduler> class friend access
    friend class MemberSchedSapProvider<NsPfFfMacScheduler>;

    /**
     * \brief transmission mode configuration update
     *
     * \param rnti the RNTI
     * \param txMode the transmit mode
     */
    void TransmissionModeConfigurationUpdate(uint16_t rnti, uint8_t txMode);
    void ReceiveDrbAllocation(const json& action);
    void ReceivePrbAllocation(const json& action);
    void ReceiveSrbAllocation(const json& action);

  private:
    //
    // Implementation of the CSCHED API primitives
    // (See 4.1 for description of the primitives)
    //

    /**
     * \brief CSched cell config request
     *
     * \param params FfMacCschedSapProvider::CschedCellConfigReqParameters
     */
    void DoCschedCellConfigReq(
        const struct FfMacCschedSapProvider::CschedCellConfigReqParameters& params);

    /**
     * \brief CSched UE config request
     *
     * \param params FfMacCschedSapProvider::CschedUeConfigReqParameters
     */
    void DoCschedUeConfigReq(
        const struct FfMacCschedSapProvider::CschedUeConfigReqParameters& params);

    /**
     * \brief CSched LC config request
     *
     * \param params FfMacCschedSapProvider::CschedLcConfigReqParameters
     */
    void DoCschedLcConfigReq(
        const struct FfMacCschedSapProvider::CschedLcConfigReqParameters& params);

    /**
     * \brief CSched LC release request
     *
     * \param params FfMacCschedSapProvider::CschedLcReleaseReqParameters
     */
    void DoCschedLcReleaseReq(
        const struct FfMacCschedSapProvider::CschedLcReleaseReqParameters& params);

    /**
     * \brief CSched UE release request
     *
     * \param params FfMacCschedSapProvider::CschedLcReleaseReqParameters
     */
    void DoCschedUeReleaseReq(
        const struct FfMacCschedSapProvider::CschedUeReleaseReqParameters& params);

    //
    // Implementation of the SCHED API primitives
    // (See 4.2 for description of the primitives)
    //

    /**
     * \brief Sched DL RLC buffer request
     *
     * \param params FfMacSchedSapProvider::SchedDlRlcBufferReqParameters
     */
    void DoSchedDlRlcBufferReq(
        const struct FfMacSchedSapProvider::SchedDlRlcBufferReqParameters& params);

    /**
     * \brief Sched DL paging buffer request
     *
     * \param params FfMacSchedSapProvider::SchedDlPagingBufferReqParameters
     */
    void DoSchedDlPagingBufferReq(
        const struct FfMacSchedSapProvider::SchedDlPagingBufferReqParameters& params);

    /**
     * \brief Sched DL MAC buffer request
     *
     * \param params FfMacSchedSapProvider::SchedDlMacBufferReqParameters
     */
    void DoSchedDlMacBufferReq(
        const struct FfMacSchedSapProvider::SchedDlMacBufferReqParameters& params);

    /**
     * \brief Sched DL trigger request
     *
     * \param params FfMacSchedSapProvider::SchedDlTriggerReqParameters
     */
    void DoSchedDlTriggerReq(
        const struct FfMacSchedSapProvider::SchedDlTriggerReqParameters& params);

    /**
     * \brief Sched DL RACH info request
     *
     * \param params FfMacSchedSapProvider::SchedDlRachInfoReqParameters
     */
    void DoSchedDlRachInfoReq(
        const struct FfMacSchedSapProvider::SchedDlRachInfoReqParameters& params);

    /**
     * \brief Sched DL CQI info request
     *
     * \param params FfMacSchedSapProvider::SchedDlCqiInfoReqParameters
     */
    void DoSchedDlCqiInfoReq(
        const struct FfMacSchedSapProvider::SchedDlCqiInfoReqParameters& params);

    /**
     * \brief Sched UL trigger request
     *
     * \param params FfMacSchedSapProvider::SchedUlTriggerReqParameters
     */
    void DoSchedUlTriggerReq(
        const struct FfMacSchedSapProvider::SchedUlTriggerReqParameters& params);

    /**
     * \brief Sched UL noise interference request
     *
     * \param params FfMacSchedSapProvider::SchedUlNoiseInterferenceReqParameters
     */
    void DoSchedUlNoiseInterferenceReq(
        const struct FfMacSchedSapProvider::SchedUlNoiseInterferenceReqParameters& params);

    /**
     * \brief Sched UL SR info request
     *
     * \param params FfMacSchedSapProvider::SchedUlSrInfoReqParameters
     */
    void DoSchedUlSrInfoReq(const struct FfMacSchedSapProvider::SchedUlSrInfoReqParameters& params);

    /**
     * \brief Sched UL MAC control info request
     *
     * \param params FfMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters
     */
    void DoSchedUlMacCtrlInfoReq(
        const struct FfMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params);

    /**
     * \brief Sched UL CQI info request
     *
     * \param params FfMacSchedSapProvider::SchedUlCqiInfoReqParameters
     */
    void DoSchedUlCqiInfoReq(
        const struct FfMacSchedSapProvider::SchedUlCqiInfoReqParameters& params);

    /**
     * \brief Get RBG size
     *
     * \param dlbandwidth dDL bandwidth
     * \returns the RBG size
     */
    int GetRbgSize(int dlbandwidth);

    /**
     * \brief LC active per flow
     *
     * \param rnti the RNTI
     * \returns the LC active per flow
     */
    unsigned int LcActivePerFlow(uint16_t rnti);
    unsigned int PriorityAwareLcActivePerFlow(uint16_t rnti);

    /**
     * \brief Estimate UL SINR
     *
     * \param rnti the RNTI
     * \param rb the RB
     * \returns the SINR
     */
    double EstimateUlSinr(uint16_t rnti, uint16_t rb);

    /// Refresh DL CQI maps
    void RefreshDlCqiMaps();
    /// Refresh UL CQI maps
    void RefreshUlCqiMaps();

    /**
     * \brief Update DL RCL buffer info
     *
     * \param rnti the RNTI
     * \param lcid the LCID
     * \param size the size
     */
    void UpdateDlRlcBufferInfo(uint16_t rnti, uint8_t lcid, uint16_t size);
    /**
     * \brief Update UL RCL buffer info
     *
     * \param rnti the RNTI
     * \param size the size
     */
    void UpdateUlRlcBufferInfo(uint16_t rnti, uint16_t size);

    /**
     * \brief Update and return a new process Id for the RNTI specified
     *
     * \param rnti the RNTI of the UE to be updated
     * \return the process id  value
     */
    uint8_t UpdateHarqProcessId(uint16_t rnti);

    /**
     * \brief Return the availability of free process for the RNTI specified
     *
     * \param rnti the RNTI of the UE to be updated
     * \return the availability
     */
    bool HarqProcessAvailability(uint16_t rnti);

    /**
     * \brief Refresh HARQ processes according to the timers
     *
     */
    void RefreshHarqProcesses();

    Ptr<LteAmc> m_amc; ///< AMC

    /**
     * Vectors of UE's LC info
     */
    std::map<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters> m_rlcBufferReq;

    /**
     * Map of UE statistics (per RNTI basis) in downlink
     */
    std::map<uint16_t, nsPfsFlowPerf_t> m_flowStatsDl;

    /**
     * Map of UE statistics (per RNTI basis)
     */
    std::map<uint16_t, nsPfsFlowPerf_t> m_flowStatsUl;

    /**
     * Map of UE's DL CQI P01 received
     */
    std::map<uint16_t, uint8_t> m_p10CqiRxed;
    /**
     * Map of UE's timers on DL CQI P01 received
     */
    std::map<uint16_t, uint32_t> m_p10CqiTimers;

    /**
     * Map of UE's DL CQI A30 received
     */
    std::map<uint16_t, SbMeasResult_s> m_a30CqiRxed;
    /**
     * Map of UE's timers on DL CQI A30 received
     */
    std::map<uint16_t, uint32_t> m_a30CqiTimers;

    /**
     * Map of previous allocated UE per RBG
     * (used to retrieve info from UL-CQI)
     */
    std::map<uint16_t, std::vector<uint16_t>> m_allocationMaps;

    /**
     * Map of UEs' UL-CQI per RBG
     */
    std::map<uint16_t, std::vector<double>> m_ueCqi;
    /**
     * Map of UEs' timers on UL-CQI per RBG
     */
    std::map<uint16_t, uint32_t> m_ueCqiTimers;

    /**
     * Map of UE's buffer status reports received
     */
    std::map<uint16_t, uint32_t> m_ceBsrRxed;

    // MAC SAPs
    FfMacCschedSapUser* m_cschedSapUser;         ///< CSched SAP user
    FfMacSchedSapUser* m_schedSapUser;           ///< Sched SAP user
    FfMacCschedSapProvider* m_cschedSapProvider; ///< CSched SAP provider
    FfMacSchedSapProvider* m_schedSapProvider;   ///< Sched SAP provider

    // FFR SAPs
    LteFfrSapUser* m_ffrSapUser;         ///< FFR SAP user
    LteFfrSapProvider* m_ffrSapProvider; ///< FFR SAP provider

    // Internal parameters
    FfMacCschedSapProvider::CschedCellConfigReqParameters
        m_cschedCellConfig; ///< CSched cell config

    double m_timeWindow; ///< time window

    uint16_t m_nextRntiUl; ///< RNTI of the next user to be served next scheduling in UL

    uint32_t m_cqiTimersThreshold; ///< # of TTIs for which a CQI can be considered valid

    std::map<uint16_t, uint8_t> m_uesTxMode; ///< txMode of the UEs

    // HARQ attributes
    /**
     * m_harqOn when false inhibit the HARQ mechanisms (by default active)
     */
    bool m_harqOn;
    std::map<uint16_t, uint8_t> m_dlHarqCurrentProcessId; ///< DL HARQ current process ID
    // HARQ status
    //  0: process Id available
    //  x>0: process Id equal to `x` transmission count
    std::map<uint16_t, DlHarqProcessesStatus_t> m_dlHarqProcessesStatus; ///< DL HARQ process status
    std::map<uint16_t, DlHarqProcessesTimer_t> m_dlHarqProcessesTimer;   ///< DL HARQ process timer
    std::map<uint16_t, DlHarqProcessesDciBuffer_t>
        m_dlHarqProcessesDciBuffer; ///< DL HARQ process DCI buffer
    std::map<uint16_t, DlHarqRlcPduListBuffer_t>
        m_dlHarqProcessesRlcPduListBuffer;                 ///< DL HARQ process RLC PDU list buffer
    std::vector<DlInfoListElement_s> m_dlInfoListBuffered; ///< HARQ retx buffered

    std::map<uint16_t, uint8_t> m_ulHarqCurrentProcessId; ///< UL HARQ current process ID
    // HARQ status
    //  0: process Id available
    //  x>0: process Id equal to `x` transmission count
    std::map<uint16_t, UlHarqProcessesStatus_t> m_ulHarqProcessesStatus; ///< UL HARQ process status
    std::map<uint16_t, UlHarqProcessesDciBuffer_t>
        m_ulHarqProcessesDciBuffer; ///< UL HARQ process DCI buffer

    // RACH attributes
    std::vector<struct RachListElement_s> m_rachList; ///< RACH list
    std::vector<uint16_t> m_rachAllocationMap;        ///< RACH allocation map
    uint8_t m_ulGrantMcs;                             ///< MCS for UL grant (default 0)

  /*
  * 1. For Single Slice Case:
  *
  * The Resource Block Groups (RBGs) are assigned as following.
  * 
  * +----------------+ total, e.g., 25 (RBGs)
  * |unused RBGs     |
  * +----------------+ max = m_dedicatedRbgsPerSlice + m_prioritizedRbgsPerSlice + m_sharedRbgsPerSlice (RBGs)
  * |Shared RBGs     |
  * +----------------+ min = m_dedicatedRbgsPerSlice + m_prioritizedRbgsPerSlice (RBGs)
  * |Prioritized RBGs|
  * +----------------+ dedicated = m_dedicatedRbgsPerSlice (RBGs)
  * |Dedicated RBGs  |
  * +---------------+ 0 (RBGs)
  *
  * 2. For Multiple Slice Case, e.g., N slices:
  * 
  * The scheduler will allocate dedicated RBGs for all slices first, 
  * then allocate prioritized RBGs for all slices, 
  * and the leftover RBGs are shared RBGs. As shown in the following:
  * 
  * +----------+------------------ total, e.g., 50 (RBGs)
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
  * +----------+------------------ 0 (RBGs)
  *
  * 3. Slice Aware Scheduling Algorithm:
  * step 1: Schedule dedicated and prioritized RBGs to corresponding slices.
  * step 2: Schedule non-used prioritized and shared RBGs to all slices.
  * We can treat the non-used prioritized RBGs same as shared RBGs since the prioritized
  *  slices are already allocated in step 1 and some prioritized RBGs may not be used if the TX buffer is small.
  * note: at anytime if the total used RBGs of a slice is greater than the m_maxRbgsPerSlice,
  * remove this slice (and its containing UEs) from the scheduling pool.
  */

  //for utilization measurement
  int m_totalRbs = 0; //stores the total rbs
  std::map<uint64_t, int> m_usedHarqRbsMap; //stores the used RBs for HARQ per user, the key is imsi
  std::map<uint64_t, int> m_usedDataRbsMap; //stores the used RBs per user, the key is imsi

  std::map <uint32_t, Ptr<SliceInfo>> m_sliceInfoMap;
  //stores the bearer type per slice.
  std::map < std::pair<uint32_t, LogicalChannelConfigListElement_s::QosBearerType_e>, bool > m_sliceBearerTypeMap; //key is <slice_id, bearer_type>.
  enum RBG_TYPE {
    DEDICATED_RBG = 0,
    PRIORITIZED_RBG = 1,
    SHARED_RBG = 2,
  };
  typedef std::pair<RBG_TYPE, uint32_t> RbgSliceInfo;
  RbgSliceInfo GetRbgSliceInfo (int rbgId);
  void UpdateSliceInfo(const json& action, char type); //for type, 'd', 'p', or 's'
  Time m_measurementInterval; //how frequent the scheduler Sync with xApp... The PDCP rate report is updated every 100 ms, smaller interval maybe empty rate...
  Time m_measurementGuardInterval; //guard time between 2 measurements.

  TracedCallback<std::vector<int>, std::vector<double>,  std::vector<double>, bool> m_LteEnbMeasurement; //rate, slice id, rb usage.
  typedef void (*LteEnbMeasurementTracedCallback)(std::vector<int> sliceId, std::vector<double> rate,  std::vector<double> rbUsage, bool dl);
  TracedCallback<int, double, double, uint64_t, bool> m_lteUeMeasurement; //rate, slice id, rb usage.
  typedef void (*LteUeMeasurementTracedCallback)(int sliceId, double rate, double rbUsage,
                                             uint64_t imsi, bool dl);
  void MeasurementEvent ();
  void MeasurementGuardEnd ();

  std::map<LteFlowId_t, struct LogicalChannelConfigListElement_s> m_ueLogicalChannelsConfigList;
  void RefreshSlicePriority();//refresh the bearer types that has data to send.
  std::map<LteFlowId_t, double> m_scheduledBytesPerLc; //scheduled bytes per user per lc,
};

} // namespace ns3

#endif /* NS_PF_FF_MAC_SCHEDULER_H */