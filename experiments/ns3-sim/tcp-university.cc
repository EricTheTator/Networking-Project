// University campus TCP benchmark for ns-3.
// Hierarchical topology:
//   external server -- gateway -- core -- [building_0 .. building_{B-1}]
//                                            |
//                                       [subnet_0 .. subnet_{S-1}]
//                                            |
//                                       [host_0 .. host_{H-1}]
//
// Each host runs one long-lived TCP flow up to the external server.
// Bottleneck is the gateway<->core link; core<->building and
// building<->subnet links model distribution/access tiers.
//
// Build: copy into ns-3 scratch/ (build_ns3.sh does this automatically).

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpUniversity");

int
main(int argc, char* argv[])
{
    std::string algo = "ns3::TcpCubic";
    double uplinkMbps = 1000.0;    // gateway <-> external server
    double coreMbps = 500.0;       // gateway <-> core (bottleneck)
    double distMbps = 1000.0;      // core <-> building
    double accessMbps = 100.0;     // building <-> subnet
    double rttMs = 40.0;           // base RTT host <-> external
    double bufferBdpMult = 1.0;
    uint32_t nBuildings = 4;
    uint32_t nSubnetsPerBuilding = 3;
    uint32_t nHostsPerSubnet = 4;
    double simSec = 60.0;
    double warmupSec = 5.0;
    std::string runId = "uni";
    std::string outPath = "uni.json";

    CommandLine cmd;
    cmd.AddValue("algo", "TCP congestion control TypeId", algo);
    cmd.AddValue("uplinkMbps", "Gateway<->external link Mbps", uplinkMbps);
    cmd.AddValue("coreMbps", "Gateway<->core link Mbps (bottleneck)", coreMbps);
    cmd.AddValue("distMbps", "Core<->building link Mbps", distMbps);
    cmd.AddValue("accessMbps", "Building<->subnet link Mbps", accessMbps);
    cmd.AddValue("rttMs", "Base end-to-end RTT in ms", rttMs);
    cmd.AddValue("bufferBdpMult", "Bottleneck buffer as multiple of BDP", bufferBdpMult);
    cmd.AddValue("nBuildings", "Number of building routers", nBuildings);
    cmd.AddValue("nSubnetsPerBuilding", "Subnets per building", nSubnetsPerBuilding);
    cmd.AddValue("nHostsPerSubnet", "Hosts per subnet", nHostsPerSubnet);
    cmd.AddValue("simSec", "Total simulation duration (seconds)", simSec);
    cmd.AddValue("warmupSec", "Warmup seconds excluded from metrics", warmupSec);
    cmd.AddValue("runId", "Run identifier for output JSON", runId);
    cmd.AddValue("outPath", "Output JSON path", outPath);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(algo));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));

    const double bdpBytes = (coreMbps * 1e6 / 8.0) * (rttMs / 1000.0);
    const uint32_t queueBytes =
        std::max<uint32_t>(static_cast<uint32_t>(bufferBdpMult * bdpBytes), 1500u);

    // RTT budget split roughly: half on the wide uplink, rest split across tiers.
    const double uplinkDelayMs = rttMs * 0.35;
    const double coreDelayMs = rttMs * 0.075;
    const double distDelayMs = rttMs * 0.0375;
    const double accessDelayMs = rttMs * 0.0375;

    NodeContainer server;
    server.Create(1);
    NodeContainer gateway;
    gateway.Create(1);
    NodeContainer core;
    core.Create(1);
    NodeContainer buildings;
    buildings.Create(nBuildings);

    // server <-> gateway
    PointToPointHelper uplink;
    {
        std::ostringstream r;
        r << uplinkMbps << "Mbps";
        uplink.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << uplinkDelayMs << "ms";
        uplink.SetChannelAttribute("Delay", StringValue(d.str()));
    }
    NetDeviceContainer uplinkDev = uplink.Install(server.Get(0), gateway.Get(0));

    // gateway <-> core (bottleneck)
    PointToPointHelper coreLink;
    {
        std::ostringstream r;
        r << coreMbps << "Mbps";
        coreLink.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << coreDelayMs << "ms";
        coreLink.SetChannelAttribute("Delay", StringValue(d.str()));
        coreLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("1p"));
    }
    NetDeviceContainer coreDev = coreLink.Install(gateway.Get(0), core.Get(0));

    // core <-> building
    PointToPointHelper distLink;
    {
        std::ostringstream r;
        r << distMbps << "Mbps";
        distLink.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << distDelayMs << "ms";
        distLink.SetChannelAttribute("Delay", StringValue(d.str()));
    }
    std::vector<NetDeviceContainer> buildingDevs;
    buildingDevs.reserve(nBuildings);
    for (uint32_t b = 0; b < nBuildings; ++b)
    {
        buildingDevs.push_back(distLink.Install(core.Get(0), buildings.Get(b)));
    }

    // building <-> subnet switches (modeled as routers) and hosts
    PointToPointHelper accessLink;
    {
        std::ostringstream r;
        r << accessMbps << "Mbps";
        accessLink.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << accessDelayMs << "ms";
        accessLink.SetChannelAttribute("Delay", StringValue(d.str()));
    }
    PointToPointHelper hostLink;
    hostLink.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    hostLink.SetChannelAttribute("Delay", StringValue("0.5ms"));

    NodeContainer subnets;
    NodeContainer hosts;
    std::vector<NetDeviceContainer> subnetDevs;  // building<->subnet
    std::vector<NetDeviceContainer> hostDevs;    // subnet<->host

    for (uint32_t b = 0; b < nBuildings; ++b)
    {
        for (uint32_t s = 0; s < nSubnetsPerBuilding; ++s)
        {
            Ptr<Node> subnetNode = CreateObject<Node>();
            subnets.Add(subnetNode);
            subnetDevs.push_back(
                accessLink.Install(buildings.Get(b), subnetNode));
            for (uint32_t h = 0; h < nHostsPerSubnet; ++h)
            {
                Ptr<Node> host = CreateObject<Node>();
                hosts.Add(host);
                hostDevs.push_back(hostLink.Install(subnetNode, host));
            }
        }
    }

    InternetStackHelper stack;
    stack.Install(server);
    stack.Install(gateway);
    stack.Install(core);
    stack.Install(buildings);
    stack.Install(subnets);
    stack.Install(hosts);

    // Queue discipline on the bottleneck (gateway side of core link).
    TrafficControlHelper tchBottleneck;
    std::ostringstream qsize;
    qsize << queueBytes << "B";
    tchBottleneck.SetRootQueueDisc("ns3::PfifoFastQueueDisc",
                                   "MaxSize",
                                   StringValue(qsize.str()));
    tchBottleneck.Install(coreDev);

    TrafficControlHelper tchDefault;
    tchDefault.SetRootQueueDisc("ns3::PfifoFastQueueDisc");

    // Address assignment
    Ipv4AddressHelper addr;
    addr.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer uplinkIf = addr.Assign(uplinkDev);
    addr.SetBase("10.0.1.0", "255.255.255.0");
    addr.Assign(coreDev);

    for (uint32_t b = 0; b < nBuildings; ++b)
    {
        std::ostringstream base;
        base << "10.1." << b << ".0";
        addr.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.0");
        addr.Assign(buildingDevs[b]);
    }

    uint32_t subnetIdx = 0;
    uint32_t hostIdx = 0;
    for (uint32_t b = 0; b < nBuildings; ++b)
    {
        for (uint32_t s = 0; s < nSubnetsPerBuilding; ++s)
        {
            std::ostringstream sb;
            sb << "10.2." << (b * nSubnetsPerBuilding + s) % 256 << ".0";
            addr.SetBase(Ipv4Address(sb.str().c_str()), "255.255.255.0");
            addr.Assign(subnetDevs[subnetIdx]);
            ++subnetIdx;

            for (uint32_t h = 0; h < nHostsPerSubnet; ++h)
            {
                std::ostringstream hb;
                hb << "10." << (100 + (hostIdx / 250)) << "." << (hostIdx % 250) << ".0";
                addr.SetBase(Ipv4Address(hb.str().c_str()), "255.255.255.0");
                addr.Assign(hostDevs[hostIdx]);
                ++hostIdx;
            }
        }
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Server sinks (one sink app, many connections)
    const uint16_t sinkPort = 50000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApp = sinkHelper.Install(server.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simSec + 1.0));

    Ipv4Address serverAddr = uplinkIf.GetAddress(0);

    const uint32_t totalHosts = hosts.GetN();
    for (uint32_t i = 0; i < totalHosts; ++i)
    {
        BulkSendHelper src("ns3::TcpSocketFactory",
                           InetSocketAddress(serverAddr, sinkPort));
        src.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = src.Install(hosts.Get(i));
        const double start =
            (totalHosts > 1) ? (1.0 * i / (totalHosts - 1)) : 0.0;
        app.Start(Seconds(start));
        app.Stop(Seconds(simSec));
    }

    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> monitor = fmh.InstallAll();
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001));

    Simulator::Stop(Seconds(simSec + 1.0));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());
    auto stats = monitor->GetFlowStats();

    std::vector<double> flowThroughputMbps;
    uint64_t totalTxPackets = 0;
    uint64_t totalLostPackets = 0;
    double delaySumSec = 0.0;
    uint64_t delaySamples = 0;
    std::vector<double> delayHistSec;
    std::vector<uint32_t> delayHistCount;

    const double measureWindow = std::max(0.1, simSec - warmupSec);

    for (const auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        if (t.destinationPort != sinkPort)
        {
            continue;
        }
        const auto& s = kv.second;
        double mbps = (s.rxBytes * 8.0) / (measureWindow * 1e6);
        flowThroughputMbps.push_back(mbps);
        totalTxPackets += s.txPackets;
        totalLostPackets += s.lostPackets;
        delaySumSec += s.delaySum.GetSeconds();
        delaySamples += s.rxPackets;

        const Histogram& h = s.delayHistogram;
        for (uint32_t b = 0; b < h.GetNBins(); ++b)
        {
            if (delayHistSec.size() <= b)
            {
                delayHistSec.push_back(h.GetBinStart(b) + 0.5 * h.GetBinWidth(b));
                delayHistCount.push_back(0);
            }
            delayHistCount[b] += static_cast<uint32_t>(h.GetBinCount(b));
        }
    }

    double totalThroughput = 0.0;
    double sumSq = 0.0;
    for (double m : flowThroughputMbps)
    {
        totalThroughput += m;
        sumSq += m * m;
    }
    double jain = 0.0;
    if (!flowThroughputMbps.empty() && sumSq > 0.0)
    {
        jain = (totalThroughput * totalThroughput) / (flowThroughputMbps.size() * sumSq);
    }
    double meanDelayMs = (delaySamples > 0) ? (delaySumSec / delaySamples * 1000.0) : 0.0;
    double lossRate =
        (totalTxPackets > 0) ? (static_cast<double>(totalLostPackets) / totalTxPackets) : 0.0;

    double p95DelayMs = 0.0;
    uint64_t totalCount = 0;
    for (uint32_t c : delayHistCount)
    {
        totalCount += c;
    }
    if (totalCount > 0)
    {
        uint64_t target = static_cast<uint64_t>(std::ceil(0.95 * totalCount));
        uint64_t acc = 0;
        for (size_t b = 0; b < delayHistCount.size(); ++b)
        {
            acc += delayHistCount[b];
            if (acc >= target)
            {
                p95DelayMs = delayHistSec[b] * 1000.0;
                break;
            }
        }
    }

    std::ofstream out(outPath);
    out << "{";
    out << "\"runId\":\"" << runId << "\",";
    out << "\"topology\":\"university\",";
    out << "\"algo\":\"" << algo << "\",";
    out << "\"coreMbps\":" << coreMbps << ",";
    out << "\"rttMs\":" << rttMs << ",";
    out << "\"bufferBdpMult\":" << bufferBdpMult << ",";
    out << "\"nBuildings\":" << nBuildings << ",";
    out << "\"nSubnetsPerBuilding\":" << nSubnetsPerBuilding << ",";
    out << "\"nHostsPerSubnet\":" << nHostsPerSubnet << ",";
    out << "\"nFlows\":" << totalHosts << ",";
    out << "\"simSec\":" << simSec << ",";
    out << "\"warmupSec\":" << warmupSec << ",";
    out << "\"queueBytes\":" << queueBytes << ",";
    out << "\"totalThroughputMbps\":" << totalThroughput << ",";
    out << "\"perFlowThroughputMbps\":[";
    for (size_t i = 0; i < flowThroughputMbps.size(); ++i)
    {
        if (i) out << ",";
        out << flowThroughputMbps[i];
    }
    out << "],";
    out << "\"meanDelayMs\":" << meanDelayMs << ",";
    out << "\"p95DelayMs\":" << p95DelayMs << ",";
    out << "\"lossRate\":" << lossRate << ",";
    out << "\"jainFairness\":" << jain;
    out << "}\n";
    out.close();

    Simulator::Destroy();
    return 0;
}
