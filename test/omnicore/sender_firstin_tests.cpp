#include <test/omnicore/utils_tx.h>

#include <omnicore/createpayload.h>
#include <omnicore/encoding.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/script.h>
#include <omnicore/tx.h>

#include <base58.h>
#include <coins.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <test/omnicore/test_shim.h> // omnilite-standalone: instead of test/test_bitcoin.h

#include <stdint.h>
#include <limits>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace mastercore;

BOOST_FIXTURE_TEST_SUITE(omnicore_sender_firstin_tests, BasicTestingSetup)

/** Creates a dummy class C transaction with the given inputs. */
static CTransaction TxClassC(const std::vector<CTxOut>& txInputs)
{
    CMutableTransaction mutableTx;

    // Inputs:
    for (const auto& txOut : txInputs)
    {
        // Create transaction for input:
        CMutableTransaction inputTx;
        inputTx.vout.push_back(txOut);
        CTransaction tx(inputTx);

        // Populate transaction cache:
        Coin newcoin;
        newcoin.out.scriptPubKey = txOut.scriptPubKey;
        newcoin.out.nValue = txOut.nValue;
        view.AddCoin(COutPoint(tx.GetHash(), 0), std::move(newcoin), true);

        // Add input:
        CTxIn txIn(tx.GetHash(), 0);
        mutableTx.vin.push_back(txIn);
    }

    // Outputs:
    std::vector<std::pair<CScript, int64_t> > txOutputs;
    std::vector<unsigned char> vchPayload = CreatePayload_SimpleSend(1, 1000);
    BOOST_CHECK(OmniCore_Encode_ClassC(vchPayload, txOutputs));

    for (const auto& pair : txOutputs)
    {
        CTxOut txOut(pair.second, pair.first);
        mutableTx.vout.push_back(txOut);
    }

    return CTransaction(mutableTx);
}

/** Helper to create a CTxOut object. */
static CTxOut createTxOut(int64_t amount, const std::string& dest)
{
    return CTxOut(amount, GetScriptForDestination(DecodeDestination(dest)));
}

/** Extracts the "first" sender. */
static bool GetFirstSender(const std::vector<CTxOut>& txInputs, std::string& strSender)
{
    int nBlock = std::numeric_limits<int>::max();

    CMPTransaction metaTx;
    CTransaction dummyTx = TxClassC(txInputs);

    if (ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0) {
        strSender = metaTx.getSender();
        return true;
    }

    return false;
}

BOOST_AUTO_TEST_CASE(first_vin_is_sender)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(100, "LbhChjbxWGkXSS4KTBQ2ehyD4XaaSUTkVE")); // Winner
    vouts.push_back(createTxOut(999, "MRko5zS1dib86yN3q1imXCiVyvXajgdiwG"));
    vouts.push_back(createTxOut(200, "Lbf5vowqXpBHFaiyrpQtRjSgoPp6mW6Kg9"));

    std::string strExpected("LbhChjbxWGkXSS4KTBQ2ehyD4XaaSUTkVE");

    std::string strSender;
    BOOST_CHECK(GetFirstSender(vouts, strSender));
    BOOST_CHECK_EQUAL(strExpected, strSender);
}

BOOST_AUTO_TEST_CASE(less_input_restrictions)
{
    std::vector<CTxOut> vouts;
    vouts.push_back(createTxOut(555, "MJR9iPWdgoKN8LKHq278icGXqFkwiriPkP")); // Winner
    vouts.push_back(PayToPubKey_Unrelated());
    vouts.push_back(PayToBareMultisig_1of3());
    vouts.push_back(NonStandardOutput());

    std::string strExpected("MJR9iPWdgoKN8LKHq278icGXqFkwiriPkP");

    std::string strSender;
    BOOST_CHECK(GetFirstSender(vouts, strSender));
    BOOST_CHECK_EQUAL(strExpected, strSender);
}

BOOST_AUTO_TEST_CASE(invalid_inputs)
{
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(PayToPubKey_Unrelated());
        std::string strSender;
        BOOST_CHECK(!GetFirstSender(vouts, strSender));
    }
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(PayToBareMultisig_1of3());
        std::string strSender;
        BOOST_CHECK(!GetFirstSender(vouts, strSender));
    }
    {
        std::vector<CTxOut> vouts;
        vouts.push_back(NonStandardOutput());
        std::string strSender;
        BOOST_CHECK(!GetFirstSender(vouts, strSender));
    }
}


BOOST_AUTO_TEST_SUITE_END()
