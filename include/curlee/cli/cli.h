#pragma once

/**
 * @file cli.h
 * @brief Command-line entry point for the curlee binary.
 */

namespace curlee::cli
{

/** @brief Run the CLI using argc/argv; returns process exit code. */
int run(int argc, char** argv);

} // namespace curlee::cli
