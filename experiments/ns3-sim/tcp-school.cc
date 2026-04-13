// K-12 school network TCP benchmark for ns-3.
// Topology: a single campus with three segments behind one school router,
// all sharing a WAN uplink to an external server.
//
//     classroom_hosts ─┐
//                      ├─ classroom_switch ─┐
//     admin_hosts ─────┤                    │
//                      ├─ admin_switch ─────┤─ school_router ── WAN ── server
//     guest_hosts  ────┤                    │
//                      └─ guest_switch ─────┘
//
// Bottleneck is the WAN uplink (school_router <-> server). Each host runs
// one long-lived TCP flow to the server. Segments have independent access
// bandwidths so you can model e.g. fast classroom LAN, slow guest wifi
// backhaul, etc.
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

NS_LOG_COMPONENT_DEFINE("TcpSchool");

namespace
{

struct Segment
{
    std::string name;
    uint32_t nHosts;
    double accessMbps;     // switch<->router link speed
    double accessDelayMs;  // switch<->router delay
};

}  // namespace

int
main(int argc, char* argv[])
{
    std::string algo = "ns3::TcpCubic";
    double wanMbps = 200.0;        // bottleneck: school <-> server
    double wanDelayMs = 15.0;      // one-way delay on WAN leg
    double bufferBdpMult = 1.0;
    uint32_t nClassroomHosts = 20;
    uint32_t nAdminHosts = 5;
    uint32_t nGuestHosts = 15;
    double classroomAccessMbps = 1000.0;
    double adminAccessMbps = 1000.0;
    double guestAccessMbps = 100.0;  // guest wifi backhaul is slower
    double simSec = 60.0;
    double warmupSec = 5.0;
    std::string runId = "school";
    std::string outPath = "school.json";

    CommandLine cmd;
    cmd.AddValue("algo", "TCP congestion control TypeId", algo);
    cmd.AddValue("wanMbps", "WAN uplink Mbps (bottleneck)", wanMbps);
    cmd.AddValue("wanDelayMs", "WAN one-way delay ms", wanDelayMs);
    cmd.AddValue("bufferBdpMult", "Bottleneck buffer as multiple of BDP", bufferBdpMult);
    cmd.AddValue("nClassroomHosts", "Hosts on classroom segment", nClassroomHosts);
    cmd.AddValue("nAdminHosts", "Hosts on admin segment", nAdminHosts);
    cmd.AddValue("nGuestHosts", "Hosts on guest segment", nGuestHosts);
    cmd.AddValue("classroomAccessMbps", "Classroom switch uplink Mbps", classroomAccessMbps);
    cmd.AddValue("adminAccessMbps", "Admin switch uplink Mbps", adminAccessMbps);
    cmd.AddValue("guestAccessMbps", "Guest switch uplink Mbps", guestAccessMbps);
    cmd.AddValue("simSec", "Total simulation duration (seconds)", simSec);
    cmd.AddValue("warmupSec", "Warmup seconds excluded from metrics", warmupSec);
    cmd.AddValue("runId", "Run identifier", runId);
    cmd.AddValue("outPath", "Output JSON path", outPath);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(algo));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));

    const double rttMs = 2.0 * wanDelayMs + 4.0;  // WAN + small access contribution
    const double bdpBytes = (wanMbps * 1e6 / 8.0) * (rttMs / 1000.0);
    const uint32_t queueBytes =
        std::max<uint32_t>(static_cast<uint32_t>(bufferBdpMult * bdpBytes), 1500u);

    std::vector<Segment> segments = {
        {"classroom", nClassroomHosts, classroomAccessMbps, 1.0},
        {"admin", nAdminHosts, adminAccessMbps, 1.0},
        {"guest", nGuestHosts, guestAccessMbps, 2.0},
    };

    NodeContainer server;
    server.Create(1);
    NodeContainer schoolRouter;
    schoolRouter.Create(1);

    // WAN bottleneck
    PointToPointHelper wan;
    {
        std::ostringstream r;
        r << wanMbps << "Mbps";
        wan.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << wanDelayMs << "ms";
        wan.SetChannelAttribute("Delay", StringValue(d.str()));
        wan.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("1p"));
    }
    NetDeviceContainer wanDev = wan.Install(schoolRouter.Get(0), server.Get(0));

    PointToPointHelper hostLink;
    hostLink.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    hostLink.SetChannelAttribute("Delay", StringValue("0.2ms"));

    NodeContainer switches;
    NodeContainer allHosts;
    std::vector<NetDeviceContainer> switchUplinkDevs;
    std::vector<NetDeviceContainer> hostDevs;
    std::vector<uint32_t> segmentHostCounts;
    segmentHostCounts.reserve(segments.size());

    for (const auto& seg : segments)
    {
        Ptr<Node> sw = CreateObject<Node>();
        switches.Add(sw);

        PointToPointHelper access;
        std::ostringstream r;
        r << seg.accessMbps << "Mbps";
        access.SetDeviceAttribute("DataRate", StringValue(r.str()));
        std::ostringstream d;
        d << seg.accessDelayMs << "ms";
        access.SetChannelAttribute("Delay", StringValue(d.str()));
        switchUplinkDevs.push_back(access.Install(schoolRouter.Get(0), sw));

        for (uint32_t i = 0; i < seg.nHosts; ++i)
        {
            Ptr<Node> h = CreateObject<Node>();
            allHosts.Add(h);
            hostDevs.push_back(hostLink.Install(sw, h));
        }
        segmentHostCounts.push_back(seg.nHosts);
    }

    InternetStackHelper stack;
    stack.Install(server);
    stack.Install(schoolRouter);
    stack.Install(switches);
    stack.Install(allHosts);

    TrafficControlHelper tchBottleneck;
    std::ostringstream qsize;
    qsize << queueBytes << "B";
    tchBottleneck.SetRootQueueDisc("ns3::PfifoFastQueueDisc",
                                   "MaxSize",
                                   StringValue(qsize.str()));
    tchBottleneck.Install(wanDev);

    TrafficControlHelper tchDefault;
    tchDefault.SetRootQueueDisc("ns3::PfifoFastQueueDisc");

    // Address assignment
    Ipv4AddressHelper addr;
    addr.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wanIf = addr.Assign(wanDev);

    for (size_t s = 0; s < segments.size(); ++s)
    {
        std::ostringstream base;
        base << "10.1." << s << ".0";
        addr.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.0");
        addr.Assign(switchUplinkDevs[s]);
    }

    uint32_t hostIdx = 0;
    for (size_t s = 0; s < segments.size(); ++s)
    {
        for (uint32_t h = 0; h < segments[s].nHosts; ++h)
        {
            std::ostringstream base;
            base << "10." << (100 + s) << "." << (hostIdx % 250) << ".0";
            addr.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.0");
            addr.Assign(hostDevs[hostIdx]);
            ++hostIdx;
        }
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const uint16_t sinkPort = 50000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApp = sinkHelper.Install(server.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simSec + 1.0));

    Ipv4Address serverAddr = wanIf.GetAddress(1);

    const uint32_t totalHosts = allHosts.GetN();
    for (uint32_t i = 0; i < totalHosts; ++i)
    {
        BulkSendHelper src("ns3::TcpSocketFactory",
                           InetSocketAddress(serverAddr, sinkPort));
        src.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer app = src.Install(allHosts.Get(i));
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
        const auto& st = kv.second;
        double mbps = (st.rxBytes * 8.0) / (measureWindow * 1e6);
        flowThroughputMbps.push_back(mbps);
        totalTxPackets += st.txPackets;
        totalLostPackets += st.lostPackets;
        delaySumSec += st.delaySum.GetSeconds();
        delaySamples += st.rxPackets;

        const Histogram& h = st.delayHistogram;
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

    // Per-segment throughput aggregation (assumes FlowMonitor enumerates flows
    // in creation order, matching our host indices). Segments are contiguous.
    std::vector<double> segThroughput(segments.size(), 0.0);
    {
        size_t idx = 0;
        for (size_t s = 0; s < segments.size() && idx < flowThroughputMbps.size(); ++s)
        {
            for (uint32_t h = 0; h < segmentHostCounts[s] && idx < flowThroughputMbps.size();
                 ++h, ++idx)
            {
                segThroughput[s] += flowThroughputMbps[idx];
            }
        }
    }

    std::ofstream out(outPath);
    out << "{";
    out << "\"runId\":\"" << runId << "\",";
    out << "\"topology\":\"school\",";
    out << "\"algo\":\"" << algo << "\",";
    out << "\"wanMbps\":" << wanMbps << ",";
    out << "\"wanDelayMs\":" << wanDelayMs << ",";
    out << "\"rttMs\":" << rttMs << ",";
    out << "\"bufferBdpMult\":" << bufferBdpMult << ",";
    out << "\"nClassroomHosts\":" << nClassroomHosts << ",";
    out << "\"nAdminHosts\":" << nAdminHosts << ",";
    out << "\"nGuestHosts\":" << nGuestHosts << ",";
    out << "\"nFlows\":" << totalHosts << ",";
    out << "\"simSec\":" << simSec << ",";
    out << "\"warmupSec\":" << warmupSec << ",";
    out << "\"queueBytes\":" << queueBytes << ",";
    out << "\"totalThroughputMbps\":" << totalThroughput << ",";
    out << "\"segmentThroughputMbps\":{";
    for (size_t s = 0; s < segments.size(); ++s)
    {
        if (s) out << ",";
        out << "\"" << segments[s].name << "\":" << segThroughput[s];
    }
    out << "},";
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
