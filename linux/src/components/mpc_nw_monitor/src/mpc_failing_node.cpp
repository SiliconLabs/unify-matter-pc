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

#include "mpc_failing_node.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_attribute_store_type_registration.h"
#include "sl_log.h"


#define LOG_TAG "mpc_failing_node"

using namespace attribute_store;
using namespace std;

sl_status_t mpc_mark_device_as_failing(attribute node) {
    try {
        auto networkStatusNode = node.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
        NodeStateNetworkStatus state = networkStatusNode.reported<NodeStateNetworkStatus>();

        auto prevStateNode = node.emplace_node(ATTRIBUTE_PREVIOUS_STATE_NETWORK_STATUS_ID);
        prevStateNode.set_reported<NodeStateNetworkStatus>(state);

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

