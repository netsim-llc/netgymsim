/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "ppp-tag.h"

namespace ns3 {

TypeId 
PppTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PppTag")
    .SetParent<Tag> ()
    .AddConstructor<PppTag> ()
    .AddAttribute ("SimpleValue",
                   "A simple value",
                   EmptyAttributeValue (),
                   MakeUintegerAccessor (&PppTag::GetPriority),
                   MakeUintegerChecker<uint8_t> ())
  ;
  return tid;
}
TypeId 
PppTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
PppTag::GetSerializedSize (void) const
{
  return 1;
}
void 
PppTag::Serialize (TagBuffer i) const
{
  i.WriteU8 (m_simpleValue);
}
void 
PppTag::Deserialize (TagBuffer i)
{
  m_simpleValue = i.ReadU8 ();
}
void 
PppTag::Print (std::ostream &os) const
{
  os << "v=" << (uint32_t)m_simpleValue;
}
void 
PppTag::SetPriority (uint8_t value)
{
  m_simpleValue = value;
}
uint8_t 
PppTag::GetPriority (void) const
{
  return m_simpleValue;
}

} // namespace ns3
