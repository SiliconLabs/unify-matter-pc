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
#include "mpc_command_sender.hpp"
#include "sl_log.h"
#include "mpc_failing_node.h"
#include "mpc_attribute_store.h"

#include "app/server/Server.h"

#define LOG_TAG "mpc_command_sender"
static SessionManagerProvider defaultSessionProvider;

SessionManagerProvider * AttributeReadRequest::caseSessProvider = &defaultSessionProvider;
SessionManagerProvider * WriteRequest::caseSessProvider = &defaultSessionProvider;

CHIP_ERROR AttributeReadRequest::Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
{
    CHIP_ERROR err;
    ReadPrepareParams params(sessionHandle);
    params.mpAttributePathParamsList    = mPath.data();
    params.mAttributePathParamsListSize = mPath.size();
    mBufferedReadAdapter                = Platform::MakeShared<BufferedReadCallback>(*mCallbacks);

    client = Platform::MakeUnique<ReadClient>(InteractionModelEngine::GetInstance(), &exchangeMgr, *mBufferedReadAdapter,
                                              ReadClient::InteractionType::Read);
    if (CHIP_NO_ERROR != (err = client->SendRequest(params)))
    {
        sl_log_error(LOG_TAG, "Some Problem");
        mCallbacks->OnError(err);
        return err;
    }
    return CHIP_NO_ERROR;
}

sl_status_t AttributeReadRequest::SendCommand()
{
    ScopedNodeId nodeId(mDest, 1); // TODO: Maybe we need to take fabricIndex as a parameter as well?
    caseSessProvider->FindOrEstablishSession(nodeId, &mOnConnectedCallback, &mOnConnectionFailureCallback);
    return SL_STATUS_OK;
}

CHIP_ERROR SubscribeRequest::Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
{
    CHIP_ERROR err;

    ReadPrepareParams params(sessionHandle);
    params.mpAttributePathParamsList    = mPath.data();
    params.mAttributePathParamsListSize = mPath.size();
    params.mMinIntervalFloorSeconds     = mMinInterval;
    params.mMaxIntervalCeilingSeconds   = mMaxInterval;
    params.mKeepSubscriptions           = mKeepSubs;

    mBufferedReadAdapter = Platform::MakeShared<BufferedReadCallback>(*mCallbacks);

    client = Platform::MakeUnique<ReadClient>(InteractionModelEngine::GetInstance(), &exchangeMgr, *mBufferedReadAdapter,
                                              ReadClient::InteractionType::Subscribe);
    if (CHIP_NO_ERROR != (err = client->SendRequest(params)))
    {
        sl_log_error(LOG_TAG, "Some Problem");
        mCallbacks->OnError(err);
        return err;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR WriteRequest::Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle)
{
    CHIP_ERROR err;

    client = Platform::MakeUnique<WriteClient>(&exchangeMgr, this, chip::NullOptional);

    mReader.Next(TLV::kTLVType_List, TLV::AnonymousTag());
    for (const auto& pathParams : mPath) {

        TLV::TLVType containerType;
        mReader.EnterContainer(containerType);
        mReader.Next();

        err = client->PutPreencodedAttribute(
                chip::app::ConcreteAttributePath(pathParams.mEndpointId, pathParams.mClusterId,
                pathParams.mAttributeId), mReader);

        mReader.ExitContainer(containerType);
        mReader.Next();
    }

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "PutPreencodedAttribute failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    if (CHIP_NO_ERROR != (err = client->SendWriteRequest(sessionHandle)))
    {
        ChipLogError(NotSpecified, "Write client send Request failed: %" CHIP_ERROR_FORMAT, err.Format());
        attribute_store::attribute unid;
        if(SL_STATUS_OK == mpc_attribute_store_get_unid_from_matter_peer_nodeid(ScopedNodeId(mDest, 1), unid))
        {
            check_and_mark_failing_node(unid, err);
        }
        return err;
    }
    return CHIP_NO_ERROR;
}

sl_status_t WriteRequest::SendCommand(chip::TLV::TLVReader & data)
{
    mReader.Init(data);
    ScopedNodeId nodeId(mDest, 1);
    caseSessProvider->FindOrEstablishSession(nodeId, &mOnConnectedCallback, &mOnConnectionFailureCallback);
    return SL_STATUS_OK;
}

void WriteRequest::OnResponse(const WriteClient * apWriteClient, const ConcreteDataAttributePath & aPath,
                                StatusIB attributeStatus) {
    CHIP_ERROR err = attributeStatus.ToChipError();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "Write Request Failed with: %" CHIP_ERROR_FORMAT, err.Format());
        mCallbacks(err, ScopedNodeId(mDest, 1), AttributePathParams(aPath.mEndpointId, aPath.mClusterId, aPath.mAttributeId));
        return;
    } else {
        mCallbacks(err, ScopedNodeId(mDest, 1), AttributePathParams(aPath.mEndpointId, aPath.mClusterId, aPath.mAttributeId));
    }
}

void WriteRequest::OnError(const WriteClient * apWriteClient, CHIP_ERROR aError) {

    ChipLogProgress(NotSpecified, "Write Request Failed with: %" CHIP_ERROR_FORMAT, aError.Format());
    invokeFailureCallback(aError);
    return;
}

void WriteRequest::OnDone(WriteClient * apWriteClient) {
    sl_log_info(LOG_TAG, "Write Request Completed Successfully");
    Platform::Delete(this);
    return;
}
