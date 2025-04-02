/*
 * Copyright (c) 2006 INRIA
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "gma-ideal-wifi-manager.h"

#include "ns3/log.h"
#include "ns3/wifi-phy.h"

#include <algorithm>
#include "ns3/core-module.h"

namespace ns3
{

/**
 * \brief hold per-remote-station state for Ideal Wifi manager.
 *
 * This struct extends from WifiRemoteStation struct to hold additional
 * information required by the Ideal Wifi manager
 */
struct GmaIdealWifiRemoteStation : public WifiRemoteStation
{
    double m_lastSnrObserved; //!< SNR of most recently reported packet sent to the remote station
    uint16_t m_lastChannelWidthObserved; //!< Channel width (in MHz) of most recently reported
                                         //!< packet sent to the remote station
    uint16_t m_lastNssObserved; //!<  Number of spatial streams of most recently reported packet
                                //!<  sent to the remote station
    double m_lastSnrCached;     //!< SNR most recently used to select a rate
    uint8_t m_lastNss;   //!< Number of spatial streams most recently used to the remote station
    WifiMode m_lastMode; //!< Mode most recently used to the remote station
    uint16_t
        m_lastChannelWidth; //!< Channel width (in MHz) most recently used to the remote station
};

/// To avoid using the cache before a valid value has been cached
static const double CACHE_INITIAL_VALUE = -100;

NS_OBJECT_ENSURE_REGISTERED(GmaIdealWifiManager);

NS_LOG_COMPONENT_DEFINE("GmaIdealWifiManager");

TypeId
GmaIdealWifiManager::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::GmaIdealWifiManager")
            .SetParent<WifiRemoteStationManager>()
            .SetGroupName("Wifi")
            .AddConstructor<GmaIdealWifiManager>()
            .AddAttribute("BerThreshold",
                          "The maximum Bit Error Rate acceptable at any transmission mode",
                          DoubleValue(1e-6),
                          MakeDoubleAccessor(&GmaIdealWifiManager::m_ber),
                          MakeDoubleChecker<double>())
            .AddTraceSource("Rate",
                            "Traced value for rate changes (b/s)",
                            MakeTraceSourceAccessor(&GmaIdealWifiManager::m_currentRate),
                            "ns3::TracedValueCallback::Uint64")
            .AddAttribute("MeasurementInterval",
                          "Measurement Interval",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&GmaIdealWifiManager::m_measurementInterval),
                          MakeTimeChecker())
            .AddAttribute("MeasurementGuardInterval",
                          "Guard time between two measurement intervals",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&GmaIdealWifiManager::m_measurementGuardInterval),
                          MakeTimeChecker())
            .AddTraceSource("RateMeasurement",
                            "The transmission rate Measurement",
                            MakeTraceSourceAccessor(&GmaIdealWifiManager::m_rateMeasurement),
                            "ns3::GmaIdealWifiManager::RateMeasurementTracedCallback");
    return tid;
}

GmaIdealWifiManager::GmaIdealWifiManager()
    : m_currentRate(0)
{
    NS_LOG_FUNCTION(this);
}

GmaIdealWifiManager::~GmaIdealWifiManager()
{
    NS_LOG_FUNCTION(this);
}

void
GmaIdealWifiManager::SetupPhy(const Ptr<WifiPhy> phy)
{
    NS_LOG_FUNCTION(this << phy);
    WifiRemoteStationManager::SetupPhy(phy);
    Simulator::Schedule(m_measurementGuardInterval, &GmaIdealWifiManager::MeasurementGuardIntervalEnd, this);
}

uint16_t
GmaIdealWifiManager::GetChannelWidthForNonHtMode(WifiMode mode) const
{
    NS_ASSERT(mode.GetModulationClass() != WIFI_MOD_CLASS_HT &&
              mode.GetModulationClass() != WIFI_MOD_CLASS_VHT &&
              mode.GetModulationClass() != WIFI_MOD_CLASS_HE);
    if (mode.GetModulationClass() == WIFI_MOD_CLASS_DSSS ||
        mode.GetModulationClass() == WIFI_MOD_CLASS_HR_DSSS)
    {
        return 22;
    }
    else
    {
        return 20;
    }
}

void
GmaIdealWifiManager::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    BuildSnrThresholds();
}

void
GmaIdealWifiManager::BuildSnrThresholds()
{
    m_thresholds.clear();
    WifiMode mode;
    WifiTxVector txVector;
    uint8_t nss = 1;
    for (const auto& mode : GetPhy()->GetModeList())
    {
        txVector.SetChannelWidth(GetChannelWidthForNonHtMode(mode));
        txVector.SetNss(nss);
        txVector.SetMode(mode);
        NS_LOG_DEBUG("Adding mode = " << mode.GetUniqueName());
        AddSnrThreshold(txVector, GetPhy()->CalculateSnr(txVector, m_ber));
    }
    // Add all MCSes
    if (GetHtSupported())
    {
        for (const auto& mode : GetPhy()->GetMcsList())
        {
            for (uint16_t j = 20; j <= GetPhy()->GetChannelWidth(); j *= 2)
            {
                txVector.SetChannelWidth(j);
                if (mode.GetModulationClass() == WIFI_MOD_CLASS_HT)
                {
                    uint16_t guardInterval = GetShortGuardIntervalSupported() ? 400 : 800;
                    txVector.SetGuardInterval(guardInterval);
                    // derive NSS from the MCS index
                    nss = (mode.GetMcsValue() / 8) + 1;
                    NS_LOG_DEBUG("Adding mode = " << mode.GetUniqueName() << " channel width " << j
                                                  << " nss " << +nss << " GI " << guardInterval);
                    txVector.SetNss(nss);
                    txVector.SetMode(mode);
                    AddSnrThreshold(txVector, GetPhy()->CalculateSnr(txVector, m_ber));
                }
                else // VHT or HE
                {
                    uint16_t guardInterval;
                    if (mode.GetModulationClass() == WIFI_MOD_CLASS_VHT)
                    {
                        guardInterval = GetShortGuardIntervalSupported() ? 400 : 800;
                    }
                    else
                    {
                        guardInterval = GetGuardInterval();
                    }
                    txVector.SetGuardInterval(guardInterval);
                    for (uint8_t k = 1; k <= GetPhy()->GetMaxSupportedTxSpatialStreams(); k++)
                    {
                        if (mode.IsAllowed(j, k))
                        {
                            NS_LOG_DEBUG("Adding mode = " << mode.GetUniqueName()
                                                          << " channel width " << j << " nss " << +k
                                                          << " GI " << guardInterval);
                            txVector.SetNss(k);
                            txVector.SetMode(mode);
                            AddSnrThreshold(txVector, GetPhy()->CalculateSnr(txVector, m_ber));
                        }
                        else
                        {
                            NS_LOG_DEBUG("Mode = " << mode.GetUniqueName() << " disallowed");
                        }
                    }
                }
            }
        }
    }
}

double
GmaIdealWifiManager::GetSnrThreshold(WifiTxVector txVector)
{
    NS_LOG_FUNCTION(this << txVector);
    auto it = std::find_if(m_thresholds.begin(),
                           m_thresholds.end(),
                           [&txVector](const std::pair<double, WifiTxVector>& p) -> bool {
                               return ((txVector.GetMode() == p.second.GetMode()) &&
                                       (txVector.GetNss() == p.second.GetNss()) &&
                                       (txVector.GetChannelWidth() == p.second.GetChannelWidth()));
                           });
    if (it == m_thresholds.end())
    {
        // This means capabilities have changed in runtime, hence rebuild SNR thresholds
        BuildSnrThresholds();
        it = std::find_if(m_thresholds.begin(),
                          m_thresholds.end(),
                          [&txVector](const std::pair<double, WifiTxVector>& p) -> bool {
                              return ((txVector.GetMode() == p.second.GetMode()) &&
                                      (txVector.GetNss() == p.second.GetNss()) &&
                                      (txVector.GetChannelWidth() == p.second.GetChannelWidth()));
                          });
        NS_ASSERT_MSG(it != m_thresholds.end(), "SNR threshold not found");
    }
    return it->first;
}

void
GmaIdealWifiManager::AddSnrThreshold(WifiTxVector txVector, double snr)
{
    NS_LOG_FUNCTION(this << txVector.GetMode().GetUniqueName() << txVector.GetChannelWidth()
                         << snr);
    m_thresholds.emplace_back(snr, txVector);
}

WifiRemoteStation*
GmaIdealWifiManager::DoCreateStation() const
{
    NS_LOG_FUNCTION(this);
    GmaIdealWifiRemoteStation* station = new GmaIdealWifiRemoteStation();
    Reset(station);
    return station;
}

void
GmaIdealWifiManager::Reset(WifiRemoteStation* station) const
{
    NS_LOG_FUNCTION(this << station);
    GmaIdealWifiRemoteStation* st = static_cast<GmaIdealWifiRemoteStation*>(station);
    st->m_lastSnrObserved = 0.0;
    st->m_lastChannelWidthObserved = 0;
    st->m_lastNssObserved = 1;
    st->m_lastSnrCached = CACHE_INITIAL_VALUE;
    st->m_lastMode = GetDefaultMode();
    st->m_lastChannelWidth = 0;
    st->m_lastNss = 1;
}

void
GmaIdealWifiManager::DoReportRxOk(WifiRemoteStation* station, double rxSnr, WifiMode txMode)
{
    NS_LOG_FUNCTION(this << station << rxSnr << txMode);
}

void
GmaIdealWifiManager::DoReportRtsFailed(WifiRemoteStation* station)
{
    NS_LOG_FUNCTION(this << station);
}

void
GmaIdealWifiManager::DoReportDataFailed(WifiRemoteStation* station)
{
    NS_LOG_FUNCTION(this << station);
}

void
GmaIdealWifiManager::DoReportRtsOk(WifiRemoteStation* st,
                                double ctsSnr,
                                WifiMode ctsMode,
                                double rtsSnr)
{
    NS_LOG_FUNCTION(this << st << ctsSnr << ctsMode.GetUniqueName() << rtsSnr);
    GmaIdealWifiRemoteStation* station = static_cast<GmaIdealWifiRemoteStation*>(st);
    station->m_lastSnrObserved = rtsSnr;
    station->m_lastChannelWidthObserved =
        GetPhy()->GetChannelWidth() >= 40 ? 20 : GetPhy()->GetChannelWidth();
    station->m_lastNssObserved = 1;
    m_addrToStationMap[station->m_state->m_address] = station;
}

void
GmaIdealWifiManager::DoReportDataOk(WifiRemoteStation* st,
                                 double ackSnr,
                                 WifiMode ackMode,
                                 double dataSnr,
                                 uint16_t dataChannelWidth,
                                 uint8_t dataNss)
{
    NS_LOG_FUNCTION(this << st << ackSnr << ackMode.GetUniqueName() << dataSnr << dataChannelWidth
                         << +dataNss);
    GmaIdealWifiRemoteStation* station = static_cast<GmaIdealWifiRemoteStation*>(st);
    if (dataSnr == 0)
    {
        NS_LOG_WARN("DataSnr reported to be zero; not saving this report.");
        return;
    }
    station->m_lastSnrObserved = dataSnr;
    station->m_lastChannelWidthObserved = dataChannelWidth;
    station->m_lastNssObserved = dataNss;
    m_addrToStationMap[station->m_state->m_address] = station;
}

void
GmaIdealWifiManager::DoReportAmpduTxStatus(WifiRemoteStation* st,
                                        uint16_t nSuccessfulMpdus,
                                        uint16_t nFailedMpdus,
                                        double rxSnr,
                                        double dataSnr,
                                        uint16_t dataChannelWidth,
                                        uint8_t dataNss)
{
    NS_LOG_FUNCTION(this << st << nSuccessfulMpdus << nFailedMpdus << rxSnr << dataSnr
                         << dataChannelWidth << +dataNss);
    GmaIdealWifiRemoteStation* station = static_cast<GmaIdealWifiRemoteStation*>(st);
    if (dataSnr == 0)
    {
        NS_LOG_WARN("DataSnr reported to be zero; not saving this report.");
        return;
    }
    station->m_lastSnrObserved = dataSnr;
    station->m_lastChannelWidthObserved = dataChannelWidth;
    station->m_lastNssObserved = dataNss;
    m_addrToStationMap[station->m_state->m_address] = station;
}

void
GmaIdealWifiManager::DoReportFinalRtsFailed(WifiRemoteStation* station)
{
    NS_LOG_FUNCTION(this << station);
    Reset(station);
}

void
GmaIdealWifiManager::DoReportFinalDataFailed(WifiRemoteStation* station)
{
    NS_LOG_FUNCTION(this << station);
    Reset(station);
}

WifiTxVector
GmaIdealWifiManager::DoGetDataTxVector(WifiRemoteStation* st, uint16_t allowedWidth)
{
    NS_LOG_FUNCTION(this << st << allowedWidth);
    GmaIdealWifiRemoteStation* station = static_cast<GmaIdealWifiRemoteStation*>(st);
    
    // We search within the Supported rate set the mode with the
    // highest data rate for which the SNR threshold is smaller than m_lastSnr
    // to ensure correct packet delivery.
    WifiMode maxMode = GetDefaultModeForSta(st);
    WifiTxVector txVector;
    WifiMode mode;
    uint64_t bestRate = 0;
    uint8_t selectedNss = 1;
    uint16_t guardInterval;
    uint16_t channelWidth = std::min(GetChannelWidth(station), allowedWidth);
    txVector.SetChannelWidth(channelWidth);
    if ((station->m_lastSnrCached != CACHE_INITIAL_VALUE) &&
        (station->m_lastSnrObserved == station->m_lastSnrCached) &&
        (channelWidth == station->m_lastChannelWidth))
    {
        // SNR has not changed, so skip the search and use the last mode selected
        maxMode = station->m_lastMode;
        selectedNss = station->m_lastNss;
        NS_LOG_DEBUG("Using cached mode = " << maxMode.GetUniqueName() << " last snr observed "
                                            << station->m_lastSnrObserved << " cached "
                                            << station->m_lastSnrCached << " channel width "
                                            << station->m_lastChannelWidth << " nss "
                                            << +selectedNss);
    }
    else
    {
        if (GetHtSupported() && GetHtSupported(st))
        {
            for (uint8_t i = 0; i < GetNMcsSupported(station); i++)
            {
                mode = GetMcsSupported(station, i);
                txVector.SetMode(mode);
                if (mode.GetModulationClass() == WIFI_MOD_CLASS_HT)
                {
                    guardInterval = static_cast<uint16_t>(
                        std::max(GetShortGuardIntervalSupported(station) ? 400 : 800,
                                 GetShortGuardIntervalSupported() ? 400 : 800));
                    txVector.SetGuardInterval(guardInterval);
                    // If the node and peer are both VHT capable, only search VHT modes
                    if (GetVhtSupported() && GetVhtSupported(station))
                    {
                        continue;
                    }
                    // If the node and peer are both HE capable, only search HE modes
                    if (GetHeSupported() && GetHeSupported(station))
                    {
                        continue;
                    }
                    // Derive NSS from the MCS index. There is a different mode for each possible
                    // NSS value.
                    uint8_t nss = (mode.GetMcsValue() / 8) + 1;
                    txVector.SetNss(nss);
                    if (!txVector.IsValid() || nss > std::min(GetMaxNumberOfTransmitStreams(),
                                                              GetNumberOfSupportedStreams(st)))
                    {
                        NS_LOG_DEBUG("Skipping mode " << mode.GetUniqueName() << " nss " << +nss
                                                      << " width " << txVector.GetChannelWidth());
                        continue;
                    }
                    double threshold = GetSnrThreshold(txVector);
                    uint64_t dataRate = mode.GetDataRate(txVector.GetChannelWidth(),
                                                         txVector.GetGuardInterval(),
                                                         nss);
                    NS_LOG_DEBUG("Testing mode " << mode.GetUniqueName() << " data rate "
                                                 << dataRate << " threshold " << threshold
                                                 << " last snr observed "
                                                 << station->m_lastSnrObserved << " cached "
                                                 << station->m_lastSnrCached);
                    double snr = GetLastObservedSnr(station, channelWidth, nss);
                    if (dataRate > bestRate && threshold < snr)
                    {
                        NS_LOG_DEBUG("Candidate mode = " << mode.GetUniqueName() << " data rate "
                                                         << dataRate << " threshold " << threshold
                                                         << " channel width " << channelWidth
                                                         << " snr " << snr);
                        bestRate = dataRate;
                        maxMode = mode;
                        selectedNss = nss;
                    }
                }
                else if (mode.GetModulationClass() == WIFI_MOD_CLASS_VHT)
                {
                    guardInterval = static_cast<uint16_t>(
                        std::max(GetShortGuardIntervalSupported(station) ? 400 : 800,
                                 GetShortGuardIntervalSupported() ? 400 : 800));
                    txVector.SetGuardInterval(guardInterval);
                    // If the node and peer are both HE capable, only search HE modes
                    if (GetHeSupported() && GetHeSupported(station))
                    {
                        continue;
                    }
                    // If the node and peer are not both VHT capable, only search HT modes
                    if (!GetVhtSupported() || !GetVhtSupported(station))
                    {
                        continue;
                    }
                    for (uint8_t nss = 1; nss <= std::min(GetMaxNumberOfTransmitStreams(),
                                                          GetNumberOfSupportedStreams(station));
                         nss++)
                    {
                        txVector.SetNss(nss);
                        if (!txVector.IsValid())
                        {
                            NS_LOG_DEBUG("Skipping mode " << mode.GetUniqueName() << " nss " << +nss
                                                          << " width "
                                                          << txVector.GetChannelWidth());
                            continue;
                        }
                        double threshold = GetSnrThreshold(txVector);
                        uint64_t dataRate = mode.GetDataRate(txVector.GetChannelWidth(),
                                                             txVector.GetGuardInterval(),
                                                             nss);
                        NS_LOG_DEBUG("Testing mode = " << mode.GetUniqueName() << " data rate "
                                                       << dataRate << " threshold " << threshold
                                                       << " last snr observed "
                                                       << station->m_lastSnrObserved << " cached "
                                                       << station->m_lastSnrCached);
                        double snr = GetLastObservedSnr(station, channelWidth, nss);
                        if (dataRate > bestRate && threshold < snr)
                        {
                            NS_LOG_DEBUG("Candidate mode = "
                                         << mode.GetUniqueName() << " data rate " << dataRate
                                         << " channel width " << channelWidth << " snr " << snr);
                            bestRate = dataRate;
                            maxMode = mode;
                            selectedNss = nss;
                        }
                    }
                }
                else // HE
                {
                    guardInterval = std::max(GetGuardInterval(station), GetGuardInterval());
                    txVector.SetGuardInterval(guardInterval);
                    // If the node and peer are not both HE capable, only search (V)HT modes
                    if (!GetHeSupported() || !GetHeSupported(station))
                    {
                        continue;
                    }
                    for (uint8_t nss = 1; nss <= std::min(GetMaxNumberOfTransmitStreams(),
                                                          GetNumberOfSupportedStreams(station));
                         nss++)
                    {
                        txVector.SetNss(nss);
                        if (!txVector.IsValid())
                        {
                            NS_LOG_DEBUG("Skipping mode " << mode.GetUniqueName() << " nss " << +nss
                                                          << " width "
                                                          << +txVector.GetChannelWidth());
                            continue;
                        }
                        double threshold = GetSnrThreshold(txVector);
                        uint64_t dataRate = mode.GetDataRate(txVector.GetChannelWidth(),
                                                             txVector.GetGuardInterval(),
                                                             nss);
                        NS_LOG_DEBUG("Testing mode = " << mode.GetUniqueName() << " data rate "
                                                       << dataRate << " threshold " << threshold
                                                       << " last snr observed "
                                                       << station->m_lastSnrObserved << " cached "
                                                       << station->m_lastSnrCached);
                        double snr = GetLastObservedSnr(station, channelWidth, nss);
                        if (dataRate > bestRate && threshold < snr)
                        {
                            NS_LOG_DEBUG("Candidate mode = "
                                         << mode.GetUniqueName() << " data rate " << dataRate
                                         << " threshold " << threshold << " channel width "
                                         << channelWidth << " snr " << snr);
                            bestRate = dataRate;
                            maxMode = mode;
                            selectedNss = nss;
                        }
                    }
                }
            }
        }
        else
        {
            // Non-HT selection
            selectedNss = 1;
            for (uint8_t i = 0; i < GetNSupported(station); i++)
            {
                mode = GetSupported(station, i);
                txVector.SetMode(mode);
                txVector.SetNss(selectedNss);
                uint16_t channelWidth = GetChannelWidthForNonHtMode(mode);
                txVector.SetChannelWidth(channelWidth);
                double threshold = GetSnrThreshold(txVector);
                uint64_t dataRate = mode.GetDataRate(txVector.GetChannelWidth(),
                                                     txVector.GetGuardInterval(),
                                                     txVector.GetNss());
                NS_LOG_DEBUG("mode = " << mode.GetUniqueName() << " threshold " << threshold
                                       << " last snr observed " << station->m_lastSnrObserved);
                double snr = GetLastObservedSnr(station, channelWidth, 1);
                if (dataRate > bestRate && threshold < snr)
                {
                    NS_LOG_DEBUG("Candidate mode = " << mode.GetUniqueName() << " data rate "
                                                     << dataRate << " threshold " << threshold
                                                     << " snr " << snr);
                    bestRate = dataRate;
                    maxMode = mode;
                }
            }
        }
        NS_LOG_DEBUG("Updating cached values for station to " << maxMode.GetUniqueName() << " snr "
                                                              << station->m_lastSnrObserved);
        station->m_lastSnrCached = station->m_lastSnrObserved;
        station->m_lastMode = maxMode;
        station->m_lastNss = selectedNss;

    }
    NS_LOG_DEBUG("Found maxMode: " << maxMode << " channelWidth: " << channelWidth
                                   << " nss: " << +selectedNss);
    station->m_lastChannelWidth = channelWidth;
    m_addrToStationMap[station->m_state->m_address] = station;

    if (maxMode.GetModulationClass() == WIFI_MOD_CLASS_HE)
    {
        guardInterval = std::max(GetGuardInterval(station), GetGuardInterval());
    }
    else if ((maxMode.GetModulationClass() == WIFI_MOD_CLASS_HT) ||
             (maxMode.GetModulationClass() == WIFI_MOD_CLASS_VHT))
    {
        guardInterval =
            static_cast<uint16_t>(std::max(GetShortGuardIntervalSupported(station) ? 400 : 800,
                                           GetShortGuardIntervalSupported() ? 400 : 800));
    }
    else
    {
        guardInterval = 800;
    }
    WifiTxVector bestTxVector{
        maxMode,
        GetDefaultTxPowerLevel(),
        GetPreambleForTransmission(maxMode.GetModulationClass(), GetShortPreambleEnabled()),
        guardInterval,
        GetNumberOfAntennas(),
        selectedNss,
        0,
        GetPhy()->GetTxBandwidth(maxMode, channelWidth),
        GetAggregation(station)};
    uint64_t maxDataRate = maxMode.GetDataRate(bestTxVector);
    if (m_currentRate != maxDataRate)
    {
        NS_LOG_DEBUG("New datarate: " << maxDataRate);
        m_currentRate = maxDataRate;
    }
    return bestTxVector;
}

WifiTxVector
GmaIdealWifiManager::DoGetRtsTxVector(WifiRemoteStation* st)
{
    NS_LOG_FUNCTION(this << st);
    GmaIdealWifiRemoteStation* station = static_cast<GmaIdealWifiRemoteStation*>(st);
    // We search within the Basic rate set the mode with the highest
    // SNR threshold possible which is smaller than m_lastSnr to
    // ensure correct packet delivery.
    double maxThreshold = 0.0;
    WifiTxVector txVector;
    WifiMode mode;
    uint8_t nss = 1;
    WifiMode maxMode = GetDefaultMode();
    // RTS is sent in a non-HT frame
    for (uint8_t i = 0; i < GetNBasicModes(); i++)
    {
        mode = GetBasicMode(i);
        txVector.SetMode(mode);
        txVector.SetNss(nss);
        txVector.SetChannelWidth(GetChannelWidthForNonHtMode(mode));
        double threshold = GetSnrThreshold(txVector);
        if (threshold > maxThreshold && threshold < station->m_lastSnrObserved)
        {
            maxThreshold = threshold;
            maxMode = mode;
        }
    }
    return WifiTxVector(
        maxMode,
        GetDefaultTxPowerLevel(),
        GetPreambleForTransmission(maxMode.GetModulationClass(), GetShortPreambleEnabled()),
        800,
        GetNumberOfAntennas(),
        nss,
        0,
        GetChannelWidthForNonHtMode(maxMode),
        GetAggregation(station));
}

double
GmaIdealWifiManager::GetLastObservedSnr(GmaIdealWifiRemoteStation* station,
                                     uint16_t channelWidth,
                                     uint8_t nss) const
{
    double snr = station->m_lastSnrObserved;
    if (channelWidth != station->m_lastChannelWidthObserved)
    {
        snr /= (static_cast<double>(channelWidth) / station->m_lastChannelWidthObserved);
    }
    if (nss != station->m_lastNssObserved)
    {
        snr /= (static_cast<double>(nss) / station->m_lastNssObserved);
    }
    NS_LOG_DEBUG("Last observed SNR is " << station->m_lastSnrObserved << " for channel width "
                                         << station->m_lastChannelWidthObserved << " and nss "
                                         << +station->m_lastNssObserved << "; computed SNR is "
                                         << snr << " for channel width " << channelWidth
                                         << " and nss " << +nss);
    return snr;
}

void
GmaIdealWifiManager::MeasurementGuardIntervalEnd()
{
    Simulator::Schedule(m_measurementInterval, &GmaIdealWifiManager::MeasurementIntervalEnd, this);
    m_measurementActive = true; //start measurement
}

void
GmaIdealWifiManager::MeasurementIntervalEnd()
{
    //std::cout <<"Measurement Interval end:" << Now().GetSeconds() << " map size:" << m_addrToStationMap.size() << std::endl;
    std::map<Mac48Address, GmaIdealWifiRemoteStation*>::iterator iter = m_addrToStationMap.begin();
    while(iter!=m_addrToStationMap.end())
    {
        //if(iter->second->m_lastChannelWidth != 0)
        {
            //std::cout << Simulator::Now().GetSeconds() << " mode:" << iter->second->m_lastMode.GetUniqueName() 
            //<< " test:" << iter->second->m_lastChannelWidth << std::endl;
            WifiTxVector txVector = DoGetDataTxVector(iter->second, iter->second->m_lastChannelWidth);
            m_rateMeasurement(txVector.GetMode().GetDataRate(txVector), iter->first);
        }
        iter++;
    }
    Simulator::Schedule(m_measurementGuardInterval, &GmaIdealWifiManager::MeasurementGuardIntervalEnd, this);
    m_measurementActive = false; //start measurement guard time. pause measurement
}

} // namespace ns3
