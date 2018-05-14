/**
 * @file timely-sweep-params.h
 * @brief Timely parameters that need to be sweeped
 */
static constexpr bool kPatched = true;  ///< Patch from ECN-vs-delay
static constexpr double kEwmaAlpha = kPatched ? .875 : 0.02;
static constexpr double kBeta = kPatched ? .008 : 0.8;
