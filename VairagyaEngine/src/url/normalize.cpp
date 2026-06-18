#include "url/normalize.hpp"
#include <boost/url/url.hpp>
#include <cctype>
#include <string>


using namespace std;
using namespace boost::urls;


optional<string> normalizeURI(const string& rawURI) {
    try {

        url u(rawURI);

        if (!u.has_scheme() || u.host().empty()) {
            return nullopt;
        }

        u.normalize();
	    u.remove_fragment();

        if ((u.scheme() == "http" && u.port_number() == 80) ||
            (u.scheme() == "https" && u.port_number() == 443)) {
            u.remove_port();
        }
        

        return string(u.buffer());
	}
	catch (const boost::system::system_error&) {
        return nullopt;
    }
}
