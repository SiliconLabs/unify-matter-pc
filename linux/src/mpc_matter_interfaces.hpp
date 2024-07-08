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
 * @defgroup mpc_matter_interfaces MPC matter interfaces
 * @ingroup mpc_components
 * @brief mpc_matter_interfaces implements classes for provided matter interfaces
 * @{
 */

#ifndef MPC_MATTER_INTERFACES_HPP
#define MPC_MATTER_INTERFACES_HPP

#include "app/server/Server.h"

using namespace chip;
using namespace chip::app;

class SessionManagerProvider
{
public:
    virtual ~SessionManagerProvider() = default;
    virtual void FindOrEstablishSession(ScopedNodeId nodeId, Callback::Callback<OnDeviceConnected> * OnConnectedCallback,
                                        Callback::Callback<OnDeviceConnectionFailure> * OnConnectionFailureCallback);
};

class ChipServer
{
public:
    virtual ~ChipServer() = default;
    virtual const FabricInfo* FindFabricWithIndex(FabricIndex fabricIndex);
    virtual const FabricTable & GetFabricTable();
    virtual const FabricInfo* FindFabricWithCompressedId(CompressedFabricId compressedFabricId);

    static ChipServer* GetChipServer();

private:
    static ChipServer * chipServerProvider;
    friend class TestChipServer;
};

extern SessionManagerProvider defaultSessionProvider;

#endif // MPC_MATTER_INTERFACES_HPP
/** @} end mpc_matter_interfaces */
