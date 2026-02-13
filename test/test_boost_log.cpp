#include <boost/log/trivial.hpp>
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/shared_ptr.hpp>
#include <iostream>
#include <iomanip>

using namespace boost::log;

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)
BOOST_LOG_ATTRIBUTE_KEYWORD(counter, "LineCounter", int)
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "Timestamp", boost::posix_time::ptime)
typedef sinks::asynchronous_sink<sinks::text_ostream_backend> text_sink;
boost::shared_ptr<text_sink> sink = boost::make_shared<text_sink>();
sources::severity_logger_mt<trivial::severity_level> lg;

#define BOOST_MLL_START(PREF) { std::ostringstream PREF_ostr;
#define BOOST_MLL_LOG(PREF) PREF_ostr 
#define BOOST_MLL_STOP(PREF, LOG, LEV) BOOST_LOG_SEV(LOG, LEV) << PREF_ostr.str(); } 
#define BOOST_LOG_FLUSH() sink->flush()
#define BOOST_LOG_INFO() BOOST_LOG_SEV(lg, trivial::info)
#define BOOST_LOG_WARN() BOOST_LOG_SEV(lg, trivial::warning)
#define BOOST_LOG_ERR() BOOST_LOG_SEV(lg, trivial::error)

void defineLogger()
{
  boost::shared_ptr<std::ostream> stream{&std::clog,
    boost::null_deleter{}};
  sink->locked_backend()->add_stream(stream);
//  sink->set_filter(expressions::is_in_range(severity, 1, 3));
  //sink->set_formatter(expressions::stream << std::setw(5) << counter <<
  //  " - " << severity << ": " << expressions::smessage << " (" <<
  //  expressions::format_date_time(timestamp, "%H:%M:%S") << ")");
  sink->set_formatter(expressions::stream << 
    counter <<
    ": [" << expressions::format_date_time(timestamp, "%H:%M:%S") << "]" <<
    " {" << trivial::severity << "}" <<
    " " << expressions::smessage);
  core::get()->add_sink(sink);
  core::get()->add_global_attribute("LineCounter",
    attributes::counter<int>{});
  core::get()->add_global_attribute("Timestamp",
    attributes::local_clock{});
}

int main()
{
  defineLogger();

  BOOST_LOG_INFO() << "note";
  BOOST_LOG_WARN() << "warning";
  BOOST_LOG_ERR() << "another error\n" << ">\t double line";
  BOOST_LOG_ERR() << "another error\n" << ">\t double line";

  BOOST_MLL_START(my)
  BOOST_MLL_LOG(my) << "this is a multi-\n";
  BOOST_MLL_LOG(my) << "line log message";
  BOOST_MLL_STOP(my, lg, trivial::error);

  BOOST_LOG_FLUSH();
}
