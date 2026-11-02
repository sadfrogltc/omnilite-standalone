#include <test/omnicore/utils_tx.h>

#include <omnicore/createpayload.h>
#include <omnicore/encoding.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/rules.h>
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

BOOST_FIXTURE_TEST_SUITE(omnicore_parsing_a_tests, BasicTestingSetup)

/** Creates a dummy transaction with the given inputs and outputs. */
static CTransaction TxClassA(const std::vector<CTxOut>& txInputs, const std::vector<CTxOut>& txOuts)
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

    for (std::vector<CTxOut>::const_iterator it = txOuts.begin(); it != txOuts.end(); ++it)
    {
        const CTxOut& txOut = *it;
        mutableTx.vout.push_back(txOut);
    }

    return CTransaction(mutableTx);
}

/** Helper to create a CTxOut object. */
static CTxOut createTxOut(int64_t amount, const std::string& dest)
{
    return CTxOut(amount, GetScriptForDestination(DecodeDestination(dest)));
}

BOOST_AUTO_TEST_CASE(valid_class_a)
{
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1765000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));
        txInputs.push_back(createTxOut(50000, "LNDPcx5qiFBciDoyZmio83VNzDp742DrNv"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16cZF7pJgmVWuH8nf8ZC4EVw"));
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK_EQUAL(ParseTransaction(dummyTx, nBlock, 1, metaTx), 0);
        BOOST_CHECK_EQUAL(metaTx.getFeePaid(), 50000);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000100000002540be400000000");
    }
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(907500, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));
        txInputs.push_back(createTxOut(907500, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16cgQX5Ubebt3JijevUy4tht"));
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getFeePaid(), 50000);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000200000002540be400000000");
    }
    {
        int nBlock = std::numeric_limits<int>::max();

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1815000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(NonStandardOutput());
        txOutputs.push_back(NonStandardOutput());
        txOutputs.push_back(NonStandardOutput());
        txOutputs.push_back(NonStandardOutput());
        txOutputs.push_back(NonStandardOutput());
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(PayToPubKey_Unrelated());
        txOutputs.push_back(PayToPubKey_Unrelated());
        txOutputs.push_back(PayToPubKey_Unrelated());
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(OpReturn_Unrelated());
        txOutputs.push_back(OpReturn_Unrelated());
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16cZF7pJgmVWuH8nf8ZC4EVw"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000100000002540be400000000");
    }
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(87000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LbYRDYtfhFX8XRXyLjSBwH13FDHyxWZSUJ"));
        txOutputs.push_back(createTxOut(6000, "LbT8YMmMH5HS18GXoDYeBi2kypQXiJJS11"));
        txOutputs.push_back(createTxOut(7000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(7000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getFeePaid(), 55000);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000100000002540be400000000");
    }
    {
        int nBlock = ConsensusParams().SCRIPTHASH_BLOCK;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(100000, "MBAqpeJKCM7rFw3QktgyCRFrHuoxk52z9z"));
        txInputs.push_back(createTxOut(100000, "MBAqpeJKCM7rFw3QktgyCRFrHuoxk52z9z"));
        txInputs.push_back(createTxOut(200000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));
        txInputs.push_back(createTxOut(100000, "MBAqpeJKCM7rFw3QktgyCRFrHuoxk52z9z"));
        txInputs.push_back(createTxOut(200000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LbYRDYtfhFX8XRXyLjSBwH13FDHyxWZSUJ"));
        txOutputs.push_back(createTxOut(6000, "Lbdhtk1z7Rkq4DT1vvyG8JXRrQAbrHGKaz"));
        txOutputs.push_back(createTxOut(6001, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(665999, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getFeePaid(), 10000);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "Lbdhtk1z7Rkq4DT1vvyG8JXRrQAbrHGKaz");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000100000002540be400000000");
    }
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(70000, "LKrxzZC3zttwwjzFuK2XBDinL5ttndLamA"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(9001, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(9001, "LKrxzZC3zttwwjzFuK2XBDinL5ttndLamA"));
        txOutputs.push_back(createTxOut(9001, "LKrxzZC3zttwxUxfULVoqQYwqmsePptmz7"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "LKrxzZC3zttwwjzFuK2XBDinL5ttndLamA");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "LKrxzZC3zttwxUxfULVoqQYwqmsePptmz7");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "00000000000000010000000777777700000000");
    }
    {
        int nBlock = ConsensusParams().SCRIPTHASH_BLOCK;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1815000, "MS2nxgYKSvQDkcfYUU4bJZ7XvSuKW7aEDp"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6001, "MAGdoRBfLcJyb5eSUc3JWPsNNeBKFvsp7o"));
        txOutputs.push_back(createTxOut(6002, "MAR6Xt6sNQeNxbwqiVUXUqJLXGGNwi5u6c"));
        txOutputs.push_back(createTxOut(6003, "MQKPLu3vsQNwdy9YAjjLFkmzJCuEongBR9"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) == 0);
        BOOST_CHECK_EQUAL(metaTx.getSender(), "MS2nxgYKSvQDkcfYUU4bJZ7XvSuKW7aEDp");
        BOOST_CHECK_EQUAL(metaTx.getReceiver(), "MAR6Xt6sNQeNxbwqiVUXUqJLXGGNwi5u6c");
        BOOST_CHECK_EQUAL(metaTx.getPayload(), "000000000000000200000002540be400000000");
    }
}

BOOST_AUTO_TEST_CASE(invalid_class_a)
{
    // More than one data packet
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1815000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16cZF7pJgmVWuH8nf8ZC4EVw"));
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16cZF7pJgmVWuH8nf8ZC4EVw"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) != 0);
    }
    // Not MSC or TMSC
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1815000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LWiBQPCqbu16dJDXPLA49hjSeUdtBHDEHM"));
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) != 0);
    }
    // Seq collision
    {
        int nBlock = 0;

        std::vector<CTxOut> txInputs;
        txInputs.push_back(createTxOut(1815000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        std::vector<CTxOut> txOutputs;
        txOutputs.push_back(createTxOut(6000, "LTceXoduS2cetpWJSe47M25i5oKjEccN1h"));
        txOutputs.push_back(createTxOut(6000, "LbYRDYtfhFX8XRXyLjSBwH13FDHyxWZSUJ"));
        txOutputs.push_back(createTxOut(6000, "Lbdhtk1z7Rkq4DT1vvyG8JXRrQAbrHGKaz"));
        txOutputs.push_back(createTxOut(6000, "LWrCy4CUcWoxrQZ1YEYCWcdFYGDjWq2GGs"));
        txOutputs.push_back(createTxOut(1747000, "LbgQdvaBknU1AfLnGGFCWZrTCD51iLnTSV"));

        CTransaction dummyTx = TxClassA(txInputs, txOutputs);

        CMPTransaction metaTx;
        BOOST_CHECK(ParseTransaction(dummyTx, nBlock, 1, metaTx) != 0);
    }
}


BOOST_AUTO_TEST_SUITE_END()
