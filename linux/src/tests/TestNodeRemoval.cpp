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

#include <app/MessageDef/ReportDataMessage.h>
#include <app/ReadClient.h>
#include <lib/support/UnitTestContext.h>
#include <lib/support/UnitTestRegistration.h>
#include <system/TLVPacketBufferBackingStore.h>

#include "attribute.hpp"
#include "sl_log.h"
#include "attribute_store_fixt.h"
#include "mpc_attribute_resolver.h"
#include "mpc_attribute_store_defined_attribute_types.h"
#include "mpc_node_monitor.h"
#include "mpc_attribute_store.h"
#include "mpc_matter_interfaces_mock.h"
#include "mpc_matter_interfaces.hpp"
#include "mpc_nw_monitor.hpp" 
#include "zap-types.h"
#include "mpc_attribute_parser_fwk.h"
#include "mpc_attribute_resolver_callbacks.h"
#include "datastore_fixt.h"

#include <credentials/OperationalCertificateStore.h>
#include <credentials/PersistentStorageOpCertStore.h> 
#include <credentials/tests/CHIPCert_unit_test_vectors.h>
#include <credentials/tests/CHIPCert_test_vectors.h>

#include <filesystem>
#include <string>

using namespace std;
using namespace chip;
using namespace chip::app;
using namespace attribute_store;
using namespace chip::Credentials;
using namespace unify::mpc::Test;
using namespace chip::DeviceLayer::PersistedStorage;

class TestContext : public UnifyMPCContext
{
public:
    nlTestSuite * mTestSuite;
    uint32_t mNumTimersHandled;
    attribute mMpcNode;
    attribute mEndNode;
    attribute epNode;

    static int initialize(void * inContext)
    {
       TestContext * ctxt = static_cast<TestContext *>(inContext);
        
        // Makes sure our required state folders exists
        std::filesystem::create_directories(LOCALSTATEDIR);

        if (CHIP_NO_ERROR != ctxt->Initialize())
            return FAILURE;

        attribute_store_init();
        mpc_attribute_store_init();

        ctxt->mMpcNode = attribute::root().add_node(ATTRIBUTE_NODE_ID);
        attribute_store_set_reported_string(ctxt->mMpcNode, "mt-01");
        auto state = ctxt->mMpcNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
        state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);
        ctxt->mMpcNode.emplace_node<std::string>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST,"15063804729682968350:1");

        return SUCCESS;
    }

    static int finalize(void * inContext)
    {
        attribute_store_teardown();
        return TestContext::nlTestTearDownTestSuite(inContext);
    }
};

// Create a fabric table
static void TestFabricTableInitialize(FabricTable& fabricTable,FabricIndex& fabricIndex){
    static FabricTable::InitParams initParams;
    static KvsPersistentStorageDelegate sKvsPersistenStorageDelegate;
    static PersistentStorageOpCertStore sPersistentStorageOpCertStore;

    static KeyValueStoreManager & kvsManager = KeyValueStoreMgr();
    sKvsPersistenStorageDelegate.Init(&kvsManager);

    initParams.storage = &sKvsPersistenStorageDelegate;
    sPersistentStorageOpCertStore.Init(&sKvsPersistenStorageDelegate);
    initParams.opCertStore = &sPersistentStorageOpCertStore;
    fabricTable.Init(initParams);

    static Crypto::P256Keypair injectedOpKey;
    static Crypto::P256SerializedKeypair injectedOpKeysSerialized;
    static P256SerializedKeypair opKeysSerialized;

    memcpy(opKeysSerialized.Bytes(), chip::TestCerts::sTestCert_Node01_02_PublicKey.data(), chip::TestCerts::sTestCert_Node01_02_PublicKey.size());
    memcpy(opKeysSerialized.Bytes() + chip::TestCerts::sTestCert_Node01_02_PublicKey.size(), chip::TestCerts::sTestCert_Node01_02_PrivateKey.data(),
               chip::TestCerts::sTestCert_Node01_02_PrivateKey.size());
    opKeysSerialized.SetLength(chip::TestCerts::sTestCert_Node01_02_PublicKey.size() + chip::TestCerts::sTestCert_Node01_02_PrivateKey.size());
    static chip::ByteSpan opKeySpan(opKeysSerialized.ConstBytes(), opKeysSerialized.Length());
    
    static chip::ByteSpan rcacSpan(chip::TestCerts::sTestCert_Root01_Chip);
    static chip::ByteSpan icacSpan(chip::TestCerts::sTestCert_ICA01_Chip);
    static chip::ByteSpan nocSpan(chip::TestCerts::sTestCert_Node01_02_Chip);
    fabricTable.AddNewFabricForTestIgnoringCollisions(rcacSpan, icacSpan, nocSpan, opKeySpan,&fabricIndex);
}

// Test case for scenario 1, Removing end node when haszerottl value true
static void TestNodeRemovalMPCEndNodeDelete(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;

    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);
    auto pFabricInfo = ChipServer::GetChipServer()->FindFabricWithIndex(fabricIndex);
    ctxt->mEndNode =  attribute::root().add_node(ATTRIBUTE_NODE_ID);
    attribute_store_set_reported_string(ctxt->mEndNode, "mt-02");
    auto endNode_state = ctxt->mEndNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    endNode_state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);

    auto mpcfabricid = pFabricInfo->GetCompressedFabricId();
    auto networkItem = std::to_string(mpcfabricid);
    networkItem.append(std::string(":"));
    networkItem.append(std::to_string(2));
    ctxt->mEndNode.emplace_node<std::string>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST,networkItem);

    // setup for ep0
    ctxt->epNode = ctxt->mEndNode.add_node(ATTRIBUTE_ENDPOINT_ID).set_reported<EndpointId>(0);
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "29,31,40,42");
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_PARTSLIST_ID, "1");

    // setup mode ep1
    auto appEpNode = ctxt->mEndNode.emplace_node(ATTRIBUTE_ENDPOINT_ID, 1);

    // setup ep1 with onoff and its manditory attributes defined
    appEpNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "6");
    appEpNode.emplace_node<string>(ONOFF_ATTRIBUTE_LIST, "0");
    appEpNode.emplace_node<bool>(DOTDOT_ATTRIBUTE_ID_ON_OFF_ON_OFF, false);
    
    OperationalDiscover discoverInstance;
    PeerId peerId(pFabricInfo->GetCompressedFabricId(), 2);

    chip::Dnssd::OperationalNodeBrowseData operationalNodeBrowseData;
    operationalNodeBrowseData.peerId = peerId;
    operationalNodeBrowseData.hasZeroTTL = true;

    chip::Dnssd::DiscoveredNodeData discoveredNodeData;
    discoveredNodeData.Set<chip::Dnssd::OperationalNodeBrowseData>(operationalNodeBrowseData);

    discoverInstance.OnNodeDiscovered(discoveredNodeData);
    NL_TEST_ASSERT(inSuite, !ctxt->mEndNode.is_valid());
    fabricTable.Delete(fabricIndex);
}

static void TestFailedNodeRemoval(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;

    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);
    auto pFabricInfo = ChipServer::GetChipServer()->FindFabricWithIndex(fabricIndex);
    ctxt->mEndNode =  attribute::root().add_node(ATTRIBUTE_NODE_ID);
    attribute_store_set_reported_string(ctxt->mEndNode, "mt-02");
    auto endNode_state = ctxt->mEndNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    endNode_state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_OFFLINE);

    auto mpcfabricid = pFabricInfo->GetCompressedFabricId();
    auto networkItem = std::to_string(mpcfabricid);
    networkItem.append(std::string(":"));
    networkItem.append(std::to_string(2));
    ctxt->mEndNode.emplace_node<std::string>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST,networkItem);

    // setup for ep0
    ctxt->epNode = ctxt->mEndNode.add_node(ATTRIBUTE_ENDPOINT_ID).set_reported<EndpointId>(0);
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "29,31,40,42");
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_PARTSLIST_ID, "1");

    // setup mode ep1
    auto appEpNode = ctxt->mEndNode.emplace_node(ATTRIBUTE_ENDPOINT_ID, 1);

    // setup ep1 with onoff and its manditory attributes defined
    appEpNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "6");
    appEpNode.emplace_node<string>(ONOFF_ATTRIBUTE_LIST, "0");
    appEpNode.emplace_node<bool>(DOTDOT_ATTRIBUTE_ID_ON_OFF_ON_OFF, false);
    
    OperationalDiscover discoverInstance;
    PeerId peerId(pFabricInfo->GetCompressedFabricId(), 2);

    chip::Dnssd::OperationalNodeBrowseData operationalNodeBrowseData;
    operationalNodeBrowseData.peerId = peerId;
    operationalNodeBrowseData.hasZeroTTL = true;

    chip::Dnssd::DiscoveredNodeData discoveredNodeData;
    discoveredNodeData.Set<chip::Dnssd::OperationalNodeBrowseData>(operationalNodeBrowseData);

    discoverInstance.OnNodeDiscovered(discoveredNodeData);
    NL_TEST_ASSERT(inSuite, ctxt->mEndNode.is_valid());
    ctxt->mEndNode.delete_node();
    fabricTable.Delete(fabricIndex);
}

// Test case for scenario 1, Removing end node with FabricWillBeRemoved
static void TestNodeFabricWillBeRemovedEndNodeRemoved(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;
    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);

    auto pFabricInfo = ChipServer::GetChipServer()->FindFabricWithIndex(fabricIndex);
    ctxt->mEndNode =  attribute::root().add_node(ATTRIBUTE_NODE_ID);
    attribute_store_set_reported_string(ctxt->mEndNode, "mt-02");
    auto endNode_state = ctxt->mEndNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    endNode_state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);

    auto mpcfabricid = pFabricInfo->GetCompressedFabricId();
    auto networkItem = std::to_string(mpcfabricid);
    networkItem.append(std::string(":"));
    networkItem.append(std::to_string(2));
    ctxt->mEndNode.emplace_node<std::string>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST,networkItem);

    // setup for ep0
    ctxt->epNode = ctxt->mEndNode.add_node(ATTRIBUTE_ENDPOINT_ID).set_reported<EndpointId>(0);
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "29,31,40,42");
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_PARTSLIST_ID, "1");

    // setup mode ep1
    auto appEpNode = ctxt->mEndNode.emplace_node(ATTRIBUTE_ENDPOINT_ID, 1);

    // setup ep1 with onoff and its manditory attributes defined
    appEpNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "6");
    appEpNode.emplace_node<string>(ONOFF_ATTRIBUTE_LIST, "0");
    appEpNode.emplace_node<bool>(DOTDOT_ATTRIBUTE_ID_ON_OFF_ON_OFF, false);
    
    auto networkStatusList = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
    auto CompressedFabricId = pFabricInfo->GetCompressedFabricId();
    auto MpcnetworkList = std::to_string(CompressedFabricId);
    MpcnetworkList.append(std::string(":"));
    MpcnetworkList.append(std::to_string(pFabricInfo->GetNodeId()));
    networkStatusList.set_reported(MpcnetworkList);

    delegate.FabricWillBeRemoved(fabricTable,fabricIndex);

    NL_TEST_ASSERT(inSuite, !ctxt->mEndNode.is_valid());
    NL_TEST_ASSERT(inSuite, attribute::root().child_count() == 1);
    auto networkStatusNode = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    NL_TEST_ASSERT(inSuite, (networkStatusNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL));
    networkStatusList = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
    NL_TEST_ASSERT(inSuite, !networkStatusList.reported_exists());

    fabricTable.Delete(fabricIndex);
}

// Test case for find_mpc_and_update_networklist api by passing undefined fabric index
static void TestNodeFindMpcAndUpdateNetworkList(nlTestSuite * inSuite, void * aContext){
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;
    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);

    auto networkStatusList = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
    attribute_store_undefine_reported(networkStatusList);
    find_mpc_and_update_networklist(kUndefinedFabricIndex);

    NL_TEST_ASSERT(inSuite, networkStatusList.reported_exists());

    fabricTable.Delete(fabricIndex);
}

// Test case for adding a node with pending interview and deleting that node
static void TestNodeFindPendingInterviewNodeDelete(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;

    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);

    auto pFabricInfo = ChipServer::GetChipServer()->FindFabricWithIndex(fabricIndex);
    ctxt->mEndNode =  attribute::root().add_node(ATTRIBUTE_NODE_ID);
    attribute_store_set_reported_string(ctxt->mEndNode, "mt-02");
    auto endNode_state = ctxt->mEndNode.emplace_node(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    endNode_state.set_reported<NodeStateNetworkStatus>(ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_FUNCTIONAL);
    auto mpcfabricid = pFabricInfo->GetCompressedFabricId();
    auto networkItem = std::to_string(mpcfabricid);
    networkItem.append(std::string(":"));
    networkItem.append(std::to_string(2));
    ctxt->mEndNode.emplace_node<std::string>(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST,networkItem);

    // setup for ep0
    ctxt->epNode = ctxt->mEndNode.add_node(ATTRIBUTE_ENDPOINT_ID).set_reported<EndpointId>(0);
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "29,31,40,42");
    ctxt->epNode.emplace_node<string>(ATTRIBUTE_PARTSLIST_ID, "1");

    // setup mode ep1
    auto appEpNode = ctxt->mEndNode.emplace_node(ATTRIBUTE_ENDPOINT_ID, 1);

    // setup ep1 with onoff and its manditory attributes defined
    appEpNode.emplace_node<string>(ATTRIBUTE_SERVERLIST_ID, "6");
    appEpNode.emplace_node<string>(ONOFF_ATTRIBUTE_LIST, "0");
    appEpNode.emplace_node<bool>(DOTDOT_ATTRIBUTE_ID_ON_OFF_ON_OFF, false);

    size_t start_count= attribute::root().child_count();
    OperationalDiscover discoverInstance;

    PeerId peerId(pFabricInfo->GetCompressedFabricId(), 3);

    chip::Dnssd::OperationalNodeBrowseData operationalNodeBrowseData;
    operationalNodeBrowseData.peerId = peerId;
    operationalNodeBrowseData.hasZeroTTL = false;

    chip::Dnssd::DiscoveredNodeData discoveredNodeData;
    discoveredNodeData.Set<chip::Dnssd::OperationalNodeBrowseData>(operationalNodeBrowseData);

    discoverInstance.OnNodeDiscovered(discoveredNodeData);
    operationalNodeBrowseData.hasZeroTTL = true;
    discoveredNodeData.Set<chip::Dnssd::OperationalNodeBrowseData>(operationalNodeBrowseData);
    size_t newnode_count= attribute::root().child_count();
    NL_TEST_ASSERT(inSuite, newnode_count == start_count + 1);
    discoverInstance.OnNodeDiscovered(discoveredNodeData);

    size_t end_count= attribute::root().child_count();
    NL_TEST_ASSERT(inSuite, end_count == start_count);
    
    fabricTable.Delete(fabricIndex);
}

// Test for MPC node itself removed
static void TestNodeFabricWillBeRemoved(nlTestSuite * inSuite, void * aContext)
{
    TestContext * ctxt = static_cast<TestContext *>(aContext);
    MPCFabricDelegate delegate;
    FabricTable fabricTable;
    FabricIndex fabricIndex;
 
    TestFabricTableInitialize(fabricTable,fabricIndex);
    TestChipServer testChipServer(&fabricTable);
 
    delegate.FabricWillBeRemoved(fabricTable,fabricIndex);
 
    auto networkStatusNode = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_STATUS);
    NL_TEST_ASSERT(inSuite, (networkStatusNode.reported<NodeStateNetworkStatus>() == ZCL_NODE_STATE_NETWORK_STATUS_ONLINE_NON_FUNCTIONAL));
    auto networkStatusList = ctxt->mMpcNode.child_by_type(DOTDOT_ATTRIBUTE_ID_STATE_NETWORK_LIST);
    NL_TEST_ASSERT(inSuite, !networkStatusList.reported_exists());
    
    fabricTable.Delete(fabricIndex);
}
 
/**
 *   Test Suite. It lists all the test functions.
 */
static const nlTest sTests[] =
{
    NL_TEST_DEF("TestNodeRemovalMPCEndNodeDelete", TestNodeRemovalMPCEndNodeDelete),
    NL_TEST_DEF("TestFailedNodeRemoval", TestFailedNodeRemoval),
    NL_TEST_DEF("TestNodeFindMpcAndUpdateNetworkList",TestNodeFindMpcAndUpdateNetworkList),
    NL_TEST_DEF("TestNodeFabricWillBeRemovedEndNodeRemoved", TestNodeFabricWillBeRemovedEndNodeRemoved),
    NL_TEST_DEF("TestNodeFindPendingInterviewNodeDelete",TestNodeFindPendingInterviewNodeDelete),
    NL_TEST_DEF("TestNodeFabricWillBeRemoved", TestNodeFabricWillBeRemoved),

    NL_TEST_SENTINEL()
};

static nlTestSuite kTheSuite =
{
    "TestNodeRemoval",
    &sTests[0],
    TestContext::initialize,
    TestContext::finalize,
    TestContext::nlTestSetUp,
    TestContext::nlTestTearDown,
};

int TestNodeRemoval(void)
{
    return chip::ExecuteTestsWithContext<TestContext>(&kTheSuite);
}

CHIP_REGISTER_TEST_SUITE(TestNodeRemoval)
