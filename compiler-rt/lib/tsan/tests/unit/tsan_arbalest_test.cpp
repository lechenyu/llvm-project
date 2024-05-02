#include <algorithm>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "tsan_avltree.h"
#include "tsan_shadow.h"
using namespace std;

namespace __tsan {

u32 LoadVsm4(RawVsm *vp);
void StoreVsm4(RawVsm *vp, RawVsm val);
void VsmSet(RawVsm* p, RawVsm* end, RawVsm val);
void VsmUpdateMapTo(RawVsm* p, RawVsm* end);
void VsmUpdateMapFrom(RawVsm* p, RawVsm* end);

void init(IntervalTree &tree, vector<Interval> &iv) {
  for (auto &i : iv) {
    tree.insert(i, {i.left_end, i.right_end - i.left_end});
  }
}

TEST(Arbalest, AvlInsert) {
  IntervalTree tree{};
  vector<Interval> iv{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9},
                      {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
  init(tree, iv);
  for (auto &i : iv) {
    Node *n = tree.find(i);
    EXPECT_NE(n, nullptr);
    EXPECT_EQ(n->interval, i);
    EXPECT_EQ(tree.size, iv.size());
  }
}

TEST(Arbalest, AvlDelete) {
  IntervalTree tree{};
  vector<Interval> iv{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9},
                      {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
  init(tree, iv);
  random_device rd;
  mt19937 g(rd());
  shuffle(iv.begin(), iv.end(), g);
  for (auto i = 0u; i < iv.size(); i++) {
    printf("delete [%p, %p]\n", reinterpret_cast<char *>(iv[i].left_end),
           reinterpret_cast<char *>(iv[i].right_end));
    tree.remove(iv[i]);
    EXPECT_EQ(tree.size, iv.size() - (i + 1));
    EXPECT_EQ(tree.find(iv[i]), nullptr);
    EXPECT_TRUE(tree.satisfyBalanceFactor(tree.getRoot()));
    for (auto j = i + 1; j < iv.size(); j++) {
      Node *n = tree.find(iv[j]);
      EXPECT_NE(n, nullptr);
      EXPECT_EQ(n->interval, iv[j]);
    }
  }
}

TEST(Arbalest, AvlSearch) {
  IntervalTree tree{};
  vector<Interval> iv{};
  uptr stepSize = 5;
  uptr gap = 3;
  int i = 0, nodes = 10;
  while (i < nodes) {
    iv.push_back({i * (stepSize + gap), i * (stepSize + gap) + stepSize});
    i++;
  }
  init(tree, iv);
  for (auto &it : iv) {
    Node *n = tree.find({it.left_end + 1, it.right_end - 1});
    EXPECT_NE(n, nullptr);
    EXPECT_EQ(n->interval, it);
  }
}

TEST(Arbalest, AvlRangeSearch) {
  IntervalTree tree{};
  vector<Interval> iv{};
  uptr stepSize = 5;
  uptr gap = 3;
  int i = 0, nodes = 10;
  while (i < nodes) {
    iv.push_back({i * (stepSize + gap), i * (stepSize + gap) + stepSize});
    i++;
  }
  init(tree, iv);
  for (auto &it : iv) {
    Vector<Interval> result{};
    tree.searchRange(result, {it.left_end + 1, it.right_end - 1});
    EXPECT_EQ(result.Size(), 1u);
    EXPECT_EQ(result[0], it);
  }

  Vector<Interval> result{};
  tree.searchRange(result, {0, iv.back().right_end + 5});
  EXPECT_EQ(result.Size(), iv.size());
  for (auto i = 0u; i < iv.size(); i++) {
    EXPECT_EQ(result[i], iv[i]);
  }
}

TEST(Arbalest, AvlRangeDelete) {
  IntervalTree tree{};
  vector<Interval> iv{};
  uptr stepSize = 5;
  uptr gap = 3;
  int i = 0, nodes = 10;
  while (i < nodes) {
    iv.push_back({i * (stepSize + gap), i * (stepSize + gap) + stepSize});
    i++;
  }
  init(tree, iv);
  unsigned begIdx = 2, endIdx = 6;
  Interval range{iv[begIdx].left_end + 1, iv[endIdx].right_end - 1};
  tree.printByHeight(tree.getRoot());
  tree.removeAllNodesWithinRange(range);
  EXPECT_TRUE(tree.satisfyBalanceFactor(tree.getRoot()));
  for (auto i = 0u; i < begIdx; i++) {
    Node *n = tree.find(iv[i]);
    EXPECT_NE(n, nullptr);
    EXPECT_EQ(n->interval, iv[i]);
  }
  for (auto i = endIdx + 1; i < iv.size(); i++) {
    Node *n = tree.find(iv[i]);
    EXPECT_NE(n, nullptr);
    EXPECT_EQ(n->interval, iv[i]);
  }
  printf("range = [%p, %p]\n", reinterpret_cast<char *>(range.left_end),
         reinterpret_cast<char *>(range.right_end));
  tree.printByHeight(tree.getRoot());
  Interval l{iv[begIdx].left_end, range.left_end};
  Interval r{range.right_end, iv[endIdx].right_end};
  Node *n1 = tree.find(l);
  EXPECT_NE(n1, nullptr);
  EXPECT_EQ(n1->interval, l);
  Node *n2 = tree.find(r);
  EXPECT_NE(n2, nullptr);
  EXPECT_EQ(n2->interval, r);
}

TEST(Arbalest, AvlIsOverflow) {
  IntervalTree tree{};
  vector<Interval> iv{};
  uptr stepSize = 5;
  uptr gap = 3;
  int i = 0, nodes = 10;
  while (i < nodes) {
    iv.push_back({i * (stepSize + gap), i * (stepSize + gap) + stepSize});
    i++;
  }
  init(tree, iv);
  for (auto &it : iv) {
    uptr addr = it.left_end;
    for (; addr < it.right_end; addr++) {
      EXPECT_FALSE(tree.isOverflow(it.left_end, addr));
    }
    EXPECT_TRUE(tree.isOverflow(it.left_end, addr));
    EXPECT_TRUE(tree.isOverflow(it.left_end, addr + gap));
    EXPECT_TRUE(tree.isOverflow(it.left_end, addr + gap + 1));
  }
}

TEST(Arbalest, AvlIterator) {
  IntervalTree tree{};
  EXPECT_EQ(tree.begin(), tree.end());
  vector<Interval> iv{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9},
                      {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
  vector<Interval> iv2 = iv;
  sort(iv2.begin(), iv2.end());
  init(tree, iv);
  random_device rd;
  mt19937 g(rd());
  shuffle(iv.begin(), iv.end(), g);
  int idx = 0;
  for (Node *n : tree) {
    EXPECT_EQ(n->interval, iv2[idx++]);
  }
}

TEST(Arbalest, AvlEmptyIterator) {
  IntervalTree tree{};
  int count = 0;
  for (Node *n : tree) {
    count++;
  }
  EXPECT_EQ(count, 0);
}

TEST(Arbalest, VsmSet) {
  RawVsm state[4]{};
  u8 val = static_cast<u8>(VariableStateMachine::kDeviceMask) | static_cast<u8>(VariableStateMachine::kHostMask);
  RawVsm rv = static_cast<RawVsm>(val);
  VsmSet(state, state + 4, rv);
  for (int i = 0; i < 4; i++) {
    RawVsm rv2 = LoadVsm(&state[i]);
    VariableStateMachine vsm(rv2);
    EXPECT_TRUE(vsm.IsDeviceInit());
    EXPECT_TRUE(vsm.IsDeviceLatest());
    EXPECT_TRUE(vsm.IsHostInit());
    EXPECT_TRUE(vsm.IsHostLatest());
  }
}

TEST(Arbalest, VsmMapTo) {
  RawVsm state[8]{};
  RawVsm rv = VariableStateMachine::kHostMask;
  VsmSet(state, state + 8, rv);
  for (int i = 0; i < 8; i++) {
    RawVsm rv2 = LoadVsm(&state[i]);
    VariableStateMachine vsm(rv2);
    EXPECT_FALSE(vsm.IsDeviceInit());
    EXPECT_FALSE(vsm.IsDeviceLatest());
    EXPECT_TRUE(vsm.IsHostInit());
    EXPECT_TRUE(vsm.IsHostLatest());
  }
  VsmUpdateMapTo(state, state + 8);
  for (int i = 0; i < 8; i++) {
    RawVsm rv2 = LoadVsm(&state[i]);
    VariableStateMachine vsm(rv2);
    EXPECT_TRUE(vsm.IsDeviceInit());
    EXPECT_TRUE(vsm.IsDeviceLatest());
    EXPECT_TRUE(vsm.IsHostInit());
    EXPECT_TRUE(vsm.IsHostLatest());
  }
}

TEST(Arbalest, VsmMapFrom) {
  RawVsm state[8]{};
  RawVsm rv = VariableStateMachine::kDeviceMask;
  VsmSet(state, state + 8, rv);
  for (int i = 0; i < 8; i++) {
    RawVsm rv2 = LoadVsm(&state[i]);
    VariableStateMachine vsm(rv2);
    EXPECT_TRUE(vsm.IsDeviceInit());
    EXPECT_TRUE(vsm.IsDeviceLatest());
    EXPECT_FALSE(vsm.IsHostInit());
    EXPECT_FALSE(vsm.IsHostLatest());
  }
  VsmUpdateMapFrom(state, state + 8);
  for (int i = 0; i < 8; i++) {
    RawVsm rv2 = LoadVsm(&state[i]);
    VariableStateMachine vsm(rv2);
    EXPECT_TRUE(vsm.IsDeviceInit());
    EXPECT_TRUE(vsm.IsDeviceLatest());
    EXPECT_TRUE(vsm.IsHostInit());
    EXPECT_TRUE(vsm.IsHostLatest());
  }  
}

}  // namespace __tsan
