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
 * @defgroup mpc_datastore MPC failing nodes Datastore
 * @ingroup mpc_components
 * @brief MPC failing nodes datastore specific functions.
 *
 * MPC failing node datastore is to manipulate the database
 * for the failing node recovery
 *
 * @{
 */

#ifndef FAILING_NODE_DATASTORE_H
#define FAILING_NODE_DATASTORE_H

#include <iostream>
#include <vector>
#include <sqlite3.h>
#include "sl_status.h"
#include "mpc_failing_node.h"

/**
 * @brief Class to manage the failing node datastore.
 *
 * The FailingNodeDatastore class provides methods to initialize, insert,
 * read, and delete failing node data using an SQLite database.
 */
class FailingNodeDatastore
{
public:
    /**
     * @brief Constructor for the FailingNodeDatastore class.
     *
     * Creates fndb nullptr
     */
    FailingNodeDatastore();

    /**
     * @brief Destructor for the FailingNodeDatastore class.
     *
     * Closes the SQLite database and sets the database pointer to nullptr.
     */
    ~FailingNodeDatastore();

    /**
     * @brief Initializes the datastore.
     *
     * Opens the SQLite database and creates the failing node table if it does not exist.
     *
     * @return SL_STATUS_OK if successful, SL_STATUS_FAIL otherwise.
     */
    sl_status_t initialize();

    /**
     * @brief Inserts a failing node into the datastore.
     *
     * @param[in] node The failing node to insert of type mpc_failing_node.
     * @return SL_STATUS_OK if successful, SL_STATUS_FAIL otherwise.
     */
    sl_status_t insert_failing_node(mpc_failing_node node);

    /**
     * @brief Reads all failing nodes from the datastore.
     *
     * @param[out] failing_nodes A vector type of mpc_failing_node to store the read failing nodes.
     * @return SL_STATUS_OK if successful, SL_STATUS_FAIL otherwise.
     */
    sl_status_t read_failing_nodes(std::vector<mpc_failing_node>& failing_nodes);

    /**
     * @brief Deletes a failing node from the datastore.
     *
     * @param[in] nodeID The ID of the node to delete.
     * @return SL_STATUS_OK if successful, SL_STATUS_FAIL otherwise.
     */
    sl_status_t delete_data(int nodeID);

private:
    sqlite3 *fndb; // Pointer to the SQLite database

    // Creates the failing node table in the database.
    // return 0 if successful, non-zero otherwise.
    int create_table();

    // Closes the SQLite database.
    void close_database();

    // Executes an SQL command on the database.
    // param[in] sql The SQL command to execute.
    // return 0 if successful, non-zero otherwise.
    int datastore_exe_sql_cmd(const char *sql);
};

/**
 * @brief Global instance of the FailingNodeDatastore.
 */
inline FailingNodeDatastore failingNodeDataStore;

/** @} end of mpc_datastore */
#endif // FAILING_NODE_DATASTORE_H
