#pragma once

#include "initiation/siptypes.h"

// various helper functions associated with SIP

// request and string
RequestType stringToRequest(QString request);
QString requestToString(RequestType request);

// Response code and string
uint16_t stringToResponseCode(QString code);

// Response and response code
ResponseType codeToResponse(uint16_t code);
uint16_t responseToCode(ResponseType response);

// phrase and response code
QString codeToPhrase(uint16_t code);
QString responseToPhrase(ResponseType response);

// connection type and string
Transport stringToConnection(QString type);
QString connectionToString(Transport connection);

// contentType
ContentType stringToContentType(QString typeStr);
QString contentTypeToString(ContentType type);
