#ifndef QUALITY_SIGNALS_BUILDER_H
#define QUALITY_SIGNALS_BUILDER_H

#include <storage/db_schema.hpp>
#include <string>

using namespace std;

class QualitySignalsBuilder {
public:
    static storage::QualitySignals build(const string& clean_text, time_t last_changed); // build quality signals from clean text
    static float calculateReadability(const string& text); // calculate readability from text
    static float calculateSpamScore(const string& text); // calculate spam score from text
};

#endif // QUALITY_SIGNALS_BUILDER_H
