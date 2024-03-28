#pragma once

#include "framework/math/quadratures/quadrature_order.h"
#include "framework/mesh/mesh.h"
#include "framework/object.h"
#include <vector>

namespace opensn
{

class SpatialQuadrature : public Object
{
protected:
  /**Interval on which the quadrature is defined
   * (relevant for one-dimensional quadratures only).*/
  std::pair<double, double> range_;
  bool verbose_;

  explicit SpatialQuadrature(const InputParameters& params)
    : Object(params),
      range_({0, 0}),
      verbose_(params.GetParamValue<bool>("verbose")),
      order_(static_cast<QuadratureOrder>(params.GetParamValue<int>("order")))
  {
  }

  explicit SpatialQuadrature(QuadratureOrder order) : range_({0, 0}), verbose_(false), order_(order)
  {
  }

public:
  QuadratureOrder order_;
  std::vector<Vector3> qpoints_;
  std::vector<double> weights_;

  static InputParameters GetInputParameters();

  /**Get the range on which the quadrature is defined
   * (relevant for one-dimensional quadratures only).*/
  const std::pair<double, double>& GetRange() const { return range_; }

  /**Set the range on which the quadrature is defined.
   * (relevant for one-dimensional quadratures only).
   * Note that calling this method results in translation
   * of the abscissae and scaling of the weights.*/
  void SetRange(const std::pair<double, double>& range);
};

} // namespace opensn
