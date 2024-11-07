// SPDX-FileCopyrightText: 2024 The OpenSn Authors <https://open-sn.github.io/opensn/>
// SPDX-License-Identifier: MIT

#include "framework/math/quadratures/spatial/line_quadrature.h"
#include "framework/logging/log_exceptions.h"

namespace opensn
{

LineQuadrature::LineQuadrature(QuadratureOrder order) : SpatialQuadrature(order), range_({0, 0})
{
  auto glq = GaussLegendreQuadrature(order);
  qpoints = glq.qpoints;
  weights = glq.weights;
  range_ = glq.Range();
  SetRange({0, 1});
}

void
LineQuadrature::SetRange(const std::pair<double, double>& range)
{
  const auto& old_range = range_;
  const auto& new_range = range;
  const double h_new = new_range.second - new_range.first;
  const double h_old = old_range.second - old_range.first;

  OpenSnInvalidArgumentIf(h_new <= 0.0 or h_old <= 0.0, "Called with negative or zero ranges.");
  OpenSnInvalidArgumentIf(qpoints.empty(), "Called with no abscissae initialized.");

  const double scale_factor = h_new / h_old;
  for (unsigned int i = 0; i < qpoints.size(); ++i)
  {
    qpoints[i](0) = new_range.first + (qpoints[i][0] - old_range.first) * scale_factor;
    weights[i] *= scale_factor;
  }
  range_ = range;
}

} // namespace opensn
