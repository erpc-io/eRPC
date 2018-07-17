#pragma once

/**
 * @file timely-sweep-params.h
 * @brief Timely parameters that need to be sweeped
 */
static constexpr bool kPatched = false;  ///< Patch from ECN-vs-delay
static constexpr double kEwmaAlpha = 0.46;
static constexpr double kBeta = 0.26;
