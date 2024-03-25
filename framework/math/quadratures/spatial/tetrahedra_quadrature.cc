#include "framework/math/quadratures/spatial/tetrahedra_quadrature.h"
#include <stdexcept>

namespace opensn
{

QuadratureTetrahedron::QuadratureTetrahedron(QuadratureOrder order) : SpatialQuadrature(order)
{
  switch (order)
  {
    case QuadratureOrder::FIRST:
    {
      qpoints_ = { { 2.500000000000000000e-01, 2.500000000000000000e-01, 2.500000000000000000e-01 } };
      weights_ = {   1.666666666666666574e-01 };
      break;
    }
    case QuadratureOrder::SECOND:
    {
      qpoints_ = { { 1.800296935103653517e-01, 3.653145188146345590e-01, 6.923235573627466513e-03 },
                   { 1.559331204991860620e-01, 4.574615870855954780e-01, 3.817653560693466952e-01 },
                   { 2.160764291848478180e-01, 3.755150287292619016e-04, 4.307017070778361156e-01 },
                   { 8.215725409676197799e-01, 1.236668003284584066e-01, 3.993304864149841565e-02 } };
      weights_ = {   5.008682322282933391e-02,
                     4.646292944776136968e-02,
                     5.318232258357916825e-02,
                     1.693459141249678557e-02 };
      break;
    }
    case QuadratureOrder::THIRD:
    {
      qpoints_ = { { 1.901170024392839220e-01, 4.398589476492750228e-01, 1.140332944455716910e-02 },
                   { 1.586851632274405843e-01, 1.248048621652471935e-01, 5.856628056552157791e-01 },
                   { 1.090521221118923023e-02, 3.454441557197306945e-01, 2.815238021235462185e-01 },
                   { 5.712260521491151488e-01, 1.414827519695044944e-01, 1.469183900871695869e-01 },
                   { 1.708169251649890030e-01, 3.787163178235702921e-02, 1.528181430909273386e-01 },
                   { 1.620014916985244580e-01, 6.414297914956963442e-01, 1.838503504920977472e-01 } };
      weights_ = {   2.209467119074086730e-02,
                     3.740252781959289147e-02,
                     2.134440211845781490e-02,
                     4.200066346825037655e-02,
                     2.343740161006720515e-02,
                     2.038700045955750897e-02 };
      break;
    }
    case QuadratureOrder::FOURTH:
    {
      qpoints_ = { { 9.720464458758329429e-02, 1.066041725619936154e-01, 6.843904154530400241e-01 },
                   { 2.956949520647961238e-02, 3.292329597426468801e-01, 3.179035602133946092e-01 },
                   { 4.327102390477685634e-01, 1.038441164109931564e-01, 3.538232392092970913e-01 },
                   { 2.402766649280726197e-01, 3.044484024344968898e-01, 1.268017259153920162e-01 },
                   { 1.294113737889104054e-01, 5.380072039161857278e-01, 3.301904148374644743e-01 },
                   { 1.215419913339277902e-01, 8.991260093335770587e-03, 3.064939884296902783e-01 },
                   { 4.507658760912768292e-01, 4.329534904813556184e-01, 5.945661629943382875e-02 },
                   { 4.192663138795130195e-01, 5.334123953574518295e-02, 4.778143555908666296e-02 },
                   { 6.722329489338339792e-02, 7.412288820936225875e-01, 3.518392977359871554e-02 },
                   { 7.525085070096549922e-01, 8.140491840285923875e-02, 6.809937093820665754e-02 },
                   { 4.049050672759042790e-02, 1.746940586972305920e-01, 1.356070187980288125e-02 } };
      weights_ = {   1.774467235924835282e-02,
                     1.837237207141627707e-02,
                     2.582935266937435095e-02,
                     3.223513534160574873e-02,
                     1.269378587425972621e-02,
                     1.323778001133755350e-02,
                     1.157832765627256344e-02,
                     9.988864191093255879e-03,
                     9.232299811929396346e-03,
                     9.212228192656150838e-03,
                     6.541848487473327610e-03 };
      break;
    }
    default:
    {
      throw std::invalid_argument(std::string(__FUNCTION__) + " Invalid tetrahedral quadrature order");
    }
  } // switch order
}

} // namespace opensn
