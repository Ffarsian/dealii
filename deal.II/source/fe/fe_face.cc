// ---------------------------------------------------------------------
// $Id$
//
// Copyright (C) 2009 - 2013 by the deal.II authors
//
// This file is part of the deal.II library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the deal.II distribution.
//
// ---------------------------------------------------------------------


#include <deal.II/fe/fe_face.h>
#include <deal.II/fe/fe_poly_face.templates.h>
#include <deal.II/fe/fe_nothing.h>
#include <deal.II/base/quadrature_lib.h>
#include <sstream>

DEAL_II_NAMESPACE_OPEN


namespace
{
  std::vector<Point<1> >
  get_QGaussLobatto_points (const unsigned int degree)
  {
    if (degree > 0)
      {
        QGaussLobatto<1> quad(degree+1);
        return quad.get_points();
      }
    else
      return std::vector<Point<1> >(1, Point<1>(0.5));
  }
}

template <int dim, int spacedim>
FE_FaceQ<dim,spacedim>::FE_FaceQ (const unsigned int degree)
  :
  FE_PolyFace<TensorProductPolynomials<dim-1>, dim, spacedim> (
    TensorProductPolynomials<dim-1>(Polynomials::generate_complete_Lagrange_basis(get_QGaussLobatto_points(degree))),
    FiniteElementData<dim>(get_dpo_vector(degree), 1, degree, FiniteElementData<dim>::L2),
    std::vector<bool>(1,true))
{
  // initialize unit face support points
  const unsigned int codim = dim-1;
  this->unit_face_support_points.resize(Utilities::fixed_power<codim>(this->degree+1));

  if (this->degree == 0)
    for (unsigned int d=0; d<codim; ++d)
      this->unit_face_support_points[0][d] = 0.5;
  else
    {
      const double step = 1./this->degree;
      Point<codim> p;
      std::vector<Point<1> > points = get_QGaussLobatto_points(degree);

      unsigned int k=0;
      for (unsigned int iz=0; iz <= ((codim>2) ? this->degree : 0) ; ++iz)
        for (unsigned int iy=0; iy <= ((codim>1) ? this->degree : 0) ; ++iy)
          for (unsigned int ix=0; ix<=this->degree; ++ix)
            {
              p(0) = points[ix][0];
              if (codim>1)
                p(1) = points[iy][0];
              if (codim>2)
                p(2) = points[iz][0];

              this->unit_face_support_points[k++] = p;
            }
      AssertDimension (k, this->unit_face_support_points.size());
    }

  // initialize unit support points
  this->unit_support_points.resize(GeometryInfo<dim>::faces_per_cell*
                                   this->unit_face_support_points.size());
  const unsigned int n_face_dofs = this->unit_face_support_points.size();
  for (unsigned int i=0; i<n_face_dofs; ++i)
    for (unsigned int d=0; d<dim; ++d)
      {
        for (unsigned int e=0, c=0; e<dim; ++e)
          if (d!=e)
            {
              this->unit_support_points[n_face_dofs*2*d+i][e] =
                this->unit_face_support_points[i][c];
              this->unit_support_points[n_face_dofs*(2*d+1)+i][e] =
                this->unit_face_support_points[i][c];
              this->unit_support_points[n_face_dofs*(2*d+1)+i][d] = 1;
              ++c;
            }
      }
}


template <int dim, int spacedim>
FiniteElement<dim,spacedim> *
FE_FaceQ<dim,spacedim>::clone() const
{
  return new FE_FaceQ<dim,spacedim>(this->degree);
}


template <int dim, int spacedim>
std::string
FE_FaceQ<dim,spacedim>::get_name () const
{
  // note that the
  // FETools::get_fe_from_name
  // function depends on the
  // particular format of the string
  // this function returns, so they
  // have to be kept in synch

  std::ostringstream namebuf;
  namebuf << "FE_FaceQ<" << dim << ">(" << this->degree << ")";

  return namebuf.str();
}



template <int dim, int spacedim>
void
FE_FaceQ<dim,spacedim>::
get_face_interpolation_matrix (const FiniteElement<dim,spacedim> &x_source_fe,
                               FullMatrix<double>       &interpolation_matrix) const
{
  // this function is similar to the respective method in FE_Q

  // this is only implemented, if the source FE is also a FE_FaceQ element
  AssertThrow ((dynamic_cast<const FE_FaceQ<dim,spacedim> *>(&x_source_fe) != 0),
               (typename FiniteElement<dim,spacedim>::
                ExcInterpolationNotImplemented()));

  Assert (interpolation_matrix.n() == this->dofs_per_face,
          ExcDimensionMismatch (interpolation_matrix.n(),
                                this->dofs_per_face));
  Assert (interpolation_matrix.m() == x_source_fe.dofs_per_face,
          ExcDimensionMismatch (interpolation_matrix.m(),
                                x_source_fe.dofs_per_face));

  // ok, source is a FaceQ element, so we will be able to do the work
  const FE_FaceQ<dim,spacedim> &source_fe
    = dynamic_cast<const FE_FaceQ<dim,spacedim>&>(x_source_fe);

  // Make sure that the element for which the DoFs should be constrained is
  // the one with the higher polynomial degree.  Actually the procedure will
  // work also if this assertion is not satisfied. But the matrices produced
  // in that case might lead to problems in the hp procedures, which use this
  // method.
  Assert (this->dofs_per_face <= source_fe.dofs_per_face,
          (typename FiniteElement<dim,spacedim>::
           ExcInterpolationNotImplemented ()));

  // generate a quadrature with the unit face support points. 
  Quadrature<dim-1> face_quadrature (source_fe.get_unit_face_support_points ());

  // Rule of thumb for FP accuracy, that can be expected for a given
  // polynomial degree.  This value is used to cut off values close to zero.
  const double eps = 2e-13*this->degree*(dim-1);

  // compute the interpolation matrix by simply taking the value at the
  // support points.
  for (unsigned int i=0; i<source_fe.dofs_per_face; ++i)
    {
      const Point<dim-1> &p = face_quadrature.point (i);

      for (unsigned int j=0; j<this->dofs_per_face; ++j)
        {
          double matrix_entry = this->poly_space.compute_value (j, p);

          // Correct the interpolated value. I.e. if it is close to 1 or 0,
          // make it exactly 1 or 0. Unfortunately, this is required to avoid
          // problems with higher order elements.
          if (std::fabs (matrix_entry - 1.0) < eps)
            matrix_entry = 1.0;
          if (std::fabs (matrix_entry) < eps)
            matrix_entry = 0.0;

          interpolation_matrix(i,j) = matrix_entry;
        }
    }

  // make sure that the row sum of each of the matrices is 1 at this
  // point. this must be so since the shape functions sum up to 1
  for (unsigned int j=0; j<source_fe.dofs_per_face; ++j)
    {
      double sum = 0.;

      for (unsigned int i=0; i<this->dofs_per_face; ++i)
        sum += interpolation_matrix(j,i);

      Assert (std::fabs(sum-1) < 2e-13*this->degree*(dim-1),
              ExcInternalError());
    }
}



template <int dim, int spacedim>
void
FE_FaceQ<dim,spacedim>::
get_subface_interpolation_matrix (const FiniteElement<dim,spacedim> &x_source_fe,
                                  const unsigned int        subface,
                                  FullMatrix<double>       &interpolation_matrix) const
{
  // this function is similar to the respective method in FE_Q

  Assert (interpolation_matrix.n() == this->dofs_per_face,
          ExcDimensionMismatch (interpolation_matrix.n(),
                                this->dofs_per_face));
  Assert (interpolation_matrix.m() == x_source_fe.dofs_per_face,
          ExcDimensionMismatch (interpolation_matrix.m(),
                                x_source_fe.dofs_per_face));

  // see if source is a FaceQ element
  if (const FE_FaceQ<dim,spacedim> *source_fe
      = dynamic_cast<const FE_FaceQ<dim,spacedim> *>(&x_source_fe))
    {

      // Make sure that the element for which the DoFs should be constrained
      // is the one with the higher polynomial degree.  Actually the procedure
      // will work also if this assertion is not satisfied. But the matrices
      // produced in that case might lead to problems in the hp procedures,
      // which use this method.
      Assert (this->dofs_per_face <= source_fe->dofs_per_face,
              (typename FiniteElement<dim,spacedim>::
               ExcInterpolationNotImplemented ()));

      // generate a quadrature with the unit face support points. 
      Quadrature<dim-1> face_quadrature (source_fe->get_unit_face_support_points ());

      // Rule of thumb for FP accuracy, that can be expected for a given
      // polynomial degree.  This value is used to cut off values close to
      // zero.
      const double eps = 2e-13*this->degree*(dim-1);

      // compute the interpolation matrix by simply taking the value at the
      // support points.
      for (unsigned int i=0; i<source_fe->dofs_per_face; ++i)
        {
          const Point<dim-1> p =
            GeometryInfo<dim-1>::child_to_cell_coordinates (face_quadrature.point(i),
                                                            subface);

          for (unsigned int j=0; j<this->dofs_per_face; ++j)
            {
              double matrix_entry = this->poly_space.compute_value (j, p);

              // Correct the interpolated value. I.e. if it is close to 1 or 0,
              // make it exactly 1 or 0. Unfortunately, this is required to avoid
              // problems with higher order elements.
              if (std::fabs (matrix_entry - 1.0) < eps)
                matrix_entry = 1.0;
              if (std::fabs (matrix_entry) < eps)
                matrix_entry = 0.0;

              interpolation_matrix(i,j) = matrix_entry;
            }
        }

      // make sure that the row sum of each of the matrices is 1 at this
      // point. this must be so since the shape functions sum up to 1
      for (unsigned int j=0; j<source_fe->dofs_per_face; ++j)
        {
          double sum = 0.;

          for (unsigned int i=0; i<this->dofs_per_face; ++i)
            sum += interpolation_matrix(j,i);

          Assert (std::fabs(sum-1) < 2e-13*this->degree*(dim-1),
                  ExcInternalError());
        }
    }
  else if (dynamic_cast<const FE_Nothing<dim> *>(&x_source_fe) != 0)
    {
      // nothing to do here, the FE_Nothing has no degrees of freedom anyway
    }
  else
    AssertThrow (false,(typename FiniteElement<dim,spacedim>::
                        ExcInterpolationNotImplemented()));
}



template <int dim, int spacedim>
bool
FE_FaceQ<dim,spacedim>::has_support_on_face (
  const unsigned int shape_index,
  const unsigned int face_index) const
{
  return (face_index == (shape_index/this->dofs_per_face));
}



template <int dim, int spacedim>
std::vector<unsigned int>
FE_FaceQ<dim,spacedim>::get_dpo_vector (const unsigned int deg)
{
  std::vector<unsigned int> dpo(dim+1, 0U);
  dpo[dim-1] = deg+1;
  for (unsigned int i=1; i<dim-1; ++i)
    dpo[dim-1] *= deg+1;
  return dpo;
}



template <int dim, int spacedim>
bool
FE_FaceQ<dim,spacedim>::hp_constraints_are_implemented () const
{
  return true;
}



template <int dim, int spacedim>
FiniteElementDomination::Domination
FE_FaceQ<dim,spacedim>::
compare_for_face_domination (const FiniteElement<dim,spacedim> &fe_other) const
{
  if (const FE_FaceQ<dim,spacedim> *fe_q_other
      = dynamic_cast<const FE_FaceQ<dim,spacedim>*>(&fe_other))
    {
      if (this->degree < fe_q_other->degree)
        return FiniteElementDomination::this_element_dominates;
      else if (this->degree == fe_q_other->degree)
        return FiniteElementDomination::either_element_can_dominate;
      else
        return FiniteElementDomination::other_element_dominates;
    }
  else if (dynamic_cast<const FE_Nothing<dim>*>(&fe_other) != 0)
    {
      // the FE_Nothing has no degrees of freedom and it is typically used in
      // a context where we don't require any continuity along the interface
      return FiniteElementDomination::no_requirements;
    }

  Assert (false, ExcNotImplemented());
  return FiniteElementDomination::neither_element_dominates;
}

// explicit instantiations
#include "fe_face.inst"


DEAL_II_NAMESPACE_CLOSE
