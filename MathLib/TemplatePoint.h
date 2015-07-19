/**
 * \file
 * \author Thomas Fischer
 * \date   2010-01-28
 * \brief  Definition of the TemplatePoint class.
 *
 * \copyright
 * Copyright (c) 2012-2015, OpenGeoSys Community (http://www.opengeosys.org)
 *            Distributed under a Modified BSD License.
 *              See accompanying file LICENSE.txt or
 *              http://www.opengeosys.org/project/license
 *
 */

#ifndef TEMPLATEPOINT_H_
#define TEMPLATEPOINT_H_

// STL
#include <array>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <iostream>

namespace MathLib
{
/**
 * \ingroup GeoLib
 *
 * \brief class-template for points can be instantiated by a numeric type.
 * \tparam T the coordinate type
 */
template <typename T, std::size_t DIM = 3> class TemplatePoint
{
public:
	typedef T FP_T;

	/** default constructor with zero coordinates */
	TemplatePoint();

	/** constructor - constructs a TemplatePoint object
	 *
	 * @param x std::array containing the coordinates of the point
	 */
	explicit TemplatePoint(std::array<T,DIM> const& x);

	/** virtual destructor */
	virtual ~TemplatePoint() {}

	/** \brief const access operator
	 *  The access to the point coordinates is like the access to a field. Code example:
	 * \code
	 * Point<double> point (1.0, 2.0, 3.0);
	 * double sqrNrm2 = point[0] * point[0] + point[1] * point[1] + point[2] + point[2];
	 * \endcode
	 */
	const T& operator[] (std::size_t idx) const
	{
		assert (idx < DIM);
		return _x[idx];
	}
	/** \brief access operator (see book Effektiv C++ programmieren - subsection 1.3.2 ).
	 * \sa const T& operator[] (std::size_t idx) const
	 */
	T& operator[] (std::size_t idx)
	{
		return const_cast<T&> (static_cast<const TemplatePoint&> (*this)[idx]);
	}

	/** returns an array containing the coordinates of the point */
	const T* getCoords () const
	{
		return _x.data();
	}

	/** write point coordinates into stream (used from operator<<)
	 * \param os a standard output stream
	 */
	virtual void write (std::ostream &os) const
	{
		std::copy(_x.cbegin(), _x.cend(), std::ostream_iterator<T>(os, " "));
	}

	/** read point coordinates into stream (used from operator>>) */
	virtual void read (std::istream &is)
	{
		std::copy(std::istream_iterator<T>(is), std::istream_iterator<T>(), _x.begin());
	}

protected:
	std::array<T, DIM> _x;
};

template <typename T, std::size_t DIM>
TemplatePoint<T,DIM>::TemplatePoint() :
	_x({{0}})
{}

template <typename T, std::size_t DIM>
TemplatePoint<T,DIM>::TemplatePoint(std::array<T,DIM> const& x) :
	_x(x)
{}

/** Equality of TemplatePoint's up to an epsilon.
 */
template <typename T, std::size_t DIM>
bool operator==(TemplatePoint<T,DIM> const& a, TemplatePoint<T,DIM> const& b)
{
	T const sqr_dist(sqrDist(a,b));
	auto const eps = std::numeric_limits<T>::epsilon();
	return (sqr_dist < eps*eps);
}

template <typename T, std::size_t DIM>
bool operator< (TemplatePoint<T,DIM> const& a, TemplatePoint<T,DIM> const& b)
{
	for (std::size_t i = 0; i < DIM; ++i)
	{
		if (a[i] > b[i]) {
			return false;
		} else {
			if (a[i] < b[i]) {
				return true;
			}
		}
		// continue with next dimension, because a[0] == b[0]
	}

	// The values in all dimenisions are equal.
	return false;
}

/**
 * lexicographical comparison of points taking an epsilon into account
 * @param p0 first input Point3d
 * @param p1 second input Point3d
 * @param eps tolerance (if in the comparison operation the property abs(p0[k] - p1[k]) < eps
 *     holds for the k-th coordinate the points are assumed the be equal in this coordinate)
 * @return true, if p0 is lexicographically smaller than p1
 */
template <typename T, std::size_t DIM>
bool lessEq(TemplatePoint<T, DIM> const& a, TemplatePoint<T, DIM> const& b,
		double eps = std::numeric_limits<double>::epsilon())
{
	// test a relative and an absolute criterion
	if ((std::fabs(a[0]-b[0]) > eps * std::min(std::fabs(b[0]), std::fabs(a[0])) && std::fabs(a[0]-b[0]) > eps) {
		if (a[0] <= b[0])
			return true;
		else
			return false;
	} else {
		// assume a[0] == b[0]
		if (std::fabs (a[1]-b[1]) > eps * std::fabs(a[1]) && std::fabs(a[1]-b[1]) > eps) {
			if (a[1] <= b[1])
				return true;
			else
				return false;
		} else {
			// assume a[1] == b[1] and a[0] == b[0]
			if (std::fabs (a[2]-b[2]) > eps * std::fabs(a[2]) && std::fabs(a[2]-b[2]) > eps) {
				if (a[2] <= b[2])
					return true;
				else
					return false;
			} else {
				return true;
			}
		}
	}
}

/** Distance between points p0 and p1 in the maximum norm. */
template <typename T>
T maxNormDist(const MathLib::TemplatePoint<T>* p0, const MathLib::TemplatePoint<T>* p1)
{
	const T x = fabs((*p1)[0] - (*p0)[0]);
	const T y = fabs((*p1)[1] - (*p0)[1]);
	const T z = fabs((*p1)[2] - (*p0)[2]);

	return std::max(x, std::max(y, z));
}

/** overload the output operator for class Point */
template <typename T, std::size_t DIM>
std::ostream& operator<< (std::ostream &os, const TemplatePoint<T,DIM> &p)
{
	p.write (os);
	return os;
}

/** overload the input operator for class Point */
template <typename T, std::size_t DIM>
std::istream& operator>> (std::istream &is, TemplatePoint<T,DIM> &p)
{
	p.read (is);
	return is;
}
} // end namespace MathLib

#endif /* TEMPLATEPOINT_H_ */
