#include "sipserver.h"

#include "initiation/siptransactionuser.h"
#include "initiation/transaction/sipdialogstate.h"

#include "common.h"

#include <QVariant>

SIPServer::SIPServer():
  receivedRequest_(nullptr)
{}


void SIPServer::processIncomingRequest(SIPRequest& request, QVariant& content)
{
  Q_UNUSED(content)

  if((receivedRequest_ == nullptr && request.method != SIP_ACK) ||
     request.method == SIP_BYE)
  {
    receivedRequest_ = std::shared_ptr<SIPRequest> (new SIPRequest);
    *receivedRequest_ = request;
  }
  else if (request.method != SIP_ACK && request.method != SIP_CANCEL)
  {
    printPeerError(this, "New request when previous transaction has not been completed. Ignoring...");
    return;
  }

  emit receivedRequest(request.method, request.message->from.address.realname);
}


void SIPServer::respond(SIPResponseStatus type)
{
  Q_ASSERT(receivedRequest_ != nullptr);

  if(receivedRequest_ == nullptr)
  {
    printDebug(DEBUG_PROGRAM_ERROR, this,
               "We are trying to respond when we have not received a request!");
    return;
  }

  printNormal(this, "Initiate sending of a dialog response");

  SIPResponse response;
  response.type = type;
  response.sipVersion = SIP_VERSION;

  copyResponseDetails(receivedRequest_->message, response.message);
  response.message->maxForwards = nullptr; // no max-forwards in responses

  response.message->contentLength = 0;
  response.message->contentType = MT_NONE;

  int responseCode = type;
  if(responseCode >= 200)
  {
    printDebug(DEBUG_NORMAL, this,
               "Sending a final response. Deleting request details.",
               {"Code", "Cseq"},
               {QString::number(responseCode),
                QString::number(receivedRequest_->message->cSeq.cSeq)});

    // reset the request since we have responded to it
    receivedRequest_ = nullptr;
  }

  QVariant content;
  emit outgoingResponse(response, content);
}


void SIPServer::copyResponseDetails(std::shared_ptr<SIPMessageHeader>& inMessage,
                                    std::shared_ptr<SIPMessageHeader>& copy)
{
  Q_ASSERT(inMessage);
  Q_ASSERT(inMessage->from.tagParameter != "");
  Q_ASSERT(inMessage->to.tagParameter != "");
  copy = std::shared_ptr<SIPMessageHeader> (new SIPMessageHeader());
  // Which fields to copy are listed in section 8.2.6.2 of RFC 3621

  // Call-ID field
  copy->callID = inMessage->callID;

  // CSeq
  copy->cSeq = inMessage->cSeq;

  // from-field
  copy->from = inMessage->from;

  // To field, expect if To tag is missing, in which case it should be added
  // To tag is added in dialog when checking the first request.
  copy->to = inMessage->to;

  // Via- fields in same order
  copy->vias = inMessage->vias;

  copy->recordRoutes = inMessage->recordRoutes;
}


bool SIPServer::isCANCELYours(SIPRequest& cancel)
{
  return receivedRequest_ != nullptr &&
      !receivedRequest_->message->vias.empty() &&
      !cancel.message->vias.empty() &&
      receivedRequest_->message->vias.first().branch == cancel.message->vias.first().branch &&
      equalURIs(receivedRequest_->requestURI, cancel.requestURI) &&
      receivedRequest_->message->callID == cancel.message->callID &&
      receivedRequest_->message->cSeq.cSeq == cancel.message->cSeq.cSeq &&
      equalToFrom(receivedRequest_->message->from, cancel.message->from) &&
      equalToFrom(receivedRequest_->message->to, cancel.message->to);
}


bool SIPServer::equalURIs(SIP_URI& first, SIP_URI& second)
{
  return first.type == second.type &&
      first.hostport.host == second.hostport.host &&
      first.hostport.port == second.hostport.port &&
      first.userinfo.user == second.userinfo.user;
}


bool SIPServer::equalToFrom(ToFrom& first, ToFrom& second)
{
  return first.address.realname == second.address.realname &&
      equalURIs(first.address.uri, second.address.uri);
}
