/*******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "attribute.hpp"
#include "dotdot_attribute_id_definitions.h"
#include "dotdot_mqtt.h"
#include "mpc_attribute_store.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_command_sender.hpp"
#include "mpc_sendable_command.hpp"
#include "sl_log.h"
#include "unify_dotdot_attribute_store.h"
#include "unify_dotdot_defined_attribute_types.h"
#include <AppMain.h>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <sstream>
#include <string>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OnOff;
using namespace chip::app::Clusters::LevelControl;
using namespace mpc;
using namespace attribute_store;

#define LOG_TAG "matter_cluster_command_server"

/**
 * @brief Checks if the cluster's command is supported by device with the given UNID under the given endpoint
 */
static sl_status_t check_cluster_support_for_node(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint, ClusterId clusID,
    CommandId cmdId)
{
    // get the attribute store node for the given endpoint ID
    attribute_store_node_t epNode = mpc_attribute_store_network_helper_get_endpoint_node(unid, endpoint);

    // get the cluster's AcceptedCommandList attribute entry from attribute store
    attribute_store_type_t type = ((clusID & 0xFFFF) << 16) | chip::app::Clusters::Globals::Attributes::AcceptedCommandList::Id;
    auto cmdList = attribute(epNode).child_by_type(type);
    if (!cmdList.is_valid())
        return SL_STATUS_FAIL;

    // check if the command ID is listed in the AcceptedCommandList entry
    if (cmdList.reported<std::string>().find(std::to_string(cmdId)) != std::string::npos)
        return SL_STATUS_OK;
    return SL_STATUS_FAIL;
}

static sl_status_t check_endpoint_support_for_node(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint, ClusterId clusID)
{
    // get the attribute store node for the given endpoint ID
    attribute_store_node_t epNode = mpc_attribute_store_network_helper_get_endpoint_node(unid, endpoint);
    auto serverListNode = attribute(epNode).child_by_type(ATTRIBUTE_SERVERLIST_ID);

    if (!serverListNode.is_valid()) {
        return SL_STATUS_OK; // serverList will not be present for MPC, just return true
    }

    auto serverListStr = serverListNode.reported<std::string>();
    std::vector<std::string> server_list;
    boost::algorithm::split(server_list, serverListStr.c_str(), boost::is_any_of(","));

    for (size_t i = 0; i < server_list.size(); i++) {
        if (stoul(server_list[i]) == clusID) {
            return SL_STATUS_OK;
        }
    }
    return SL_STATUS_FAIL;
}

void myWriteCallbackFunction(CHIP_ERROR error, ScopedNodeId nodeId, AttributePathParams params)
{
    auto pFabricInfo = chip::Server::GetInstance().GetFabricTable().FindFabricWithIndex(nodeId.GetFabricIndex());
    auto mpcfabricid = pFabricInfo->GetCompressedFabricId();
    auto networkItem = std::to_string(mpcfabricid);
    networkItem.append(std::string(":"));
    networkItem.append(std::to_string(nodeId.GetNodeId()));

    uint32_t formulatedAttributeId = (params.mClusterId << 16) | params.mAttributeId;

    attribute_store_node_t attrNode;

    for (auto unidNode : attribute::root().children(ATTRIBUTE_NODE_ID)) {
        auto networkListAttr = unidNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
        if (!networkListAttr.is_valid()) {
            continue;
        }

        auto networkList = networkListAttr.reported<std::string>();
        sl_log_debug(LOG_TAG, "NetworkList %s", networkList.c_str());

        std::vector<std::string> networkItems;
        boost::algorithm::split(networkItems, networkList, boost::is_any_of(","));

        bool found = false;
        for (const auto& item : networkItems) {
            if (item == networkItem) {
                found = true;
                break;
            }
        }

        if (found) {
            std::string nodeUNID = unidNode.reported<std::string>().c_str();
            const char* nodeUNIDStr = nodeUNID.c_str();
            attribute_store_node_t epNode = mpc_attribute_store_network_helper_get_endpoint_node(nodeUNIDStr, params.mEndpointId);
            sl_log_debug(LOG_TAG, "node %d", epNode);

            if (epNode == ATTRIBUTE_STORE_INVALID_NODE) {
                sl_log_error(LOG_TAG, "Failed due to Invalid end device node");
                return;
            }

            attrNode = attribute_store_get_first_child_by_type(epNode, formulatedAttributeId);
            sl_log_debug(LOG_TAG, "attrNode %d", attrNode);

            if (attrNode == ATTRIBUTE_STORE_INVALID_NODE) {
                sl_log_error(LOG_TAG, "Failed due to invalid Attribute node Id");
                return;
            }
        }
    }

    if (error == CHIP_NO_ERROR) {
        attribute_store_set_reported_as_desired(attrNode);
        return;
    } else {
        attribute_store_set_desired_as_reported(attrNode);
        return;
    }
    return;
}

// Callbacks used by the on_off cluster
static sl_status_t mpc_on_off_cluster_off_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::Off::Type> cmd;

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::Off::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_on_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::On::Type> cmd;

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::On::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_toggle_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::Toggle::Type> cmd;

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::Toggle::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_off_with_effect_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type,
    OffWithEffectEffectIdentifier effect_identifier,

    uint8_t effect_variant)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::OffWithEffect::Type> cmd;
    cmd.Data().effectIdentifier = static_cast<EffectIdentifierEnum>(effect_identifier);
    cmd.Data().effectVariant = static_cast<uint8_t>(effect_variant);

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::OffWithEffect::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_on_with_recall_global_scene_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Type> cmd;

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_on_with_timed_off_command(const dotdot_unid_t unid, dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type,
    uint8_t on_off_control,

    uint16_t on_time,

    uint16_t off_wait_time)
{
    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    SendableCommand<chip::app::Clusters::OnOff::Commands::OnWithTimedOff::Type> cmd;
    cmd.Data().onOffControl = static_cast<chip::BitMask<OnOffControlBitmap>>(on_off_control);
    cmd.Data().onTime = static_cast<uint16_t>(on_time);
    cmd.Data().offWaitTime = static_cast<uint16_t>(off_wait_time);

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type)
        return check_cluster_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id,
            chip::app::Clusters::OnOff::Commands::OnWithTimedOff::Id);

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        cmd.Send(chip::ScopedNodeId(nodeId, 1), endpointId);
        return SL_STATUS_OK;
    }
    return SL_STATUS_FAIL;
}

static sl_status_t mpc_on_off_cluster_write_attributes_callback(const dotdot_unid_t unid, const dotdot_endpoint_id_t endpoint,
    uic_mqtt_dotdot_callback_call_type_t callback_type,
    uic_mqtt_dotdot_on_off_state_t on_off_state,
    uic_mqtt_dotdot_on_off_updated_state_t updated_state)
{

    chip::EndpointId endpointId;
    chip::NodeId nodeId;
    TLV::TLVReader reader;
    std::vector<AttributePathParams> paths;

    if (UIC_MQTT_DOTDOT_CALLBACK_TYPE_SUPPORT_CHECK == callback_type) {
        return check_endpoint_support_for_node(unid, endpoint, chip::app::Clusters::OnOff::Id);
    }

    if (mpc_attribute_store_get_endpoint_and_node_from_unid(unid, endpoint, nodeId, endpointId) == SL_STATUS_OK) {
        // for each attribute marked true in updated_state
        // 1. test writable and skip non-writable attributes
        uint64_t value;
        chip::ClusterId clusId = chip::app::Clusters::OnOff::Id;
        chip::AttributeId attrId;

        System::PacketBufferTLVWriter writer;
        System::PacketBufferHandle payload = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);

        writer.Init(std::move(payload));
        TLV::TLVType outerType;

        CHIP_ERROR err = CHIP_NO_ERROR;
        if (updated_state.on_off) {
            sl_log_debug(LOG_TAG, "OnOff Cluster OnOff is not writable attribute");
        }
        if (updated_state.global_scene_control) {
            sl_log_debug(LOG_TAG, "OnOff Cluster GlobalSceneControl is not writable attribute");
        }
        if (updated_state.on_time) {
            value = on_off_state.on_time;
            attrId = chip::app::Clusters::OnOff::Attributes::OnTime::Id;

            writer.StartContainer(chip::TLV::AnonymousTag(), TLV::kTLVType_List, outerType);
            writer.Put(chip::TLV::ContextTag(attrId), value);
            writer.EndContainer(outerType);

            if (err != CHIP_NO_ERROR) {
                sl_log_info(LOG_TAG, "Writer Put Data failed with: %s", chip::ErrorStr(err));
                return SL_STATUS_FAIL;
            }

            paths.push_back(AttributePathParams(endpointId, clusId, attrId));
        }
        if (updated_state.off_wait_time) {
            value = on_off_state.off_wait_time;
            attrId = chip::app::Clusters::OnOff::Attributes::OffWaitTime::Id;

            writer.StartContainer(chip::TLV::AnonymousTag(), TLV::kTLVType_List, outerType);
            writer.Put(chip::TLV::ContextTag(attrId), value);
            writer.EndContainer(outerType);

            if (err != CHIP_NO_ERROR) {
                sl_log_info(LOG_TAG, "Writer Put Data failed with: %s", chip::ErrorStr(err));
                return SL_STATUS_FAIL;
            }

            paths.push_back(AttributePathParams(endpointId, clusId, attrId));
        }
        if (updated_state.start_up_on_off) {
            value = on_off_state.start_up_on_off;
            attrId = chip::app::Clusters::OnOff::Attributes::StartUpOnOff::Id;

            writer.StartContainer(chip::TLV::AnonymousTag(), TLV::kTLVType_List, outerType);
            writer.Put(chip::TLV::ContextTag(attrId), value);
            writer.EndContainer(outerType);

            if (err != CHIP_NO_ERROR) {
                sl_log_info(LOG_TAG, "Writer Put Data failed with: %s", chip::ErrorStr(err));
                return SL_STATUS_FAIL;
            }

            paths.push_back(AttributePathParams(endpointId, clusId, attrId));
        }

        err = writer.Finalize(&payload);
        if (err != CHIP_NO_ERROR) {
            sl_log_info(LOG_TAG, "Writer Finalize failed with: %s", chip::ErrorStr(err));
            return SL_STATUS_FAIL;
        }

        reader.Init(payload->Start(), payload->DataLength());
        auto request = chip::Platform::New<WriteRequest>(nodeId, paths, &myWriteCallbackFunction);
        request->SendCommand(reader);
    }
    sl_log_debug(LOG_TAG, "on_off Cluster write attribute %d \n", unid);
    return SL_STATUS_OK;
}
sl_status_t mpc_on_off_cluster_mapper_init()
{
    sl_log_debug(LOG_TAG, "on_off Cluster mapper initialization\n");
    uic_mqtt_dotdot_on_off_off_callback_set(&mpc_on_off_cluster_off_command);
    uic_mqtt_dotdot_on_off_on_callback_set(&mpc_on_off_cluster_on_command);
    uic_mqtt_dotdot_on_off_toggle_callback_set(&mpc_on_off_cluster_toggle_command);
    uic_mqtt_dotdot_on_off_off_with_effect_callback_set(&mpc_on_off_cluster_off_with_effect_command);
    uic_mqtt_dotdot_on_off_on_with_recall_global_scene_callback_set(&mpc_on_off_cluster_on_with_recall_global_scene_command);
    uic_mqtt_dotdot_on_off_on_with_timed_off_callback_set(&mpc_on_off_cluster_on_with_timed_off_command);
    uic_mqtt_dotdot_set_on_off_write_attributes_callback(&mpc_on_off_cluster_write_attributes_callback);

    return SL_STATUS_OK;
}
