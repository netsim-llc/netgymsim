/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "gma-helper.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("GmaHelper");

NS_OBJECT_ENSURE_REGISTERED (GmaHelper);


GmaHelper::GmaHelper ()
{
  NS_LOG_FUNCTION (this);
}

TypeId
GmaHelper::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::GmaHelper")
    .SetParent<Object> ()
    .SetGroupName("Gma")
    .AddConstructor<GmaHelper> ()
  ;
  return tid;
}

GmaHelper::~GmaHelper ()
{
	NS_LOG_FUNCTION (this);
}

}

