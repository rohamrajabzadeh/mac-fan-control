// history.h — fixed-capacity ring buffer of timestamped samples for many
// named series (temperatures, fan RPM, CPU, memory). Logical index 0 is the
// oldest sample, size()-1 the newest. Missing values are NaN.
#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <cmath>
#include <limits>

class History {
public:
    explicit History(int capacity = 3600) : cap_(capacity) { times_.resize(cap_); }

    void push(qint64 tMs, const QHash<QString, float> &values) {
        int pos;
        if (count_ < cap_) { pos = (start_ + count_) % cap_; ++count_; }
        else               { pos = start_; start_ = (start_ + 1) % cap_; }
        times_[pos] = tMs;
        for (auto it = series_.begin(); it != series_.end(); ++it)
            (*it)[pos] = nan();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            auto s = series_.find(it.key());
            if (s == series_.end())
                s = series_.insert(it.key(), QVector<float>(cap_, nan()));
            (*s)[pos] = it.value();
        }
    }

    int size() const { return count_; }
    bool isEmpty() const { return count_ == 0; }
    qint64 time(int i) const { return times_[phys(i)]; }
    qint64 latestTime() const { return count_ ? time(count_ - 1) : 0; }

    float value(const QString &id, int i) const {
        auto it = series_.constFind(id);
        if (it == series_.constEnd() || i < 0 || i >= count_) return nan();
        return (*it)[phys(i)];
    }
    bool has(const QString &id) const { return series_.contains(id); }

    // First logical index whose timestamp is >= t (binary search).
    int firstIndexSince(qint64 t) const {
        int lo = 0, hi = count_;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (time(mid) < t) lo = mid + 1; else hi = mid;
        }
        return lo;
    }

    static float nan() { return std::numeric_limits<float>::quiet_NaN(); }

private:
    int phys(int i) const { return (start_ + i) % cap_; }

    int cap_;
    int start_ = 0;
    int count_ = 0;
    QVector<qint64> times_;
    QHash<QString, QVector<float>> series_;
};
