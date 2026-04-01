/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourcePathTests
#include <boost/test/unit_test.hpp>

#define private public
#include "utils/ResourcePath.hpp"
#undef private

#include <filesystem>
#include <fstream>

using HammerEngine::ResourcePath;

namespace fs = std::filesystem;

namespace {

struct ResourcePathFixture {
    ResourcePathFixture()
    {
        ResourcePath::s_searchPaths.clear();
        ResourcePath::s_initialized = false;
        ResourcePath::s_isBundle = false;
    }

    ~ResourcePathFixture()
    {
        ResourcePath::s_searchPaths.clear();
        ResourcePath::s_initialized = false;
        ResourcePath::s_isBundle = false;

        std::error_code ec;
        if (!m_tempRoot.empty()) {
            fs::remove_all(m_tempRoot, ec);
        }
    }

    fs::path makeTempRoot()
    {
        if (m_tempRoot.empty()) {
            m_tempRoot = fs::temp_directory_path() / "hammer_resource_path_tests";
            std::error_code ec;
            fs::remove_all(m_tempRoot, ec);
            fs::create_directories(m_tempRoot / "high" / "res" / "img");
            fs::create_directories(m_tempRoot / "low" / "res" / "img");

            std::ofstream(m_tempRoot / "high" / "res" / "img" / "icon.png") << "high";
            std::ofstream(m_tempRoot / "low" / "res" / "img" / "icon.png") << "low";
        }
        return m_tempRoot;
    }

    fs::path m_tempRoot;
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(ResourcePathTests, ResourcePathFixture)

BOOST_AUTO_TEST_CASE(TestResolveReturnsRelativePathBeforeInit)
{
    const std::string relative = "res/img/icon.png";
    BOOST_CHECK_EQUAL(ResourcePath::resolve(relative), relative);
}

BOOST_AUTO_TEST_CASE(TestSearchPathPriorityControlsResolution)
{
    const fs::path tempRoot = makeTempRoot();

    ResourcePath::addSearchPath((tempRoot / "low").string(), 1);
    ResourcePath::addSearchPath((tempRoot / "high").string(), 5);
    ResourcePath::s_initialized = true;

    const std::string resolved = ResourcePath::resolve("res/img/icon.png");
    BOOST_CHECK_EQUAL(resolved, (tempRoot / "high" / "res" / "img" / "icon.png").string());
    BOOST_CHECK(ResourcePath::exists("res/img/icon.png"));
    BOOST_CHECK_EQUAL(ResourcePath::getBasePath(), (tempRoot / "high").string());
}

BOOST_AUTO_TEST_CASE(TestRemoveSearchPathFallsBackToNextCandidate)
{
    const fs::path tempRoot = makeTempRoot();

    ResourcePath::addSearchPath((tempRoot / "low").string(), 1);
    ResourcePath::addSearchPath((tempRoot / "high").string(), 5);
    ResourcePath::s_initialized = true;

    ResourcePath::removeSearchPath((tempRoot / "high").string());

    const std::string resolved = ResourcePath::resolve("res/img/icon.png");
    BOOST_CHECK_EQUAL(resolved, (tempRoot / "low" / "res" / "img" / "icon.png").string());
    BOOST_CHECK_EQUAL(ResourcePath::getBasePath(), (tempRoot / "low").string());
}

BOOST_AUTO_TEST_SUITE_END()
