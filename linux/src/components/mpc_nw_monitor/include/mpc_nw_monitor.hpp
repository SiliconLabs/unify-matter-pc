/******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 ******************************************************************************
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 *****************************************************************************/

/**
 * @defgroup mpc_nw_monitor MPC network monitoring extension
 * @ingroup mpc_components
 *
 * @brief Add the MPC-specific fixtures to the Unify \ref system.
 *
 * This module is the source of all MPC-specific network monitoring.
 *
 * The after initialization the network monitor will begin.
 *
 * @{
 */

#ifndef MPC_MW_MONITOR_HPP
#define MPC_MW_MONITOR_HPP

#include "attribute.hpp"
#include "app/server/Server.h"

using namespace chip;
using namespace attribute_store;
using namespace std;


class OperationalDiscover : public chip::Dnssd::DiscoverNodeDelegate 
{
public:
    // Gets called when node needs an update
    bool needs_update(attribute node);

    // Gets called when mpc node in queue for interview
    void mpc_queue_node_for_interview(attribute node);

    // Gets called to remove the node 
    void mpc_node_removal_handle(attribute unid);

    //Gets called once a node is discovered
    void OnNodeDiscovered(const chip::Dnssd::DiscoveredNodeData & discoveredNodeData) override;

};


class MPCFabricDelegate : public FabricTable::Delegate {
public:
    // Gets called when operational credentials are changed, which may not be persistent.
    // Can be used to affect what is needed for UpdateNOC prior to commit.
    void OnFabricUpdated(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override;
    
    // Gets called when a fabric is deleted, such as on FabricTable::Delete().
    void OnFabricRemoved(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override;
    
    // Gets called when a fabric is about to be deleted, such as on
    // FabricTable::Delete().  This allows actions to be taken that need the
    // fabric to still be around before we delete it.
    void FabricWillBeRemoved(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override;
};

// Gets called when MPC Node to find and update the networklist 
void find_mpc_and_update_networklist(FabricIndex removeIndex);

#endif // MPC_NW_MONITOR_HPP