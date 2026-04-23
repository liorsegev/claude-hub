include(GoogleTest)

add_executable(claude_hub_tests
	test_jsonl_parsing.cpp
	test_agent_driver.cpp
	test_claude_session.cpp
	test_jsonl_probe.cpp

	# Source files under test. We compile them directly into the test binary
	# rather than packaging the production target as a lib — nothing else
	# currently shares these units, so a lib split would be extra scaffolding.
	${CMAKE_SOURCE_DIR}/src/AgentDrivers.cpp
	${CMAKE_SOURCE_DIR}/src/ClaudeSessionDiscovery.cpp
	${CMAKE_SOURCE_DIR}/src/JsonlActivityProbe.cpp
	${CMAKE_SOURCE_DIR}/src/Logger.cpp
)

target_include_directories(claude_hub_tests PRIVATE
	${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(claude_hub_tests PRIVATE
	GTest::gtest_main
)

gtest_discover_tests(claude_hub_tests)
