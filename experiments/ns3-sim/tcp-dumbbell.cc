// Dumbbell TCP congestion control benchmark for ns-3.
// Build: drop into ns-3's scratch/ directory, then `./ns3 build`.
// Run:   ./ns3 run "scratch/tcp-dumbbell --algo=ns3::TcpCubic --bwMbps=10 \
//                   --rttMs=50 --bufferBdpMult=1 --nFlows=2 --simSec=60 \
//                   --warmupSec=5 --runId=smoke --outPath=/tmp/smoke.json"

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

NS_LOG_COMPONENT_DEFINE("TcpDumbbell");

int
main(int argc, char* argv[])
{
    std::string algo = "ns3::TcpCubic";
    double bwMbps = 10.0;
    double rttMs = 50.0;
    double bufferBdpMult = 1.0;
    uint32_t nFlows = 2;
    double simSec = 60.0;
    double warmupSec = 5.0;
    std::string runId = "run";
    std::string outPath = "run.json";

    CommandLine cmd;
    cmd.AddValue("algo", "TCP congestion control TypeId (e.g. ns3::TcpBbr)", algo);
    cmd.AddValue("bwMbps", "Bottleneck bandwidth in Mbps", bwMbps);
    cmd.AddValue("rttMs", "Base round-trip time in ms", rttMs);
    cmd.AddValue("bufferBdpMult", "Bottleneck buffer as multiple of BDP", bufferBdpMult);
    cmd.AddValue("nFlows", "Number of concurrent TCP flows", nFlows);
    cmd.AddValue("simSec", "Total simulation duration (seconds)", simSec);
    cmd.AddValue("warmupSec", "Warmup window excluded from metrics", warmupSec);
    cmd.AddValue("runId", "Run identifier written into output JSON", runId);
    cmd.AddValue("outPath", "Output JSON path", outPath);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(algo));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));

    const double bdpBytes = (bwMbps * 1e6 / 8.0) * (rttMs / 1000.0);
    const uint32_t queueBytes =
        std::max<uint32_t>(static_cast<uint32_t>(bufferBdpMult * bdpBytes), 1500u);

    NodeContainer routers;
    routers.Create(2);
    NodeContainer senders;
    senders.Create(nFlows);
    NodeContainer receivers;
    receivers.Create(nFlows);

    PointToPointHelper bottleneck;
    {
        std::ostringstream rate;
        rate << bwMbps << "Mbps";
        bottleneck.SetDeviceAttribute("DataRate", StringValue(rate.str()));
        std::ostringstream delay;
        delay << (rttMs / 2.0) << "ms";
        bottleneck.SetChannelAttribute("Delay", StringValue(delay.str()));
        bottleneck.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("1p"));
    }
    NetDeviceContainer bottleneckDev = bottleneck.Install(routers.Get(0), routers.Get(1));

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    std::vector<NetDeviceContainer> senderDevs;
    std::vector<NetDeviceContainer> receiverDevs;
    senderDevs.reserve(nFlows);
    receiverDevs.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        senderDevs.push_back(access.Install(senders.Get(i), routers.Get(0)));
        receiverDevs.push_back(access.Install(routers.Get(1), receivers.Get(i)));
    }

    InternetStackHelper stack;
    stack.InstallAll();

    TrafficControlHelper tchBottleneck;
    std::ostringstream qsize;
    qsize << queueBytes << "B";
    tchBottleneck.SetRootQueueDisc("ns3::PfifoFastQueueDisc",
                                   "MaxSize",
                                   StringValue(qsize.str()));
    tchBottleneck.Install(bottleneckDev);

    TrafficControlHelper tchAccess;
    tchAccess.SetRootQueueDisc("ns3::PfifoFastQueueDisc");

    Ipv4AddressHelper addr;
    addr.SetBase("10.0.0.0", "255.255.255.252");
    addr.Assign(bottleneckDev);

    std::vector<Ipv4InterfaceContainer> rxIfaces;
    rxIfaces.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::ostringstream base;
        base << "10.1." << i << ".0";
        addr.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.0");
        addr.Assign(senderDevs[i]);

        std::ostringstream base2;
        base2 << "10.2." << i << ".0";
        addr.SetBase(Ipv4Address(base2.str().c_str()), "255.255.255.0");
        rxIfaces.push_back(addr.Assign(receiverDevs[i]));
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const uint16_t sinkPort = 50000;
    ApplicationContainer sinkApps;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
        sinkApps.Add(sink.Install(receivers.Get(i)));
    }
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simSec + 1.0));

    ApplicationContainer srcApps;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        Ipv4Address dst = rxIfaces[i].GetAddress(1);
        BulkSendHelper src("ns3::TcpSocketFactory", InetSocketAddress(dst, sinkPort));
        src.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = src.Install(senders.Get(i));
        const double start = (nFlows > 1) ? (1.0 * i / (nFlows - 1)) : 0.0;
        app.Start(Seconds(start));
        app.Stop(Seconds(simSec));
        srcApps.Add(app);
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
        jain =
            (totalThroughput * totalThroughput) / (flowThroughputMbps.size() * sumSq);
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
    out << "\"algo\":\"" << algo << "\",";
    out << "\"bwMbps\":" << bwMbps << ",";
    out << "\"rttMs\":" << rttMs << ",";
    out << "\"bufferBdpMult\":" << bufferBdpMult << ",";
    out << "\"nFlows\":" << nFlows << ",";
    out << "\"simSec\":" << simSec << ",";
    out << "\"warmupSec\":" << warmupSec << ",";
    out << "\"queueBytes\":" << queueBytes << ",";
    out << "\"totalThroughputMbps\":" << totalThroughput << ",";
    out << "\"perFlowThroughputMbps\":[";
    for (size_t i = 0; i < flowThroughputMbps.size(); ++i)
    {
        if (i)
        {
            out << ",";
        }
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
