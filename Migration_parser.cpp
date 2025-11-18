#include "Migration_parser.h"


// int MigrationParser::parse_table_from_file(std::vector<Table>& tables, const std::string& filename) 
// {
//     std::ifstream file(filename);
//     if (!file.is_open()) 
//     {
//         std::cerr << "Cannot open config file: " << filename << std::endl;
//         return -1;
//     }

//     std::regex tableRegex("TABLE\\(\"([^\"]+)\"\\)\\(");
//     std::regex columnRegex("COLUMN\\(\"([^\"]+)\",\\s*\"([^\"]+)\"\\)");

//     std::string line;
//     std::string currentTable;
//     std::vector<Column> currentColumns;
//     bool inTable = false;

//     while (std::getline(file, line)) 
//     {
//         line.erase(0, line.find_first_not_of(" \t\r\n"));
//         line.erase(line.find_last_not_of(" \t\r\n") + 1);
//         if (line.empty()) continue;

//         std::smatch m;

//         if (std::regex_match(line, m, tableRegex)) 
//         {
//             if (inTable) tables.push_back({currentTable, currentColumns});
//             currentTable = m[1];
//             currentColumns.clear();
//             inTable = true;
//             continue;
//         }

//         if (inTable && std::regex_match(line, m, columnRegex)) 
//         {
//             currentColumns.push_back({m[1], m[2]});
//             continue;
//         }

//         if (inTable && (line == ")" || line == ");")) 
//         {
//             tables.push_back({currentTable, currentColumns});
//             inTable = false;
//             continue;
//         }
//     }

//     if (inTable) tables.push_back({currentTable, currentColumns});

//     for (auto &t : tables) 
//     {
//         std::cout << "Table: " << t.name << "\n";
//         for (auto &c : t.columns) std::cout << "  " << c.name << " : " << c.type << "\n";
//     }

//     return 0;
// }

#include "Migration_parser.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

int MigrationParser::parse_table_from_file(std::vector<Table>& tables, const std::string& filename) 
{
    std::ifstream file(filename);
    if (!file.is_open()) 
    {
        std::cerr << "Cannot open config file: " << filename << std::endl;
        return -1;
    }

    // Retain the existing regex definitions
    std::regex tableRegex("TABLE\\(\"([^\"]+)\"\\)\\(");
    std::regex columnRegex("COLUMN\\(\"([^\"]+)\",\\s*\"([^\"]+)\"\\)");

    std::string line;
    std::string currentTable;
    std::vector<Column> currentColumns;
    bool inTable = false;

    while (std::getline(file, line)) 
    {
        // 1. Trim leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;

        std::smatch m;

        // 2. Use std::regex_search for flexibility
        if (std::regex_search(line, m, tableRegex)) // <-- CHANGE: Use search
        {
            // Finish the previous table if one was being processed
            if (inTable) tables.push_back({currentTable, currentColumns});
            currentTable = m[1].str(); // Capture group 1 (table name)
            currentColumns.clear();
            inTable = true;
            continue;
        }

        // 3. Use std::regex_search for column definitions
        if (inTable && std::regex_search(line, m, columnRegex)) // <-- CHANGE: Use search
        {
            currentColumns.push_back({m[1].str(), m[2].str()}); // Capture groups 1 (name) and 2 (type)
            continue;
        }

        // 4. Check for end-of-table delimiter ')' or ');'
        // This check is fine as long as you rely on the trimmed line.
        // It's robust enough for this simple format.
        if (inTable && (line.find(')') != std::string::npos)) 
        {
            // If the line contains a closing parenthesis, it signifies the end of the table
            tables.push_back({currentTable, currentColumns});
            inTable = false;
            continue;
        }
    }

    // Handle the last table if the file ended abruptly without a closing ')'
    if (inTable) tables.push_back({currentTable, currentColumns});

    // Output for verification (optional but good for debugging)
    for (auto &t : tables) 
    {
        std::cout << "Table: " << t.name << "\n";
        for (auto &c : t.columns) std::cout << "  " << c.name << " : " << c.type << "\n";
    }

    return 0;
}