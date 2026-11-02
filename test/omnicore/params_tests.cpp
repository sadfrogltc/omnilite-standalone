#include <omnicore/omnicore.h>
#include <omnicore/rules.h>

#include <chainparams.h>
#include <test/omnicore/test_shim.h> // omnilite-standalone: instead of test/test_bitcoin.h

#include <boost/test/unit_test.hpp>

#include <stdint.h>
#include <string>

using namespace mastercore;

BOOST_FIXTURE_TEST_SUITE(omnicore_params_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(get_params)
{
    const CConsensusParams& params = ConsensusParams();
    BOOST_CHECK_EQUAL(params.MIN_ACTIVATION_BLOCKS, 8192); // omnilite-standalone: OmniLite's value (Omni Core: 20160)
}

BOOST_AUTO_TEST_CASE(network_restrictions_main)
{
    const CConsensusParams& params = ConsensusParams("main");
    BOOST_CHECK_EQUAL(params.MSC_STO_BLOCK, 2010500); // omnilite-standalone: OmniLite's genesis block (Omni Core: 3454000)
}

BOOST_AUTO_TEST_CASE(network_restrictions_test)
{
    const CConsensusParams& params = ConsensusParams("test");
    BOOST_CHECK_EQUAL(params.MSC_STO_BLOCK, 0);
}

BOOST_AUTO_TEST_SUITE_END()
