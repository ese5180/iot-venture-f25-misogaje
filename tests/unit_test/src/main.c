/*
 * =============================================================================
 * Unit Test Main Entry Point
 * =============================================================================
 * This file registers all test suites for the Ztest framework.
 * Add your test suite declarations here.
 * =============================================================================
 */

#include <zephyr/ztest.h>

/* 
 * Test suites are automatically registered using ZTEST_SUITE macro
 * in their respective test_*.c files. This main.c just needs to
 * exist for the build system.
 *
 * The Ztest framework will automatically discover and run all
 * registered test suites.
 */
