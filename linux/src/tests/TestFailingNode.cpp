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
#include "UnifyMPCContext.h"
#include "attribute.hpp"
#include "attribute_store_fixt.h"
#include "datastore_fixt.h"
#include "zap-types.h"

#include "mpc_attribute_parser_fwk.h"
#include "mpc_attribute_resolver.h"
#include "mpc_attribute_resolver_callbacks.h"
#include "mpc_attribute_store.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_failing_node.h"
#include "failing_nodes_datastore.h"
#include "mpc_node_monitor.h"
#include "mpc_matter_interfaces_mock.h"
#include "zap-types.h"

// Chip components
#include <lib/core/TLVDebug.h>
#include <lib/support/UnitTestContext.h>
#include <lib/support/UnitTestRegistration.h>
#include <nlunit-test.h>
#include <system/TLVPacketBufferBackingStore.h>

#include <filesystem>

#include <string>

using namespace unify::mpc::Test;
using namespace chip;
using namespace chip::app;
using namespace attribute_store;
using namespace std;

class TestContext : public UnifyMPCContext
{
public:
    nlTestSuite * mTestSuite;
    uint32_t mNumTimersHandled;
    attribute epNode;
    attribute mDevNode;

    static int initialize(void * inContext)
    {
        TestContext * ctxt = static_cast<TestContext *>(inContext);
        
        // Makes sure our required state folders exists
        std::filesystem::create_directories(LOCALSTATEDIR);

        if (CHIP_NO_ERROR != ctxt->Initialize())
            return FAILURE;

        attribute_store_init();
        mpc_attribute_store_init();
        ctxt->mDevNode = attribute::root().add_node(ATTRIBUTE_NODE_ID);
        attribute_store_set_reported_string(ctxt->mDevNode, "mt-01");
        ctxt->epNode = ctxt->mDevNode.add_node(ATTRIBUTE_ENDPOINT_ID).set_reported<EndpointId>(0);

        auto state = ctxt->mDevNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
        state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_INTERVIEWING);
        mpc_attribute_resolver_helper_set_resolution_listener(ctxt->mDevNode);

        // setup mock serverList and partsList for ep0
        ctxt->epNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "29,31,40,42");
        ctxt->epNode.emplace_node<string>(ATTRIBUTE_PARTSLIST_ID, "1");

        // setup mode ep1
        auto appEpNode = ctxt->mDevNode.emplace_node(ATTRIBUTE_ENDPOINT_ID, 1);

        // setup ep1 with onoff and its manditory attributes defined
        appEpNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "6");
        appEpNode.emplace_node<string>(ONOFF_ATTRIBUTE_LIST, "0");
        appEpNode.emplace_node<bool>(DOTDOT_ATTRIBUTE_ID_ON_OFF_ON_OFF, false);

        mpc_attribute_resolver_resolution_completion(ctxt->mDevNode);

        if(state.reported<NodeStateNetworkStatus>() != ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL) {
            return FAILURE;
        }

        failingNodeDataStore.initialize();
        return SUCCESS;
    }

    static int finalize(void * inContext)
    {
        attribute_store_teardown();
        return TestContext::nlTestTearDownTestSuite(inContext);
    }
};


static void TestMarkDeviceAsFailing(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    auto networkStatusNode = ctxt->mDevNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    NodeStateNetworkStatus state = networkStatusNode.reported<NodeStateNetworkStatus>();
    NodeStateNetworkStatus saved_state;
    NL_TEST_ASSERT(inSuite, (mpc_mark_device_as_failing(ctxt->mDevNode) == SL_STATUS_OK));
    NL_TEST_ASSERT(inSuite, (mpc_fetch_saved_state(ctxt->mDevNode, saved_state) == SL_STATUS_OK));
    NL_TEST_ASSERT(inSuite, (saved_state == state));
    NL_TEST_ASSERT(inSuite, (networkStatusNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE));

}

static void TestMarkDeviceOffline(nlTestSuite * inSuite, void * aContext)
{
    CHIP_ERROR error = CHIP_ERROR_TIMEOUT;
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    auto networkStatusNode = ctxt->mDevNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    NL_TEST_ASSERT(inSuite, (check_and_mark_failing_node(ctxt->mDevNode, error) == SL_STATUS_OK));
    NL_TEST_ASSERT(inSuite, (networkStatusNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE));
}

static void TestFailingNodeRecovery(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    auto networkStatusNode = ctxt->mDevNode.child_by_type(ATTRIBUTE_PREVIOUS_STATE_NETWORK_STATUS_ID);
    networkStatusNode.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);

    auto currentNode = ctxt->mDevNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    currentNode.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE);

    NL_TEST_ASSERT(inSuite, (mpc_failing_node_recovery(ctxt->mDevNode) == SL_STATUS_OK));
    NL_TEST_ASSERT(inSuite, (currentNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL));
}

static void TestNodeMonitorInit(nlTestSuite * inSuite, void * aContext)
{
    NL_TEST_ASSERT(inSuite, (SL_STATUS_OK == mpc_node_monitor_init()));
}

static void TestAutoRecovery(nlTestSuite * inSuite, void * aContext)
{
    TestContext & ctxt = *static_cast<TestContext *>(aContext);

    TestSessionProvider testSession(ctxt.GetExchangeManager(), ctxt.GetSessionBobToAlice(), false);
    auto destNodeId     = ctxt.GetAliceFabric()->GetNodeId();
    auto destFabric     = ctxt.GetAliceFabric()->GetCompressedFabricId();
    std::string nwkList = std::to_string(destFabric) + ":" + std::to_string(destNodeId);
    attribute_store_set_child_reported(ctxt.mDevNode, DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST, nwkList.c_str(), nwkList.length());

    mpc_attribute_resolver_helper_set_resolution_listener(ctxt.mDevNode);
    auto epNode = ctxt.mDevNode.emplace_node<EndpointId>(ATTRIBUTE_ENDPOINT_ID, 0);
    attribute_store_type_t reportNodeType =
        ((Clusters::OnOff::Id & 0xFFFF) << 16) | (Clusters::OnOff::Attributes::OnOff::Id && 0xFFFF);

    epNode.emplace_node<bool>(reportNodeType, false);
    CHIP_ERROR error = CHIP_ERROR_TIMEOUT;
    auto networkStatusNode = ctxt.mDevNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    NL_TEST_ASSERT(inSuite, (check_and_mark_failing_node(ctxt.mDevNode, error) == SL_STATUS_OK));
    NL_TEST_ASSERT(inSuite, (networkStatusNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE));

    NL_TEST_ASSERT(inSuite, (mpc_failing_node_auto_recovery() == SL_STATUS_OK));

    recover_next_failing_node();
    ctxt.GetLoopback().mNumMessagesToDrop = 1;
    ctxt.DrainAndServiceIO();
}

/**
 *   Test Suite. It lists all the test functions.
 */
// clang-format off
static const nlTest sTests[] =
{
    NL_TEST_DEF("TestMarkDeviceAsFailing", TestMarkDeviceAsFailing),
    NL_TEST_DEF("TestMarkDeviceOffline", TestMarkDeviceOffline),
    NL_TEST_DEF("TestFailingNodeRecovery", TestFailingNodeRecovery),
    NL_TEST_DEF("TestNodeMonitorInit", TestNodeMonitorInit),
    NL_TEST_DEF("TestAutoRecovery", TestAutoRecovery),

    NL_TEST_SENTINEL()
};

// clang-format off
static nlTestSuite kTheSuite =
{
    "TestFailingNodeInterface",
    &sTests[0],
    TestContext::initialize,
    TestContext::finalize,
    TestContext::nlTestSetUp,
    TestContext::nlTestTearDown,

};

int TestFailingNodeInterface(void)
{
    return chip::ExecuteTestsWithContext<TestContext>(&kTheSuite);
}

CHIP_REGISTER_TEST_SUITE(TestFailingNodeInterface)