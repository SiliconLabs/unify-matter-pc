/******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 ******************************************************************************
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 *****************************************************************************/

#include "mpc_nw_monitor.h"
#include "mpc_nw_monitor.hpp"
#include "attribute.hpp"
#include "attribute_store.h"
#include "attribute_store_helper.h"
#include "mpc_attribute_resolver.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_node_monitor.h"
#include "sl_log.h"
#include "uic_attribute_definitions.h"
#include "unify_dotdot_attribute_store.h"
#include "zap-types.h"
#include <boost/algorithm/string.hpp>
#include "matter_pc_main.hpp"
#include "mpc_matter_interfaces.hpp"

#include "process.h"
#include "clock.h"
#include "etimer.h"
#include "mpc_failing_node.h"
#include "failing_nodes_datastore.h"

// Matter includes
#include "app/server/Server.h"
#include "crypto/CHIPCryptoPAL.h"
#include "lib/core/DataModelTypes.h"
#include <app/InteractionModelEngine.h>
#include <lib/dnssd/Resolver.h>
#include <lib/dnssd/ResolverProxy.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <sstream>
#include <stdbool.h>
#include <string>
#include <vector>

#define LOG_TAG "mpc_nw_monitor"

using namespace chip;
using namespace attribute_store;
using namespace std;

// Declare the Contiki Process for the MPC network monitoring
PROCESS(mpc_nw_mon_process, "mpc_nw_mon_process");

struct interviewable_node {
public:
    attribute       node; 
    clock_time_t    interviewAfter; //seconds since epoch
};

typedef enum {
    MPC_INTERVIEW_TIMER_SET_EVENT,
} attribute_resolver_worker_event_t;

struct etimer interview_trigger_timer;
vector<struct interviewable_node> pending_interviews;
const int INTERVIEW_DELAY_MSEC = 10000; //10s = 10,000 ms

static void generateUNID(string & unid)
{
    unsigned long rn;

    stringstream stream;
    stream.setf(ios::hex, ios::basefield);
    stream.setf(ios::internal, ios::adjustfield);
    stream.fill('0');
    stream.width(16);

    Crypto::DRBG_get_bytes(reinterpret_cast<uint8_t *>(&rn), sizeof(rn));
    stream << rn;

    unid.append("mt-" + stream.str());
}

bool OperationalDiscover::needs_update(attribute node)
{
    try
    {
        auto state = node.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS).reported<NodeStateNetworkStatus>();
        return ((state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL) &&
                    (state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL));
    } catch (std::invalid_argument const & ex)
    {
        return false;
    }
}

void OperationalDiscover::mpc_queue_node_for_interview(attribute node)
{
    clock_time_t interviewDelay = clock_time() + INTERVIEW_DELAY_MSEC;
        
    struct interviewable_node nodeEntry;
    nodeEntry.node = node;
    nodeEntry.interviewAfter = interviewDelay;
    pending_interviews.push_back(nodeEntry);

    // if there list has only the entry we just added or if timer is no longer running, post event to start timer
    if (pending_interviews.size() == 1 || etimer_expired(&interview_trigger_timer))
    {
        process_post(&mpc_nw_mon_process, MPC_INTERVIEW_TIMER_SET_EVENT, (void *)INTERVIEW_DELAY_MSEC);
    }
}

void OperationalDiscover::mpc_node_removal_handle(attribute unid)
{
    NodeStateNetworkStatus state = unid.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS).reported<NodeStateNetworkStatus>();

    // Search for the node in pending_interviews list
    auto nodeEntryToDelete = std::find_if(pending_interviews.begin(), pending_interviews.end(), [&unid](const interviewable_node &n) {
        return n.node == unid;
    });
    // proceed to delete the node entry from the queue
    if (nodeEntryToDelete != pending_interviews.end())
    {
        if (nodeEntryToDelete == pending_interviews.begin() && (nodeEntryToDelete + 1) != pending_interviews.end())
        {
            clock_time_t nextTrigger = 0;
            if (clock_time() < (nodeEntryToDelete + 1)->interviewAfter)
            {
                nextTrigger = (nodeEntryToDelete + 1)->interviewAfter - clock_time();
            }
            process_post(&mpc_nw_mon_process, MPC_INTERVIEW_TIMER_SET_EVENT, (void *)nextTrigger);
        }
        // Erase the node entry from the vector
        pending_interviews.erase(nodeEntryToDelete);

        if (state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_INTERVIEWING)
        {
            // Returning since the interview is not started and no further actions are required
            return;
        }
    }

    attribute node= unid.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
    auto node_nw_list_str = node.reported<string>();
    if (!node_nw_list_str.empty() && node_nw_list_str.find(":") != std::string::npos)
    {
        std::string nodeIdStr = node_nw_list_str.erase(0, node_nw_list_str.find(":") + 1);
        sl_log_debug(LOG_TAG, "Node ID: %s [%llu]", nodeIdStr.c_str(), stoull(nodeIdStr));               

        if (state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_INTERVIEWING)
        {
            NodeId nodeId = stoull(nodeIdStr);
            // TODO: Multifabtric support requires extracting index from networklist
            chip::app::InteractionModelEngine::GetInstance()->ShutdownSubscriptions(1, nodeId);
        }
        unid.delete_node();
    }
    else
    {
        sl_log_debug(LOG_TAG,"node_nw_list_str is empty");
    }
}
    
void OperationalDiscover::OnNodeDiscovered(const chip::Dnssd::DiscoveredNodeData & discoveredNodeData)
{
        
    if (!discoveredNodeData.Is<chip::Dnssd::OperationalNodeBrowseData>()) {
        // not Operational Node Browse Data
        return;
    }
        
    auto & operationalData  = discoveredNodeData.Get<chip::Dnssd::OperationalNodeBrowseData>();
        
    sl_log_debug(LOG_TAG, "Found matter node with node ID " ChipLogFormatX64 ":" ChipLogFormatX64, 
                        ChipLogValueX64(operationalData.peerId.GetCompressedFabricId()),
                        ChipLogValueX64(operationalData.peerId.GetNodeId()));
    if (ChipServer::GetChipServer()->FindFabricWithCompressedId(operationalData.peerId.GetCompressedFabricId()))
    {
        // prepare networkList entry
        auto networkListEntry = to_string(operationalData.peerId.GetCompressedFabricId());
        networkListEntry.append(":");
        networkListEntry.append(to_string(operationalData.peerId.GetNodeId()));
        attribute node;

        try
        {
            // If networkList entry already exists in one of the node, we need to skip processing
            for (auto unids : attribute::root().children(ATTRIBUTE_NODE_ID))
            {
                node = unids.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
                if (!node.is_valid()) 
                {
                    // ideally not a possible case but skip if networklist doesn't exist
                    sl_log_debug(LOG_TAG, "Skipped node [%x] due to absence of networkList attribute", node);
                    continue;
                }
                auto node_nw_list_str = node.reported<string>();
                std::vector<std::string> node_nw_list;
                size_t findIndex = 0;
                boost::algorithm::split(node_nw_list, node_nw_list_str.c_str(), boost::is_any_of(","));
                for (findIndex = 0; (node_nw_list[findIndex] != networkListEntry) 
                                        && (findIndex < node_nw_list.size()); findIndex++);
                if (findIndex != node_nw_list.size())
                {
                    sl_log_debug(LOG_TAG, "entry already exists in networkList of %s", unids.reported<string>().c_str());
                    break;
                }
                node = attribute(ATTRIBUTE_STORE_INVALID_NODE);
            }
            if(operationalData.hasZeroTTL)
            {
                if (!node.is_valid())
                {
                    return;
                }
                attribute unid = node.parent();
                try
                {
                    auto state = unid.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS).reported<NodeStateNetworkStatus>();
                    if (state == ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE)
                    {
                        return;
                    }
                } catch (std::invalid_argument const & ex)
                {
                    sl_log_warning(LOG_TAG, "Removing node %s without state check",unid.reported<string>().c_str());
                }
                sl_log_debug(LOG_TAG, "Removing node %s",unid.reported<string>().c_str());
                mpc_node_removal_handle(unid);
                return;
            }

            if (node.is_valid())
            {
                if (!needs_update(node))
                {
                    sl_log_debug(LOG_TAG, "skipping addition of node %u since it already exists in attribute tree",
                                    operationalData.peerId.GetNodeId());
                    return;
                }
            }
            else
            {
                sl_log_debug(LOG_TAG, "Found matter node with node ID " ChipLogFormatX64, ChipLogValueX64(operationalData.peerId.GetNodeId()));
                string unid;
                generateUNID(unid);
                node = attribute::root().add_node(ATTRIBUTE_NODE_ID);
                attribute_store_set_reported_string(node, unid.c_str());
            }
            auto state = ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_INTERVIEWING;
            // MPC itself doesn't need to be interviewed, directly move to Online Functional
            if (ChipServer::GetChipServer()->FindFabricWithCompressedId(operationalData.peerId.GetCompressedFabricId())
                    ->GetNodeId() == operationalData.peerId.GetNodeId())
            {
                sl_log_info(LOG_TAG, "MPC moved to Online Functional");
                auto ep = node.emplace_node<EndpointId>(ATTRIBUTE_ENDPOINT_ID, 0);
                ep.emplace_node<NodeStateSecurity>(DOTDOT_ATTRIBUTE_ID_STATE_SECURITY, ZCL_NODE_STATE_SECURITY_MATTER);
                ep.emplace_node<uint32_t>(DOTDOT_ATTRIBUTE_ID_STATE_MAXIMUM_COMMAND_DELAY, 1);
                state = ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL;
            }
            else
            {
                mpc_queue_node_for_interview(node);
            }
            sl_log_debug(LOG_TAG, "networkList formed [%s] -> <%s>", node.reported<string>().c_str(), networkListEntry.c_str());
            attribute_store_set_reported_string(node.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST), networkListEntry.c_str());
            node.emplace_node<NodeStateNetworkStatus>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS, state);

        } catch (...)
        {
            if (node.is_valid())
                node.delete_node();
            std::exception_ptr p = std::current_exception();
            sl_log_error(LOG_TAG, "Failed to add discovered node [%s]", (p ? p.__cxa_exception_type()->name() : "null"));
        }
    }
    mpc_schedule_contiki();
}

static void mpc_on_ep_change_cb(attribute_store_node_t node, attribute_store_change_t change)
{
    // if there is only one node in under root, then it must belong to MPC itself
    if (attribute::root().child_count() > 1)
    {
        // If attribute node belongs to MPC itself we can skip the interview process.
        auto pFabricInfo = chip::Server::GetInstance().GetFabricTable().FindFabricWithIndex(1);
        if (!pFabricInfo) return;
        auto mpcNodeId = pFabricInfo->GetNodeId();

        auto parentOfChangedAttributeNode = attribute(node).parent().child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
        if (!parentOfChangedAttributeNode.is_valid())
        {
            return;
        }
        auto parentNetworkList = parentOfChangedAttributeNode.reported<string>();

        if (change == ATTRIBUTE_CREATED)
        {
            vector<std::string> network_list_parsed;
            boost::algorithm::split(network_list_parsed, parentNetworkList.c_str(), boost::is_any_of(":,"));
            for (uint8_t i = 1; i < network_list_parsed.size(); i += 2)
            {
                if (network_list_parsed[i] == to_string(mpcNodeId))
                {
                    sl_log_info(LOG_TAG, "skipping descriptor cluster addition for MPC");
                    return;
                }
            }

            attribute ep = attribute(node);
            try
            {
                ep.emplace_node(ATTRIBUTE_SERVERLIST_ID);
                ep.emplace_node(ATTRIBUTE_CLIENTLIST_ID);
                ep.emplace_node(ATTRIBUTE_PARTSLIST_ID);
                ep.emplace_node(ATTRIBUTE_DEVICETYPELIST_ID);
            } catch (...)
            {
                uint8_t epid = ep.reported<uint8_t>();
                sl_log_error(LOG_TAG, "Failure in adding description cluster or its attributes to ep-%u", epid);
            }
        }
    }
}

static void mpc_start_node_discovery()
{
    static OperationalDiscover mDNSdiscover;
    static chip::Dnssd::DiscoveryContext * mContext = nullptr;
    sl_log_debug(LOG_TAG, "in mpc_start_node_discovery()");

    // Start discovering nodes
    if (chip::Dnssd::Resolver::Instance().IsInitialized())
    {
        mContext = Platform::New<chip::Dnssd::DiscoveryContext>();
        mContext->SetDiscoveryDelegate(&mDNSdiscover);
        chip::Dnssd::Resolver::Instance().StartDiscovery(chip::Dnssd::DiscoveryType::kOperational, chip::Dnssd::DiscoveryFilter(), *mContext);
        sl_log_debug(LOG_TAG, "in mpc_start_node_discovery()mResolver Init done");
    }
}

void find_mpc_and_update_networklist(FabricIndex removeIndex)
{
    attribute mpcNode;
    string networkList;

    if (!attribute::root().child_count())
    {
        sl_log_warning(LOG_TAG, "find_mpc_and_update_networklist() called before MPC entry");
        return;
    }
    // if there is only one node under root, then it must belong to MPC itself
    if (attribute::root().child_count() == 1)
    {
        mpcNode = attribute::root().child_by_type(ATTRIBUTE_NODE_ID);
    }

    // loop logic here is applicable when multi-fabric is to be supported
    auto fabric = ChipServer::GetChipServer()->GetFabricTable().cbegin();
    for (; fabric != ChipServer::GetChipServer()->GetFabricTable().cend(); fabric++)
    {
        // prepare networkList entry
        auto networkListItem = to_string(fabric->GetCompressedFabricId());
        networkListItem.append(":");
        networkListItem.append(to_string(fabric->GetNodeId()));
        if (fabric->GetFabricIndex() != removeIndex)
        {
            // append if it can fit into attribute storage size
            if (networkList.size() + networkListItem.size() + 1 < ATTRIBUTE_STORE_MAXIMUM_VALUE_LENGTH)
                networkList.append(networkListItem + ",");
            else
                sl_log_warning(LOG_TAG, "networkListItem not appended because of size constraint, update attribute storage strategy");
        }
        else
        {
            sl_log_info(LOG_TAG, "Removing fabric index %d from network list", fabric->GetFabricIndex());
        }
        // check if mpcNode is already found, we skip the search and move to check iteration
        if (mpcNode.is_valid())
            continue;
        // else search for node with networkList entry matching that of MPC
        for (auto node : attribute::root().children(ATTRIBUTE_NODE_ID))
        {
            auto nwListEntry = node.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
            if (nwListEntry.is_valid() && nwListEntry.reported<string>().find(networkListItem) != string::npos)
                mpcNode = node;
        }
    }
    if (!mpcNode.is_valid())
    {
        sl_log_error(LOG_TAG, "did not find node that belongs to MPC in attribute store!");
        return;
    }
    if (networkList.size() != 0)
    {
        networkList.pop_back(); // remove the last "," delimiter
        attribute_store_set_reported_string(mpcNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST), networkList.c_str());
        mpcNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS)
            .set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);
    }
    else
    {
            auto networkListAttr = mpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
            if (!networkListAttr.is_valid())
            {
                sl_log_error(LOG_TAG, "did not find node that belongs to MPC in attribute store!");
                return;
            }
            attribute_store_undefine_reported(networkListAttr);
            mpcNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS)
                .set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL);
    }
}


static void EventHandler(const DeviceLayer::ChipDeviceEvent * event, intptr_t arg)
{
    if (event->Type == DeviceLayer::DeviceEventType::kCommissioningComplete)
    {
        mpc_start_node_discovery();
    }
}

static sl_status_t mpc_populate_attribute_store()
{
    try
    {
        string mpcUnid;
        generateUNID(mpcUnid);
        auto mpcNode = attribute::root().emplace_node(ATTRIBUTE_NODE_ID);
        attribute_store_set_reported_string(mpcNode, mpcUnid.c_str());
        auto epNode = mpcNode.emplace_node<EndpointId>(ATTRIBUTE_ENDPOINT_ID, 0);
        epNode.emplace_node<NodeStateSecurity>(DOTDOT_ATTRIBUTE_ID_STATE_SECURITY, ZCL_NODE_STATE_SECURITY_MATTER);
        epNode.emplace_node<uint32_t>(DOTDOT_ATTRIBUTE_ID_STATE_MAXIMUM_COMMAND_DELAY, 1);
        mpcNode.emplace_node<NodeStateNetworkStatus>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS,
                                                     ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL);
        
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() != 0)
        {
            find_mpc_and_update_networklist(kUndefinedFabricIndex);
            mpc_start_node_discovery();
        }
        // else we won't populate networkList here as the MPC would not yet be commisioned
        return SL_STATUS_OK;
    } catch (...)
    {
        sl_log_error(LOG_TAG, "Error occured while populating MPC entries to attribute store");
        return SL_STATUS_FAIL;
    }
}

void MPCFabricDelegate::OnFabricUpdated(const FabricTable & fabricTable, FabricIndex fabricIndex)
{
    sl_log_info(LOG_TAG, "fabric with ID %x added at index %u",
            fabricTable.FindFabricWithIndex(fabricIndex)->GetCompressedFabricId(), fabricIndex);
    find_mpc_and_update_networklist(kUndefinedFabricIndex);
}

void MPCFabricDelegate::OnFabricRemoved(const FabricTable & fabricTable, FabricIndex fabricIndex)
{
    sl_log_info(LOG_TAG, "fabric count for MPC [%u]", fabricTable.FabricCount());
        
}
    
void MPCFabricDelegate::FabricWillBeRemoved(const FabricTable & fabricTable, FabricIndex fabricIndex)
{
    find_mpc_and_update_networklist(fabricIndex);

    for (auto unids : attribute::root().children(ATTRIBUTE_NODE_ID)) 
    {
        auto nwListAttribute = unids.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);       
        if (nwListAttribute.reported_exists()) 
        {
            std::string nwList = nwListAttribute.reported<std::string>();           
            if (!nwList.empty() && nwList.find(":") != std::string::npos) 
            {
                sl_log_debug(LOG_TAG, "Node nwlist: %s", nwList.c_str());
                std::string nodeIdStr = nwList.erase(0, nwList.find(":") + 1);
                sl_log_debug(LOG_TAG, "Node ID: %s [%llu]", nodeIdStr.c_str(), stoull(nodeIdStr));               
                NodeId nodeId = stoull(nodeIdStr);
                if (unids.is_valid()) 
                {
                    chip::app::InteractionModelEngine::GetInstance()->ShutdownSubscriptions(fabricIndex, nodeId);
                    unids.delete_node();
                }
            }
        }
    }
}

sl_status_t mpc_nw_monitor_init()
{
    static MPCFabricDelegate mpc_fabric_delegate;
    attribute_store_register_callback_by_type(mpc_on_ep_change_cb, ATTRIBUTE_ENDPOINT_ID);

    // Listen to fabricTable events to update networkList of MPC
    Server::GetInstance().GetFabricTable().AddFabricDelegate(&mpc_fabric_delegate);

    // Listen to events to start node discovery on commision completion
    DeviceLayer::PlatformMgrImpl().AddEventHandler(EventHandler, 0);

    // register listeners needed to setup reportables
    mpc_node_monitor_init();

    failingNodeDataStore.initialize();
    process_start(&mpc_nw_mon_process, 0);

    // Create attribute tree entries for MPC itself
    if (attribute::root().child_count() == 0)
    {
        // need to create entries for MPC only on first boot-up
        // i.e. there is no entries in attribute tree or data store.
        mpc_populate_attribute_store();
        return SL_STATUS_OK;
    }
    else
    {
        // TODO: we may need to check networkList against fabrictable to identify and delete stale fabric's entries
        // just checking availability if any fabric for now.
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
        {
            //if MPC doesn't have fabric info then its not present in any fabric 
            // in which case any data in database or attribute store is stale
            sl_log_warning(LOG_TAG, "MPC isn't part of any fabric, "
                                "attribute store is stale. deleting stale attribute store entries!");
            auto nodeList = attribute::root().children(ATTRIBUTE_NODE_ID);
            std::for_each(nodeList.begin(), nodeList.end(), [](attribute node){node.delete_node();});
            // wiped attribute store so need populate MPC entry
            mpc_populate_attribute_store();
            return SL_STATUS_OK;
        }
        // also start node discovery immediately to discover new nodes
        mpc_start_node_discovery();
    }

    // if MPC already has nodes available in attribute store, refresh them to publish last
    // known status of all the nodes including MPC itself
    sl_log_info(LOG_TAG, "refreshing all node states");
    for (auto unidNode : attribute::root().children())
    {
        mpc_attribute_resolver_helper_set_resolution_listener(unidNode);
        attribute_store_refresh_node_and_children_callbacks(unidNode);
    }

    mpc_failing_node_auto_recovery();

    return SL_STATUS_OK;
}

static void mpc_nw_monitor_ev_init()
{
    pending_interviews.clear();
}

static void mpc_start_next_pending_interview()
{
    if (pending_interviews.empty())
    {
        sl_log_debug(LOG_TAG, "interview timer expired when there is no pending node to be interviewed");
        return;
    }

    // fetch first node from pending list and start interview by adding ep0 and then remove it from list
    auto nodeEntry = pending_interviews.cbegin();
    attribute node = nodeEntry->node;
    sl_log_info(LOG_TAG, "Starting Interview for node %x", node);
    // Register resolver completion call back for all other node for post interview processing
    mpc_attribute_resolver_helper_set_resolution_listener(node);
    auto ep = node.emplace_node<EndpointId>(ATTRIBUTE_ENDPOINT_ID, 0);
    ep.emplace_node<NodeStateSecurity>(DOTDOT_ATTRIBUTE_ID_STATE_SECURITY, ZCL_NODE_STATE_SECURITY_MATTER);
    ep.emplace_node<uint32_t>(DOTDOT_ATTRIBUTE_ID_STATE_MAXIMUM_COMMAND_DELAY, 1);
    pending_interviews.erase(nodeEntry);

    if (!pending_interviews.empty())
    {
        auto nextNodeEntry = pending_interviews.cbegin();
        clock_time_t nextTrigger = 0;
        if (clock_time() < nextNodeEntry->interviewAfter)
        {
            nextTrigger = clock_time() - nextNodeEntry->interviewAfter;
        }
        process_post(&mpc_nw_mon_process, MPC_INTERVIEW_TIMER_SET_EVENT, (void *)nextTrigger);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Contiki Process thread
///////////////////////////////////////////////////////////////////////////////
PROCESS_THREAD(mpc_nw_mon_process, ev, data)
{
    PROCESS_BEGIN();
    while (1) {
        if (ev == PROCESS_EVENT_INIT) {
            mpc_nw_monitor_ev_init();
        } else if (ev == PROCESS_EVENT_EXIT) {
            sl_log_debug(LOG_TAG, "Teardown of mpc network monitor");
        } else if ((ev == PROCESS_EVENT_TIMER)
               && (data == &interview_trigger_timer)) {
            mpc_start_next_pending_interview();
        } else if (ev == MPC_INTERVIEW_TIMER_SET_EVENT) {
            sl_log_debug(LOG_TAG,
                   "Restarting timer for Next interview trigger [%lu ms from now]",
                   (clock_time_t)data);
            etimer_set(&interview_trigger_timer, (clock_time_t)data);
        }

        PROCESS_WAIT_EVENT();
    }
    PROCESS_END()
}