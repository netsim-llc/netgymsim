/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

//              (0,5)- - - - - - - - - - -+ 
//               enb                      |
//                |                       |
//    (-10,0)   (0,0)  p2p (5,0)  Wi-Fi (10, 5*(m-1))
//    server ---router ----- AP1 - - - - client1, client2, ..., clientm
//                |                       |
//                |        (5,-dis)        |
//                + ------- AP2 - - - - - +
//                |         ...           |
//                |        (5,-dis*(n-1))  |
//                + ------- APn - - - - - +
// server to router 10.1.1.0
// router to APn 10.2.n.0
// APn to clients 10.3.n.0
//
// a total of m clients
// virtual client IP 10.1.1.101 - 10.1.1.101+m

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/virtual-net-device.h"
#include "ns3/wifi-module.h"
#include "ns3/gma-protocol.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include <ns3/spectrum-module.h>
#include <ns3/fq-ppp-queue-disc.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("StaticRoutingSlash32Test");

std::vector<Ipv4InterfaceContainer> g_iCList;
std::vector<Ipv4InterfaceContainer> g_iRiAPList;
Ptr<GmaProtocol> g_routerGma;


  NodeContainer g_clientNodes;
  NodeContainer g_eNodeBs;
  NodeContainer g_apNodes;
  std::vector<NetDeviceContainer> g_stationDeviceList;//first demension is the AP id, the seconds demension is the user station id.
  bool g_crossLayer = true;

  Time g_stopTime = Seconds(100.0);
  double g_wifiApSwitchPowerThresh = -80;

  uint32_t g_acceptableDelayMs = 20;

  void
NotifyHandoverStartUe (std::string context,
                       uint64_t imsi,
                       uint16_t cellId,
                       uint16_t rnti,
                       uint16_t targetCellId)
{
  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " UE IMSI " << imsi
            << ": previously connected to CellId " << cellId
            << " with RNTI " << rnti
            << ", doing handover to CellId " << targetCellId
            << std::endl;
}

void
NotifyHandoverEndOkUe (std::string context,
                       uint64_t imsi,
                       uint16_t cellId,
                       uint16_t rnti)
{
  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " UE IMSI " << imsi
            << ": successful handover to CellId " << cellId
            << " with RNTI " << rnti
            << std::endl;
}

void
NotifyHandoverStartEnb (std::string context,
                        uint64_t imsi,
                        uint16_t cellId,
                        uint16_t rnti,
                        uint16_t targetCellId)
{
  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " eNB CellId " << cellId
            << ": start handover of UE with IMSI " << imsi
            << " RNTI " << rnti
            << " to CellId " << targetCellId
            << std::endl;
}

void
NotifyHandoverEndOkEnb (std::string context,
                        uint64_t imsi,
                        uint16_t cellId,
                        uint16_t rnti)
{
  std::cout << Simulator::Now ().GetSeconds () << " " << context
            << " eNB CellId " << cellId
            << ": completed handover of UE with IMSI " << imsi
            << " RNTI " << rnti
            << std::endl;
}

static void RcvFrom (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet, const Address &from)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << packet->GetSize()<< std::endl;
}

struct AppMeasureParam : public SimpleRefCount<AppMeasureParam>
{
  //loss = m_endSn - m_startSn - m_pktCount;
  uint32_t m_startSn = 0;
  uint32_t m_endSn = 0;
  uint32_t m_pktCount;

  //owd
  uint32_t m_minOwd = UINT32_MAX;
  uint32_t m_maxOwd = 0;
  uint64_t m_owdSum = 0;
  uint32_t m_highDelayPkt = 0;
};

typedef std::pair<int, uint8_t> AppMeasureKey_t; //the first one is the clientInd, the second one is the ppp.

std::map <AppMeasureKey_t, Ptr<AppMeasureParam> > m_pppToMeasureParam;


static int Sn32Diff(int x1, int x2)
{
	int diff = x1 - x2;
	if (diff > 1073741824)
	{
		diff = diff - 2147483648;
	}
	else if (diff < -1073741824)
	{
		diff = diff + 2147483648;
	}
	return diff;
}

static void Rcv (Ptr<OutputStreamWrapper> stream, int clientInd, Ptr<const Packet> packet)
{
  PppTag tag;
  packet->PeekPacketTag (tag);

  SeqTsHeader hdr;
  packet->PeekHeader (hdr);

  AppMeasureKey_t tempKey = std::make_pair(clientInd, tag.GetPriority());


  if(m_pppToMeasureParam.find(tempKey) == m_pppToMeasureParam.end())
  {
    Ptr<AppMeasureParam> measureParam = Create <AppMeasureParam> ();
    m_pppToMeasureParam[tempKey] = measureParam;
  }

  //loss
  if(Sn32Diff(hdr.GetSeq(),  m_pppToMeasureParam[tempKey]->m_endSn) > 0)
  {
    m_pppToMeasureParam[tempKey]->m_endSn = hdr.GetSeq();
  }
  m_pppToMeasureParam[tempKey]->m_pktCount++;

  //owd
  uint32_t owd = Simulator::Now ().GetMilliSeconds() - hdr.GetTs ().GetMilliSeconds();
  if(owd > g_acceptableDelayMs)
  {
    m_pppToMeasureParam[tempKey]->m_highDelayPkt++;
  }

  if(m_pppToMeasureParam[tempKey]->m_maxOwd < owd)
  {
    m_pppToMeasureParam[tempKey]->m_maxOwd = owd;
  }

  if(m_pppToMeasureParam[tempKey]->m_minOwd > owd)
  {
    m_pppToMeasureParam[tempKey]->m_minOwd = owd;
  }
  m_pppToMeasureParam[tempKey]->m_owdSum += owd;

  /**stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << packet->GetSize() << "\t"
  << +tag.GetPriority() << "\t"
  << hdr.GetSeq() << "\t"
  << owd << "\t"
  << std::endl;*/

}

static void AppMeasurement ()
{

  std::map <AppMeasureKey_t, Ptr<AppMeasureParam> >::iterator iter = m_pppToMeasureParam.begin();
  while(iter != m_pppToMeasureParam.end())
  {
    //log file;
    std::ostringstream fileName;
    fileName <<"app-"<<std::get<0>(iter->first)<<".txt";
    std::ofstream myfile;
    myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);

    int loss = Sn32Diff(iter->second->m_endSn, iter->second->m_startSn)- iter->second->m_pktCount;

    myfile << Now().GetSeconds() << "\t" << +std::get<1>(iter->first) 
    << "\t" << iter->second->m_startSn << "\t" << iter->second->m_endSn
    << "\t";

    if(iter->second->m_pktCount > 0)
    {
      myfile << iter->second->m_pktCount << "\t"  << loss << "\t";
      myfile << iter->second->m_minOwd << "\t"
      << iter->second->m_owdSum/iter->second->m_pktCount << "\t"
      << iter->second->m_maxOwd << "\t"
      << iter->second->m_highDelayPkt << "\n";
    }
    else{
      myfile << "\n";
    }
    myfile.close();

    /*std::cout<< Now().GetSeconds() << " client:" << std::get<0>(iter->first) << " ppp:" << +std::get<1>(iter->first) 
    << " start SN: " << iter->second->m_startSn << " end SN:" << iter->second->m_endSn
    << " pkt:" << iter->second->m_pktCount
    << " Lost = "<< iter->second->m_endSn - iter->second->m_startSn << "-" << iter->second->m_pktCount 
    << "=" <<  loss;
    if(iter->second->m_pktCount > 0)
    {
      std::cout << " owdmin:"<< iter->second->m_minOwd << " owdMean:"
      << iter->second->m_owdSum/iter->second->m_pktCount << " owdMax:"
      << iter->second->m_maxOwd << " highDelay:" << iter->second->m_highDelayPkt << "\n";
    }
    else
    {
      std::cout << "\n";
    }*/

    //reset
    iter->second->m_startSn = iter->second->m_endSn;
    iter->second->m_pktCount = 0;

    iter->second->m_highDelayPkt = 0;
    iter->second->m_minOwd = UINT32_MAX;
    iter->second->m_maxOwd = 0;
    iter->second->m_owdSum = 0;

    iter++;
  }
}


static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldRtt.GetSeconds () << "\t" << newRtt.GetSeconds () << std::endl;
}

static void
DonwlinkTraces(uint16_t nodeNum)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW<<"/NodeList/0/$ns3::TcpL4Protocol/SocketList/"<<nodeNum<<"/CongestionWindow";

  std::ostringstream fileCW;
  fileCW<<"cwnd-"<<nodeNum<<".txt";

  std::ostringstream pathRTT;
  pathRTT<<"/NodeList/0/$ns3::TcpL4Protocol/SocketList/"<<nodeNum<<"/RTT";

  std::ostringstream fileRTT;
  fileRTT<<"rtt-"<<nodeNum<<".txt";

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

}

static void
UplinkTraces(uint16_t nodeNum)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW<<"/NodeList/"<<nodeNum+1<<"/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow";

  std::ostringstream fileCW;
  fileCW<<"cwnd-"<<nodeNum<<".txt";

  std::ostringstream pathRTT;
  pathRTT<<"/NodeList/"<<nodeNum+1<<"/$ns3::TcpL4Protocol/SocketList/0/RTT";

  std::ostringstream fileRTT;
  fileRTT<<"rtt-"<<nodeNum<<".txt";

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

}

/*static void
ChangeSpeed(Ptr<Node> node)
{
   node->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (-2, 0, 0));
}*/

static int
GetClosestWifiAp (Ptr<Node> userNode)
{
  NS_ASSERT_MSG (g_apNodes.GetN () > 0, "empty wifi node container");
  Vector uepos = userNode->GetObject<MobilityModel> ()->GetPosition ();

  double minDistance = std::numeric_limits<double>::infinity ();
  Ptr<Node> apNode;
  int minDisApId = -1;
  for (uint32_t apInd = 0; apInd < g_apNodes.GetN(); apInd++)
  {
      apNode = g_apNodes.Get(apInd);
      Vector enbpos = apNode->GetObject<MobilityModel> ()->GetPosition ();
      double distance = CalculateDistance (uepos, enbpos);
      if (distance < minDistance)
        {
          minDistance = distance;
          minDisApId = apInd; 
        }
  }

  NS_ASSERT (minDisApId != -1);
  return minDisApId;
}

static void
UpdateStatus(){
          //log file
  std::ifstream ifTraceFile;
  ifTraceFile.open ("stop.txt", std::ifstream::in);
  if (ifTraceFile.good ())
  {
    //stop the simulatio right now!!
    std::ostringstream fileName;
    fileName <<"status.txt";
    std::ofstream myfile;
    myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);
    time_t seconds;

    seconds = time (NULL);
    myfile << (int)seconds << "/"<< g_stopTime.GetSeconds() << "/" <<g_stopTime.GetSeconds() <<"\n";
    myfile.close();

    NS_FATAL_ERROR("find the stop file");

  }
  else
  {
    std::ostringstream fileName;
    fileName <<"status.txt";
    std::ofstream myfile;
    myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);
    time_t seconds;

    seconds = time (NULL);

    myfile << (int)seconds << "/"<< Simulator::Now ().GetSeconds () << "/" <<g_stopTime.GetSeconds() <<"\n";
    myfile.close();
  }
    
}


static void 
LogLocations (int distance, double userSpeed){

      //log file
    std::ostringstream fileName;
    fileName <<"config.txt";
    std::ofstream myfile;
    myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);
    if(Simulator::Now ().GetSeconds () < 0.1)
    {
        myfile << "EnB locations: ";
        for (uint32_t apInd = 0; apInd < g_eNodeBs.GetN(); apInd++)
        {
            Vector enbpos = g_eNodeBs.Get(apInd)->GetObject<MobilityModel> ()->GetPosition ();
            myfile <<"["<< +enbpos.x << "," << +enbpos.y << "," << +enbpos.z << "] ";
        }
        myfile << "\n";
        for (uint32_t apInd = 0; apInd < g_apNodes.GetN(); apInd++)
        {
            Vector wifipos = g_apNodes.Get(apInd)->GetObject<MobilityModel> ()->GetPosition ();
            myfile << "Wi-Fi AP "<< apInd+1 << " location: [" << +wifipos.x << "," << +wifipos.y << "," << +wifipos.z << "]\n";
        }
         for (uint32_t apInd = 0; apInd < g_clientNodes.GetN(); apInd++)
        {
            Vector uepos = g_clientNodes.Get(apInd)->GetObject<MobilityModel> ()->GetPosition ();
            myfile << "UE "<< apInd+1 << " location: [" << +uepos.x << "," << +uepos.y << "," << +uepos.z << "]\n";
        }
    }
    //myfile << "Time: "<< Simulator::Now ().GetSeconds ()<< " Second\n";

    for (uint32_t apInd = 0; apInd < g_clientNodes.GetN(); apInd++)
    {
        Vector uepos = g_clientNodes.Get(apInd)->GetObject<MobilityModel> ()->GetPosition ();
        //myfile << "UE "<< apInd+1 << " location: " << +uepos.x << "," << +uepos.y << "," << +uepos.z << "\n";
        if(userSpeed > 0)
        {
          if(uepos.x >= (4*distance - 1))
          {
            g_clientNodes.Get(apInd)->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (userSpeed*(-1), 0, 0));
          }
          else if (uepos.x <= 1)
          {
            g_clientNodes.Get(apInd)->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (userSpeed, 0, 0));
          }
        }

    }
    myfile.close();
    UpdateStatus();
    AppMeasurement();
    Simulator::Schedule (Seconds(1.0), &LogLocations, distance, userSpeed);

}

class MyApp : public Application
{
public:
  MyApp ();
  virtual ~MyApp ();

  /**
   * Register this type.
   * \return The TypeId.
   */
  static TypeId GetTypeId (void);
  void Setup (Ptr<Socket> socket, Address address);
  void AddStream (uint32_t packetSize, DataRate dataRate, uint8_t ppp);

private:

  struct StreamParams : public SimpleRefCount<StreamParams>
  {
    EventId         m_sendEvent;
    DataRate        m_dataRate;
    uint32_t        m_packetSize;
    uint32_t        m_packetsSent;
    uint8_t         m_ppp;
    uint8_t         m_streamId;
    Ptr<UniformRandomVariable> m_uniform;
  };

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (Ptr<StreamParams> streamParams);
  void SendPacket (Ptr<StreamParams> streamParams);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  bool            m_running;
  std::vector< Ptr<StreamParams> > m_streamParamsList;
};

MyApp::MyApp ()
  : m_socket (0),
    m_peer (),
    m_running (false)
{
}

MyApp::~MyApp ()
{
  m_socket = 0;
}

/* static */
TypeId MyApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("MyApp")
    .SetParent<Application> ()
    .SetGroupName ("Tutorial")
    .AddConstructor<MyApp> ()
    ;
  return tid;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address)
{
  m_socket = socket;
  m_peer = address;
}

void
MyApp::AddStream (uint32_t packetSize, DataRate dataRate, uint8_t ppp)
{
  Ptr<StreamParams> streamParams = Create<StreamParams> ();
  streamParams->m_packetSize = packetSize;
  streamParams->m_dataRate = dataRate;
  streamParams->m_ppp = ppp;
  streamParams->m_packetsSent = 0;
  streamParams->m_streamId = m_streamParamsList.size();
  streamParams->m_uniform = CreateObject<UniformRandomVariable> ();
  m_streamParamsList.push_back(streamParams);
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  for(uint32_t ind = 0; ind < m_streamParamsList.size(); ind++)
  {
    SendPacket (m_streamParamsList.at(ind));
  }
}

void
MyApp::StopApplication (void)
{
  m_running = false;

  for(uint32_t ind = 0; ind < m_streamParamsList.size(); ind++)
  {
  if (m_streamParamsList.at(ind)-> m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_streamParamsList.at(ind)-> m_sendEvent);
    }
  }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void
MyApp::SendPacket (Ptr<StreamParams> streamParams)
{
  streamParams->m_packetsSent++;
  Ptr<Packet> packet = Create<Packet> (streamParams->m_packetSize);

  PppTag tag;
  tag.SetPriority (streamParams->m_ppp);
  packet->AddPacketTag (tag);

  SeqTsHeader hdr;
  hdr.SetSeq(streamParams->m_packetsSent);
  packet->AddHeader(hdr);

  m_socket->Send (packet);
  ScheduleTx (streamParams);
}

void
MyApp::ScheduleTx (Ptr<StreamParams> streamParams)
{
  if (m_running)
    {
      //uint64_t scaleN = Now().GetMilliSeconds()/10000%3; // 0 1 and 2
      //double scaler[] = {0.5, 0.75, 1};
      //Time tNext (Seconds (streamParams->m_packetSize * 8 / static_cast<double> (streamParams->m_dataRate.GetBitRate ()*scaler[scaleN])));
      Time tNext (Seconds (streamParams->m_packetSize * 8 / static_cast<double> (streamParams->m_dataRate.GetBitRate ())));

      //streamParams->m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this, streamParams);
      Time randomizeTime = MicroSeconds (streamParams->m_uniform ->GetInteger (0, 1000)) - MicroSeconds(500) + tNext;
      streamParams->m_sendEvent = Simulator::Schedule (randomizeTime, &MyApp::SendPacket, this, streamParams);
    }
}


int 
main (int argc, char *argv[])
{
  // 0 wifi, 11 lte, -1 all
  int radioType = 0;
  int numOfClients = 1;
  int distance = 60;
  bool tcpData = true;
  int packetSize = 1400;
  double updRateMbps = 2.0;
  std::string wifiBackhaulDelay = "0ms";
  double userSpeed = 2.0; //m/s
  bool downlink = true;
  double wifiLowPowerThresh = -78.0;
  double wifiHighPowerThresh = -75.0;
  bool enableRtsCts = false;
  int wifiDownTime = 10; //ms
  std::string dlGmaMode = "auto"; // 3 modes: "auto"; "split"; "steer"; "wifi" or "cell";
  std::string ulGmaMode = "auto"; // 3 modes: "auto"; "split"; "steer"; "wifi" or "cell";
  double gmaDelayThresh = 5;
  uint8_t cellCid= 11;
  std::string aqmType = "PPP_AQM_V2";
  // Allow the user to override any of the defaults and the above
  // DefaultValue::Bind ()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.AddValue ("stopTime", "simulation stop time", g_stopTime);
  cmd.AddValue ("radioType", "simulation stop time", radioType);
  cmd.AddValue ("numOfClients", "number of users", numOfClients);
  cmd.AddValue ("distance", "distance between two wifi ap", distance);
  cmd.AddValue ("tcpData", "tcp data traffic", tcpData);
  cmd.AddValue ("packetSize", "packet size", packetSize);
  cmd.AddValue ("updRateMbps", "upp rate mbps", updRateMbps);
  cmd.AddValue ("AcceptableDelayMs", "Acceptable Delay in Ms", g_acceptableDelayMs);
  cmd.AddValue ("wifiBackhaulDelay", "wifi backhaul delay", wifiBackhaulDelay);
  cmd.AddValue ("userSpeed", "user speed m/s", userSpeed);
  cmd.AddValue ("downlink", "downlink or uplink", downlink);
  cmd.AddValue ("wifiLowPowerThresh", "wifi low power thresh", wifiLowPowerThresh);
  cmd.AddValue ("wifiHighPowerThresh", "wifi high power thresh", wifiHighPowerThresh);
  cmd.AddValue ("enableRtsCts", "wifi rts cts", enableRtsCts);
  cmd.AddValue ("g_wifiApSwitchPowerThresh", "power thresh that control switches between wifi AP", g_wifiApSwitchPowerThresh);
  cmd.AddValue ("wifiDownTime", "time interval to disable wifi after ap switch", wifiDownTime);
  cmd.AddValue ("dlGmaMode", "GMA mode for dl traffic", dlGmaMode);
  cmd.AddValue ("ulGmaMode", "GMA mode for ul traffic", ulGmaMode);
  cmd.AddValue ("gmaDelayThresh", "GMA Thresh", gmaDelayThresh);
  cmd.AddValue ("aqmType", "Active Queue Management Type", aqmType);

  cmd.Parse (argc, argv);

  std::ostringstream fileName;
  fileName <<"config.txt";
  std::ofstream myfile;
  myfile.open (fileName.str ().c_str (), std::ios::out | std::ios::app);

  if(radioType == 0)
  {
    Config::SetDefault ("ns3::GmaTxControl::SplittingAlgorithm", EnumValue (GmaTxControl::DefaultLink));//Tx use default link
    Config::SetDefault ("ns3::GmaRxControl::SplittingAlgorithm", EnumValue (GmaRxControl::DefaultLink));//Rx use default link, no tsu update
    Config::SetDefault ("ns3::LinkState::DefaultLinkCid", UintegerValue (0));//default over wifi
    Config::SetDefault ("ns3::LinkState::FixDefaultLink", BooleanValue (true));//never update default link

    myfile << "Radio: Wifi Only\n";
    dlGmaMode = "wifi";
    ulGmaMode = "wifi";

  }
  else if(radioType == cellCid)
  {
    Config::SetDefault ("ns3::GmaTxControl::SplittingAlgorithm", EnumValue (GmaTxControl::DefaultLink));//Tx use default link
    Config::SetDefault ("ns3::GmaRxControl::SplittingAlgorithm", EnumValue (GmaRxControl::DefaultLink));//Rx use default link, no tsu update
    Config::SetDefault ("ns3::LinkState::DefaultLinkCid", UintegerValue (cellCid));//default over LTE
    Config::SetDefault ("ns3::LinkState::FixDefaultLink", BooleanValue (true));//never update default link

    myfile << "Radio: Cell Only\n";

    dlGmaMode = "cell";
    ulGmaMode = "cell";

  }
  else
  {
    myfile << "Radio: GMA (Wi-Fi and Cell)\n";

    if(dlGmaMode == "auto")
    {
      //change dl mode for auto mode
      if(tcpData && downlink)
      {
        dlGmaMode = "split";
      }
      else
      {
        dlGmaMode = "steer";
      }
      myfile << "Downlink GMA mode: auto(config as " << dlGmaMode << ")\n";

    }
    else
    {
      myfile << "Downlink GMA mode:" << dlGmaMode << "\n";
    }

    if(ulGmaMode == "auto")
    {
      //change ul mode for auto mode
      if(tcpData && !downlink)
      {
        ulGmaMode = "split";
      }
      else
      {
        ulGmaMode = "steer";
      }
      myfile << "Uplink GMA mode: auto(config as " << ulGmaMode << ")\n";
    }
    else
    {
      myfile << "Uplink GMA mode:" << ulGmaMode << "\n";
    }
  }

  uint64_t wifiMaxDelay = 200;
  Config::SetDefault ("ns3::WifiMacQueue::MaxDelay", TimeValue (MilliSeconds (wifiMaxDelay)));
  //int qSize = 1000*(numOfClients/16 + (numOfClients % 16 != 0));
  int qSize = 1000;
  Config::SetDefault ("ns3::WifiMacQueue::MaxSize", QueueSizeValue (QueueSize (std::to_string(qSize) + "p")));
  //Config::SetDefault ("ns3::WifiPhy::CcaEdThreshold", DoubleValue (-62.0));
  //Config::SetDefault ("ns3::WifiMacQueue::DropPolicy", EnumValue(WifiMacQueue::DROP_OLDEST));

  if (aqmType == "NO_AQM")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::NO_AQM));
  }
  else if (aqmType == "CODEL")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::MULTI_QUEUE));
    Config::SetDefault ("ns3::FqPppQueueDisc::MaxSize",
                QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, qSize)));
    Config::SetDefault ("ns3::FqPppQueueDisc::Type",EnumValue (FqPppQueueDisc::CoDel));
  }
  else if (aqmType == "PPP")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::MULTI_QUEUE));
    Config::SetDefault ("ns3::FqPppQueueDisc::MaxSize",
                QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, qSize)));
    Config::SetDefault ("ns3::FqPppQueueDisc::Type",EnumValue (FqPppQueueDisc::PPP));
  }
  else if (aqmType == "PPP_DELAY")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::MULTI_QUEUE));
    Config::SetDefault ("ns3::FqPppQueueDisc::MaxSize",
                QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, qSize)));
    Config::SetDefault ("ns3::FqPppQueueDisc::Type",EnumValue (FqPppQueueDisc::PPP_DELAY));
  }
    else if (aqmType == "PPP_AQM")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::MULTI_QUEUE));
    Config::SetDefault ("ns3::FqPppQueueDisc::MaxSize",
                QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, qSize)));
    Config::SetDefault ("ns3::FqPppQueueDisc::Type",EnumValue (FqPppQueueDisc::PPP_AQM));
  }
  else if (aqmType == "PPP_AQM_V2")
  {
    //Config::SetDefault ("ns3::WifiMacQueue::AQM", EnumValue (WifiMacQueue::MULTI_QUEUE));
    Config::SetDefault ("ns3::FqPppQueueDisc::MaxSize",
                QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, qSize)));
    Config::SetDefault ("ns3::FqPppQueueDisc::Type",EnumValue (FqPppQueueDisc::PPP_AQM_V2));
  }
  else
  {
    NS_FATAL_ERROR("unknwon AQM type:" << aqmType);
  }
  myfile << "AQM Type: " << aqmType << " (If AQM enabled, Wi-Fi Queue Max Size stands for the size of AQM queue)\n";


  if(downlink)
  {
    myfile <<"Downlink Traffic\n";
  }
  else
  {
    myfile << "Uplink Traffic\n";
  }

  if(tcpData)
  {
     myfile << "Traffic: high throughput (TCP)" << "\n";

  }
  else
  {
     myfile << "Traffic: low-latency (UDP)" << "\n";
     myfile << "Rate per user: " << updRateMbps << " mbps\n";
  }
  myfile << "Packet size: " << packetSize << " bytes\n";

  if(enableRtsCts)
  {
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("0"));
    myfile << "Wi-Fi RTS/CTS enabled!\n";
  }
  Config::SetDefault ("ns3::PhyAccessControl::SimulateLinkDownTime", TimeValue(MilliSeconds(wifiDownTime)));
  myfile << "Wi-Fi down time after AP switch "<< wifiDownTime <<" ms!\n";


  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1 << 24));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1 << 24));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName ("ns3::TcpCubic")));
  SeedManager::SetSeed (4);

  Config::SetDefault ("ns3::GmaVirtualInterface::AcceptableDelayMs", UintegerValue (g_acceptableDelayMs));

  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  Config::SetDefault ("ns3::GmaVirtualInterface::WifiLowPowerThresh", DoubleValue (wifiLowPowerThresh));
  Config::SetDefault ("ns3::GmaVirtualInterface::WifiHighPowerThresh", DoubleValue (wifiHighPowerThresh));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUm::ReorderingTimer", TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (1024 * 1024));

  Config::SetDefault ("ns3::LteAmc::AmcModel", EnumValue (LteAmc::PiroEW2010));
  Config::SetDefault ("ns3::StaWifiMac::MaxMissedBeacons", UintegerValue (100));
  
  Config::SetDefault ("ns3::ArpCache::WaitReplyTimeout", TimeValue (MilliSeconds (300)));
  Config::SetDefault ("ns3::ArpCache::AliveTimeout", TimeValue (Seconds (1020)));
  Config::SetDefault ("ns3::ArpCache::DeadTimeout", TimeValue (Seconds (1000)));


  //Config::SetDefault ("ns3::LteUePhy::TxPower", DoubleValue (30.0));
  //Config::SetDefault ("ns3::LteUePhy::EnableUplinkPowerControl", BooleanValue (false));

  //ca
  Config::SetDefault ("ns3::LteHelper::UseCa", BooleanValue (true));
  Config::SetDefault ("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue (4));
  Config::SetDefault ("ns3::LteHelper::EnbComponentCarrierManager", StringValue ("ns3::RrComponentCarrierManager"));

  //2 layers mimo
  Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (2)); // MIMO Spatial Multiplexity (2 layers) ONLY for Downlink...

  //configure he guard interval
  Config::SetDefault ("ns3::HeConfiguration::GuardInterval", TimeValue (NanoSeconds(1600))); //1600 or 800?
  Config::SetDefault ("ns3::GmaRxControl::DelayThresh", DoubleValue (gmaDelayThresh)); //1600 or 800?


  Config::SetDefault ("ns3::PhyAccessControl::RoamingLowRssi", DoubleValue(g_wifiApSwitchPowerThresh));

  myfile << "Wi-Fi Power AP switch:" << g_wifiApSwitchPowerThresh <<"dBm; Low Thresh:" << wifiLowPowerThresh << "dBm; High thresh:" << wifiHighPowerThresh <<"dBm\n";
  myfile << "Num of users: " << numOfClients << "\n";
  myfile << "User speed: " << userSpeed << " m/s\n";
  myfile << "Simulation time: " << g_stopTime.GetSeconds() << " seconds\n";
  myfile << "D (Wi-Fi AP Grid size): " << distance << " meters\n";
  myfile << "Acceptable edge owd: " << g_acceptableDelayMs << " ms\n";
  myfile << "Wi-Fi backhaul delay: " << wifiBackhaulDelay << "\n";
  myfile << "GMA split algorithm delay thresh: " << gmaDelayThresh << " ms\n";

  myfile << "Wi-Fi Queue MaxDelay:" << wifiMaxDelay << " ms" << ", Queue MaxSize:" << qSize << " packets\n";
  myfile << "LTE UM mode, buffer size: 1MB, t-reordering: 10 ms\n";

  

  std::vector< Vector > ueLocations;

  std::ifstream ifTraceFile;
  ifTraceFile.open ("../../userlocation.txt", std::ifstream::in);
  if (!ifTraceFile.good ())
    {
        ifTraceFile.open ("../userlocation.txt", std::ifstream::in);
        if (!ifTraceFile.good ())
          {
            ifTraceFile.open ("userlocation.txt", std::ifstream::in);
            if(!ifTraceFile.good ())
            {
              NS_FATAL_ERROR("UE location file not found !!!!");
            }
          }
    }

//   NS_LOG_INFO (this << " length " << m_traceLength.GetSeconds ());
//   NS_LOG_INFO (this << " RB " << (uint32_t)m_rbNum << " samples " << m_samplesNum);
  for (int i = 0; i < numOfClients; i++)
    {
      double xLoc;
      double yloc;
      double zloc;
      ifTraceFile >> xLoc >> yloc >> zloc;
      //std::cout << xLoc << " " << yloc << " " << zloc << "\n";
      ueLocations.push_back(Vector(xLoc, yloc, zloc));
    }


  int numOfAps = 8;
  int numOfEnbs = 4;
  //std::string udpRate = "10Kb/s";
  //std::string udpRate = "100Mb/s";
  bool enableTCPtrace = false;
  bool fixWifiRate = false;
  //bool fixWifiRate = true;
  bool enableRxTrace = true;


  std::vector<Ipv4Address> clientVirtualIpList;
  for (int clientInd = 0; clientInd < numOfClients; clientInd++)
  {
    //store the virtual IPs for clients.
    std::ostringstream virtualIp;
    virtualIp << "10.1.1." << 101+clientInd;//start from 10.1.1.101
    clientVirtualIpList.push_back(Ipv4Address(virtualIp.str ().c_str()));
  }

  Ptr<Node> server = CreateObject<Node> ();
  //Ptr<Node> router = CreateObject<Node> ();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();

  //NodeContainer g_clientNodes;
  g_clientNodes.Create(numOfClients);
  //NodeContainer g_eNodeBs;
  g_eNodeBs.Create(numOfEnbs);

  //NodeContainer g_apNodes;
  g_apNodes.Create(numOfAps);

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  //lteHelper->SetSchedulerAttribute ("UlCqiFilter", EnumValue (FfMacScheduler::PUSCH_UL_CQI));

  lteHelper->SetFfrAlgorithmType ("ns3::LteFrHardAlgorithm");

  lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");
  lteHelper->SetHandoverAlgorithmAttribute ("Hysteresis",
                                              DoubleValue (3.0));
  lteHelper->SetHandoverAlgorithmAttribute ("TimeToTrigger",
                                              TimeValue (MilliSeconds (256)));

  //lteHelper->SetHandoverAlgorithmType ("ns3::A2A4RsrqHandoverAlgorithm");
  //lteHelper->SetHandoverAlgorithmAttribute ("ServingCellThreshold",
  //                                          UintegerValue (30));
  //lteHelper->SetHandoverAlgorithmAttribute ("NeighbourCellOffset",
  //                                          UintegerValue (1));

  lteHelper->SetEpcHelper (epcHelper);
  lteHelper->SetEnbDeviceAttribute ("DlBandwidth", UintegerValue (100)); //maxmize LTE bandwidth.
  lteHelper->SetEnbDeviceAttribute ("UlBandwidth", UintegerValue (100)); //maxmize LTE bandwidth.
  lteHelper->SetSchedulerAttribute ("HarqEnabled",  BooleanValue (true));
  Ptr<Node> router = epcHelper->GetPgwNode ();

  InternetStackHelper internet;
  internet.Install (server);
  //internet.Install (router);
  internet.Install (g_apNodes);
  internet.Install (g_clientNodes);

  // Point-to-point links
  // We create the channels first without any IP addressing information
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gb/s"));
  p2p.SetChannelAttribute ("Delay", StringValue ("10ms"));

    // Wifi network
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());

  //4 wifi antenas
  phy.Set ("Antennas", UintegerValue (2));
  phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
  phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax);
  if (fixWifiRate)
  {
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", 
                                  "DataMode", StringValue ("HtMcs0"), 
                                  "ControlMode", StringValue ("HtMcs0"));
  }
  else
  {
    //wifi.SetRemoteStationManager ("ns3::MinstrelHtWifiManager");
    wifi.SetRemoteStationManager ("ns3::IdealWifiManager");
    
  }
  WifiMacHelper mac;

  NodeContainer nSnR = NodeContainer (server, router);
  NetDeviceContainer dSdR = p2p.Install (nSnR);

    // Later, we add IP addresses.
  Ipv4AddressHelper ipv4Server;
  ipv4Server.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iSiR = ipv4Server.Assign (dSdR);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  //Create static routes from server to client
  Ptr<Ipv4StaticRouting> staticRoutingS = ipv4RoutingHelper.GetStaticRouting (server->GetObject<Ipv4> ());
  // add the client network to the server routing table
  staticRoutingS->AddNetworkRouteTo (Ipv4Address ("10.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);
  staticRoutingS->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  std::vector<Ipv4InterfaceContainer> iAPList;

  // Create static routes from router to AP
  Ptr<Ipv4StaticRouting> staticRoutingR = ipv4RoutingHelper.GetStaticRouting (router->GetObject<Ipv4> ());

  // Create static routes from client to router
  Ipv4AddressHelper ipv4;
  NetDeviceContainer staDevices, apDevice;

  for (int apInd = 0; apInd < numOfAps; apInd++)
  {

    p2p.SetChannelAttribute ("Delay", StringValue (wifiBackhaulDelay));
 
    NodeContainer nRnAP = NodeContainer (router, g_apNodes.Get (apInd));
    NetDeviceContainer dRdAP = p2p.Install (nRnAP);

    // Later, we add IP addresses.
    std::ostringstream subnet;
    subnet << "10.2." << apInd+1 << ".0";
    ipv4.SetBase (subnet.str ().c_str (), "255.255.255.0");
    g_iRiAPList.push_back (ipv4.Assign (dRdAP));

    Ssid ssid = Ssid ("network-"+std::to_string(apInd+1));
    phy.SetChannel (channel.Create ());
    phy.Set ("ChannelNumber", UintegerValue (36+8*(apInd%4)));
    mac.SetType ("ns3::StaWifiMac",
                 "Ssid", SsidValue (ssid));
    staDevices = wifi.Install (phy, mac, g_clientNodes);
    g_stationDeviceList.push_back(staDevices);

    mac.SetType ("ns3::ApWifiMac",
                    "Ssid", SsidValue (ssid));
    //              "Ssid", SsidValue (ssid),
    //              "BeaconInterval", TimeValue (Seconds (0.512)));
    apDevice = wifi.Install (phy, mac, g_apNodes.Get (apInd));
    // Later, we add IP addresses.
    std::ostringstream subnetWifi;
    subnetWifi << "10.3." << apInd+1 << ".0";
    ipv4.SetBase (subnetWifi.str ().c_str (), "255.255.255.0");

    iAPList.push_back (ipv4.Assign (apDevice));
    g_iCList.push_back (ipv4.Assign (staDevices));

    //add default route for wifi client IP
    staticRoutingR->AddNetworkRouteTo (Ipv4Address (subnetWifi.str ().c_str ()), Ipv4Mask ("255.255.255.0"), apInd + 4);

    //add default route for router IP
    std::ostringstream routerIp;
    routerIp << "10.2." << apInd+1 << ".1";
    std::ostringstream apIp;
    apIp << "10.3." << apInd+1 << ".1";
    for (int clientInd = 0; clientInd < numOfClients; clientInd++)
    {
      Ptr<Ipv4StaticRouting> staticRoutingC = ipv4RoutingHelper.GetStaticRouting (g_clientNodes.Get (clientInd)->GetObject<Ipv4> ());
      staticRoutingC->AddHostRouteTo (Ipv4Address (routerIp.str ().c_str()), Ipv4Address (apIp.str ().c_str()), apInd + 1);
    }
  }

  //Mobility
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (-10.0, 0.0, 0.0));
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));

  //EnB locations
  positionAlloc->Add (Vector (30.0*distance/60, 30.0*distance/60, 3.0));
  positionAlloc->Add (Vector (150.0*distance/60, 30.0*distance/60, 3.0));
  positionAlloc->Add (Vector (90.0*distance/60, 90.0*distance/60, 3.0));
  positionAlloc->Add (Vector (210.0*distance/60, 90.0*distance/60, 3.0));

  //AP locations
  positionAlloc->Add (Vector (30.0*distance/60, 30.0*distance/60, 3.0));
  positionAlloc->Add (Vector (90.0*distance/60, 30.0*distance/60, 3.0));
  positionAlloc->Add (Vector (30.0*distance/60, 90.0*distance/60, 3.0));
  positionAlloc->Add (Vector (90.0*distance/60, 90.0*distance/60, 3.0));

  positionAlloc->Add (Vector (150.0*distance/60, 30.0*distance/60, 3.0));
  positionAlloc->Add (Vector (210.0*distance/60, 30.0*distance/60, 3.0));  
  positionAlloc->Add (Vector (150.0*distance/60, 90.0*distance/60, 3.0));
  positionAlloc->Add (Vector (210.0*distance/60, 90.0*distance/60, 3.0));

  for (int clientInd = 0; clientInd < numOfClients; clientInd++)
  {
    Vector vec = ueLocations.at(clientInd);
    positionAlloc->Add (Vector (vec.x * distance/60, vec.y * distance/60, vec.z));
  }

  mobility.SetPositionAllocator (positionAlloc);

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

  mobility.Install (server);
  mobility.Install (router);
  mobility.Install (g_eNodeBs);

  mobility.Install (g_apNodes);
  if(userSpeed > 0)
  {
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install (g_clientNodes);
    for (int i = 0; i < numOfClients; i++)
      {
        Vector vec = ueLocations.at(i);
        g_clientNodes.Get (i)->GetObject<MobilityModel> ()->SetPosition (Vector (vec.x * distance/60, vec.y * distance/60, vec.z));
        g_clientNodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (Vector (userSpeed*(-1), 0, 0));

        //Simulator::Schedule (Seconds (40), &ChangeSpeed, g_clientNodes.Get (i));
    }
  }
  else
  { 
    mobility.Install (g_clientNodes);
  }

    // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs;
  lteHelper->SetEnbDeviceAttribute ("UlEarfcn", UintegerValue (20850));//3.51GHz
  lteHelper->SetEnbDeviceAttribute ("DlEarfcn", UintegerValue (2850));//3.63GHz

  for (uint32_t ind = 0; ind < g_eNodeBs.GetN (); ind++)
  {

    lteHelper->SetFfrAlgorithmAttribute ("DlSubBandOffset", UintegerValue (25*ind));
    lteHelper->SetFfrAlgorithmAttribute ("DlSubBandwidth", UintegerValue (25));
    lteHelper->SetFfrAlgorithmAttribute ("UlSubBandOffset", UintegerValue (25*ind));
    lteHelper->SetFfrAlgorithmAttribute ("UlSubBandwidth", UintegerValue (25));
    enbLteDevs.Add (lteHelper->InstallEnbDevice (g_eNodeBs.Get(ind)));
  }

  //NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (g_eNodeBs);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (g_clientNodes);
  // Install the IP stack on the UEs
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));

  for (uint32_t u = 0; u < g_clientNodes.GetN (); ++u)
  {
    Ptr<Node> ueNode = g_clientNodes.Get (u);
    // Set the default gateway for the UE
    Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
    ueStaticRouting->AddHostRouteTo (epcHelper->GetUeDefaultGatewayAddress (), epcHelper->GetUeDefaultGatewayAddress (), numOfAps+1);
  }

    // Attach one UE per eNodeB
  //for (uint16_t i = 0; i < numOfClients; i++)
  //{
  //    lteHelper->Attach (ueLteDevs.Get(i), enbLteDevs.Get(0));
      // side effect: the default EPS bearer will be activated
  //}
   lteHelper->AttachToClosestEnb (ueLteDevs, enbLteDevs);

  //lteHelper->EnableTraces();
  lteHelper->AddX2Interface (g_eNodeBs);
  //PopulateARPcache();

  // Assign IP address to UEs, and install applications

  //Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  int startTimeCount = 0;
  //install GMA to the router
  g_routerGma = CreateObject<GmaProtocol>(router);
  //add the virtual server IP to the router GMA
  g_routerGma->AddLocalVirtualInterface (iSiR.GetAddress (0), dSdR.Get(1)); //this can add all virtual IP of the same subnet

  std::vector< Ptr<GmaProtocol> > clientGmaList;
  for (int clientInd = 0; clientInd < numOfClients; clientInd++)
  {
    Ptr<GmaVirtualInterface> routerInterface;

    if(radioType == 0 || radioType == 11)
    {
      routerInterface = g_routerGma->CreateGmaTxVirtualInterface(MilliSeconds(500+startTimeCount++));
    }
    else
    {
      //create an virtual interface per client
      routerInterface = g_routerGma->CreateGmaVirtualInterface(MilliSeconds(500+startTimeCount++));
    }


    if (dlGmaMode == "wifi")
    {
      routerInterface->SetFixedTxCid(0);
    }
    else if (dlGmaMode == "cell")
    {
      routerInterface->SetFixedTxCid(cellCid);
    }
    else if (dlGmaMode == "duplicate")
    {
      routerInterface->SetDuplicateMode(true);
    }
    //other mode nothing to config

    if(ulGmaMode == "split")
    {
      routerInterface->ConfigureRxAlgorithm(32);//this will send TSU to allow the other side to split traffic
    }
    //the other mode, no thing to config...


    //set the virtual IP of the clients to the router GMA
    g_routerGma->AddRemoteVirIp (routerInterface, clientVirtualIpList.at(clientInd), g_clientNodes.Get(clientInd)->GetId());
    /*for (int apInd = 0; apInd < numOfAps; apInd++)
    {
      //add available PHY link IPs to the virtual interface
      g_routerGma->AddRemotePhyIpCandidate (clientVirtualIpList.at(clientInd), g_iCList.at(apInd).GetAddress(clientInd), 0, apInd);// add Wi-Fi link, cid 0
    }*/
    /*for (int apInd = 0; apInd < numOfAps; apInd++)
    {
      //add available PHY link IPs to the virtual interface
      g_routerGma->AddRemotePhyIp (routerInterface, g_iCList.at(apInd).GetAddress(clientInd), apInd); //add Wi-Fi links, cid 0
    }*/

    int apInd = GetClosestWifiAp(g_clientNodes.Get(clientInd));

    //set the initial client ip, for intra-rat handover, the ip will be updated by probes from client...
    g_routerGma->AddRemotePhyIp (clientVirtualIpList.at(clientInd), g_iCList.at(apInd).GetAddress(clientInd), 0);// add Wi-Fi link, cid 0
    //add lte IP
    g_routerGma->AddRemotePhyIp (clientVirtualIpList.at(clientInd), ueIpIface.GetAddress(clientInd), cellCid); //lte cid = 11

    //add a GMA to each client
    Ptr<GmaProtocol> clientGma = CreateObject<GmaProtocol> (g_clientNodes.Get(clientInd));

    //add the client virtual IP to the client GMA
    clientGma->AddLocalVirtualInterface (clientVirtualIpList.at(clientInd));
    Ptr<GmaVirtualInterface> clientInterface;

    if(radioType == 0 || radioType == 11)
    {
      clientInterface = clientGma->CreateGmaTxVirtualInterface(MilliSeconds(100+startTimeCount++));
    }
    else
    {
      //create an virtual interface
      clientInterface = clientGma->CreateGmaVirtualInterface(MilliSeconds(100+startTimeCount++));
    }

    if (ulGmaMode == "wifi")
    {
      clientInterface->SetFixedTxCid(0);
    }
    else if (ulGmaMode == "cell")
    {
      clientInterface->SetFixedTxCid(cellCid);
    }
    else if (ulGmaMode == "duplicate")
    {
      clientInterface->SetDuplicateMode(true);//reordering will be enabled at the receiver since this is flow ID 3
    }
    //other mode nothing to config

    if(dlGmaMode == "split")
    {
      clientInterface->ConfigureRxAlgorithm(32);//this will send TSU to allow the other side to split traffic, enable owd offset (ave owd - min owd)
    }
    //the other mode, no thing to config...

    //add the virtual IP of the server to client GMA
    clientGma->AddRemoteVirIp(clientInterface, iSiR.GetAddress (0), router->GetId());

    /*for (int apInd = 0; apInd < numOfAps; apInd++)
    {
      //add available PHY link IPs to the virtual interface
      clientGma->AddRemotePhyIp (clientInterface, g_iRiAPList.at(apInd).GetAddress (0), apInd);// add Wi-Fi link, cid 0
    }*/
    //apInd = GetClosestWifiAp(g_clientNodes.Get(clientInd), g_apNodes);

    if(g_crossLayer)
    {      
        //myfile << "UE "<< clientInd+1 << " listens to Wi-Fi APs: ";
        Vector clientPos = g_clientNodes.Get(clientInd) ->GetObject<MobilityModel> ()->GetPosition ();
        for(int apTemp = 0; apTemp < numOfAps; apTemp++)
        {
          Vector apPos = g_apNodes.Get(apTemp) ->GetObject<MobilityModel> ()->GetPosition ();

          if(std::abs(clientPos.y - apPos.y) > distance/2)
          {
            //disable WiFi devices to that connect to AP at different row... since we only move left and right.
              //std::cout << "UE: " << clientInd << " wifi ap:" << apTemp<< " y pos: abs(" << clientPos.y << "-" << apPos.y << ")=" << std::abs(clientPos.y - apPos.y)<< " [DEVICE DISABLED]\n";
              //comment for now
              //DynamicCast<WifiNetDevice>(g_stationDeviceList.at(apTemp).Get(clientInd))->GetPhy()->DisableRx(true);
          }
          else
          {
             //myfile << apTemp+1<< ", ";
             //std::cout << "UE: " << clientInd << " wifi ap:" << apTemp<< " y pos: abs(" << clientPos.y << "-" << apPos.y << ")=" << std::abs(clientPos.y - apPos.y)<< "\n";
          }

        }
        //myfile << "\n";
      //Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(g_stationDeviceList.at(apInd).Get(clientInd));
      //wifiDev->GetPhy()->SetPowerCallback (MakeCallback (&GmaVirtualInterface::WifiPowerTrace, clientInterface), 0);

      //Ptr<LteUeNetDevice> lteDev = DynamicCast<LteUeNetDevice>(ueLteDevs.Get(clientInd));
      //lteDev->GetRrc()->SetPeriodicPowerCallback (MakeCallback (&GmaVirtualInterface::LtePeriodicPowerTrace, clientInterface));

    }

    clientGma->AddRemotePhyIp (iSiR.GetAddress (0), g_iRiAPList.at(apInd).GetAddress (0), 0, apInd);// add Wi-Fi link, cid 0

    for (int intraId = 0; intraId < numOfAps; intraId++)
    {
      //add available PHY link IPs to the virtual interface
      Mac48Address macAddr;
      clientGma->AddRemotePhyIpCandidate (iSiR.GetAddress (0), g_iRiAPList.at(intraId).GetAddress (0), macAddr, 0, intraId);// add Wi-Fi link, cid 0

      Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(g_stationDeviceList.at(intraId).Get(clientInd));
      //comment for now
      //wifiDev->GetPhy()->SetPeriodicPowerCallback (MakeCallback (&GmaVirtualInterface::WifiPeriodicPowerTrace, clientInterface), 0, intraId);

    }

    clientGma->AddRemotePhyIp (iSiR.GetAddress (0), epcHelper->GetUeDefaultGatewayAddress (), cellCid); //lte cid = 11

    clientGmaList.push_back(clientGma);

    //Simulator::Schedule (Seconds(1.0), &UpdateWifiLinkIp, apInd, clientInd, clientVirtualIpList.at(clientInd), clientGma, iSiR.GetAddress (0), clientInterface);
  }
  myfile.close();

  // Create the OnOff application to send UDP datagrams of size
  // 210 bytes at a rate of 448 Kb/s

  ApplicationContainer sendApps;
  ApplicationContainer sinkApps;

  for (int clientInd = 0; clientInd < numOfClients; clientInd++)
  {
    uint16_t port = clientInd + 9;   // Discard port (RFC 863)

    Ipv4Address receiverAddress;

    if(downlink)
    {
      receiverAddress = clientVirtualIpList.at(clientInd);
    }
    else
    {
      receiverAddress = iSiR.GetAddress (0);
    }

    if(tcpData)
    {
      AddressValue remoteAddress (InetSocketAddress (receiverAddress, port));
      Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (packetSize));
      BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("SendSize", UintegerValue (packetSize));
      if (downlink)
      {
        sendApps.Add(ftp.Install (server));
      }
      else
      {
        sendApps.Add(ftp.Install (g_clientNodes.Get(clientInd)));
      }
    }
    else
    {

      //UdpClientHelper onoff (receiverAddress, port);
      //onoff.SetAttribute ("MaxPackets", UintegerValue (UINT32_MAX));
      //double interval = (double)packetSize*8*1e-6/updRateMbps;
      //onoff.SetAttribute ("Interval", TimeValue (Seconds(interval)));
      //onoff.SetAttribute ("PacketSize", UintegerValue (packetSize));



      if (downlink)
      {
        Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());

        Ptr<MyApp> app = CreateObject<MyApp> ();

        InetSocketAddress destC (receiverAddress, port);
        destC.SetTos (0xb8); //AC_VI

        app->Setup (ns3UdpSocket, destC);
        /*app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 0);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 1);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 2);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 3);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 4);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 5);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 6);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 7);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 8);
        app->AddStream (1000, DataRate (std::to_string(updRateMbps/10)+"Mbps"), 9);*/

        app->AddStream (1000, DataRate (std::to_string(0.45)+"Mbps"), 0);
        app->AddStream (1000, DataRate (std::to_string(0.45)+"Mbps"), 1);
        app->AddStream (1000, DataRate (std::to_string(1.35)+"Mbps"), 2);
        app->AddStream (1000, DataRate (std::to_string(1.35)+"Mbps"), 3);
        app->AddStream (1000, DataRate (std::to_string(2.2)+"Mbps"), 4);
        app->AddStream (1000, DataRate (std::to_string(2.2)+"Mbps"), 5);


        server->AddApplication (app);
        sendApps.Add(app);
      }
      else
      {
        Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (g_clientNodes.Get(clientInd), UdpSocketFactory::GetTypeId ());

        Ptr<MyApp> app = CreateObject<MyApp> ();
        InetSocketAddress destC (receiverAddress, port);
        destC.SetTos (0xb8); //AC_VI

        app->Setup (ns3UdpSocket, destC);
        app->AddStream (1000, DataRate (std::to_string(0.45)+"Mbps"), 0);
        app->AddStream (1000, DataRate (std::to_string(0.45)+"Mbps"), 1);
        app->AddStream (1000, DataRate (std::to_string(1.35)+"Mbps"), 2);
        app->AddStream (1000, DataRate (std::to_string(1.35)+"Mbps"), 3);
        app->AddStream (1000, DataRate (std::to_string(2.2)+"Mbps"), 4);
        app->AddStream (1000, DataRate (std::to_string(2.2)+"Mbps"), 5);

        g_clientNodes.Get(clientInd)->AddApplication (app);
        sendApps.Add(app);
      }
    }

    if(tcpData)
    {
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory",
                     Address (InetSocketAddress (Ipv4Address::GetAny (), port)));
      sinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
      if (downlink)
      {
        sinkApps.Add(sinkHelper.Install (g_clientNodes.Get(clientInd)));
      }
      else
      {
        sinkApps.Add(sinkHelper.Install (server));
      }

    }
    else
    {
      // Create a packet sink to receive these packets
      //PacketSinkHelper sink ("ns3::UdpSocketFactory",
      //                       Address (InetSocketAddress (Ipv4Address::GetAny (), port)));

      UdpServerHelper sink (port);

      if (downlink)
      {
        sinkApps.Add(sink.Install (g_clientNodes.Get(clientInd)));
      }
      else
      {
        sinkApps.Add(sink.Install (server));
      }
    }

    if(enableRxTrace)
    {

      std::ostringstream fileName;
      //fileName<<"data-"<<clientInd<<".txt";
      fileName<<"data.txt";
      AsciiTraceHelper asciiTraceHelper;
      Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
      if (tcpData)
      {
        sinkApps.Get(clientInd)->TraceConnectWithoutContext("Rx",MakeBoundCallback (&RcvFrom, stream));
      }
      else
      {
        sinkApps.Get(clientInd)->TraceConnectWithoutContext("Rx",MakeBoundCallback (&Rcv, stream, clientInd));
      }
    }

    //std::ostringstream fileNameA;
    //fileNameA<<"data"<<clientInd+1<<".txt";

    //AsciiTraceHelper asciiTraceHelperA;
    //Ptr<OutputStreamWrapper> streamA = asciiTraceHelperA.CreateFileStream (fileNameA.str ().c_str ());
    //sinkApps.Get(clientInd+1)->TraceConnectWithoutContext("Rx",MakeBoundCallback (&RxFrom, streamA));
    if(tcpData && enableTCPtrace)
    {
      if(downlink)
      {
        Simulator::Schedule (Seconds (1.001), &DonwlinkTraces, clientInd);
        //Simulator::Schedule (Seconds (30*clientInd+1.001), &DonwlinkTraces, clientInd);
      }
      else
      {
        Simulator::Schedule (Seconds (1.001), &UplinkTraces, clientInd);
        //Simulator::Schedule (Seconds (30*clientInd+1.001), &UplinkTraces, clientInd);
      }
    }

    sendApps.Get(clientInd)->SetStartTime (Seconds (1));
    //sendApps.Get(clientInd)->SetStartTime (Seconds (30*clientInd+1));

  }

  //sendApps.Start (Seconds (1.0));
  sendApps.Stop (g_stopTime - Seconds(1.5));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (g_stopTime);

  LogLocations (distance, userSpeed);

Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                 MakeCallback (&NotifyHandoverStartEnb));
Config::Connect ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                 MakeCallback (&NotifyHandoverStartUe));
Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                 MakeCallback (&NotifyHandoverEndOkEnb));
Config::Connect ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                 MakeCallback (&NotifyHandoverEndOkUe));


  //AsciiTraceHelper ascii;
  //p2p.EnableAsciiAll (ascii.CreateFileStream ("gma.tr"));
  //p2p.EnablePcapAll ("gma");
  //phy.EnableAsciiAll (ascii.CreateFileStream ("gma-wifi.tr"));
  //phy.EnablePcapAll ("gma-wifi");

  Simulator::Stop (g_stopTime + Seconds(0.1));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
