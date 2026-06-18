#include "pipeline/quality_signals_builder.hpp"
#include <cmath>

using namespace storage;
using namespace std;

QualitySignals QualitySignalsBuilder::build(const string& clean_text, time_t last_changed) {
    QualitySignals signals;
    signals.content_last_changed_time = last_changed;
    signals.readability_score = calculateReadability(clean_text);
    signals.spam_score = calculateSpamScore(clean_text);
    signals.quality_score = (1.0f - signals.spam_score) * signals.readability_score;
    // placeholder for update frequency
    signals.update_frequency = 1.0f; 
    return signals;
}

float QualitySignalsBuilder::calculateReadability(const string& text) {
    if (text.empty()) return 0.0f;
    // Simple Flesch-Kincaid style approximation
    float words = 1.0f;
    for (char c : text) if (c == ' ') words++;
    float chars = (float)text.length();
    return clamp(1.0f - (words / chars), 0.0f, 1.0f);
}

float QualitySignalsBuilder::calculateSpamScore(const string& text) {
    // Basic logic: keyword density check or length check
    if (text.length() < 100) return 0.7f; // Thin content
    return 0.1f; // Stub for low spam
}
