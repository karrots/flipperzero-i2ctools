#pragma once

#include "../crypto_cli_types.h"

/**
 * @file crypto_cmd_mac.h
 * @brief MAC/HMAC authentication tag computation CLI command
 *
 * Implements the "crypto mac" command for computing HMAC-SHA256 authentication
 * tags using keys stored in ATECC608B chip slots.
 *
 * Usage:
 *   crypto mac --slot <N> --msg <hex> [--json]
 *   crypto mac --slot <N> --use-tempkey [--json]
 *
 * Examples:
 *   crypto mac --slot 12 --msg 48656C6C6F
 *   crypto mac --slot 12 --msg AABBCCDD --json
 *   crypto mac --slot 12 --use-tempkey --json
 */

/**
 * MAC command implementation
 */
extern const CryptoCommand crypto_command_mac;

/**
 * Register MAC command with CLI framework
 */
void crypto_cmd_mac_register(void);
