// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEY_IO_H
#define BITCOIN_KEY_IO_H

#include <chainparams.h>
// omnilite-standalone: no private keys here, see vendor/VENDOR.md
#include <pubkey.h>
#include <script/standard.h>

#include <string>

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

bool DecodeIndexKey(const std::string& str, uint256& hashBytes, int& type);

#endif // BITCOIN_KEY_IO_H
