/**
 * @file rules.cpp
 *
 * This file contains consensus rules and restrictions.
 */

#include <omnicore/rules.h>

// omnilite-standalone: phase 3 // #include <omnicore/activation.h>
// omnilite-standalone: phase 3 // #include <omnicore/consensushash.h>
// omnilite-standalone: phase 3 // #include <omnicore/dbtxlist.h>
#include <omnicore/log.h>
#include <omnicore/omnicore.h>
// omnilite-standalone: phase 3 // #include <omnicore/notifications.h>
// omnilite-standalone: phase 3 // #include <omnicore/utilsbitcoin.h>
// omnilite-standalone: phase 3 // #include <omnicore/version.h>

#include <chainparams.h> // omnilite-standalone: our src/chainparams.h
// omnilite-standalone: not needed // #include <validation.h>
#include <script/standard.h>
#include <uint256.h>
// omnilite-standalone: not needed // #include <ui_interface.h>

#include <stdint.h>
#include <limits>
#include <string>
#include <vector>

namespace mastercore
{
/**
 * Returns a mapping of transaction types, and the blocks at which they are enabled.
 */
std::vector<TransactionRestriction> CConsensusParams::GetRestrictions() const
{
    const TransactionRestriction vTxRestrictions[] =
    { //  transaction type                    version        allow 0  activation block
      //  ----------------------------------  -------------  -------  ------------------
        { OMNICORE_MESSAGE_TYPE_ALERT,        0xFFFF,        true,    MSC_ALERT_BLOCK    },
        { OMNICORE_MESSAGE_TYPE_ACTIVATION,   0xFFFF,        true,    MSC_ALERT_BLOCK    },
        { OMNICORE_MESSAGE_TYPE_DEACTIVATION, 0xFFFF,        true,    MSC_ALERT_BLOCK    },

        { MSC_TYPE_SIMPLE_SEND,               MP_TX_PKT_V0,  false,   MSC_SEND_BLOCK     },

        { MSC_TYPE_TRADE_OFFER,               MP_TX_PKT_V0,  false,   MSC_DEX_BLOCK      },
        { MSC_TYPE_TRADE_OFFER,               MP_TX_PKT_V1,  false,   MSC_DEX_BLOCK      },
        { MSC_TYPE_ACCEPT_OFFER_BTC,          MP_TX_PKT_V0,  false,   MSC_DEX_BLOCK      },

        { MSC_TYPE_CREATE_PROPERTY_FIXED,     MP_TX_PKT_V0,  false,   MSC_SP_BLOCK       },
        { MSC_TYPE_CREATE_PROPERTY_VARIABLE,  MP_TX_PKT_V0,  false,   MSC_SP_BLOCK       },
        { MSC_TYPE_CREATE_PROPERTY_VARIABLE,  MP_TX_PKT_V1,  false,   MSC_SP_BLOCK       },
        { MSC_TYPE_CLOSE_CROWDSALE,           MP_TX_PKT_V0,  false,   MSC_SP_BLOCK       },

        { MSC_TYPE_CREATE_PROPERTY_MANUAL,    MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_GRANT_PROPERTY_TOKENS,     MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_REVOKE_PROPERTY_TOKENS,    MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_CHANGE_ISSUER_ADDRESS,     MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_ENABLE_FREEZING,           MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_DISABLE_FREEZING,          MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_FREEZE_PROPERTY_TOKENS,    MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },
        { MSC_TYPE_UNFREEZE_PROPERTY_TOKENS,  MP_TX_PKT_V0,  false,   MSC_MANUALSP_BLOCK },

        { MSC_TYPE_SEND_TO_OWNERS,            MP_TX_PKT_V0,  false,   MSC_STO_BLOCK      },
        { MSC_TYPE_SEND_TO_OWNERS,            MP_TX_PKT_V1,  false,   MSC_STOV1_BLOCK    },

        { MSC_TYPE_SEND_ALL,                  MP_TX_PKT_V0,  false,   MSC_SEND_ALL_BLOCK },
        
        { MSC_TYPE_ANYDATA,                   MP_TX_PKT_V0,  true,    MSC_ANYDATA_BLOCK },

        { MSC_TYPE_SEND_NONFUNGIBLE,          MP_TX_PKT_V0,  false,   MSC_NONFUNGIBLE_BLOCK  },
        { MSC_TYPE_NONFUNGIBLE_DATA,          MP_TX_PKT_V0,  false,   MSC_NONFUNGIBLE_BLOCK  },
    };

    const size_t nSize = sizeof(vTxRestrictions) / sizeof(vTxRestrictions[0]);

    return std::vector<TransactionRestriction>(vTxRestrictions, vTxRestrictions + nSize);
}

/**
 * Returns an empty vector of consensus checkpoints.
 *
 * This method should be overwritten by the child classes, if needed.
 */
std::vector<ConsensusCheckpoint> CConsensusParams::GetCheckpoints() const
{
    return std::vector<ConsensusCheckpoint>();
}

/**
 * Returns an empty vector of transaction checkpoints.
 *
 * This method should be overwritten by the child classes, if needed.
 */
std::vector<TransactionCheckpoint> CConsensusParams::GetTransactions() const
{
    return std::vector<TransactionCheckpoint>();
}

/**
 * Returns consensus checkpoints for mainnet, used to verify transaction processing.
 */
std::vector<ConsensusCheckpoint> CMainConsensusParams::GetCheckpoints() const
{
    return std::vector<ConsensusCheckpoint>();
}

/**
 * Returns transactions checkpoints for mainnet, used to verify DB consistency.
 */
std::vector<TransactionCheckpoint> CMainConsensusParams::GetTransactions() const
{
    return std::vector<TransactionCheckpoint>();
}

/**
 * Constructor for mainnet consensus parameters.
 */
CMainConsensusParams::CMainConsensusParams()
{
    // OmniLite genesis block
    GENESIS_BLOCK = 2010500;
    // Notice range for feature activations:
    MIN_ACTIVATION_BLOCKS = 8192;  // ~2 weeks
    MAX_ACTIVATION_BLOCKS = 49152; // ~12 weeks
    // Waiting period for enabling freezing
    OMNI_FREEZE_WAIT_PERIOD = 4096; // ~4 weeks
    // Script related:
    PUBKEYHASH_BLOCK = 0;
    SCRIPTHASH_BLOCK = GENESIS_BLOCK;
    MULTISIG_BLOCK = 0;
    NULLDATA_BLOCK = GENESIS_BLOCK;
    // Transaction restrictions:
    MSC_ALERT_BLOCK = 0;
    MSC_SEND_BLOCK = GENESIS_BLOCK;
    MSC_DEX_BLOCK = GENESIS_BLOCK;
    MSC_SP_BLOCK = GENESIS_BLOCK;
    MSC_MANUALSP_BLOCK = GENESIS_BLOCK;
    MSC_STO_BLOCK = GENESIS_BLOCK;
    MSC_SEND_ALL_BLOCK = GENESIS_BLOCK;
    MSC_STOV1_BLOCK = 100000000;
    MSC_ANYDATA_BLOCK = 0;
    MSC_NONFUNGIBLE_BLOCK = 2262000;
    // Other feature activations:
    FREEZENOTICE_FEATURE_BLOCK = 100000000;
    FREEDEX_FEATURE_BLOCK = GENESIS_BLOCK;
    NONFUNGIBLETOKEN_ISSUER_DATA = 100000000;
}

/**
 * Constructor for testnet consensus parameters.
 */
CTestNetConsensusParams::CTestNetConsensusParams()
{
    // OmniLite genesis block
    GENESIS_BLOCK = 101;
    // Notice range for feature activations:
    MIN_ACTIVATION_BLOCKS = 0;
    MAX_ACTIVATION_BLOCKS = 9999999;
    // Waiting period for enabling freezing
    OMNI_FREEZE_WAIT_PERIOD = 0;
    // Script related:
    PUBKEYHASH_BLOCK = 0;
    SCRIPTHASH_BLOCK = 0;
    MULTISIG_BLOCK = 0;
    NULLDATA_BLOCK = 0;
    // Transaction restrictions:
    MSC_ALERT_BLOCK = 0;
    MSC_SEND_BLOCK = 0;
    MSC_DEX_BLOCK = 0;
    MSC_SP_BLOCK = 0;
    MSC_MANUALSP_BLOCK = 0;
    MSC_STO_BLOCK = 0;
    MSC_SEND_ALL_BLOCK = 0;
    MSC_STOV1_BLOCK = 0;
    MSC_ANYDATA_BLOCK = 0;
    MSC_NONFUNGIBLE_BLOCK = 0;
    // Other feature activations:
    FREEZENOTICE_FEATURE_BLOCK = 0;
    FREEDEX_FEATURE_BLOCK = 0;
    NONFUNGIBLETOKEN_ISSUER_DATA = 0;
}

/**
 * Constructor for regtest consensus parameters.
 */
CRegTestConsensusParams::CRegTestConsensusParams()
{
    // OmniLite genesis block
    GENESIS_BLOCK = 101;
    // Notice range for feature activations:
    MIN_ACTIVATION_BLOCKS = 5;
    MAX_ACTIVATION_BLOCKS = 10;
    // Waiting period for enabling freezing
    OMNI_FREEZE_WAIT_PERIOD = 10;
    // Script related:
    PUBKEYHASH_BLOCK = 0;
    SCRIPTHASH_BLOCK = 0;
    MULTISIG_BLOCK = 0;
    NULLDATA_BLOCK = 0;
    // Transaction restrictions:
    MSC_ALERT_BLOCK = 0;
    MSC_SEND_BLOCK = 0;
    MSC_DEX_BLOCK = 0;
    MSC_SP_BLOCK = 0;
    MSC_MANUALSP_BLOCK = 0;
    MSC_STO_BLOCK = 0;
    MSC_SEND_ALL_BLOCK = 0;
    MSC_STOV1_BLOCK = 100000000;
    MSC_ANYDATA_BLOCK = 0;
    MSC_NONFUNGIBLE_BLOCK = 0;
    // Other feature activations:
    FREEZENOTICE_FEATURE_BLOCK = 100000000;
    FREEDEX_FEATURE_BLOCK = 100000000;
    NONFUNGIBLETOKEN_ISSUER_DATA = 100000000;
}

//! Consensus parameters for mainnet
static CMainConsensusParams mainConsensusParams;
//! Consensus parameters for testnet
static CTestNetConsensusParams testNetConsensusParams;
//! Consensus parameters for regtest mode
static CRegTestConsensusParams regTestConsensusParams;

/**
 * Returns consensus parameters for the given network.
 */
CConsensusParams& ConsensusParams(const std::string& network)
{
    if (network == "main") {
        return mainConsensusParams;
    }
    if (network == "test") {
        return testNetConsensusParams;
    }
    if (network == "regtest") {
        return regTestConsensusParams;
    }
    // Fallback:
    return mainConsensusParams;
}

/**
 * Returns currently active consensus parameter.
 */
const CConsensusParams& ConsensusParams()
{
    const std::string& network = Params().NetworkIDString();

    return ConsensusParams(network);
}

/**
 * Returns currently active mutable consensus parameter.
 */
CConsensusParams& MutableConsensusParams()
{
    const std::string& network = Params().NetworkIDString();

    return ConsensusParams(network);
}

/**
 * Resets consensus parameters.
 */
void ResetConsensusParams()
{
    mainConsensusParams = CMainConsensusParams();
    testNetConsensusParams = CTestNetConsensusParams();
    regTestConsensusParams = CRegTestConsensusParams();
}

/**
 * Checks, if the script type is allowed as input.
 */
bool IsAllowedInputType(int whichType, int nBlock)
{
    const CConsensusParams& params = ConsensusParams();

    switch (whichType)
    {
        case TX_PUBKEYHASH:
            return (params.PUBKEYHASH_BLOCK <= nBlock);

        case TX_SCRIPTHASH:
            return (params.SCRIPTHASH_BLOCK <= nBlock);
    }

    return false;
}

/**
 * Checks, if the script type qualifies as output.
 */
bool IsAllowedOutputType(int whichType, int nBlock)
{
    const CConsensusParams& params = ConsensusParams();

    switch (whichType)
    {
        case TX_PUBKEYHASH:
            return (params.PUBKEYHASH_BLOCK <= nBlock);

        case TX_SCRIPTHASH:
            return (params.SCRIPTHASH_BLOCK <= nBlock);

        case TX_MULTISIG:
            return (params.MULTISIG_BLOCK <= nBlock);

        case TX_NULL_DATA:
            return (params.NULLDATA_BLOCK <= nBlock);
    }

    return false;
}

// omnilite-standalone: activations, feature checks and checkpoints of the
// original rules.cpp follow in phase 3.

} // namespace mastercore
