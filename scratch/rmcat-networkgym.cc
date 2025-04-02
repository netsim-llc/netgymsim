/******************************************************************************
 * Copyright 2016-2017 cisco Systems, Inc.                                    *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *     http://www.apache.org/licenses/LICENSE-2.0                             *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 ******************************************************************************/

/**
 * @file
 * Simple example demonstrating the usage of the rmcat ns3 module, using:
 *  - NADA as controller for rmcat flows
 *  - Statistics-based traffic source as codec
 *  - [Optionally] TCP flows
 *  - [Optionally] UDP flows
 *
 * @version 0.1.1
 * @author Jiantao Fu
 * @author Sergio Mena
 * @author Xiaoqing Zhu
 */

#include "ns3/nada-controller.h"
#include "ns3/rmcat-sender.h"
#include "ns3/rmcat-receiver.h"
#include "ns3/rmcat-constants.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/data-rate.h"
#include "ns3/bulk-send-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/core-module.h"
#include "ns3/networkgym-module.h"
using json = nlohmann::json;
#include "ns3/point-to-point-net-device.h"

using namespace ns3;

static void ChangeRate(NodeContainer nodes, uint64_t bps)
{
    for (size_t i = 0; i < nodes.GetN(); i++) 
    {
        auto node = nodes.Get(i);
        for (size_t j = 0; j < node->GetNDevices(); j++)
        {
            auto device = node->GetDevice(j)->GetObject<PointToPointNetDevice>();
            if (device)
            {
                // set datarate
                device->SetDataRate(DataRate(bps));
            }
        }
    }
}

static NodeContainer BuildExampleTopo (uint64_t bps,
                                       uint32_t msDelay)
{
    NodeContainer nodes;
    nodes.Create (2);


    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
    pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (msDelay)));
    //auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, bps * msQdelay / 8000);
    //pointToPoint.SetQueue ("ns3::DropTailQueue",
    //                       "Mode", StringValue ("QUEUE_MODE_BYTES"),
    //                       "MaxBytes", UintegerValue (bufSize));
    pointToPoint.SetQueue ("ns3::DropTailQueue");
    

    NetDeviceContainer devices = pointToPoint.Install (nodes);

    InternetStackHelper stack;
    stack.Install (nodes);
    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.0");
    address.Assign (devices);

    // Uncomment to capture simulated traffic
    // pointToPoint.EnablePcapAll ("rmcat-example");

    // disable tc for now, some bug in ns3 causes extra delay
    TrafficControlHelper tch;
    tch.Uninstall (devices);

    return nodes;
}

static void InstallTCP (Ptr<Node> sender,
                        Ptr<Node> receiver,
                        uint16_t port,
                        float startTime,
                        float stopTime)
{
    // configure TCP source/sender/client
    auto serverAddr = receiver->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();
    BulkSendHelper source{"ns3::TcpSocketFactory",
                           InetSocketAddress{serverAddr, port}};
    // Set the amount of data to send in bytes. Zero is unlimited.
    source.SetAttribute ("MaxBytes", UintegerValue (0));
    source.SetAttribute ("SendSize", UintegerValue (DEFAULT_PACKET_SIZE));

    auto clientApps = source.Install (sender);
    clientApps.Start (Seconds (startTime));
    clientApps.Stop (Seconds (stopTime));

    // configure TCP sink/receiver/server
    PacketSinkHelper sink{"ns3::TcpSocketFactory",
                           InetSocketAddress{Ipv4Address::GetAny (), port}};
    auto serverApps = sink.Install (receiver);
    serverApps.Start (Seconds (startTime));
    serverApps.Stop (Seconds (stopTime));

}

static Time GetIntervalFromBitrate (uint64_t bitrate, uint32_t packetSize)
{
    if (bitrate == 0u) {
        return Time::Max ();
    }
    const auto secs = static_cast<double> (packetSize + IPV4_UDP_OVERHEAD) /
                            (static_cast<double> (bitrate) / 8. );
    return Seconds (secs);
}

static void InstallUDP (Ptr<Node> sender,
                        Ptr<Node> receiver,
                        uint16_t serverPort,
                        uint64_t bitrate,
                        uint32_t packetSize,
                        uint32_t startTime,
                        uint32_t stopTime)
{
    // configure UDP source/sender/client
    auto serverAddr = receiver->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();
    const auto interPacketInterval = GetIntervalFromBitrate (bitrate, packetSize);
    uint32_t maxPacketCount = 0XFFFFFFFF;
    UdpClientHelper client{serverAddr, serverPort};
    client.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
    client.SetAttribute ("Interval", TimeValue (interPacketInterval));
    client.SetAttribute ("PacketSize", UintegerValue (packetSize));

    auto clientApps = client.Install (sender);
    clientApps.Start (Seconds (startTime));
    clientApps.Stop (Seconds (stopTime));

    // configure TCP sink/receiver/server
    UdpServerHelper server{serverPort};
    auto serverApps = server.Install (receiver);
    serverApps.Start (Seconds (startTime));
    serverApps.Stop (Seconds (stopTime));
}

static void InstallApps (bool nada,
                         Ptr<DataProcessor> dataProcessor,
                         Ptr<Node> sender,
                         Ptr<Node> receiver,
                         uint16_t port,
                         float startTime,
                         float stopTime,
                         uint64_t id)
{
    Ptr<RmcatSender> sendApp = CreateObject<RmcatSender> ();
    Ptr<RmcatReceiver> recvApp = CreateObject<RmcatReceiver> ();
    sender->AddApplication (sendApp);
    receiver->AddApplication (recvApp);
    auto nadaController = std::make_shared<rmcat::NadaController> ();
    nadaController->SetNetworkStatsCallback (id, MakeCallback (&ns3::DataProcessor::AppendMeasurement, dataProcessor));
    dataProcessor->SetNetworkGymActionCallback("rmcat::srate", id, MakeCallback (&rmcat::NadaController::ReceiveNetworkGymAction, nadaController));
    if (nada) {
        sendApp->SetController (nadaController);
    }
    Ptr<Ipv4> ipv4 = receiver->GetObject<Ipv4> ();
    Ipv4Address receiverIp = ipv4->GetAddress (1, 0).GetLocal ();
    sendApp->Setup (receiverIp, port); // initBw, minBw, maxBw);

    const auto fps = 25.;
    auto innerCodec = new syncodecs::StatisticsCodec{fps};
    auto codec = new syncodecs::ShapedPacketizer{innerCodec, DEFAULT_PACKET_SIZE};
    sendApp->SetCodec (std::shared_ptr<syncodecs::Codec>{codec});

    recvApp->Setup (port);

    sendApp->SetStartTime (Seconds (startTime));
    sendApp->SetStopTime (Seconds (stopTime));

    recvApp->SetStartTime (Seconds (startTime));
    recvApp->SetStopTime (Seconds (stopTime));
}

int main (int argc, char *argv[])
{

    std::ifstream jsonStream("env-configure.json");
    json jsonConfig;
    jsonStream >> jsonConfig;

    std::string env_name = jsonConfig["env"].get<std::string>();

    std::cout << "env = " << env_name <<  "\n";

    if(env_name.compare("rmcat") != 0)
    {
        NS_FATAL_ERROR("["+env_name+"] use case not implemented in rmcat-networkgym.cc");
    }
    int measurement_interval_ms = jsonConfig["measurement_interval_ms"].get<int>();
    if(measurement_interval_ms != 100)
    {
        NS_FATAL_ERROR("The measurement_interval_ms for RMCAT is fixed to 100 ms!");
    }

    float endTime = (float)jsonConfig["env_end_time_ms"].get<int>()/1000;
    int nRmcat = jsonConfig["nada_flows"].get<int>();
    if (nRmcat != 1)
    {
        NS_FATAL_ERROR("For single agent environment, we only support nada_flows = 1!");
    }
    int nTcp = jsonConfig["tcp_flows"].get<int>();
    int nUdp = jsonConfig["udp_flows"].get<int>();
    bool log = false;
    bool nada = true;
    std::string strArg  = "strArg default";
    int measurement_start_time_ms = jsonConfig["measurement_start_time_ms"].get<int>();

    json bw_trace = jsonConfig["bw_trace"];
    //std::cout << bw_trace << std::endl;

    CommandLine cmd;
    cmd.AddValue ("rmcat", "Number of RMCAT (NADA) flows", nRmcat);
    cmd.AddValue ("tcp", "Number of TCP flows", nTcp);
    cmd.AddValue ("udp", "Number of UDP flows", nUdp);
    cmd.AddValue ("log", "Turn on logs", log);
    cmd.AddValue ("nada", "true: use NADA, false: use dummy", nada);
    cmd.Parse (argc, argv);

    if (log) {
        LogComponentEnable ("RmcatSender", LOG_INFO);
        LogComponentEnable ("RmcatReceiver", LOG_INFO);
        LogComponentEnable ("Packet", LOG_FUNCTION);
    }

    // configure default TCP parameters
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpCubic"));
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1000));

    const uint64_t linkBw   = jsonConfig["topo_default_bw_kbps"].get<int>()*1000;
    const int msDelay  = jsonConfig["topo_default_delay_ms"].get<int>();

    NodeContainer nodes = BuildExampleTopo (linkBw, msDelay);
    for (uint32_t i = 0; i <bw_trace["time_ms"].size(); i++ )
    {
        if (bw_trace["loss"][i] > 0)
        {
            NS_FATAL_ERROR("Cureently, we only support loss = 0.0!");
        }
        if (bw_trace["delay_ms"][i].get<int>() != msDelay)
        {
            NS_FATAL_ERROR("Cureently, we does not support updating the delay over time, please set the delay_ms in bw_trace file to the same as topo_default_delay_ms in config.json");
        }
        Simulator::Schedule (MilliSeconds (bw_trace["time_ms"][i]), &ChangeRate, nodes, bw_trace["bw_kbps"][i].get<int>()*1000);
    }

    int port = 8000;
    auto dataProcessor = CreateObject<DataProcessor> ();
    nodes.Get (0)->AggregateObject(dataProcessor);//need to aggregate to a node, such that the dispose function will be called when ns3 exits.
    Simulator::Schedule(MilliSeconds(measurement_start_time_ms+1), &DataProcessor::StartMeasurement, dataProcessor);
    nRmcat = std::max<int> (0, nRmcat); // No negative RMCAT flows
    const uint64_t nada_flow_start_time_diff_ms = jsonConfig["nada_flow_start_time_diff_ms"].get<int>();

    for (size_t i = 0; i < (unsigned int) nRmcat; ++i) {
        auto start = nada_flow_start_time_diff_ms/1000.0 * i;
        auto end = std::max (start + 1., endTime - start - 0.01);
        InstallApps (nada, dataProcessor, nodes.Get (0), nodes.Get (1), port++, start, end, i);
    }

    nTcp = std::max<int> (0, nTcp); // No negative TCP flows
    const uint64_t tcp_flow_start_time_diff_ms = jsonConfig["tcp_flow_start_time_diff_ms"].get<int>();

    for (size_t i = 0; i < (unsigned int) nTcp; ++i) {
        auto start = tcp_flow_start_time_diff_ms/1000.0 * i;
        auto end = std::max (start + 1., endTime - start);
        InstallTCP (nodes.Get (0), nodes.Get (1), port++, start, end);
    }

    // UDP parameters
    const uint64_t bandwidth = jsonConfig["udp_flow_bw_kbps"].get<int>()*1000;
    const uint32_t pktSize = DEFAULT_PACKET_SIZE;

    nUdp = std::max<int> (0, nUdp); // No negative UDP flows
    const uint64_t udp_flow_start_time_diff_ms = jsonConfig["upd_flow_start_time_diff_ms"].get<int>();

    for (size_t i = 0; i < (unsigned int) nUdp; ++i) {
        auto start = udp_flow_start_time_diff_ms/1000.0 * i;
        auto end = std::max (start + 1., endTime - start);
        InstallUDP (nodes.Get (0), nodes.Get (1), port++,
                    bandwidth, pktSize, start, end);
    }

    std::cout << "Running Simulation..." << std::endl;
    Simulator::Stop (Seconds (endTime));
    Simulator::Run ();
    Simulator::Destroy ();
    std::cout << "Done" << std::endl;

    return 0;
}
