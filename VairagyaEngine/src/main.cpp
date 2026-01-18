// GurenEngine.cpp : Defines the entry point for the application.
//

#include <string>
#include <iostream>

#include "url/normalize.h"
#include "url/validate.h"
#include "url/process.h"
#include "crawler/engine.h"
#include "utils/log.h"
#include "net/fetcher.h"

using namespace std; 

inline const char* enum_to_string(net::FetchStatus status) {
    switch (status) {
        case net::FetchStatus::SUCCESS: return "SUCCESS";
        case net::FetchStatus::FAILED: return "FAILED";
        case net::FetchStatus::TIMEOUT: return "TIMEOUT";
        case net::FetchStatus::NOT_FOUND: return "NOT_FOUND";
        case net::FetchStatus::UNAUTHORIZED: return "UNAUTHORIZED";
        case net::FetchStatus::FORBIDDEN: return "FORBIDDEN";
        case net::FetchStatus::SERVER_ERROR: return "SERVER_ERROR";
        case net::FetchStatus::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
        default: return "INVALID_STATUS";
    }
}

inline const char *enum_to_string(URLStatus status) {
    switch (status) {
        case URLStatus::INVALID: return "INVALID";
        case URLStatus::RELATIVE: return "RELATIVE";
        case URLStatus::DISALLOWED: return "DISALLOWED";
        case URLStatus::ACCEPTED: return "ACCEPTED";
        default: return "INVALID_STATUS";
    }
}

int main()
{
    // Call the function with its namespace
 //   cout << *(normalizeURI("HTTP://www.Example.com:80/a%c2%b1b/%7Eusername/?q=Test%20Query#Fragment")) << std::endl;

    //string urls[] = {
        //"https://www.google.com",
        //"https://duckduckgo.com",
        //"https://stackoverflow.com",
        //"https://chatgpt.com"
        //"https://bhagavadgita.com/api"
 //       "http://httpforever.com/"
 //   };

 //   size_t n = _countof(urls);
	//cout << "Total URLs: " << n << endl;
 //   ProcessedURL pURL;
 //   for (int i = 0; i < n; i++) {
 //       string normalizedURI = normalizeURI(urls[i]).value_or("");
 //       if (normalizedURI.empty()) {
 //           std::cout << "Invalid URL\n";
 //           //continue;
 //       }
	//	pURL =  processURL(urls[i]);
 //       cout << "URI: " << normalizedURI; 
 //       cout << "\tValidity: " << analyzeURL(normalizedURI);
	//	cout << "\tPriority: " << priorityScore(pURL.normalized) << endl;
 //   }

    // crawler::runCrawler();

	//debug("Priority : %d", priorityScore("https://example.com/a/b/c/d/e/f/g/h/i/j/k\n"));

    // cout << processURL("https://www.example.com").normalized << endl;
	//cout << priorityScore("https://www.example.com") << endl;
	//ProcessedURL pURL;
 //   for (int i = 0; i < n; i++) {
 //   	pURL = processURL(urls[i]);
	//	cout << "----------------------------------------" << endl;
 //       cout << pURL.original << endl ;
	//	cout << pURL.normalized << endl;
	//	cout << pURL.priority << endl;
	//	cout << enum_to_string(pURL.status) << endl;
 //   }
	//net::FetchResult fetchedResult;
 //   for (int i = 0; i < n; i++) {
 //       fetchedResult = net::fetch(urls[i]);
 //       cout << "----------------------------------------" << endl;
 //       cout << fetchedResult.content << endl;
 //       cout << fetchedResult.http_code << endl;
 //       cout << enum_to_string(fetchedResult.status) << endl;
 //   }


    crawler::runCrawler();

    return 0;
}
