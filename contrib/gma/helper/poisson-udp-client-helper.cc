/*
Copyright(C) 2025 Network Simulation Solutions LLC
SPDX-License-Identifier: GPLv2.0
*/

#include "poisson-udp-client-helper.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"

namespace ns3 {

PoissonUdpClientHelper::PoissonUdpClientHelper ()
{
  m_factory.SetTypeId (PoissonUdpClient::GetTypeId ());
}

PoissonUdpClientHelper::PoissonUdpClientHelper (Address address, uint16_t port)
{
  m_factory.SetTypeId (PoissonUdpClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("RemotePort", UintegerValue (port));
}

PoissonUdpClientHelper::PoissonUdpClientHelper (Address address)
{
  m_factory.SetTypeId (PoissonUdpClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
}

void
PoissonUdpClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
PoissonUdpClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<PoissonUdpClient> client = m_factory.Create<PoissonUdpClient> ();
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

} // namespace ns3