#include "failing_nodes_datastore.h"
#include "sl_log.h"

#define LOG_TAG "failing_node_datastore"

FailingNodeDatastore::FailingNodeDatastore() : fndb(nullptr) {}

FailingNodeDatastore::~FailingNodeDatastore()
{
    close_database();
}

sl_status_t FailingNodeDatastore::initialize()
{
    int rc = sqlite3_open_v2("failing_nodes.db", &fndb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK)
    {
        sl_log_error(LOG_TAG, "Failed to open database: %s\n", sqlite3_errmsg(fndb));
        return SL_STATUS_FAIL;
    }

    if (create_table() != SQLITE_OK)
    {
        sl_log_error(LOG_TAG, "Failed to create table");
        close_database();
        return SL_STATUS_FAIL;
    }
    return SL_STATUS_OK;
}

sl_status_t FailingNodeDatastore::insert_failing_node(mpc_failing_node node)
{
    const char *sql = "INSERT INTO mpc_failing_nodes (nodeID, failed_time) VALUES (?, ?)";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(fndb, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sl_log_error(LOG_TAG, "Failed to prepare statement: %s\n", sqlite3_errmsg(fndb));
        return SL_STATUS_FAIL;
    }

    sqlite3_bind_int(stmt, 1, node.nodeID);
    sqlite3_bind_int(stmt, 2, node.failed_time);

    int result = sqlite3_step(stmt);
    if (result != SQLITE_DONE)
    {
        sl_log_error(LOG_TAG, "Failed to execute statement: %s\n", sqlite3_errmsg(fndb));
    }

    sl_log_debug(LOG_TAG, "inserting the data for node %d", node);
    sqlite3_finalize(stmt);
    return SL_STATUS_OK;
}

sl_status_t FailingNodeDatastore::read_failing_nodes(std::vector<mpc_failing_node>& failing_nodes)
{
    const char *sql = "SELECT nodeID, failed_time FROM mpc_failing_nodes;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(fndb, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sl_log_error(LOG_TAG, "Failed to prepare statement: ", sqlite3_errmsg(fndb));
        return SL_STATUS_FAIL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int nodeID = sqlite3_column_int(stmt, 0);
        uint32_t failed_time = sqlite3_column_int(stmt, 1);

        failing_nodes.push_back({nodeID, failed_time});
    }

    sqlite3_finalize(stmt);
    return SL_STATUS_OK;
}

sl_status_t FailingNodeDatastore::delete_data(int nodeID)
{
    const char *sql = "DELETE FROM mpc_failing_nodes WHERE nodeID = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(fndb, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sl_log_error(LOG_TAG, "Failed to prepare statement: ", sqlite3_errmsg(fndb));
        return SL_STATUS_FAIL;
    }

    sqlite3_bind_int(stmt, 1, nodeID);

    int result = sqlite3_step(stmt);
    if (result != SQLITE_DONE)
    {
        sl_log_error(LOG_TAG, "Failed to execute statement: ", sqlite3_errmsg(fndb));
    }

    sl_log_info(LOG_TAG, "deleting the data for node %d", nodeID);
    sqlite3_finalize(stmt);
    return SL_STATUS_OK;
}

int FailingNodeDatastore::create_table()
{
    const char *sql = "CREATE TABLE IF NOT EXISTS mpc_failing_nodes ("
                        "nodeID INTEGER PRIMARY KEY,"
                        "failed_time INTEGER);";
    return datastore_exe_sql_cmd(sql);
}

void FailingNodeDatastore::close_database()
{
    if (fndb) {
        sqlite3_close(fndb);
        fndb = nullptr;
    }
}

int FailingNodeDatastore::datastore_exe_sql_cmd(const char *sql)
{
    char *err_msg = nullptr;
    int rc = sqlite3_exec(fndb, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg != nullptr) {
            sl_log_error(LOG_TAG, "SQL Error: %s", err_msg);
            sqlite3_free(err_msg);
        } else {
            sl_log_error(LOG_TAG,  "SQL Error: Unknown");
        }
    }
    return rc;
}
