#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/lte-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/channel-condition-model.h"
#include "ns3/gma-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("LenaSimpleEpc");

std::map<FlowId, uint64_t> m_lastRxBytesMap;
std::map<FlowId, uint64_t> m_lastTxBytesMap;
Time g_rateInterval = Seconds(20);
static void CalAverageRate(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
{
  monitor->CheckForLostPackets ();
  map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  double Throughput = 0.0;
  //double throughputSum = 0.0;
  for (map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end ();
       ++i)
    {

      if (i->first != 5)
        continue;
      std::map<FlowId, uint64_t>::iterator iter = m_lastRxBytesMap.find(i->first);
      uint64_t lastRxBytes = 0;
      if(iter != m_lastRxBytesMap.end())
      {
        lastRxBytes = iter->second;
      }
      m_lastRxBytesMap[i->first] = i->second.rxBytes;
      Throughput = (i->second.rxBytes-lastRxBytes) * 8.0 / g_rateInterval.GetSeconds() /1024;

      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);

      cout <<"Flow ID: " << i->first << "\t(" << t.sourceAddress << " -> "
           << t.destinationAddress << ")\t";
      cout << "Tx Pkt = " << i->second.txPackets << "\t";
      cout << "Rx Pkt = " << i->second.rxPackets << "\t ";
      cout << "Rate: " << Throughput << " Kbps"<< endl;


    }
    Simulator::Schedule (g_rateInterval, &CalAverageRate, monitor, classifier);
}


int
main (int argc, char *argv[])
{
  // LogComponentEnable ("MyRrMacScheduler", LOG_LEVEL_INFO);
  Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod", TimeValue(MilliSeconds (0.0))); // do not update the channel condition
  Config::SetDefault ("ns3::ThreeGppPropagationLossModel::ShadowingEnabled", BooleanValue (false)); // do not update the channel condition
  Ptr<ChannelConditionModel> nlosCondModel = CreateObject<NeverLosChannelConditionModel> ();
  Config::SetDefault ("ns3::ThreeGppPropagationLossModel::ChannelConditionModel",  PointerValue (nlosCondModel)); // always nlos channel model...
  // Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (2)); // MIMO Spatial Multiplexity (2 layers) ONLY for Downlink...
  // Config::SetDefault ("ns3::LteEnbPhy::MacToChannelDelay", UintegerValue (2)); //The delay in TTI units that occurs between a scheduling decision in the MAC and the actual start of the transmission by the PHY. This is intended to be used to model the latency of real PHY and MAC implementations.
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUm::ReorderingTimer", TimeValue (MilliSeconds (10)));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (1024 * 1024));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName ("ns3::TcpBbr")));



  // Number of Users
  uint16_t m_nUser = 1;
  // Distance
  double distance = 10;
  std::string traceFileName = "src/lte/model/fading-traces/fading_trace_EPA_3kmph.fad";

  double speed = 1;
  string datarate = "50Mbps";
  uint32_t packetSize = 1400;
  bool fastfading = false;

  // Set the simulation time
  double simTime = 10.0;
  
  // Command line arguments
  CommandLine cmd;
  cmd.AddValue ("numberOfNodes", "Number of eNodeBs + UE pairs", m_nUser);
  cmd.AddValue ("simTime", "Total duration of the simulation [s])", simTime);
  cmd.AddValue ("distance", "Distance between eNBs [m]", distance);
  cmd.AddValue ("datarate", "datarate", datarate);
  cmd.AddValue ("packetSize", "packetSize", packetSize);
  cmd.AddValue ("speed", "speed", speed);
  // cmd.AddValue ("interPacketInterval", "Inter packet interval [ms])", interPacketInterval);
  cmd.Parse (argc, argv);

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults ();

  // parse again so you can override default values from the command line
  cmd.Parse (argc, argv);

  RngSeedManager::SetSeed (8);
  RngSeedManager::SetRun (14);

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
  lteHelper->SetEpcHelper (epcHelper);
  //lteHelper->SetSchedulerType ("ns3::NsPfFfMacScheduler");
  lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::ThreeGppIndoorOfficePropagationLossModel"));

  if(fastfading)
  {
    lteHelper->SetAttribute ("FadingModel", StringValue ("ns3::TraceFadingLossModel"));
    
    std::ifstream ifTraceFile;
    ifTraceFile.open (traceFileName, std::ifstream::in);
    if (ifTraceFile.good ())
      {
        // script launched by test.py
        lteHelper->SetFadingModelAttribute ("TraceFilename", StringValue (traceFileName));
      }
    else
      {
        // script launched as an example
        std::cout << "[error]file not found\n";
        return 0;
        //lteHelper->SetFadingModelAttribute ("TraceFilename", StringValue ("src/lte/model/fading-traces/fading_trace_EPA_3kmph.fad"));
      }
      
    // these parameters have to be set only in case of the trace format 
    // differs from the standard one, that is
    // - 10 seconds length trace
    // - 10,000 samples
    // - 0.5 seconds for window size
    // - 100 RB
    lteHelper->SetFadingModelAttribute ("TraceLength", TimeValue (Seconds (10.0)));
    lteHelper->SetFadingModelAttribute ("SamplesNum", UintegerValue (10000));
    lteHelper->SetFadingModelAttribute ("WindowSize", TimeValue (Seconds (0.5)));
    lteHelper->SetFadingModelAttribute ("RbNum", UintegerValue (100));
  }

  lteHelper->SetEnbDeviceAttribute ("DlBandwidth", UintegerValue (100)); //maxmize LTE bandwidth.


  Ptr<Node> pgw = epcHelper->GetPgwNode ();

  // Create a single RemoteHost
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (0)));
  NetDeviceContainer internetDevices = p2ph.Install (remoteHost, pgw);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  // Ipv4Address remoteHostAddr = serverVirIp;

  //make it a function...
  std::vector<Ipv4Address> clientVirtualIpList;
  for (int clientInd = 0; clientInd < m_nUser; clientInd++)
  {
    //store the virtual IPs for clients.
     clientVirtualIpList.push_back(ipv4h.NewAddress());
  }

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("1.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  NodeContainer ueNodes;
  NodeContainer enbNodes;
  enbNodes.Create (1);
  ueNodes.Create (m_nUser);

  // Install Mobility Model
  Ptr<ListPositionAllocator> apPositionAlloc = CreateObject<ListPositionAllocator> ();
  Ptr<ListPositionAllocator> staPositionAlloc = CreateObject<ListPositionAllocator> ();
  Ptr<ListPositionAllocator> pgwPositionAlloc = CreateObject<ListPositionAllocator> ();
  apPositionAlloc->Add (Vector (0, 0, 25));
  Ptr<UniformRandomVariable> urv = CreateObject<UniformRandomVariable> ();
  urv->SetAttribute ("Min", DoubleValue (distance));
  urv->SetAttribute ("Max", DoubleValue (distance+40));
  double x_dis = 0, y_dis = 0;
  std::cout<<"UE Locations:";
  for (int32_t ueId = 0; ueId < m_nUser; ueId ++)
  {
    x_dis = urv->GetValue();
    y_dis = urv->GetValue();
    staPositionAlloc->Add (Vector (x_dis, y_dis, 1.5));
    std::cout << "(" << x_dis << "," << y_dis << ") ";
  }
  std::cout <<std::endl;
  pgwPositionAlloc->Add (Vector (0, 0, 0));

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.SetPositionAllocator (apPositionAlloc);
  mobility.Install (enbNodes);
  mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  mobility.SetPositionAllocator (staPositionAlloc);
  mobility.Install (ueNodes);
  mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  mobility.SetPositionAllocator (pgwPositionAlloc);
  mobility.Install (pgw);

  /*Vector sp (-1*speed, 0, 0);
  for (int32_t ueId = 0; ueId < m_nUser; ueId ++)
  {
    ueNodes.Get (ueId)->GetObject<ConstantVelocityMobilityModel> ()->SetVelocity (sp);
  }*/

  // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueNodes);

  Ptr<LteEnbNetDevice> lteEnbDev = enbLteDevs.Get (0)->GetObject<LteEnbNetDevice> ();
  Ptr<LteEnbPhy> enbPhy = lteEnbDev->GetPhy ();
  enbPhy->SetAttribute ("TxPower", DoubleValue (30.0));
  enbPhy->SetAttribute ("NoiseFigure", DoubleValue (5.0));

  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting =
          ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  enum EpsBearer::Qci q = EpsBearer::GBR_CONV_VOICE;
  EpsBearer bearer (q);

  // Attach one UE per eNodeB
  for (uint16_t i = 0; i < m_nUser; i++)
    {
      lteHelper->Attach (ueLteDevs.Get (i), enbLteDevs.Get (0));
      // side effect: the default EPS bearer will be activated
    }

  int startTimeCount = 0;

  //install GMA to the pgw
  Ptr<GmaProtocol> serverGma = CreateObject<GmaProtocol>(pgw);
  //add the virtual server IP to the pgw GMA
  Ipv4Address serverVirIp = internetIpIfaces.GetAddress (0); //the vir IP of the gma server: remote host IP
  Ptr<NetDevice> serverDevice = internetDevices.Get(1);
  serverGma->AddLocalVirtualInterface (serverVirIp, serverDevice); //this can add all virtual IP of the same subnet

  std::vector< Ptr<GmaProtocol> > clientGmaList;
  uint8_t lteCid = 11;  //11 for cellular
  for (int clientInd = 0; clientInd < m_nUser; clientInd++)
  {
    //GMA server configure:
    Ptr<Node> clientNode = ueNodes.Get(clientInd);
    Ipv4Address clientVirIp = clientVirtualIpList.at(clientInd);

    Ptr<GmaVirtualInterface> serverInterface;
    serverInterface = serverGma->CreateGmaTxVirtualInterface(MilliSeconds(500+startTimeCount++));
    serverInterface->SetFixedTxCid(lteCid); //this GMA interface only send to LTE link.

    //set the virtual IP of the clients to the pgw GMA
    serverGma->AddRemoteVirIp (serverInterface, clientVirIp, clientNode->GetId());

    //set the initial client ip, for intra-rat handover, the ip will be updated by probes from client...
    //add lte IP
    serverGma->AddRemotePhyIp (clientVirIp, ueIpIface.GetAddress(clientInd), lteCid);

    //GMA client configure:
    Ptr<GmaProtocol> clientGma = CreateObject<GmaProtocol> (clientNode);

    //add the client virtual IP to the client GMA
    clientGma->AddLocalVirtualInterface (clientVirIp);
    Ptr<GmaVirtualInterface> clientInterface;
    clientInterface = clientGma->CreateGmaTxVirtualInterface(MilliSeconds(100+startTimeCount++));
    clientInterface->SetFixedTxCid(lteCid); //this GMA interface only send to LTE link.
   
    //add the virtual IP of the server to client GMA
    clientGma->AddRemoteVirIp(clientInterface, serverVirIp, pgw->GetId());

    //Ptr<LteUeNetDevice> lteDev = DynamicCast<LteUeNetDevice>(ueLteDevs.Get(clientInd));
    //lteDev->GetRrc()->SetPeriodicPowerCallback (MakeCallback (&GmaVirtualInterface::LtePeriodicPowerTrace, clientInterface));

    clientGma->AddRemotePhyIp (serverVirIp, epcHelper->GetUeDefaultGatewayAddress (), lteCid);

    clientGmaList.push_back(clientGma);

  }

  Time udpInterval = Time::FromDouble (
      (packetSize * 8) / static_cast<double> (DataRate (datarate).GetBitRate ()), Time::S);

  // Install and start applications on UEs and remote host
  uint16_t dlPort = 1234;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      dlPort++;
      //PacketSinkHelper dlPacketSinkHelper ("ns3::UdpSocketFactory",
      //                                     InetSocketAddress (Ipv4Address::GetAny (), dlPort));

      PacketSinkHelper dlPacketSinkHelper ("ns3::TcpSocketFactory",
                                           InetSocketAddress (Ipv4Address::GetAny (), dlPort));

      serverApps.Add (dlPacketSinkHelper.Install (ueNodes.Get (u)));

      //UdpClientHelper dlClient (ueIpIface.GetAddress (u), dlPort);
      //UdpClientHelper dlClient (clientVirtualIpList.at (u), dlPort);
      //dlClient.SetAttribute ("PacketSize", UintegerValue (packetSize));
      //dlClient.SetAttribute ("Interval", TimeValue (udpInterval));
      //dlClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
      SvcTraceClientHelper dlClient (clientVirtualIpList.at (u), dlPort, "../scratch/mot17-10-quality.trace");
      dlClient.SetAttribute ("MaxPacketSize", UintegerValue (packetSize));

      clientApps.Add (dlClient.Install (remoteHost));
      clientApps.Get (u)->SetStartTime (MilliSeconds (100 + u));
    }

  serverApps.Start (MilliSeconds (100));
  //clientApps.Start (MilliSeconds (100));
  clientApps.Stop (Seconds (simTime));
  lteHelper->EnableTraces ();
  // Uncomment to enable PCAP tracing
  //p2ph.EnablePcapAll("lena-simple-epc");

  FlowMonitorHelper flowmon;
  
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  // flowmon.Install (enbNodes.Get (0));
  //monitor->SetAttribute("DelayBinWidth", DoubleValue(0.01));

  //Ptr<RadioBearerStatsCalculator> rlcStats = lteHelper->GetRlcStats ();
  //rlcStats->SetAttribute ("EpochDuration", TimeValue (Seconds (simTime)));

  //Ptr<RadioBearerStatsCalculator> pdcpStats = lteHelper->GetPdcpStats ();
  //pdcpStats->SetAttribute ("EpochDuration", TimeValue (Seconds (0.1)));

  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  Simulator::Schedule (g_rateInterval, &CalAverageRate, monitor, classifier);

  Simulator::Stop (Seconds (simTime+0.01));
  Simulator::Run ();

  /*GtkConfigStore config;
  config.ConfigureAttributes();*/
  cout<<"--------------------------------------------------------" <<endl;
  cout << "Simulation End: " << simTime << " Seconds"<< endl;
  cout<<"--------------------------------------------------------" <<endl;

  monitor->CheckForLostPackets ();

  //Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  double Throughput = 0.0;
  for (map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end ();
       ++i)
    {
      //if (i->first <= 4)
      //  continue;
      
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);

      cout <<"Flow ID: " << i->first << "\t(" << t.sourceAddress << " -> "
           << t.destinationAddress << ")\t";
      cout << "Tx Pkt = " << i->second.txPackets << "\t";
      cout << "Rx Pkt = " << i->second.rxPackets << "\t ";
      //Throughput =
      //    i->second.rxBytes * 8.0 /
      //    (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) /
      //    1024;
      Throughput = 
          i->second.rxBytes * 8.0 /
          (simTime - i->second.timeFirstTxPacket.GetSeconds ()) /
          1024;
      cout << "Rate: " << Throughput << " Kbps \t Delay: ";

      for (uint32_t binId = 0; binId < i->second.delayHistogram.GetNBins (); binId++)
        {
          Histogram& ref = const_cast <Histogram&>(i->second.delayHistogram);
          if(ref.GetBinCount (binId) > 0)
          {
            cout <<ref.GetBinStart (binId) <<"(" << ref.GetBinCount (binId) << ") ";
          }
        }
      cout << std::endl;


    }

  NS_LOG_UNCOND ("Done");


  Simulator::Destroy ();
  return 0;
}