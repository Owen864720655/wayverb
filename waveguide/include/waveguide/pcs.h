#pragma once

#include "common/aligned/vector.h"
#include "common/filters_common.h"

/// See sheaffer2014 and the source-modelling-toolbox.
/// paper: physical and numerical constraints in source modeling for finite
/// difference simulation of room acoustics
/// code:  http://code.soundsoftware.ac.uk/projects/smt/

template <typename T>
constexpr auto factdbl(T t) {
    double out{1};
    for (auto i{t}; i >= 1; i -= 2) {
        out *= i;
    }
    return out;
}

/// Design a maxflat lowpass pulse.
///
/// f0:     normalised cutoff frequency
/// N:      filter order
/// A:      pulse peak amplitude
/// hLen:   full, padded length
aligned::vector<double> maxflat(double f0, uint32_t N, double A, uint32_t hlen);

/// Design a mechanical filter simulating a small pulsating sphere.
///
/// M:      mass of sphere
/// f0:     normalised low resonance of mechanical system
/// Q:      Q of mechanical system
/// T:      temporal sample period
filter::biquad::coefficients mech_sphere(double M,
                                         double f0,
                                         double Q,
                                         double T);

/// Design a physically-constrained source for fdtd simulation.
/// Uses a maxflat fir kernel as the pulse shaping filter, and a
/// pulsating sphere model as the mechanical filter.
///
/// M:      mass of sphere
/// f0:     normalised low resonance of mechanical system (0.5=nyquist)
/// Q:      Q of mechanical system
/// maxF:   maximum driving force of sphere (newtons)
/// fc:     normalised cutoff frequency pulse (0.5=nyquist)
/// T:      temporal sample period (s)
aligned::vector<double> pcs_design(
        double M, double f0, double Q, double maxF, double fc, double T);