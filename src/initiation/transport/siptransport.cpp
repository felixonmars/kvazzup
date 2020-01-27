#include "siptransport.h"

#include "sipconversions.h"
#include "sipfieldparsing.h"
#include "sipfieldcomposing.h"
#include "initiation/negotiation/sipcontent.h"
#include "statisticsinterface.h"
#include "common.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QList>
#include <QHostInfo>

#include <iostream>
#include <sstream>
#include <string>

#include <functional>

const uint16_t SIP_PORT = 5060;


// TODO: separate this into common, request and response field parsing.
// This is so we can ignore nonrelevant fields (7.3.2)

// RFC3261_TODO: support compact forms (7.3.3)

const std::map<QString, std::function<bool(SIPField& field,
                                           std::shared_ptr<SIPMessageInfo>)>> parsing =
{
    {"To", parseToField},
    {"From", parseFromField},
    {"CSeq", parseCSeqField},
    {"Call-ID", parseCallIDField},
    {"Via", parseViaField},
    {"Max-Forwards", parseMaxForwardsField},
    {"Contact", parseContactField},
    {"Content-Type", parseContentTypeField},
    {"Content-Length", parseContentLengthField},
    {"Server", parseServerField},
    {"User-Agent", parseUserAgentField},
    {"Record-Route", parseRecordRouteField}
};


SIPTransport::SIPTransport(quint32 transportID, StatisticsInterface *stats):
  partialMessage_(""),
  connection_(nullptr),
  transportID_(transportID),
  stats_(stats)
{}

SIPTransport::~SIPTransport()
{}

void SIPTransport::cleanup()
{
  destroyConnection();
}

bool SIPTransport::isConnected()
{
  return connection_ && connection_->isConnected();
}

QString SIPTransport::getLocalAddress()
{
  Q_ASSERT(connection_);

  QString address = connection_->localAddress().toString();
  if (connection_->localAddress().protocol() == QAbstractSocket::IPv6Protocol)
  {
    address = "[" + address + "]";
  }

  return address;
}

QString SIPTransport::getRemoteAddress()
{
  Q_ASSERT(connection_);

  QString address = connection_->remoteAddress().toString();
  if (connection_->remoteAddress().protocol() == QAbstractSocket::IPv6Protocol)
  {
    address = "[" + address + "]";
  }

  return address;
}

uint16_t SIPTransport::getLocalPort()
{
  Q_ASSERT(connection_);
  return connection_->localPort();
}


void SIPTransport::createConnection(ConnectionType type, QString target)
{
  if(type == TCP)
  {
    qDebug() << "Connecting, SIP Transport: Initiating TCP connection for sip connection number:"
             << transportID_;
    connection_ = std::shared_ptr<TCPConnection>(new TCPConnection());
    signalConnections();
    connection_->establishConnection(target, SIP_PORT);
  }
  else
  {
    qDebug() << "WARNING: Trying to initiate a SIP Connection with unsupported connection type.";
  }
}

void SIPTransport::incomingTCPConnection(std::shared_ptr<TCPConnection> con)
{
  qDebug() << "This SIP connection uses an incoming connection:" << transportID_;
  if(connection_ != nullptr)
  {
    qDebug() << "Replacing existing connection";
  }
  connection_ = con;

  routing_.connectionEstablished(connection_->localAddress().toString(),
                                 connection_->localPort());

  signalConnections();
}

void SIPTransport::signalConnections()
{
  Q_ASSERT(connection_);
  QObject::connect(connection_.get(), &TCPConnection::messageAvailable,
                   this, &SIPTransport::networkPackage);

  QObject::connect(connection_.get(), &TCPConnection::socketConnected,
                   this, &SIPTransport::connectionEstablished);
}

void SIPTransport::connectionEstablished(QString localAddress, QString remoteAddress)
{
  routing_.connectionEstablished(localAddress,
                                 connection_->localPort());

  emit sipTransportEstablished(transportID_,
                                localAddress,
                                remoteAddress);
}

void SIPTransport::destroyConnection()
{
  if(connection_ == nullptr)
  {
    qDebug() << "Trying to destroy an already destroyed connection";
    return;
  }
  connection_->exit(0); // stops qthread
  connection_->stopConnection(); // exits run loop
  while(connection_->isRunning())
  {
    qSleep(5);
  }
  connection_.reset();
}

void SIPTransport::sendRequest(SIPRequest& request, QVariant &content)
{
  qDebug() << "Composing SIP Request:" << requestToString(request.type);
  Q_ASSERT(request.message->content.type == NO_CONTENT || content.isValid());
  Q_ASSERT(connection_ != nullptr);

  if((request.message->content.type == APPLICATION_SDP && !content.isValid())
     || connection_ == nullptr)
  {
    qDebug() << "WARNING: SDP nullptr or connection does not exist in sendRequest";
    return;
  }

  routing_.getRequestRouting(request.message);

  // start composing the request.
  // First we turn the struct to fields which are then turned to string
  QList<SIPField> fields;
  if (!composeMandatoryFields(fields, request.message))
  {
    qDebug() << "WARNING: Failed to add all the fields. Probably because of missing values.";
    return;
  }

  if (!includeRouteField(fields, request.message))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this, DC_SEND_SIP_REQUEST, "Failed to add Route-fields");
  }

  if ((request.type == SIP_INVITE || request.type == SIP_REGISTER) &&
      !includeContactField(fields, request.message))
  {
   qDebug() << "WARNING: Failed to add Contact field. Probably because of missing values.";
   return;
  }

  if (request.type == SIP_REGISTER &&
      !includeExpiresField(fields, request.message->expires))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this, DC_SEND_SIP_REQUEST, "Failed to add expires-field");
    return;
  }


  QString lineEnding = "\r\n";
  QString message = "";
  // adds content fields and converts the sdp to string if INVITE
  QString sdp_str = addContent(fields,
                               request.message->content.type != NO_CONTENT,
                               content.value<SDPMessageInfo>());

  if(!getFirstRequestLine(message, request, lineEnding))
  {
    qDebug() << "WARNING: could not get first request line";
    return;
  }

  message += fieldsToString(fields, lineEnding) + lineEnding;
  message += sdp_str;

  // print the first line
  stats_->addSentSIPMessage(requestToString(request.type),
                            message,
                            connection_->remoteAddress().toString());

  connection_->sendPacket(message);
}

void SIPTransport::sendResponse(SIPResponse &response, QVariant &content)
{
  qDebug() << "Composing SIP Response:" << responseToPhrase(response.type);
  Q_ASSERT(response.message->transactionRequest != SIP_INVITE
      || response.type != SIP_OK
      || (response.message->content.type == APPLICATION_SDP && content.isValid()));
  Q_ASSERT(connection_ != nullptr);

  if((response.message->transactionRequest == SIP_INVITE && response.type == SIP_OK
     && (!content.isValid() || response.message->content.type != APPLICATION_SDP))
     || connection_ == nullptr)
  {
    qDebug() << "WARNING: SDP nullptr or connection does not exist in sendResponse";
    return;
  }

  QList<SIPField> fields;
  if(!composeMandatoryFields(fields, response.message))
  {
    qDebug() << "WARNING: Failed to add mandatory fields. Probably because of missing values.";
    return;
  }

  if (!includeRecordRouteField(fields, response.message))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this, DC_SEND_SIP_REQUEST, "Failed to add RecordRoute-fields");
  }

  routing_.getResponseContact(response.message);
  if (response.message->transactionRequest == SIP_INVITE && response.type == SIP_OK &&
      !includeContactField(fields, response.message))
  {
    qDebug() << "ERROR: Failed to compose contact field for SIP OK response.";
  }


  // TODO: if the response is 405 SIP_NOT_ALLOWED we must include an allow header field.
  // lists allowed methods.

  QString lineEnding = "\r\n";
  QString message = "";
  // adds content fields and converts the sdp to string if INVITE
  QString sdp_str = addContent(fields, response.message->transactionRequest == SIP_INVITE
                               && response.type == SIP_OK, content.value<SDPMessageInfo>());
  if(!getFirstResponseLine(message, response, lineEnding))
  {
    qDebug() << "WARNING: could not get first request line";
    return;
  }

  message += fieldsToString(fields, lineEnding) + lineEnding;
  message += sdp_str;

  stats_->addSentSIPMessage(QString::number(responseToCode(response.type))
                            + " " + responseToPhrase(response.type),
                            message,
                            connection_->remoteAddress().toString());


  connection_->sendPacket(message);
}

bool SIPTransport::composeMandatoryFields(QList<SIPField>& fields,
                                          std::shared_ptr<SIPMessageInfo> message)
{
  return includeViaFields(fields, message) &&
         includeMaxForwardsField(fields, message) &&
         includeToField(fields, message) &&
         includeFromField(fields, message) &&
         includeCallIDField(fields, message) &&
         includeCSeqField(fields, message);
}

QString SIPTransport::fieldsToString(QList<SIPField>& fields, QString lineEnding)
{
  QString message = "";
  for(SIPField& field : fields)
  {
    message += field.name + ": ";

    for (int i = 0; i < field.valueSets.size(); ++i)
    {

      // add words.
      for (int j = 0; j < field.valueSets.at(i).words.size(); ++j)
      {
        message += field.valueSets.at(i).words.at(j);

        if (j != field.valueSets.at(i).words.size() - 1)
        {
          message += " ";
        }
      }

      // add parameters
      if(field.valueSets.at(i).parameters != nullptr)
      {
        for(SIPParameter& parameter : *field.valueSets.at(i).parameters)
        {
          if (parameter.value != "")
          {
            message += ";" + parameter.name + "=" + parameter.value;
          }
          else
          {
            message += ";" + parameter.name;
          }
        }
      }

      // should we add a comma(,) or end of line
      if (i != field.valueSets.size() - 1)
      {
        message += ",";
      }
      else
      {
        message += lineEnding;
      }
    }
  }
  return message;
}


void SIPTransport::networkPackage(QString package)
{
  qDebug() << "Received a network package for SIP Connection";
  // parse to header and body
  QString header = "";
  QString body = "";

  parsePackage(package, header, body);

  QList<SIPField> fields;
  QString firstLine = "";

  if (!headerToFields(header, firstLine, fields))
  {
    qDebug() << "Parsing error converting header to fields.";
  }

  if(header != "" && firstLine != "" && !fields.empty())
  {
    std::shared_ptr<SIPMessageInfo> message;
    if(!fieldsToMessage(fields, message))
    {
      qDebug() << "The received message was not correct. ";
      emit parsingError(SIP_BAD_REQUEST, transportID_); // RFC3261_TODO support other possible error types
      return;
    }

    QVariant content;
    if(body != "" && message->content.type != NO_CONTENT)
    {
      parseContent(content, message->content.type, body);
    }

    QRegularExpression requestRE("^(\\w+) (sip:\\S+@\\S+) (SIP/2.0)");
    QRegularExpression responseRE("^(SIP/2.0) (\\d\\d\\d) (\\w| )+");
    QRegularExpressionMatch request_match = requestRE.match(firstLine);
    QRegularExpressionMatch response_match = responseRE.match(firstLine);

    if(request_match.hasMatch() && response_match.hasMatch())
    {
      printDebug(DEBUG_PROGRAM_ERROR, this, DC_RECEIVE_SIP,
                 "Both the request and response matched, which should not be possible!");
      return;
    }

    if(request_match.hasMatch() && request_match.lastCapturedIndex() == 3)
    {
      stats_->addReceivedSIPMessage(request_match.captured(1), package, connection_->remoteAddress().toString());
      if(!parseRequest(request_match.captured(1), request_match.captured(3), message, fields, content))
      {
        qDebug() << "Failed to parse request";
      }
    }
    else if(response_match.hasMatch() && response_match.lastCapturedIndex() == 3)
    {
      stats_->addReceivedSIPMessage(response_match.captured(2), package, connection_->remoteAddress().toString());
      if(!parseResponse(response_match.captured(2), response_match.captured(1), message, content))
      {
        qDebug() << "ERROR: Failed to parse response: " << response_match.captured(2);
      }
    }
    else
    {
      qDebug() << "Failed to parse first line of SIP message:" << firstLine
               << "Request index:" << request_match.lastCapturedIndex()
               << "response index:" << response_match.lastCapturedIndex();
    }
  }
  else
  {
    qDebug() << "The whole message was not received";
  }
}


bool SIPTransport::parsePackage(QString package, QString& header, QString& body)
{
  // get any parts which have been received before
  if(partialMessage_.length() > 0)
  {
    package = partialMessage_ + package;
    partialMessage_ = "";
  }

  int headerEndIndex = package.indexOf("\r\n\r\n", 0, Qt::CaseInsensitive) + 4;
  int contentLengthIndex = package.indexOf("content-length", 0, Qt::CaseInsensitive);

  printDebug(DEBUG_NORMAL, this, DC_RECEIVE_SIP, "Parsing package to header and body",
    {"Header end index", "content-length index"},
    {QString::number(headerEndIndex), QString::number(contentLengthIndex)});

  // did we get the whole header
  if (headerEndIndex <= 0)
  {
    // We did not. Wait for more rest.
    partialMessage_ = package;
    return false;
  }

  int contentLength = 0;
  // did we find the contact-length field and is it before end of header.
  if (contentLengthIndex != -1 && contentLengthIndex < headerEndIndex)
  {
    int contentLengthLineEndIndex = package.indexOf("\r\n", contentLengthIndex,
                                                    Qt::CaseInsensitive);
    QString value = package.mid(contentLengthIndex + 16,
                                contentLengthLineEndIndex - (contentLengthIndex + 16));

    contentLength = value.toInt();
    if (contentLength < 0)
    {
      // TODO: Warn the user maybe. Maybe also ban the user at least temporarily.
      printDebug(DEBUG_PEER_ERROR, this, DC_RECEIVE_SIP,
                 "Got negative content-length! Peer is doing something very strange.");
      return false;
    }
  }
  qDebug() << "Content-length:" <<  contentLength;

  // if we have the whole message
  if(package.length() >= headerEndIndex + contentLength)
  {
    partialMessage_ = package.right(package.length() - (headerEndIndex + contentLength));
    header = package.left(headerEndIndex);
    body = package.mid(headerEndIndex, contentLength);

    qDebug() << "Whole SIP message received ----------- ";
    qDebug().noquote() << "Package:" << package;

    return true;
  }

  // did not get all of content.
  partialMessage_ = package;
  return false;
}


bool SIPTransport::headerToFields(QString header, QString& firstLine, QList<SIPField>& fields)
{
  // RFC3261_TODO: support header fields that span multiple lines
  // Divide into lines
  QStringList lines = header.split("\r\n", QString::SkipEmptyParts);
  qDebug() << "Parsing SIP header with" << lines.size() << "lines";
  if(lines.size() == 0)
  {
    qDebug() << "No first line present in SIP header!";
    return false;
  }

  // The message may contain fields that extend more than one line.
  // Combine them so field is only presenton one line
  combineContinuationLines(lines);
  firstLine = lines.at(0);

  QStringList debugLineNames = {};
  for(int i = 1; i < lines.size(); ++i)
  {
    SIPField field = {"", {}};
    QStringList valueSets;

    if (parseFieldName(lines[i], field) &&
        parseFieldValueSets(lines[i], valueSets))
    {
      // Check the correct number of valueSets for Field

      if (field.name != "Contact" &&
          field.name != "Supported" &&
          field.name != "Allow")
      {
        if (valueSets.size() != 1)
        {
          printDebug(DEBUG_PEER_ERROR, this, DC_RECEIVE_SIP,
                     "Incorrect amount of comma separated sets in field",
                      {"Amount"}, {QString::number(valueSets.size())});
          return false;
        }
      }
      else if (valueSets.size() == 0 || valueSets.size() > 100)
      {
        printDebug(DEBUG_PEER_ERROR, this, DC_RECEIVE_SIP,
                   "Incorrect amount of comma separated sets in field",
                    {"Field", "Amount"}, {field.name, QString::number(valueSets.size())});
        return false;
      }

      for (QString& value : valueSets)
      {
        if (!parseFieldValue(value, field))
        {
          qDebug() << "Failed to parse field:" << field.name;
          return false;
        }
      }

      debugLineNames << field.name;
      fields.push_back(field);
    }
    else
    {
      return false;
    }
  }

  qDebug() << "Found following SIP fields:" << debugLineNames;

  // check that all required header lines are present
  if(!isLinePresent("To", fields)
     || !isLinePresent("From", fields)
     || !isLinePresent("CSeq", fields)
     || (!isLinePresent("Call-ID", fields) && !isLinePresent("i", fields))
     | !isLinePresent("Via", fields))
  {
    qDebug() << "All mandatory header lines not present!";
    return false;
  }
  return true;
}


bool SIPTransport::combineContinuationLines(QStringList& lines)
{
  for (int i = 1; i < lines.size(); ++i)
  {
    // combine current line with previous
    if (lines.at(i).front().isSpace())
    {
      printNormalDebug(this, DC_RECEIVE_SIP, "Found a continuation line");
      lines[i - 1].append(lines.at(i));
      lines.erase(lines.begin() + i);
      --i;
    }
  }

  return true;
}


bool SIPTransport::parseFieldName(QString& line, SIPField& field)
{
  QRegularExpression re_field("(\\S*):\\s+(.+)");
  QRegularExpressionMatch field_match = re_field.match(line);

  // separate name from rest
  if(field_match.hasMatch() && field_match.lastCapturedIndex() == 2)
  {
    field.name = field_match.captured(1);
    line = field_match.captured(2);
  }
  else
  {
    return false;
  }
  return true;
}


bool SIPTransport::parseFieldValueSets(QString& line, QStringList& outValueSets)
{
  // separate value sections by commas
  outValueSets = line.split(",", QString::SkipEmptyParts);
  return !outValueSets.empty();
}


bool SIPTransport::parseFieldValue(QString& valueSet, SIPField& field)
{
  // RFC3261_TODO: Uniformalize case formatting. Make everything big or small case expect quotes.
  ValueSet set = ValueSet{{}, nullptr};

  QString currentWord = "";
  bool isQuotation = false;
  bool isURI = false;
  bool isParameter = false;
  int comments = 0;

  SIPParameter parameter;

  for (QChar& character : valueSet)
  {
    // add character to word if it is not parsed out
    if (isURI ||
        (character != " "
        && character != "\""
        && character != ";"
        && character != "="
        && character != "("
        && character != ")"
        && !comments))
    {
      currentWord += character;
    }

    // push current word if it ended
    if (!comments)
    {
      if ((character == "\"" && isQuotation) ||
          (character == ">" && isURI) ||
          (character == " " && !isQuotation))
      {
        if (!isParameter && currentWord != "")
        {
          set.words.push_back(currentWord);
          currentWord = "";
        }
        else
        {
          return false;
        }
      }
      else if((character == "=" && isParameter) ||
         (character == ";" && !isURI))
      {
        if (!isParameter)
        {
          if (character == ";" && currentWord != "")
          {
            // last word before parameters
            set.words.push_back(currentWord);
            currentWord = "";
          }
        }
        else // isParameter
        {
          if (character == "=")
          {
            if (parameter.name != "")
            {
              return false; // got name when we already have one
            }
            else
            {
              parameter.name = currentWord;
              currentWord = "";
            }
          }
          else if (character == ";")
          {
            addParameterToSet(parameter, currentWord, set);
          }
        }
      }
    }

    // change the state of parsing
    if (character == "\"") // quote
    {
      isQuotation = !isQuotation;
    }
    else if (character == "<") // start of URI
    {
      if (isQuotation || isURI)
      {
        return false;
      }
      isURI = true;
    }
    else if (character == ">") // end of URI
    {
      if (!isURI || isQuotation)
      {
        return false;
      }
      isURI = false;
    }
    else if (character == "(")
    {
      ++comments;
    }
    else if (character == ")")
    {
      if (!comments)
      {
        return false;
      }

      --comments;
    }
    else if (!isURI && !isQuotation && character == ";") // parameter
    {
      isParameter = true;
    }
  }

  // add last word
  if (isParameter)
  {
    addParameterToSet(parameter, currentWord, set);
  }
  else if (currentWord != "")
  {
    set.words.push_back(currentWord);
    currentWord = "";
  }

  field.valueSets.push_back(set);

  return true;
}


void SIPTransport::addParameterToSet(SIPParameter& currentParameter, QString &currentWord,
                  ValueSet& valueSet)
{
  if (currentParameter.name == "")
  {
    currentParameter.name = currentWord;
  }
  else
  {
    currentParameter.value = currentWord;
  }
  currentWord = "";

  if (valueSet.parameters == nullptr)
  {
    valueSet.parameters = std::shared_ptr<QList<SIPParameter>> (new QList<SIPParameter>);
  }
  valueSet.parameters->push_back(currentParameter);
  currentParameter = SIPParameter{"",""};
}

bool SIPTransport::fieldsToMessage(QList<SIPField>& fields,
                                   std::shared_ptr<SIPMessageInfo>& message)
{
  message = std::shared_ptr<SIPMessageInfo> (new SIPMessageInfo);
  message->cSeq = 0;
  message->transactionRequest = SIP_NO_REQUEST;
  message->maxForwards = 0;
  message->dialog = std::shared_ptr<SIPDialogInfo> (new SIPDialogInfo);
  message->content.type = NO_CONTENT;
  message->content.length = 0;

  for(int i = 0; i < fields.size(); ++i)
  {
    if(parsing.find(fields[i].name) == parsing.end())
    {
      qDebug() << "Field not supported:" << fields[i].name;
    }
    else
    {
      if(!parsing.at(fields.at(i).name)(fields[i], message))
      {
        qDebug() << "Failed to parse following field:" << fields.at(i).name;
        //qDebug() << "Values:" << fields.at(i).valueSets.at(0).values;
        return false;
      }
    }
  }
  return true;
}


bool SIPTransport::parseRequest(QString requestString, QString version,
                                std::shared_ptr<SIPMessageInfo> message,
                                QList<SIPField> &fields, QVariant &content)
{
  qDebug() << "Request detected:" << requestString;

  message->version = version; // TODO: set only version not SIP/version
  RequestType requestType = stringToRequest(requestString);

  if(requestType == SIP_NO_REQUEST)
  {
    qDebug() << "Could not recognize request type!";
    return false;
  }

  // RFC3261_TODO: if more than one via-field, discard message

  if(!isLinePresent("Max-Forwards", fields))
  {
    qDebug() << "Mandatory Max-Forwards not present in Request header";
    return false;
  }

  if(requestType == SIP_INVITE && !isLinePresent("Contact", fields))
  {
    qDebug() << "Contact header missing from INVITE request";
    return false;
  }

  // construct request
  SIPRequest request;
  request.type = requestType;
  request.message = message;

  emit incomingSIPRequest(request, getLocalAddress(), content, transportID_);
  return true;
}


bool SIPTransport::parseResponse(QString responseString, QString version,
                                 std::shared_ptr<SIPMessageInfo> message,
                                 QVariant &content)
{
  qDebug() << "Response detected:" << responseString;
  message->version = version; // TODO: set only version not SIP/version
  ResponseType type = codeToResponse(stringToResponseCode(responseString));

  if(type == SIP_UNKNOWN_RESPONSE)
  {
    qDebug() << "Could not recognize response type!";
    return false;
  }

  SIPResponse response;
  response.type = type;
  response.message = message;

  routing_.processResponseViaFields(message->vias);

  emit incomingSIPResponse(response, content);

  return true;
}

void SIPTransport::parseContent(QVariant& content, ContentType type, QString& body)
{
  if(type == APPLICATION_SDP)
  {
    SDPMessageInfo sdp;
    if(parseSDPContent(body, sdp))
    {
      qDebug () << "Successfully parsed SDP";
      content.setValue(sdp);
    }
    else
    {
      qDebug () << "Failed to parse SDP message";
    }
  }
  else
  {
    qDebug() << "Unsupported content type detected!";
  }
}


QString SIPTransport::addContent(QList<SIPField>& fields, bool haveContent,
                                 const SDPMessageInfo &sdp)
{
  QString sdp_str = "";

  if(haveContent)
  {
    sdp_str = composeSDPContent(sdp);
    if(sdp_str == "" ||
       !includeContentLengthField(fields, sdp_str.length()) ||
       !includeContentTypeField(fields, "application/sdp"))
    {
      qDebug() << "WARNING: Could not add sdp fields to request";
      return "";
    }
  }
  else if(!includeContentLengthField(fields, 0))
  {
    printDebug(DEBUG_PROGRAM_ERROR, this, DC_SIP_CONTENT,
               "Could not add content-length field to sip message!");
  }
  return sdp_str;
}
