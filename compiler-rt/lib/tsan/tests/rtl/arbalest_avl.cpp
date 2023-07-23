#include "tsan_avltree.h"
#include "tsan_test_util.h"
#include "gtest/gtest.h"
#include <vector>
using namespace std;

static void init(IntervalTree &tree, vector<Interval> &iv) {
    for (auto &i : iv) {
        tree.insert(i, {i.left_end, i.right_end - i.left_end});
    }
}

TEST(Arbalest, AvlBasic) {
  IntervalTree tree{};
  vector<Interval> iv{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, 
              {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
  init(tree, iv);
  for (auto &i : iv) {
    Node *n = tree.find(i);
    EXPECT_EQ(n->interval, i);
  }
}




