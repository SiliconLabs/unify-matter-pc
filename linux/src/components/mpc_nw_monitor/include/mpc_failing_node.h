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

#ifndef MPC_FAILING_NODE_H
#define MPC_FAILING_NODE_H

#include <lib/core/CHIPError.h>
#include "sl_status.h"
#include "attribute.hpp"
#include "zap-types.h"

#ifdef __cplusplus
extern "C" {
#endif

using namespace attribute_store;
using namespace std;

/**
 * @brief mark device as failing for given node
 *
 * @param node node id of type ATTRIBUTE_NODE_ID corresponding to the end node/device
 */

sl_status_t mpc_mark_device_as_failing(attribute node);

/**
 * @brief mark device as failing for given node
 *
 * @param node node id of type ATTRIBUTE_NODE_ID corresponding to the end node/device
 * @param state state of network status for node
 */

sl_status_t mpc_fetch_saved_state(attribute node, NodeStateNetworkStatus &state);

/**
 * @brief recovers the failing/offline node by changing the state to previous state
 *
 * @param node node id of type ATTRIBUTE_NODE_ID corresponding to the end node/device
 */
sl_status_t mpc_failing_node_recovery(attribute node);

/**
 * @brief verify CHIP_ERROR and call mpc_mark_device_as_failing
 *
 * @param node node id of type ATTRIBUTE_NODE_ID corresponding to the end node/device
 * @param error transmission status
 */
sl_status_t check_and_mark_failing_node(attribute node, CHIP_ERROR error);
#ifdef __cplusplus
}
#endif

#endif // MPC_FAILING_NODE_H