// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"
#include "chain.h"
#include "chainparams.h"
#include "config.h"
#include "random.h"
#include "test/test_bitcoin.h"
#include "util.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work) {
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739; // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, config),
        0x1d00d86a);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit) {
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996; // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, config),
        0x1d00ffff);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual) {
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671; // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, config),
        0x1c0168fd);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual) {
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443; // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, config),
        0x1d00e1fd);
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test) {
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params &params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork =
            i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i])
              : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

static CBlockIndex GetBlockIndex(CBlockIndex *pindexPrev, int64_t nTimeInterval,
                                 uint32_t nBits) {
    CBlockIndex block;
    block.pprev = pindexPrev;
    block.nHeight = pindexPrev->nHeight + 1;
    block.nTime = pindexPrev->nTime + nTimeInterval;
    block.nBits = nBits;

    block.nChainWork = pindexPrev->nChainWork + GetBlockProof(block);
    return block;
}

BOOST_AUTO_TEST_CASE(cash_plus_difficulty_test) {
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;

    std::vector<CBlockIndex> blocks(3000);

    const Consensus::Params &params = config.GetChainParams().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.PowLimit(false));
    uint32_t powLimitBits = powLimit.GetCompact();
    arith_uint256 currentPow = powLimit >> 4;
    uint32_t initialBits = currentPow.GetCompact();

    // Genesis block.
    blocks[0] = CBlockIndex();
    blocks[0].nHeight = 0;
    blocks[0].nTime = 1269211443;
    blocks[0].nBits = initialBits;

    blocks[0].nChainWork = GetBlockProof(blocks[0]);

    // Block counter.
    size_t i;

    // Pile up some blocks every 10 mins to establish some history.
    for (i = 1; i < 2050; i++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, initialBits);
    }

    CBlockHeader blkHeaderDummy;
    uint32_t nBits =
        GetNextCashPlusWorkRequired(&blocks[2049], &blkHeaderDummy, config);

    // Difficulty stays the same as long as we produce a block every 10 mins.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, nBits);
        BOOST_CHECK_EQUAL(
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config),
            nBits);
    }

    // Make sure we skip over blocks that are out of wack. To do so, we produce
    // a block that is far in the future, and then produce a block with the
    // expected timestamp.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    BOOST_CHECK_EQUAL(
        GetNextCashPlusWorkRequired(&blocks[i++], &blkHeaderDummy, config), nBits);
    blocks[i] = GetBlockIndex(&blocks[i - 1], 2 * 600 - 6000, nBits);
    BOOST_CHECK_EQUAL(
        GetNextCashPlusWorkRequired(&blocks[i++], &blkHeaderDummy, config), nBits);

    // The system should continue unaffected by the block with a bogous
    // timestamps.
    for (size_t j = 0; j < 20; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, nBits);
        BOOST_CHECK_EQUAL(
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config),
            nBits);
    }

    // We start emitting blocks slightly faster. The first block has no impact.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 550, nBits);
    BOOST_CHECK_EQUAL(
        GetNextCashPlusWorkRequired(&blocks[i++], &blkHeaderDummy, config), nBits);

    // Now we should see difficulty increase slowly.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 550, nBits);
        const uint32_t nextBits =
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases very slowly.
        BOOST_CHECK(nextTarget < currentTarget);
        BOOST_CHECK((currentTarget - nextTarget) < (currentTarget >> 10));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0fe7b1);

    // If we dramatically shorten block production, difficulty increases faster.
    for (size_t j = 0; j < 20; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 10, nBits);
        const uint32_t nextBits =
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases faster.
        BOOST_CHECK(nextTarget < currentTarget);
        BOOST_CHECK((currentTarget - nextTarget) < (currentTarget >> 4));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0db19f);

    // We start to emit blocks significantly slower. The first block has no
    // impact.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    nBits = GetNextCashPlusWorkRequired(&blocks[i++], &blkHeaderDummy, config);

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0d9222);

    // If we dramatically slow down block production, difficulty decreases.
    for (size_t j = 0; j < 93; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits =
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        BOOST_CHECK(nextTarget <= powLimit);
        BOOST_CHECK(nextTarget > currentTarget);
        BOOST_CHECK((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c2f13b9);

    // Due to the window of time being bounded, next block's difficulty actually
    // gets harder.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    nBits = GetNextCashPlusWorkRequired(&blocks[i++], &blkHeaderDummy, config);
    BOOST_CHECK_EQUAL(nBits, 0x1c2ee9bf);

    // And goes down again. It takes a while due to the window being bounded and
    // the skewed block causes 2 blocks to get out of the window.
    for (size_t j = 0; j < 192; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits =
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        BOOST_CHECK(nextTarget <= powLimit);
        BOOST_CHECK(nextTarget > currentTarget);
        BOOST_CHECK((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1d00ffff);

    // Once the difficulty reached the minimum allowed level, it doesn't get any
    // easier.
    for (size_t j = 0; j < 5; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits =
            GetNextCashPlusWorkRequired(&blocks[i], &blkHeaderDummy, config);

        // Check the difficulty stays constant.
        BOOST_CHECK_EQUAL(nextBits, powLimitBits);
        nBits = nextBits;
    }
}

BOOST_AUTO_TEST_SUITE_END()
