/**
 * \class StHelix
 * \author Thomas Ullrich, Sep 26 1997
 *
 * Parametrization of a helix. Can also cope with straight tracks, i.e.
 * with zero curvature. This represents only the mathematical model of
 * a helix. See the SCL user guide for more.
 */

#ifndef ST_HELIX_HH
#define ST_HELIX_HH

#include <cmath>
#include <ostream>
#include <utility>
#include <algorithm>
#include "particles/StThreeVector.hh"

class StHelix
{
 public:
  /// curvature, dip angle, phase, origin, h
  StHelix(double c, double dip, double phase,
          const StThreeVector<double> &o, int h = -1);

  virtual ~StHelix();
  // StHelix(const StHelix&);			// use default
  // StHelix& operator=(const StHelix&);	// use default

  double       dipAngle()   const;
  double       curvature()  const;	/// 1/R in xy-plane
  double       phase()      const;	/// aziumth in xy-plane measured from ring center
  double       xcenter()    const;	/// x-center of circle in xy-plane
  double       ycenter()    const;	/// y-center of circle in xy-plane
  int          h()          const;	/// -sign(q*B);

  const StThreeVector<double> &origin() const;	/// starting point

  void setParameters(double c, double dip, double phase, const StThreeVector<double> &o, int h);

  /// coordinates of helix at point s
  double       x(double s)  const;
  double       y(double s)  const;
  double       z(double s)  const;

  StThreeVector<double>  at(double s) const;

  /// pointing vector of helix at point s
  double       cx(double s)  const;
  double       cy(double s)  const;
  double       cz(double s = 0)  const;

  StThreeVector<double>  cat(double s) const;

  /// returns period length of helix
  double       period()       const;

  /// path length at given r (cylindrical r)
  std::pair<double, double> pathLength(double r)   const;

  /// path length at given r (cylindrical r, cylinder axis at x,y)
  std::pair<double, double> pathLength(double r, double x, double y);

  /// path length at distance of closest approach to a given point
  double       pathLength(const StThreeVector<double> &p, bool scanPeriods = true) const;

  /// path length at intersection with plane
  double       pathLength(const StThreeVector<double> &r,
                          const StThreeVector<double> &n) const;

  /// path length at distance of closest approach in the xy-plane to a given point
  double       pathLength(double x, double y) const;

  /// path lengths in centimeters at dca between two helices
  std::pair<double, double> pathLengths(const StHelix &,
                                        double minStepSize = 1.e-3,
                                        double minRange = 10) const;

  /// minimal distance between point and helix
  double       distance(const StThreeVector<double> &p, bool scanPeriods = true) const;

  /// checks for valid parametrization
  bool         valid(double world = 1.e+5) const {return !bad(world);}
  int            bad(double world = 1.e+5) const;

  /// move the origin along the helix to s which becomes then s=0
  virtual void moveOrigin(double s);

  static const double NoSolution;

 protected:
  StHelix();

  void setCurvature(double);	/// performs also various checks
  void setPhase(double);
  void setDipAngle(double);

  /// value of S where distance in x-y plane is minimal
  double fudgePathLength(const StThreeVector<double> &) const;

 protected:
  bool                   mSingularity;	// true for straight line case (B=0)
  StThreeVector<double>  mOrigin;
  double                 mDipAngle;
  double                 mCurvature;
  double                 mPhase;
  int                    mH;			// -sign(q*B);

  double                 mCosDipAngle;
  double                 mSinDipAngle;
  double                 mCosPhase;
  double                 mSinPhase;
};

//
//     Non-member functions
//
int operator== (const StHelix &, const StHelix &);
int operator!= (const StHelix &, const StHelix &);
std::ostream &operator<<(std::ostream &, const StHelix &);

//
//     Inline functions
//
inline int StHelix::h() const {return mH;}

inline double StHelix::dipAngle() const {return mDipAngle;}

inline double StHelix::curvature() const {return mCurvature;}

inline double StHelix::phase() const {return mPhase;}

inline double StHelix::x(double s) const
{
  if (mSingularity)
    return mOrigin.x() - s * mCosDipAngle * mSinPhase;
  else
    return mOrigin.x() + (cos(mPhase + s * mH * mCurvature * mCosDipAngle) - mCosPhase) / mCurvature;
}

inline double StHelix::y(double s) const
{
  if (mSingularity)
    return mOrigin.y() + s * mCosDipAngle * mCosPhase;
  else
    return mOrigin.y() + (sin(mPhase + s * mH * mCurvature * mCosDipAngle) - mSinPhase) / mCurvature;
}

inline double StHelix::z(double s) const
{
  return mOrigin.z() + s * mSinDipAngle;
}

inline double StHelix::cx(double s)  const
{
  if (mSingularity)
    return -mCosDipAngle * mSinPhase;
  else
    return -sin(mPhase + s * mH * mCurvature * mCosDipAngle) * mH * mCosDipAngle;
}

inline double StHelix::cy(double s)  const
{
  if (mSingularity)
    return mCosDipAngle * mCosPhase;
  else
    return cos(mPhase + s * mH * mCurvature * mCosDipAngle) * mH * mCosDipAngle;
}

inline double StHelix::cz(double /* s */)  const
{
  return mSinDipAngle;
}

inline const StThreeVector<double> &StHelix::origin() const {return mOrigin;}

inline StThreeVector<double> StHelix::at(double s) const
{
  return StThreeVector<double>(x(s), y(s), z(s));
}

inline StThreeVector<double> StHelix::cat(double s) const
{
  return StThreeVector<double>(cx(s), cy(s), cz(s));
}

inline double StHelix::pathLength(double X, double Y) const
{
  return fudgePathLength(StThreeVector<double>(X, Y, 0));
}
inline int StHelix::bad(double WorldSize) const
{

  int ierr;

  if (!::finite(mDipAngle    )) 	return   11;

  if (!::finite(mCurvature   )) 	return   12;

  ierr = mOrigin.bad(WorldSize);

  if (ierr)                           return    3 + ierr * 100;

  if (std::abs(mDipAngle)  > 1.58)	return   21;

  double qwe = std::abs(std::abs(mDipAngle) - M_PI / 2);

  if (qwe < 1. / WorldSize      ) 	return   31;

  if (std::abs(mCurvature) > WorldSize)	return   22;

  if (mCurvature < 0          )	return   32;

  if (abs(mH) != 1            )       return   24;

  return 0;
}

#endif