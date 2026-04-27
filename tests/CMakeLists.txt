include(GoogleTest)

add_executable(agents_hub_tests
	test_jsonl_parsing.cpp
	test_agent_driver.cpp
	test_claude_session.cpp
	test_jsonl_probe.cpp
	test_agent_manager.cpp
	test_agent_close.cpp

	# Source files under test. We compile them directly into the test binary
	# rather than packaging the production target as a lib — nothing else
	# currently shares these units, so a lib split would be extra scaffolding.
	${CMAKE_SOURCE_DIR}/src/Agent.cpp
	${CMAKE_SOURCE_DIR}/src/AgentDrivers.cpp
	${CMAKE_SOURCE_DIR}/src/AgentManager.cpp
	${CMAKE_SOURCE_DIR}/src/ClaudeSessionDiscovery.cpp
	${CMAKE_SOURCE_DIR}/src/JsonlActivityProbe.cpp
	${CMAKE_SOURCE_DIR}/src/Logger.cpp
	${CMAKE_SOURCE_DIR}/src/WaitingFlagWatcher.cpp
	${CMAKE_SOURCE_DIR}/src/WindowsTerminalSpawner.cpp
)

target_include_directories(agents_hub_tests PRIVATE
	${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(agents_hub_tests PRIVATE
	GTest::gtest_main
)

gtest_discover_tests(agents_hub_tests)
