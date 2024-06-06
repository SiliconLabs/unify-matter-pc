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

#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <vector>
#include <unistd.h>
#include "failing_nodes_datastore.h"
#include "mpc_config.h"
#include "mpc_failing_node.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_attribute_store_type_registration.h"
#include "mpc_node_monitor.h"
#include "sl_log.h"
#include "process.h"
#include "etimer.h"

#define LOG_TAG "mpc_failing_node"

using namespace attribute_store;
using namespace std;

enum {
    AUTO_RECOVERY_PROCESS_EXIT
};

PROCESS(mpc_failing_node_auto_recovery_process, "mpc_failing_node_auto_recovery_process");
static struct etimer throttling_timer;
std::vector<mpc_failing_node> all_failing_nodes;

sl_status_t mpc_mark_device_as_failing(attribute node) {
    try {
        auto networkStatusNode = node.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
        NodeStateNetworkStatus state = networkStatusNode.reported<NodeStateNetworkStatus>();

        auto prevStateNode = node.emplace_node(ATTRIBUTE_PREVIOUS_STATE_NETWORK_STATUS_ID);
        prevStateNode.set_reported<NodeStateNetworkStatus>(state);

        if ((state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_INTERVIEWING) && (state != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL))
        {
            mpc_failing_node failing_node;
            clock_time_t failing_time = clock_seconds();

            failing_node = {node, failing_time};
            // TODO: Create the periodic sync or shutdown time sync to database
            // This may take more time for database file operations on an unstable network/device that continuously fails.
            failingNodeDataStore.insert_failing_node(failing_node);
        }
        
        networkStatusNode.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE);

        return SL_STATUS_OK;
    } catch (...) {
        sl_log_warning(LOG_TAG, "Error on mark device as failing %u", node);
        return SL_STATUS_FAIL;
    }
}


sl_status_t mpc_fetch_saved_state(attribute node, NodeStateNetworkStatus &state){
    try {
        state = node.child_by_type(ATTRIBUTE_PREVIOUS_STATE_NETWORK_STATUS_ID).reported<NodeStateNetworkStatus>();
        return SL_STATUS_OK;
    }
    catch(...){
        return SL_STATUS_FAIL;
    }
}

sl_status_t mpc_failing_node_recovery(attribute node)
{
    try
    {
        NodeStateNetworkStatus previous_state;
        auto networkStatusNode = node.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
        auto prevNetworkStatusNode = node.child_by_type(ATTRIBUTE_PREVIOUS_STATE_NETWORK_STATUS_ID);

        NodeStateNetworkStatus current_state = networkStatusNode.reported<NodeStateNetworkStatus>();

        if (current_state == ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE)
        {
            if(SL_STATUS_OK != mpc_fetch_saved_state(node, previous_state))
            {
                sl_log_warning(LOG_TAG, "failed to fetch the previous state of the node");
                return SL_STATUS_FAIL;
            }

            attribute_store_undefine_reported(prevNetworkStatusNode);

            // Changing the node state to previous state
            networkStatusNode.set_reported<NodeStateNetworkStatus>(previous_state);
            sl_log_info(LOG_TAG, "setting the previous state to the failing node: %d", previous_state);
            failingNodeDataStore.delete_data(node);
        }
        return SL_STATUS_OK;
    }
    catch(...)
    {
        sl_log_warning(LOG_TAG, "Error on MPC failing node recovery");
        return SL_STATUS_FAIL;
    }
}

sl_status_t mpc_failing_node_auto_recovery()
{
    failingNodeDataStore.read_failing_nodes(all_failing_nodes);

    if (!process_is_running(&mpc_failing_node_auto_recovery_process))
    {
        sl_log_debug(LOG_TAG, "starting the mpc failing node auto recovery process thread");
        process_start(&mpc_failing_node_auto_recovery_process, 0);
    }
    return SL_STATUS_OK;
}

sl_status_t check_and_mark_failing_node(attribute node, CHIP_ERROR error)
{
    if (error == CHIP_ERROR_TIMEOUT || error == CHIP_ERROR_CONNECTION_CLOSED_UNEXPECTEDLY ||
        error == CHIP_ERROR_MISSING_SECURE_SESSION || error == CHIP_ERROR_CONNECTION_ABORTED) {
        sl_status_t status = mpc_mark_device_as_failing(node);
        return status;
    }
    return SL_STATUS_OK;
}

void recover_next_failing_node()
{
    auto cfg = mpc_get_config();
    for (uint8_t recoveredNodeCount = 0; recoveredNodeCount < MAX_NODES_RECOVERABLE_WITHOUT_DELAY; recoveredNodeCount++)
    {
        if (all_failing_nodes.empty())
        {
            sl_log_debug(LOG_TAG, "list for failing nodes recovery is empty, posting process exit");
            process_post(&mpc_failing_node_auto_recovery_process, AUTO_RECOVERY_PROCESS_EXIT, NULL);
            return;
        }

        auto node = all_failing_nodes.begin();
        if (clock_seconds() < node->failed_time + cfg->auto_recovery_time)
        {
            // subscribing to the failing node
            if(mpc_node_monitor_initiate_monitoring(node->nodeID) != SL_STATUS_OK)
            {
                sl_log_info(LOG_TAG, "mpc node monitor initiate failed for the node %d", node->nodeID);
            }
        }
        else
        {
            sl_log_debug(LOG_TAG, "skipping the auto recovey since the node %d exceeded the recovery time limit", node->nodeID);
            failingNodeDataStore.delete_data(node->nodeID);
        }
        all_failing_nodes.erase(node);
    }

    // the next failing node recovery will start after cfg->auto_recovery_delay for
    // every MAX_NODES_RECOVERABLE_WITHOUT_DELAY nodes
    if(!all_failing_nodes.empty())
    {
        etimer_set(&throttling_timer, CLOCK_SECOND * (cfg->auto_recovery_delay));
    }
}

///////////////////////////////////////////////////////////////////////////////
// Contiki Process thread
///////////////////////////////////////////////////////////////////////////////
PROCESS_THREAD(mpc_failing_node_auto_recovery_process, ev, data)
{
    PROCESS_BEGIN();
    while (1) {
        if (ev == PROCESS_EVENT_INIT) {
            etimer_set(&throttling_timer, 0);
        } else if ((ev == PROCESS_EVENT_TIMER) && (data == &throttling_timer)) {
            recover_next_failing_node();
        } else if (ev == AUTO_RECOVERY_PROCESS_EXIT) {
            sl_log_debug(LOG_TAG, "exiting mpc failing node auto recovery process thread");
            PROCESS_EXIT();
        }

        PROCESS_WAIT_EVENT();
    }
    PROCESS_END();
}
