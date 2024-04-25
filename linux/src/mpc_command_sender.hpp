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

/**
 * @defgroup mpc_command_sender MPC Command Sender
 * @ingroup mpc_components
 * @brief mpc_command_sender implements reusable classes to send various matter commands
 *
 * @{
 */

#ifndef MPC_COMMAND_SENDER_HPP
#define MPC_COMMAND_SENDER_HPP

#include "app/BufferedReadCallback.h"
#include "app/ConcreteAttributePath.h"
#include "app/InteractionModelEngine.h"
#include "app/server/Server.h"

#include "sl_status.h"

using namespace chip;
using namespace chip::app;

template<typename T>
void on_device_connected_common(void * context, Messaging::ExchangeManager & exchangeMgr,
                                const SessionHandle & sessionHandle)
{
    CHIP_ERROR err;
    T * handle = reinterpret_cast<T *>(context);
    if (!handle)
        return;
    if (CHIP_NO_ERROR != (err = handle->Send(exchangeMgr, sessionHandle)))
    {
        ChipLogError(NotSpecified, "Send operation failed: %" CHIP_ERROR_FORMAT, err.Format());

        handle->invokeFailureCallback(err);
    }
}

template<typename T>
void on_device_connection_failure_common(void * context, const ScopedNodeId & peerId, CHIP_ERROR error)
{
    T * handle = reinterpret_cast<T *>(context);
    ChipLogError(NotSpecified, "Device connection failure: %" CHIP_ERROR_FORMAT, error.Format());

    handle->invokeFailureCallback(error);
}

class SessionManagerProvider
{
public:
    virtual ~SessionManagerProvider() = default;
    virtual void FindOrEstablishSession(ScopedNodeId nodeId, Callback::Callback<OnDeviceConnected> * OnConnectedCallback,
                                        Callback::Callback<OnDeviceConnectionFailure> * OnConnectionFailureCallback)
    {
        Server::GetInstance().GetCASESessionManager()->FindOrEstablishSession(nodeId, OnConnectedCallback,
                                                                              OnConnectionFailureCallback);
    }
};
/**
 * @brief Class to send ReadAttribute matter command from MPC
 */
class AttributeReadRequest
{
public:
    virtual ~AttributeReadRequest() = default;
    /**
     * @brief Construct a new AttributeReadRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param epID endpointID in destination to which the cluster and attribute to be read belong
     * @param clustID cluster ID to which attribute to be read belong
     * @param attrID attribute ID for the attribute is to be read
     */
    AttributeReadRequest(NodeId dest, EndpointId epID, ClusterId clustID, AttributeId attrID) :
        mDest(dest), mPath(1, AttributePathParams(epID, clustID, attrID)), mOnConnectedCallback(on_device_connected_common<AttributeReadRequest>, this),
        mOnConnectionFailureCallback(on_device_connection_failure_common<AttributeReadRequest>, this)
    {}

    /**
     * @brief Construct a new AttributeReadRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param path vector of AttributePathParams containing the path of attributes that are to be read
     */
    AttributeReadRequest(NodeId dest, std::vector<AttributePathParams> & path) :
        mDest(dest), mPath(path), mOnConnectedCallback(on_device_connected_common<AttributeReadRequest>, this),
        mOnConnectionFailureCallback(on_device_connection_failure_common<AttributeReadRequest>, this)
    {}

    /**
     * @brief Registers a callback delegate to be invoke upon completion of read attribute
     *
     * @param callbacks class object implementing the callbacks to be invoked
     */
    void SetCallbacks(ReadClient::Callback * callbacks) { mCallbacks = callbacks; };

    /**
     * @brief Establishes a case session if doesn't already exist and then sends the command
     */
    sl_status_t SendCommand();

    /**
     * @brief Sends the command by re-using already available session
     *
     * @param exchangeMgr exchange manager linked to available session
     * @param sessionHandle session handle to available session
     */
    virtual CHIP_ERROR Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle);

    void invokeFailureCallback(const CHIP_ERROR err) const
    {
        mCallbacks->OnError(err);
    }

private:
    friend class chip::app::TestReadInteraction;
    friend class SubscribeRequest;
    friend class TestSessionProvider;

    Platform::UniquePtr<ReadClient> client;
    Platform::SharedPtr<BufferedReadCallback> mBufferedReadAdapter;
    ReadClient::Callback * mCallbacks;
    NodeId mDest;
    std::vector<AttributePathParams> mPath;
    Callback::Callback<OnDeviceConnected> mOnConnectedCallback;
    Callback::Callback<OnDeviceConnectionFailure> mOnConnectionFailureCallback;
    static SessionManagerProvider * caseSessProvider;

};

/**
 * @brief Structure holds parameter for Subscribe matter command
 */
typedef struct
{

    /// @brief minimum expected interval between consecutive subcription reports
    /// (should be a floored 16-bit integer value )
    uint16_t minInterval;
    /// @brief maximum expected interval between consecutive subcription reports
    /// (should be a ceiled 16-bit integer value )
    uint16_t maxInterval;
    /// @brief indicates if the existing subscription from MPC is to be retained or overwritten
    bool keepSubscription;
} SubscribeRequestParams;

/**
 * @brief Class to send Subscribe matter command from MPC
 */
class SubscribeRequest : public AttributeReadRequest
{
public:
    /**
     * @brief Construct a new SubscribeRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param epID endpointID in destination to which the cluster and attribute to be read belong
     * @param clustID cluster ID to which attribute to be read belong
     * @param attrID attribute ID for the attribute is to be read
     * @param params subscription parameters @ref SubscribeRequestParams
     */
    SubscribeRequest(NodeId dest, EndpointId epID, ClusterId clustID, AttributeId attrID, SubscribeRequestParams params) :
        AttributeReadRequest(dest, epID, clustID, attrID), mMinInterval(params.minInterval), mMaxInterval(params.maxInterval),
        mKeepSubs(params.keepSubscription)
    {}

    /**
     * @brief Construct a new SubscribeRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param path vector of AttributePathParams containing the path of attributes that are to be read
     * @param params subscription parameters @ref SubscribeRequestParams
     */
    SubscribeRequest(NodeId dest, std::vector<AttributePathParams> & path, SubscribeRequestParams params) :
        AttributeReadRequest(dest, path), mMinInterval(params.minInterval), mMaxInterval(params.maxInterval),
        mKeepSubs(params.keepSubscription)
    {}

    /**
     * @brief Sends the command by re-using already available session
     *
     * @param exchangeMgr exchange manager linked to available session
     * @param sessionHandle session handle to available session
     */
    CHIP_ERROR Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle) override;

private:
    uint16_t mMinInterval;
    uint16_t mMaxInterval;
    bool mKeepSubs;
};


/**
 * @brief Class to send ReadAttribute matter command from MPC
 */
class WriteRequest : public chip::app::WriteClient::Callback
{
public:
    /**
     * @brief Callback which is called when the Write operation is completed.
     *
     * first argument is the transmission status
     * second argument is the Nodeid to which the transmission was attempted
     */
    using WriteCallback = std::function<void(CHIP_ERROR, ScopedNodeId, AttributePathParams)>;

    virtual ~WriteRequest() = default;
    /**
     * @brief Construct a new WriteRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param epID endpointID in destination to which the cluster and attribute to be read belong
     * @param clustID cluster ID to which attribute to be read belong
     * @param attrID attribute ID for the attribute is to be read
     */
    WriteRequest(NodeId dest, EndpointId epID, ClusterId clustID, AttributeId attrID, WriteCallback mCB) :
        mDest(dest), mPath(1, AttributePathParams(epID, clustID, attrID)), mOnConnectedCallback(on_device_connected_common<WriteRequest>, this),
        mOnConnectionFailureCallback(on_device_connection_failure_common<WriteRequest>, this), mCallbacks(mCB)
    {}

    /**
     * @brief Construct a new WriteRequest object
     *
     * @param dest destination matter node id for which attribute needs to be read
     * @param path vector of AttributePathParams containing the path of attributes that are to be read
     */
    WriteRequest(NodeId dest, const std::vector<AttributePathParams> & path, WriteCallback mCB) :
        mDest(dest), mPath(path), mOnConnectedCallback(on_device_connected_common<WriteRequest>, this),
        mOnConnectionFailureCallback(on_device_connection_failure_common<WriteRequest>, this), mCallbacks(mCB)
    {}

    /**
     * @brief Establishes a case session if doesn't already exist and then sends the command
     */
    sl_status_t SendCommand(chip::TLV::TLVReader & data);

    /*
     * @brief Sends the command by re-using already available session
     *
     * @param exchangeMgr exchange manager linked to available session
     * @param sessionHandle session handle to available session
     */
    CHIP_ERROR Send(Messaging::ExchangeManager & exchangeMgr, const SessionHandle & sessionHandle);

    void invokeFailureCallback(const CHIP_ERROR err) const
    {
            for (const auto& pathParams : mPath) {
                mCallbacks(err, ScopedNodeId(mDest, 1), pathParams);
            }
    }

    /*
     * Callbacks to WriteClient, these callbacks trigger mCallbacks to indicate status of WriteRequest
     */
    void OnResponse(const WriteClient * apWriteClient, const ConcreteDataAttributePath & aPath,
                                StatusIB attributeStatus) override;
    void OnError(const WriteClient * apWriteClient, CHIP_ERROR aError) override;
    void OnDone(WriteClient * apWriteClient) override;

private:
    friend class chip::app::TestReadInteraction;
    friend class TestSessionProvider;
    friend class chip::app::TestWriteInteraction;

    Platform::UniquePtr<WriteClient> client;
    // WriteClient::Callback * mCallbacks;
    NodeId mDest;
    std::vector<AttributePathParams> mPath;
    chip::Callback::Callback<OnDeviceConnected> mOnConnectedCallback;
    chip::Callback::Callback<OnDeviceConnectionFailure> mOnConnectionFailureCallback;
    static SessionManagerProvider * caseSessProvider;
    chip::TLV::TLVReader mReader;
    WriteCallback mCallbacks;

};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif // MPC_COMMAND_SENDER_HPP
/** @} end mpc_command_sender */
