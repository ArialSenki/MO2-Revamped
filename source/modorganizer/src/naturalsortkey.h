#ifndef NATURALSORTKEY_H
#define NATURALSORTKEY_H

#include <QCollator>
#include <QCollatorSortKey>
#include <QString>

#include <algorithm>
#include <numeric>
#include <vector>

// MO2 Revamped: the order of MOBase::naturalCompare() (numbers by value, case
// ignored), from one sort key per string. Comparing two keys is a plain
// comparison, while naturalCompare() asks Windows to compare the two strings
// again each time: sorting tens of thousands of names spent most of its time
// there (a big mod's conflicts, a big folder in the Data tab)
inline QCollatorSortKey naturalSortKey(const QString& s)
{
  thread_local const QCollator collator = [] {
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
  }();

  return collator.sortKey(s);
}

// the order in which to put items so their keys are ascending or descending;
// `before(a, b)` can put some items first whatever their keys (folders before
// files), and items with equal keys keep their order
template <class BeforeFn>
std::vector<std::size_t> naturalSortOrder(const std::vector<QCollatorSortKey>& keys,
                                          Qt::SortOrder order, BeforeFn&& before)
{
  std::vector<std::size_t> indices(keys.size());
  std::iota(indices.begin(), indices.end(), std::size_t(0));

  std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
    if (const int first = before(a, b); first != 0) {
      return first < 0;
    }
    const int r = keys[a].compare(keys[b]);
    return order == Qt::AscendingOrder ? r < 0 : r > 0;
  });

  return indices;
}

// puts the items of `v` in the order given by `indices`
template <class T>
void applyOrder(std::vector<T>& v, const std::vector<std::size_t>& indices)
{
  std::vector<T> sorted;
  sorted.reserve(v.size());
  for (std::size_t i : indices) {
    sorted.push_back(std::move(v[i]));
  }
  v = std::move(sorted);
}

#endif  // NATURALSORTKEY_H
