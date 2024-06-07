#ifndef E2SARERROR_HPP
#define E2SARERROR_HPP

#include <iostream>
#include <string>
#include <system_error>
#include <boost/outcome.hpp>

// this vodoo is based on this guide
// https://www.boost.org/doc/libs/1_85_0/libs/outcome/doc/html/motivation/plug_error_code.html

/**
 * This file defines error codes for E2SAR and plugs the error code classes into system::error_code
 * such that E2SAR functions can return result objects (either result or error code)
 */

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

namespace e2sar
{

  /** error codes (in addition to standard std::errc error codes)*/
  enum class E2SARErrorc
  {
    NoError = 0,
    CaughtException = 1,
    ParseError = 2,
    ParameterError = 3,
    ParameterNotAvailable = 4,
    OutOfRange = 5,
    Undefined = 6,
    NotFound = 7,
    RPCError = 8,
  };

  struct E2SARErrorInfo
  {
    E2SARErrorc ec;
    std::string msg;

    inline const E2SARErrorc code() const
    { 
      return ec;
    }

    inline const std::string &message() const {
      return msg;
    }
  };

  // Localise an outcome implementation specific to this namespace.
  template <class T>
  using result = outcome::result<T, E2SARErrorInfo>;

  /** constructors typically throw this exception which may be a rethrow */
  class E2SARException
  {
  private:
    std::string _error_msg;

  public:
    E2SARException(const std::string &m) : _error_msg{m} {}
    /** implicit cast to string */
    operator std::string() const
    {
      return "E2SAR exception: " + _error_msg;
    }
    operator std::string()
    {
      return _error_msg;
    }
  };

}

namespace std
{
  // Tell the C++ 11 STL metaprogramming that enum
  // is registered with the standard error code system
  template <>
  struct is_error_code_enum<e2sar::E2SARErrorc> : true_type
  {
  };
}

namespace detail
{
  // Define a custom error code category derived from std::error_category
  class E2SARErrorc_category : public std::error_category
  {
  public:
    // Return a short descriptive name for the category
    virtual const char *name() const noexcept override final { return "E2SARError"; }
    // Return what each enum means in text
    virtual std::string message(int c) const override final
    {
      switch (static_cast<e2sar::E2SARErrorc>(c))
      {
      case e2sar::E2SARErrorc::CaughtException:
        return "caught an exception";
      case e2sar::E2SARErrorc::ParseError:
        return "parsing error";
      case e2sar::E2SARErrorc::ParameterError:
        return "parameter error";
      case e2sar::E2SARErrorc::ParameterNotAvailable:
        return "parameter not available";
      case e2sar::E2SARErrorc::OutOfRange:
        return "value out of range";
      case e2sar::E2SARErrorc::Undefined:
        return "value undefined";
      case e2sar::E2SARErrorc::NotFound:
        return "file not found";
      case e2sar::E2SARErrorc::RPCError:
        return "gRPC error";
      default:
        return "unknown";
      }
    }

    /*
    // Allow generic error conditions to be compared to me
    virtual std::error_condition default_error_condition(int c) const noexcept override final
    {
      switch (static_cast<e2sar::E2SARErrorc>(c))
      {
      case e2sar::E2SARErrorc::ParameterError:
        return make_error_condition(std::errc::invalid_argument);
      default:
        // I have no mapping for this code
        return std::error_condition(c, *this);
      }
    }
    */
  };
}

// Define the linkage for this function to be used by external code.
// This would be the usual __declspec(dllexport) or __declspec(dllimport)
// if we were in a Windows DLL etc. But for this example use a global
// instance but with inline linkage so multiple definitions do not collide.
#define THIS_MODULE_API_DECL extern inline

// Declare a global function returning a static instance of the custom category
THIS_MODULE_API_DECL const detail::E2SARErrorc_category &E2SARErrorc_category()
{
  static detail::E2SARErrorc_category c;
  return c;
}

namespace e2sar
{
  // Overload the global make_error_code() free function with our
  // custom enum. It will be found via ADL by the compiler if needed.
  // inline const std::error_code &make_error_code(const E2SARErrorInfo &ei) { return ei.ec; }

  // make a STANDARD error code from our internal error code by adding custom category
  inline std::error_code make_error_code(e2sar::E2SARErrorc e)
  {
    return {static_cast<int>(e), E2SARErrorc_category()};
  }

  // make an error code from error info with our custom category
  inline std::error_code make_error_code(E2SARErrorInfo e)
  {
    return {static_cast<int>(e.ec), E2SARErrorc_category()};
  }

  // need to know how to make an exception pointer, but since we don't know
  // how to make one, we just assume it knows how to make it from a custom error code
  inline boost::exception_ptr make_exception_ptr(E2SARErrorInfo e)
  {
    return make_exception_ptr(make_error_code(e));
  }
}
/*
How to use:

int main(void)
{
  // Note that we can now supply ConversionErrc directly to error_code
  std::error_code ec = ConversionErrc::IllegalChar;

  std::cout << "ConversionErrc::IllegalChar is printed by std::error_code as "
    << ec << " with explanatory message " << ec.message() << std::endl;

  // We can compare ConversionErrc containing error codes to generic conditions
  std::cout << "ec is equivalent to std::errc::invalid_argument = "
    << (ec == std::errc::invalid_argument) << std::endl;
  std::cout << "ec is equivalent to std::errc::result_out_of_range = "
    << (ec == std::errc::result_out_of_range) << std::endl;
  return 0;
}
*/
#endif