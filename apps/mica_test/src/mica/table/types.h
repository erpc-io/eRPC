#pragma once
#ifndef MICA_TABLE_TYPES_H_
#define MICA_TABLE_TYPES_H_

#include <string>
#include "mica/common.h"

namespace mica {
namespace table {
enum class Result {
  kSuccess = 0,
  kLocked,
  kError,
  kInsufficientSpacePool,
  kInsufficientSpaceIndex,
  kExists,
  kNotEven,
  kNotFound,
  kPartialValue,
  kNotProcessed,
  kNotSupported,
  kTimedOut,
  kRejected,
};

static std::string ResultString(enum Result r)
{
	switch(r) {
		case Result::kSuccess:
			return std::string("Success");
		case Result::kLocked:
			return std::string("Locked");
		case Result::kError:
			return std::string("Error");
		case Result::kInsufficientSpacePool:
			return std::string("Insufficient space in pool");
		case Result::kInsufficientSpaceIndex:
			return std::string("Insufficient space in index");
		case Result::kExists:
			return std::string("Exists");
		case Result::kNotEven:
			return std::string("Value not even (F&A)");
		case Result::kNotFound:
			return std::string("Not found");
		case Result::kPartialValue:
			return std::string("Partial value");
		case Result::kNotProcessed:
			return std::string("Not processed");
		case Result::kNotSupported:
			return std::string("Not supported");
		case Result::kTimedOut:
			return std::string("Timed out");
		case Result::kRejected:
			return std::string("Rejected");
		default:
			return std::string("Invalid ::mica::table Result type");
	};
}

}
}

#endif
