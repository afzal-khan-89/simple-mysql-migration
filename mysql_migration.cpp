#include <iostream>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <regex>
#include <unordered_map>
#include <unordered_set>

#include "Migration_parser.h"

using namespace std;



vector<Table> desiredTables;


bool tableExists(sql::Connection* conn, const string& tbl) 
{
    unique_ptr<sql::Statement> stmt(conn->createStatement());
    string q = "SHOW TABLES LIKE '" + tbl + "'";
    unique_ptr<sql::ResultSet> rs(stmt->executeQuery(q));
    return rs->next();
}

/* true  -> column exists in DB */
bool columnExists(sql::Connection* conn, const string& tbl, const string& col) 
{
    unique_ptr<sql::PreparedStatement> ps(
        conn->prepareStatement(
            "SELECT 1 FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND COLUMN_NAME=?")
    );
    ps->setString(1, conn->getSchema());
    ps->setString(2, tbl);
    ps->setString(3, col);
    unique_ptr<sql::ResultSet> rs(ps->executeQuery());
    return rs->next();
}

string getColumnType(sql::Connection* conn, const string& tbl, const string& col) 
{
    unique_ptr<sql::PreparedStatement> ps(
        conn->prepareStatement(
            "SELECT COLUMN_TYPE FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND COLUMN_NAME=?")
    );
    ps->setString(1, conn->getSchema());
    ps->setString(2, tbl);
    ps->setString(3, col);
    unique_ptr<sql::ResultSet> rs(ps->executeQuery());
    return rs->next() ? rs->getString(1) : "";
}


string normaliseType(const string& raw) {
    string t = raw;

    // 1. Drop everything after the first space (DEFAULT, NOT NULL, …)
    size_t sp = t.find(' ');
    if (sp != string::npos) t = t.substr(0, sp);

    // 2. Strip size/precision (11), (10,2) …
    t = regex_replace(t, regex(R"(\(\d+(,\d+)?\))"), "");

    // 3. Trim whitespace
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);

    // 4. Upper-case
    transform(t.begin(), t.end(), t.begin(), ::toupper);

    // 5. Alias map (int → INT, numeric → DECIMAL, …)
    static const unordered_map<string, string> alias{
        {"INT","INT"}, {"INTEGER","INT"},
        {"TINYINT","TINYINT"}, {"SMALLINT","SMALLINT"},
        {"MEDIUMINT","MEDIUMINT"}, {"BIGINT","BIGINT"},
        {"DECIMAL","DECIMAL"}, {"NUMERIC","DECIMAL"},
        {"FLOAT","FLOAT"}, {"DOUBLE","DOUBLE"},
        {"VARCHAR","VARCHAR"}, {"CHAR","CHAR"},
        {"TEXT","TEXT"},
        {"TIMESTAMP","TIMESTAMP"}, {"DATETIME","DATETIME"}, {"DATE","DATE"}
    };
    auto it = alias.find(t);
    return it != alias.end() ? it->second : t;
}

/* --------------------------------------------------------------- */
/*  Create a table from the desired definition                     */
/* --------------------------------------------------------------- */
void createTable(sql::Connection* conn, const Table& tbl) {
    string sql = "CREATE TABLE IF NOT EXISTS " + tbl.name + " (";
    for (size_t i = 0; i < tbl.columns.size(); ++i) {
        sql += tbl.columns[i].name + " " + tbl.columns[i].type;
        if (i + 1 < tbl.columns.size()) sql += ", ";
    }
    sql += ")";
    conn->createStatement()->execute(sql);
    cout << "Created table: " << tbl.name << endl;
}

/* --------------------------------------------------------------- */
/*  Add / modify / delete columns for ONE table                    */
/* --------------------------------------------------------------- */

/* Returns the primary key column name (assuming single-column PK), or empty string if none */
string getPrimaryKeyColumn(sql::Connection* conn, const string& tbl) {
    unique_ptr<sql::PreparedStatement> ps(
        conn->prepareStatement(
            "SELECT COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE "
            "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND CONSTRAINT_NAME='PRIMARY' "
            "ORDER BY ORDINAL_POSITION LIMIT 1")
    );
    ps->setString(1, conn->getSchema());
    ps->setString(2, tbl);
    unique_ptr<sql::ResultSet> rs(ps->executeQuery());
    return rs->next() ? rs->getString(1) : "";
}

/* --------------------------------------------------------------- */
/*  Add / modify / delete columns for ONE table                    */
/* --------------------------------------------------------------- */
void syncTable(sql::Connection* conn, const Table& desired) {
    // ---- 1. Collect columns that exist in the DB -----------------
    unordered_set<string> dbColumns;
    {
        unique_ptr<sql::PreparedStatement> ps(
            conn->prepareStatement(
                "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
                "WHERE TABLE_SCHEMA=? AND TABLE_NAME=?")
        );
        ps->setString(1, conn->getSchema());
        ps->setString(2, desired.name);
        unique_ptr<sql::ResultSet> rs(ps->executeQuery());
        while (rs->next()) dbColumns.insert(rs->getString(1));
    }

    // ---- 1.5 Handle PRIMARY KEY if needed ------------------------
    // Find desired PK column (assuming at most one with "PRIMARY KEY" in type)
    string desired_pk;
    for (const auto& col : desired.columns) {
        string t = col.type;
        transform(t.begin(), t.end(), t.begin(), ::toupper);
        if (t.find("PRIMARY KEY") != string::npos) {
            desired_pk = col.name;
            break;  // Assume single-column PK
        }
    }

    string current_pk = getPrimaryKeyColumn(conn, desired.name);
    bool pk_change = (current_pk != desired_pk);
    bool desired_pk_existed = columnExists(conn, desired.name, desired_pk);

    if (pk_change) {
        if (!current_pk.empty()) {
            string sql = "ALTER TABLE " + desired.name + " DROP PRIMARY KEY";
            conn->createStatement()->execute(sql);
            cout << "Dropped existing PRIMARY KEY on '" << current_pk << "' from '" << desired.name << "'" << endl;
        }
        if (desired_pk_existed && !desired_pk.empty()) {
            string sql = "ALTER TABLE " + desired.name + " ADD PRIMARY KEY (" + desired_pk + ")";
            conn->createStatement()->execute(sql);
            cout << "Added PRIMARY KEY on '" << desired_pk << "' to '" << desired.name << "'" << endl;
        }
    }

    // ---- 2. Add / modify columns ---------------------------------
    for (const auto& col : desired.columns) {
        const string& name = col.name;
        dbColumns.erase(name);                 // will be removed later if still present

        string effective_type = col.type;
        string upper = effective_type;
        transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        bool is_pk_col = (name == desired_pk);
        bool has_auto_inc = (upper.find("AUTO_INCREMENT") != string::npos);
        bool include_pk = false;

        if (!columnExists(conn, desired.name, name)) 
        {
            // ----- ADD -----
            include_pk = is_pk_col && !desired_pk_existed;  // Include for new PK columns
            if (!include_pk) 
            {
                size_t pos = upper.find("PRIMARY KEY");
                if (pos != string::npos) 
                {
                    effective_type.erase(pos, 11);
                    upper.erase(pos, 11);
                }
            }
            string sql = "ALTER TABLE " + desired.name +
                         " ADD COLUMN " + name + " " + effective_type;
            conn->createStatement()->execute(sql);
            cout << "Added column '" << name << "' to '" << desired.name << "'" << endl;
            continue;
        }

        // ----- MODIFY (always apply the desired definition for existing columns) -----
        // For MODIFY, never include "PRIMARY KEY" to avoid errors in older MySQL versions
        size_t pk_pos = upper.find("PRIMARY KEY");
        if (pk_pos != string::npos) {
            effective_type.erase(pk_pos, 11);
            upper.erase(pk_pos, 11);
        }

        // Strip AUTO_INCREMENT if not PK column
        if (!is_pk_col) {
            size_t ai_pos = upper.find("AUTO_INCREMENT");
            if (ai_pos != string::npos) {
                effective_type.erase(ai_pos, 14);
            }
        }

        // Trim extra spaces
        effective_type = regex_replace(effective_type, regex("\\s+"), " ");
        effective_type.erase(0, effective_type.find_first_not_of(" \t"));
        effective_type.erase(effective_type.find_last_not_of(" \t") + 1);

        string sql = "ALTER TABLE " + desired.name +
                     " MODIFY COLUMN " + name + " " + effective_type;
        conn->createStatement()->execute(sql);
        cout << "Applied definition for column '" << name << "' in '" << desired.name << "'" << endl;
    }

    // ---- 3. Delete columns that are no longer desired ------------
    for (const auto& col : dbColumns) {
        // Skip primary-key columns that are auto-generated by MySQL
        // (you can customise this list if you have other protected cols)
        if (col == "id") continue;

        string sql = "ALTER TABLE " + desired.name + " DROP COLUMN " + col;
        conn->createStatement()->execute(sql);
        cout << "Deleted column '" << col << "' from '" << desired.name << "'" << endl;
    }
}
bool migrate(sql::Connection* conn, const vector<Table>& schema) 
{
    try {
        for (const auto& tbl : schema) {
            if (!tableExists(conn, tbl.name)) {
                createTable(conn, tbl);
            } else {
                cout << "Table '" << tbl.name << "' exists – syncing columns..." << endl;
                syncTable(conn, tbl);
            }
        }
        return true;
    } catch (const sql::SQLException& e) {
        cerr << "SQL error: " << e.what()
             << " (code " << e.getErrorCode()
             << ", state " << e.getSQLState() << ")" << endl;
        return false;
    }
}

/* --------------------------------------------------------------- */
int main() {
    const string host     = "tcp://127.0.0.1:3306";
    const string user     = "root";
    const string pass     = "winter2summer";
    const string dbname   = "ip_cam";

    try {
        MigrationParser::parse_table_from_file(desiredTables, "schema.conf"); 
        sql::Driver* driver = get_driver_instance();
        unique_ptr<sql::Connection> con(driver->connect(host, user, pass));
        con->setSchema(dbname);

        cout << "Connected to MySQL database '" << dbname << "'" << endl;
        cout << "Starting migration…" << endl;

        if (!migrate(con.get(), desiredTables)) {
            cerr << "Migration failed – see errors above." << endl;
            return 1;
        }

        cout << "\nAll tables are in sync!" << endl;
    } catch (const sql::SQLException& e) {
        cerr << "Connection error: " << e.what()
             << " (code " << e.getErrorCode()
             << ", state " << e.getSQLState() << ")" << endl;
        return 1;
    }
    return 0;
}