#include "MultiPoint.hpp"

namespace Slic3r {

void
MultiPoint::scale(double factor)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).scale(factor);
    }
}

void
MultiPoint::translate(double x, double y)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
MultiPoint::rotate(double angle, const Point &center)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

void
MultiPoint::reverse()
{
    std::reverse(this->points.begin(), this->points.end());
}

Point*
MultiPoint::first_point() const
{
    return new Point(this->points.front());
}

double
MultiPoint::length() const
{
    Lines lines = this->lines();
    double len = 0;
    for (Lines::iterator it = lines.begin(); it != lines.end(); ++it) {
        len += it->length();
    }
    return len;
}

bool
MultiPoint::is_valid() const
{
    return this->points.size() >= 2;
}

Points
MultiPoint::_douglas_peucker(const Points &points, const double tolerance)
{
    Points results;
    double dmax = 0;
    size_t index = 0;
    Line full(points.front(), points.back());
    for (Points::const_iterator it = points.begin() + 1; it != points.end(); ++it) {
        double d = it->distance_to(full);
        if (d > dmax) {
            index = it - points.begin();
            dmax = d;
        }
    }
    if (dmax >= tolerance) {
        Points dp0;
        dp0.reserve(index + 1);
        dp0.insert(dp0.end(), points.begin(), points.begin() + index + 1);
        Points dp1 = MultiPoint::_douglas_peucker(dp0, tolerance);
        results.reserve(results.size() + dp1.size() - 1);
        results.insert(results.end(), dp1.begin(), dp1.end() - 1);
        
        dp0.clear();
        dp0.reserve(points.size() - index + 1);
        dp0.insert(dp0.end(), points.begin() + index, points.end());
        dp1 = MultiPoint::_douglas_peucker(dp0, tolerance);
        results.reserve(results.size() + dp1.size());
        results.insert(results.end(), dp1.begin(), dp1.end());
    } else {
        results.push_back(points.front());
        results.push_back(points.back());
    }
    return results;
}

#ifdef SLIC3RXS
void
MultiPoint::from_SV(SV* poly_sv)
{
    AV* poly_av = (AV*)SvRV(poly_sv);
    const unsigned int num_points = av_len(poly_av)+1;
    this->points.resize(num_points);
    
    for (unsigned int i = 0; i < num_points; i++) {
        SV** point_sv = av_fetch(poly_av, i, 0);
        this->points[i].from_SV_check(*point_sv);
    }
}

void
MultiPoint::from_SV_check(SV* poly_sv)
{
    if (sv_isobject(poly_sv) && (SvTYPE(SvRV(poly_sv)) == SVt_PVMG)) {
        *this = *(MultiPoint*)SvIV((SV*)SvRV( poly_sv ));
    } else {
        this->from_SV(poly_sv);
    }
}

SV*
MultiPoint::to_AV() {
    const unsigned int num_points = this->points.size();
    AV* av = newAV();
    av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, this->points[i].to_SV_ref());
    }
    return newRV_noinc((SV*)av);
}

SV*
MultiPoint::to_SV_pureperl() const {
    const unsigned int num_points = this->points.size();
    AV* av = newAV();
    av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, this->points[i].to_SV_pureperl());
    }
    return newRV_noinc((SV*)av);
}
#endif

}
