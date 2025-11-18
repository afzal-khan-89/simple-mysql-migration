#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>

#pragma once

struct Column {
    std::string name;
    std::string type;
};

struct Table {
    std::string name;
    std::vector<Column> columns;
};

class MigrationParser {
public:
    static int parse_table_from_file(std::vector<Table>& tables, const std::string& filename) ;  
};
