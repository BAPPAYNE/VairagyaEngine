#ifndef STATUS_H
#define STATUS_H

enum class URLStatus {
    INVALID_URL,
    RELATIVE_URL,
    DISALLOWED_URL,
    ACCEPTED_URL
};

#endif // STATUS_H