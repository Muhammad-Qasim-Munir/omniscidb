/*
 * Copyright 2020 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file CatalogMigrationTest.cpp
 * @brief Test suite for catalog migrations
 */

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#include "Catalog/Catalog.h"
#include "MapDHandlerTestHelpers.h"
#include "SqliteConnector/SqliteConnector.h"
#include "TestHelpers.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

extern bool g_enable_fsi;

class FsiSchemaTest : public testing::Test {
 protected:
  FsiSchemaTest()
      : sqlite_connector_(
            "omnisci",
            boost::filesystem::absolute("mapd_catalogs", BASE_PATH).string()) {}

  static void SetUpTestSuite() {
    Catalog_Namespace::SysCatalog::instance().init(
        BASE_PATH, nullptr, {}, nullptr, false, false, {});
  }

  void SetUp() override {
    g_enable_fsi = false;
    dropFsiTables();
  }

  void TearDown() override { dropFsiTables(); }

  std::vector<std::string> getTables() {
    sqlite_connector_.query("SELECT name FROM sqlite_master WHERE type='table';");
    std::vector<std::string> tables;
    for (size_t i = 0; i < sqlite_connector_.getNumRows(); i++) {
      tables.emplace_back(sqlite_connector_.getData<std::string>(i, 0));
    }
    return tables;
  }

  std::unique_ptr<Catalog_Namespace::Catalog> initCatalog() {
    Catalog_Namespace::DBMetadata db_metadata;
    db_metadata.dbName = "omnisci";
    std::vector<LeafHostInfo> leaves{};
    return std::make_unique<Catalog_Namespace::Catalog>(
        BASE_PATH, db_metadata, nullptr, leaves, nullptr, false);
  }

  void assertExpectedDefaultServer(Catalog_Namespace::Catalog* catalog,
                                   const std::string& server_name,
                                   const std::string& data_wrapper,
                                   const int32_t user_id) {
    auto foreign_server = catalog->getForeignServerSkipCache(server_name);

    ASSERT_GT(foreign_server->id, 0);
    ASSERT_EQ(server_name, foreign_server->name);
    ASSERT_EQ(data_wrapper, foreign_server->data_wrapper.name);
    ASSERT_EQ(user_id, foreign_server->user_id);

    ASSERT_TRUE(
        foreign_server->options.find(foreign_storage::ForeignServer::STORAGE_TYPE_KEY) !=
        foreign_server->options.end());
    ASSERT_EQ(
        foreign_storage::ForeignServer::LOCAL_FILE_STORAGE_TYPE,
        foreign_server->options.find(foreign_storage::ForeignServer::STORAGE_TYPE_KEY)
            ->second);

    ASSERT_TRUE(
        foreign_server->options.find(foreign_storage::ForeignServer::BASE_PATH_KEY) !=
        foreign_server->options.end());
    ASSERT_EQ("/",
              foreign_server->options.find(foreign_storage::ForeignServer::BASE_PATH_KEY)
                  ->second);
  }

 private:
  SqliteConnector sqlite_connector_;

  void dropFsiTables() {
    sqlite_connector_.query("DROP TABLE IF EXISTS omnisci_foreign_servers;");
    sqlite_connector_.query("DROP TABLE IF EXISTS omnisci_foreign_tables;");
  }
};

TEST_F(FsiSchemaTest, FsiTablesNotCreatedWhenFsiIsDisabled) {
  auto tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") ==
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") ==
              tables.end());

  auto catalog = initCatalog();

  tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") ==
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") ==
              tables.end());
}

TEST_F(FsiSchemaTest, FsiTablesAreCreatedWhenFsiIsEnabled) {
  auto tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") ==
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") ==
              tables.end());

  g_enable_fsi = true;
  auto catalog = initCatalog();

  tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") !=
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") !=
              tables.end());
}

TEST_F(FsiSchemaTest, FsiTablesAreDroppedWhenFsiIsDisabled) {
  auto tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") ==
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") ==
              tables.end());

  g_enable_fsi = true;
  initCatalog();

  tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") !=
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") !=
              tables.end());

  g_enable_fsi = false;
  initCatalog();
  tables = getTables();
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_servers") ==
              tables.end());
  ASSERT_TRUE(std::find(tables.begin(), tables.end(), "omnisci_foreign_tables") ==
              tables.end());
}

class ForeignTablesTest : public MapDHandlerTestFixture {
 protected:
  void SetUp() override {
    g_enable_fsi = true;
    MapDHandlerTestFixture::SetUp();
    dropTestTables();
  }

  void TearDown() override {
    dropTestTables();
    MapDHandlerTestFixture::TearDown();
  }

 private:
  void dropTestTables() {
    g_enable_fsi = true;
    sql("DROP FOREIGN TABLE IF EXISTS test_foreign_table;");
    sql("DROP TABLE IF EXISTS test_table;");
    sql("DROP VIEW IF EXISTS test_view;");
  }
};

TEST_F(ForeignTablesTest, ForeignTablesAreDroppedWhenFsiIsDisabled) {
  sql("CREATE FOREIGN TABLE test_foreign_table (c1 int) SERVER omnisci_local_csv "
      "WITH (file_path = 'test_file.csv');");
  sql("CREATE TABLE test_table (c1 int);");
  sql("CREATE VIEW test_view AS SELECT * FROM test_table;");

  ASSERT_NE(nullptr, getCatalog().getMetadataForTable("test_foreign_table", false));
  ASSERT_NE(nullptr, getCatalog().getMetadataForTable("test_table", false));
  ASSERT_NE(nullptr, getCatalog().getMetadataForTable("test_view", false));

  g_enable_fsi = false;
  resetCatalog();
  loginAdmin();

  ASSERT_EQ(nullptr, getCatalog().getMetadataForTable("test_foreign_table", false));
  ASSERT_NE(nullptr, getCatalog().getMetadataForTable("test_table", false));
  ASSERT_NE(nullptr, getCatalog().getMetadataForTable("test_view", false));
}

class DefaultForeignServersTest : public FsiSchemaTest {};

TEST_F(DefaultForeignServersTest, DefaultServersAreCreatedWhenFsiIsEnabled) {
  g_enable_fsi = true;
  auto catalog = initCatalog();
  g_enable_fsi = false;

  assertExpectedDefaultServer(catalog.get(),
                              "omnisci_local_csv",
                              foreign_storage::DataWrapper::CSV_WRAPPER_NAME,
                              OMNISCI_ROOT_USER_ID);
  assertExpectedDefaultServer(catalog.get(),
                              "omnisci_local_parquet",
                              foreign_storage::DataWrapper::PARQUET_WRAPPER_NAME,
                              OMNISCI_ROOT_USER_ID);
}

int main(int argc, char** argv) {
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);

  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }

  return err;
}
