#pragma once

#include "framework/math/quadratures/angular/angular_quadrature.h"
#include "framework/math/quadratures/gausslegendre_quadrature.h"
#include "framework/math/dynamic_vector.h"
#include "framework/math/dynamic_matrix.h"
#include "framework/mesh/mesh.h"
#include "framework/math/math.h"
#include <vector>
#include <array>

namespace opensn
{
namespace SimplifiedLDFESQ
{
struct SphericalQuadrilateral;
struct FUNCTION_WEIGHT_FROM_RHO;
class Quadrature;

/**Base Functor to inherit from to change the function
 * to integrate in one of the integration utilities.*/
struct BaseFunctor
{
  virtual double operator()(double mu, double eta, double xi) { return 0.0; }
};
} // namespace SimplifiedLDFESQ

/**Serves as a general data structure for a
 * spherical quadrilateral (SQ).*/
struct SimplifiedLDFESQ::SphericalQuadrilateral
{
  std::array<Vertex, 4> vertices_xy_tilde;  ///< On square
  std::array<Vertex, 4> vertices_xyz_prime; ///< On cube face
  std::array<Vertex, 4> vertices_xyz;       ///< On unit sphere
  Vertex centroid_xyz;

  Matrix3x3 rotation_matrix;
  Vector3 translation_vector;

  std::array<Vector3, 4> sub_sqr_points;
  std::array<double, 4> sub_sqr_weights;

  double area = 0.0;

  Vector3 octant_modifier;
};

/** Piecewise-linear Finite element quadrature using quadrilaterals.*/
class SimplifiedLDFESQ::Quadrature : public AngularQuadrature
{
public:
  enum class QuadraturePointOptimization
  {
    CENTROID,
    EMPIRICAL,
    ISOLATED,
    //    MULTI_VARIATE_SECANT
  };
  QuadraturePointOptimization qp_optimization_type_ = QuadraturePointOptimization::EMPIRICAL;
  std::string output_filename_prefix_;

private:
  static constexpr double a = 0.57735026919; ///< Inscribed cude side length
  int initial_level_ = 0;
  std::vector<Vector3> diagonal_vertices_;
  std::vector<SphericalQuadrilateral> initial_octant_SQs_;

public:
  std::vector<SphericalQuadrilateral> deployed_SQs_;

private:
  std::vector<std::vector<SphericalQuadrilateral>> deployed_SQs_history_;

public:
  friend struct FUNCTION_WEIGHT_FROM_RHO;
  Quadrature() : AngularQuadrature(AngularQuadratureType::SLDFESQ) {}

  virtual ~Quadrature() {}

  /**
   * Generates uniform spherical quadrilaterals from the subdivision of an inscribed cube.
   */
  void GenerateInitialRefinement(int level);

private:
  /**
   * Generates diagonal spacings.
   */
  void GenerateDiagonalSpacings(int level);

  /**
   * Generates the standard points on the reference face.
   */
  void GenerateReferenceFaceVertices(const Matrix3x3& rotation_matrix,
                                     const Vector3& translation,
                                     int level);

  /**
   * Develops LDFE quantities.
   */
  void DevelopSQLDFEValues(SphericalQuadrilateral& sq, QuadratureGaussLegendre& legendre);

  /**
   * Applies empirical quadrature point optimization.
   */
  void EmpiricalQPOptimization(SphericalQuadrilateral& sq,
                               QuadratureGaussLegendre& legendre,
                               Vertex& sq_xy_tilde_centroid,
                               std::array<Vector3, 4>& radii_vectors_xy_tilde,
                               std::array<double, 4>& sub_sub_sqr_areas);

  void IsolatedQPOptimization(SphericalQuadrilateral& sq,
                              QuadratureGaussLegendre& legendre,
                              Vertex& sq_xy_tilde_centroid,
                              std::array<Vector3, 4>& radii_vectors_xy_tilde,
                              std::array<double, 4>& sub_sub_sqr_areas);

  /**
   * Computes the area of a cell. This routine uses Girard's theorem to get the area of a spherical
   * triangle using the spherical excess.
   */
  static double ComputeSphericalQuadrilateralArea(std::array<Vertex, 4>& vertices_xyz);

  /**
   * Integrates shape functions to produce weights.
   */
  static std::array<double, 4>
  IntegrateLDFEShapeFunctions(const SphericalQuadrilateral& sq,
                              std::array<DynamicVector<double>, 4>& shape_coeffs,
                              const std::vector<Vector3>& legendre_qpoints,
                              const std::vector<double>& legendre_qweights);

  /**
   * Deploys the current set of SQs to all octants.
   */
  void CopyToAllOctants();

  /**
   * Populates the quadrature abscissaes, weights and direction vectors.
   */
  void PopulateQuadratureAbscissae();

private:
  /**
   * Performs a simple Riemann integral of a base functor.
   */
  double RiemannIntegral(BaseFunctor* F, int Ni = 20000);

  /**
   * Performs a quadrature integral of a base functor using the
   * supplied SQs.
   */
  double QuadratureSSIntegral(BaseFunctor* F);

public:
  /**
   * Performs a test integration of predefined cases.
   */
  void TestIntegration(int test_case, double ref_solution, int RiemannN = 0);

public:
  /**
   * Prints the quadrature to file.
   */
  void PrintQuadratureToFile();

public:
  /**
   * Locally refines the cells.
   */
  void LocallyRefine(const Vector3& ref_dir,
                     const double cone_size,
                     const bool dir_as_plane_normal = false);

private:
  /**
   * Split a SQ.
   */
  std::array<SphericalQuadrilateral, 4> SplitSQ(SphericalQuadrilateral& sq,
                                                QuadratureGaussLegendre& legendre);
};

/**This is a utility function that encapsulates
 * all the necessary functionality to determine shape
 * function coefficients and integrate accross a
 * spherical quadrilateral.*/
struct SimplifiedLDFESQ::FUNCTION_WEIGHT_FROM_RHO
{
  Quadrature& sldfesq;
  Vertex& centroid_xy_tilde;
  std::array<Vector3, 4>& radii_vectors_xy_tilde;
  SphericalQuadrilateral& sq;

  std::array<DynamicVector<double>, 4> rhs;
  DynamicMatrix<double> A;
  DynamicMatrix<double> A_inv;
  std::array<DynamicVector<double>, 4> c_coeffs;
  /// Legendre quadrature points
  std::vector<Vector3>& lqp;
  /// Legendre quadrature weights
  std::vector<double>& lqw;

  FUNCTION_WEIGHT_FROM_RHO(SimplifiedLDFESQ::Quadrature& sldfesq,
                           Vertex& centroid_xy_tilde,
                           std::array<Vector3, 4>& radii_vectors_xy_tilde,
                           SphericalQuadrilateral& sq,
                           QuadratureGaussLegendre& legendre_quadrature)
    : sldfesq(sldfesq),
      centroid_xy_tilde(centroid_xy_tilde),
      radii_vectors_xy_tilde(radii_vectors_xy_tilde),
      sq(sq),
      A(4, 4),
      A_inv(4, 4),
      lqp(legendre_quadrature.qpoints_),
      lqw(legendre_quadrature.weights_)
  {
    // Init RHS
    for (int i = 0; i < 4; ++i)
    {
      rhs[i] = std::vector<double>(4);
      c_coeffs[i] = std::vector<double>(4);
      for (int j = 0; j < 4; ++j)
        rhs[i][i] = 1.0;
    }
  }

  /**Computes the quadrature point locations
   * from rho, followed by the shape-function coefficients and
   * then the integral of the shape function to get the weights.*/
  std::array<double, 4> operator()(const DynamicVector<double>& rho)
  {
    // Determine qpoints from rho
    std::array<Vector3, 4> qpoints;
    for (int i = 0; i < 4; ++i)
    {
      auto xy_tilde = centroid_xy_tilde + rho[i] * radii_vectors_xy_tilde[i];
      auto xyz_prime = sq.rotation_matrix * xy_tilde + sq.translation_vector;
      qpoints[i] = xyz_prime.Normalized();
    }

    // Assemble A
    for (int i = 0; i < 4; ++i)
      A[i] = {1.0, qpoints[i][0], qpoints[i][1], qpoints[i][2]};

    // Compute A-inverse
    A_inv = Inverse(A.elements_);

    // Compute coefficients
    for (int i = 0; i < 4; ++i)
      c_coeffs[i] = A_inv * rhs[i];

    return sldfesq.IntegrateLDFEShapeFunctions(sq, c_coeffs, lqp, lqw);
  }
};

} // namespace opensn
