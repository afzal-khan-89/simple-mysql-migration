cmake_minimum_required(VERSION 3.16)
project(MariaDBMigration)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include_directories(/usr/include/cppconn)  # MariaDB/MySQL connector headers

link_directories(/usr/lib)  # adjust if your connector lib is elsewhere

add_executable(mysql_migration mysql_migration.cpp Migration_parser.cpp)

target_link_libraries(mysql_migration PRIVATE mysqlcppconn)

