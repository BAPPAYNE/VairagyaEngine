// GurenEngine.cpp : Defines the entry point for the application.
//

#include <string>
#include <iostream>

#include "url/normalize.h"
#include "url/validate.h"
#include "url/process.h"
#include "crawler/engine.h"
#include "utils/log.h"

using namespace std; 

std::ostream& operator<<(std::ostream& os, URLStatus s) {
    switch (s) {
    case URLStatus::INVALID:    return os << "INVALID";
    case URLStatus::RELATIVE:   return os << "RELATIVE";
    case URLStatus::DISALLOWED: return os << "DISALLOWED";
    case URLStatus::ACCEPTED:   return os << "ACCEPTED";
    }
    return os << "UNKNOWN";
}

int main()
{
    // Call the function with its namespace
 //   cout << *(normalizeURI("HTTP://www.Example.com:80/a%c2%b1b/%7Eusername/?q=Test%20Query#Fragment")) << std::endl;

    string urls[] = {
  //      "HTTPS://WWW.Google.COM",
  //      "https://google.com:443/search?q=AI",
  //      "https://www.youtube.com//watch?v=dQw4w9WgXcQ",
  //      "https://twitter.com/./home",
  //      "https://github.com/BAPPAYNE/Friday/../Friday",
  //      "https://api.openai.com/v1/%41ssistants",
  //      "https://example.com/a/b/../../c/",
  //      "https://cdn.cloudflare.com/assets/%7Eicons/logo.svg",
  //      "https://login.microsoftonline.com//common/oauth2/v2.0/authorize",
  //      "https://aws.amazon.com:443/ec2/?region=us-east-1&service=ec2",
  //      "https://medium.com/@user//latest",
  //      "https://www.reddit.com/r/netsec/./comments/",
  //      "https://example.com/%2Fadmin%2Fpanel",
  //      "https://example.com?b=2&a=1",
  //      "https://example.com/%63%61%73%65",
  //      "https://example.com/page#section",
  //      "https://example.com/a/b/c/d/e/f/g/h/i/j/k",
		//"https://example.com/image.png",
  //      "ftp://example.com/resource",
  //      "http://incomplete-url",
		"https://example.com/this|that"
        ""
    };

    size_t n = _countof(urls);
	cout << "Total URLs: " << n << endl;
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
	ProcessedURL pURL;
    for (int i = 0; i < n; i++) {
    	pURL = processURL(urls[i]);
		cout << "----------------------------------------" << endl;
        cout << pURL.original << endl ;
		cout << pURL.normalized << endl;
		cout << pURL.priority << endl;
		cout << pURL.status << endl;
    }

    
    

    return 0;
}
