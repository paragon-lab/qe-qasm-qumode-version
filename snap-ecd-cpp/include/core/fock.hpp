#pragma once

#include <cmath>

#include <Eigen/Dense>

namespace core {

inline Eigen::MatrixXd annihilation(int n) {
    Eigen::MatrixXd a = Eigen::MatrixXd::Zero(n, n);
    for (int k = 1; k < n; ++k) a(k - 1, k) = std::sqrt(static_cast<double>(k));
    return a;
}

}
