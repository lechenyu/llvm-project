#include "tsan_avltree.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"
#include <vector>
#include <algorithm>
#include <iostream>
using namespace std;

namespace __tsan {
TEST(Crossbowman, IntervalTreeIterator) {
    Interval i{1, 10};
    MapInfo m{1, 9};
    IntervalTree tree{};
    tree.insert(i, m);
    auto &&it = tree.begin();
    auto &&ie = tree.end();
    ASSERT_NE(it, ie);
    Node *n = *it;
    ASSERT_EQ(n->interval, i);
    ++it;
    ASSERT_EQ(it, ie);
}

TEST(Crossbowman, IntervalTreeInsert) {
    vector<Interval> ints{{5, 10}, {1, 3}, {10, 15}, {15, 20}, {3, 5}};
    vector<MapInfo> maps{{5, 5}, {1, 2}, {10, 5}, {15, 5}, {3, 2}}; 

    IntervalTree tree{};
    unsigned i = 0;
    for (; i < ints.size(); i++) {
        tree.insert(ints[i], maps[i]);
    }
    sort(ints.begin(), ints.end());
    i = 0;
    for (auto &&it : tree) {
        EXPECT_EQ(it->interval, ints[i++]);
    }
}

TEST(Crossbowman, IntervalTreeDeleteLeafNode) {
    vector<Interval> ints{{5, 10}, {1, 3}, {10, 15}, {15, 20}, {3, 5}};
    vector<Interval> exps{{5, 10}, {1, 3}, {10, 15}, {3, 5}};
    vector<MapInfo> maps{{5, 5}, {1, 2}, {10, 5}, {15, 5}, {3, 2}}; 

    IntervalTree tree{};
    unsigned i = 0;
    for (; i < ints.size(); i++) {
        tree.insert(ints[i], maps[i]);
    }
    tree.remove(ints[3]);
    sort(exps.begin(), exps.end());
    i = 0;
    for (auto &&it : tree) {
        //cout << it->interval.left_end << ", " << it->interval.right_end << endl; 
        EXPECT_EQ(it->interval, exps[i++]);
    }
}


TEST(Crossbowman, IntervalTreeDeleteInternalNode1) {
    vector<Interval> ints{{5, 10}, {1, 3}, {10, 15}, {15, 20}, {3, 5}};
    vector<Interval> exps{{5, 10}, {10, 15}, {15, 20}, {3, 5}};
    vector<MapInfo> maps{{5, 5}, {1, 2}, {10, 5}, {15, 5}, {3, 2}}; 

    IntervalTree tree{};
    unsigned i = 0;
    for (; i < ints.size(); i++) {
        tree.insert(ints[i], maps[i]);
        //for (auto &&it : tree) {
            //cout << it << " " << it->interval.left_end << ", " << it->interval.right_end << ", " << it->left_child << ", " << it->right_child << ", " << it->parent << endl; 
        //} 
    }

    tree.remove(ints[1]);

    sort(exps.begin(), exps.end());
    i = 0;
    for (auto &&it : tree) {
        EXPECT_EQ(it->interval, exps[i++]);
    }
}

TEST(Crossbowman, IntervalTreeDeleteInternalNode2) {
    vector<Interval> ints{{5, 10}, {1, 3}, {10, 15}, {15, 20}, {3, 5}};
    vector<Interval> exps{{1, 3}, {10, 15}, {15, 20}, {3, 5}};
    vector<MapInfo> maps{{5, 5}, {1, 2}, {10, 5}, {15, 5}, {3, 2}}; 

    IntervalTree tree{};
    unsigned i = 0;
    for (; i < ints.size(); i++) {
        tree.insert(ints[i], maps[i]);
    }
    tree.remove(ints[0]);
    sort(exps.begin(), exps.end());
    i = 0;
    for (auto &&it : tree) {
        EXPECT_EQ(it->interval, exps[i++]);
    }

    EXPECT_EQ(tree.getRoot()->interval, (Interval{3, 5}));
}

TEST(Crossbowman, MappingStateInShadow) {
    Shadow s(0ull);
    EXPECT_EQ(s.isTargetInitialized(), false);
    EXPECT_EQ(s.isHostInitialized(), false);
    EXPECT_EQ(s.isTargetLatest(), false);
    EXPECT_EQ(s.isHostLatest(), false);
    s.setTargetLatest();
    EXPECT_EQ(s.raw(), 0x0028000000000000ull);
    EXPECT_EQ(s.isTargetInitialized(), true);
    EXPECT_EQ(s.isHostInitialized(), false);
    EXPECT_EQ(s.isTargetLatest(), true);
    EXPECT_EQ(s.isHostLatest(), false);
    s.setHostLatest();
    EXPECT_EQ(s.raw(), 0x0034000000000000ull);
    EXPECT_EQ(s.isTargetInitialized(), true);
    EXPECT_EQ(s.isHostInitialized(), true);
    EXPECT_EQ(s.isTargetLatest(), false);
    EXPECT_EQ(s.isHostLatest(), true);
    
    Shadow s2(0ull);
    s2.setTargetAndHostLatest();
    EXPECT_EQ(s2.raw(), 0x000c000000000000ull);
    EXPECT_EQ(s2.isTargetInitialized(), false);
    EXPECT_EQ(s2.isHostInitialized(), false);
    EXPECT_EQ(s2.isTargetLatest(), true);
    EXPECT_EQ(s2.isHostLatest(), true);

    Shadow s3(0x0010000000000000ull);
    s3.setTargetStateByHostState();
    EXPECT_EQ(s3.raw(), 0x0030000000000000ull);
    EXPECT_EQ(s3.isTargetInitialized(), true);
    EXPECT_EQ(s3.isHostInitialized(), true);
    EXPECT_EQ(s3.isTargetLatest(), false);
    EXPECT_EQ(s3.isHostLatest(), false);

    Shadow s4(0x0008000000000000ull);
    s4.setHostStateByTargetState();
    EXPECT_EQ(s4.raw(), 0x000c000000000000ull);
    EXPECT_EQ(s4.isTargetInitialized(), false);
    EXPECT_EQ(s4.isHostInitialized(), false);
    EXPECT_EQ(s4.isTargetLatest(), true);
    EXPECT_EQ(s4.isHostLatest(), true);

    Shadow s5(0x003c000000000000ull);
    Shadow s6(0ull);
    s6.copyMappingStates(s5);
    EXPECT_EQ(s6.raw(), s5.raw());

    Shadow s7(0xffffffffffffffffull);
    s7.resetHostState();
    EXPECT_EQ(s7.raw(), 0xffebffffffffffffull);
    EXPECT_EQ(s7.isTargetInitialized(), true);
    EXPECT_EQ(s7.isHostInitialized(), false);
    EXPECT_EQ(s7.isTargetLatest(), true);
    EXPECT_EQ(s7.isHostLatest(), false);

    Shadow s8(0xffffffffffffffffull);
    s8.resetTargetState();
    EXPECT_EQ(s8.raw(), 0xffd7ffffffffffffull);
    EXPECT_EQ(s8.isTargetInitialized(), false);
    EXPECT_EQ(s8.isHostInitialized(), true);
    EXPECT_EQ(s8.isTargetLatest(), false);
    EXPECT_EQ(s8.isHostLatest(), true);

    Shadow s9(0x0000000000000000ull);
    s9.setMappingStates();
    EXPECT_EQ(s9.raw(), 0x003c000000000000ull);
    EXPECT_EQ(s9.isTargetInitialized(), true);
    EXPECT_EQ(s9.isHostInitialized(), true);
    EXPECT_EQ(s9.isTargetLatest(), true);
    EXPECT_EQ(s9.isHostLatest(), true);

    Shadow s10(0x0000000000000000ull);
    s10.setHostInitializedAndLatest();
    EXPECT_EQ(s10.raw(), 0x0014000000000000ull);
    EXPECT_EQ(s10.isTargetInitialized(), false);
    EXPECT_EQ(s10.isHostInitialized(), true);
    EXPECT_EQ(s10.isTargetLatest(), false);
    EXPECT_EQ(s10.isHostLatest(), true);
}

} // namespace __tsan
