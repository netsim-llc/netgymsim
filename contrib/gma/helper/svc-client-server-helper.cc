/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Author: Mohamed Amine Ismail <amine.ismail@sophia.inria.fr>
 */
#include "svc-client-server-helper.h"
#include "ns3/svc-trace-client.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"

namespace ns3 {

SvcTraceClientHelper::SvcTraceClientHelper ()
{
  m_factory.SetTypeId (SvcTraceClient::GetTypeId ());
}

SvcTraceClientHelper::SvcTraceClientHelper (Address address, uint16_t port, std::string filename)
{
  m_factory.SetTypeId (SvcTraceClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("RemotePort", UintegerValue (port));
  SetAttribute ("TraceFilename", StringValue (filename));
}

SvcTraceClientHelper::SvcTraceClientHelper (Address address, std::string filename)
{
  m_factory.SetTypeId (SvcTraceClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("TraceFilename", StringValue (filename));
}

void
SvcTraceClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
SvcTraceClientHelper::Install (NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<SvcTraceClient> client = m_factory.Create<SvcTraceClient> ();
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

} // namespace ns3
